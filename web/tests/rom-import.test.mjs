import assert from "node:assert/strict";
import { createHash, randomBytes } from "node:crypto";
import { test } from "node:test";
import { Sha256 } from "../sha256.mjs";
import { hashRom, inspectRom, parseHeader, parseFat, parseFnt, importRomToFileSystem, SUPPORTED_ROMS } from "../rom-import.mjs";
const digest = bytes => createHash("sha256").update(bytes).digest("hex");
const blob = bytes => new Blob([bytes]);
function header(size = 0x1000) {
  const bytes = new Uint8Array(size), view = new DataView(bytes.buffer);
  bytes.set(new TextEncoder().encode("358/2 DAYS"));
  bytes.set(new TextEncoder().encode("YKGE"), 12);
  view.setUint32(0x80, size, true);
  return bytes;
}
function fnt(entries, count=1, firstFile=0) {
  const bytes = new Uint8Array(8 + entries.length), view = new DataView(bytes.buffer);
  view.setUint32(0,8,true); view.setUint16(4,firstFile,true); view.setUint16(6,count,true);
  bytes.set(entries,8); return bytes;
}
test("streaming SHA-256 matches Node across padding and arbitrary chunk boundaries", () => {
  for (const size of [0,1,3,55,56,63,64,65,127,128,129,1024,1024*1024+33]) {
    const bytes = randomBytes(size);
    for (const chunk of [1,31,64,97,65536]) {
      const hash = new Sha256();
      for (let offset=0; offset<size; offset+=chunk) hash.update(bytes.subarray(offset,offset+chunk));
      assert.equal(hash.hex(),digest(bytes)); assert.equal(hash.hex(),digest(bytes));
      assert.throws(()=>hash.update(bytes),/finalized/);
    }
  }
});
test("supported cartridge database contains exact upstream NA and EU revision zero hashes", () => {
  assert.deepEqual(SUPPORTED_ROMS.map(x=>[x.game_code,x.rom_version]),[["YKGP",0],["YKGE",0]]);
  assert.equal(SUPPORTED_ROMS[1].sha256,"3cbbe3f15b49fc26f43edaf0050899b3cc1091a25e7a496373409c7c8c6fd0cb");
});
test("ROM hashes normalize trimmed FF padding and header-free bytes exactly", async () => {
  const bytes = header(0x2100); bytes[0x14]=0; bytes.fill(0x54,0x200);
  // Restore used size after filling and declare a legitimate trimmed image.
  new DataView(bytes.buffer).setUint32(0x80,bytes.length,true);
  const full = new Uint8Array(0x20000); full.fill(0xff); full.set(bytes);
  const free = full.slice(); free.fill(0,0x200,0x1000);
  const events=[];
  const result = await hashRom(blob(bytes),{onProgress:x=>events.push(x)});
  assert.equal(result.sha256,digest(full)); assert.equal(result.sha256_header_free,digest(free));
  assert.equal(result.hash_size,full.length); assert.deepEqual(events.at(-1),{phase:"verify",completed:full.length,total:full.length});
  const untrimmed = await hashRom(blob(full));
  assert.equal(untrimmed.sha256,result.sha256);
});
test("bad capacity/used sizes never invent padding and cancellation stops validation", async () => {
  const bytes=header(); bytes[0x14]=255;
  let result=await hashRom(blob(bytes)); assert.equal(result.hash_size,bytes.length); assert.equal(result.sha256,digest(bytes));
  bytes[0x14]=12; new DataView(bytes.buffer).setUint32(0x80,0,true);
  result=await hashRom(blob(bytes)); assert.equal(result.hash_size,bytes.length);
  const controller=new AbortController(); controller.abort();
  await assert.rejects(hashRom(blob(bytes),{signal:controller.signal}),{name:"AbortError"});
});
test("header parser rejects truncated ranges and non-eight-byte FAT", () => {
  assert.throws(()=>parseHeader(new Uint8Array(1),1),/too small/);
  const bytes=header(),view=new DataView(bytes.buffer);
  const parsed=parseHeader(bytes,bytes.length); assert.equal(parsed.game_code,"YKGE");
  view.setUint32(0x20,bytes.length,true); view.setUint32(0x2c,1,true);
  assert.throws(()=>parseHeader(bytes,bytes.length),/outside/);
  view.setUint32(0x2c,0,true); view.setUint32(0x4c,1,true);
  assert.throws(()=>parseHeader(bytes,bytes.length),/multiple/);
});
test("FAT parser bounds every file including empty and reversed ranges", () => {
  const bytes=new Uint8Array(16),view=new DataView(bytes.buffer);
  view.setUint32(0,0x200,true); view.setUint32(4,0x204,true);
  view.setUint32(8,0x204,true); view.setUint32(12,0x204,true);
  assert.deepEqual(parseFat(bytes,0x204).map(x=>x.size),[4,0]);
  view.setUint32(12,0x203,true); assert.throws(()=>parseFat(bytes,0x204),/negative/);
  view.setUint32(12,0x205,true); assert.throws(()=>parseFat(bytes,0x204),/outside/);
});
test("FNT parser imports safe names and unnamed FAT files remain unclaimed", () => {
  const parsed=parseFnt(fnt([5,...new TextEncoder().encode("a.bin"),3,...new TextEncoder().encode("../"),0]),3);
  assert.deepEqual([...parsed],[[0,"a.bin"],[1,"_"]]);
});
test("FNT rejects unterminated, out-of-range, cyclic and colliding directories", () => {
  assert.throws(()=>parseFnt(fnt([1,65]),1),/terminator/);
  assert.throws(()=>parseFnt(fnt([1,65,0]),0),/invalid/);
  assert.throws(()=>parseFnt(fnt([0x81,65,0,0xf0,0]),0),/cyclic/);
  assert.throws(()=>parseFnt(fnt([1,65,1,65,0]),2),/colliding/);
  const bytes=fnt([0]); new DataView(bytes.buffer).setUint32(0,0,true);
  assert.throws(()=>parseFnt(bytes,0),/outside/);
});
test("ROM inspection rejects wrong games before hashing and refuses counterfeit Days data", async () => {
  const bytes=header(); bytes[12]=65;
  await assert.rejects(inspectRom(blob(bytes)),/unmodified North American/);
  bytes[12]=89;
  await assert.rejects(inspectRom(blob(bytes)),/does not match/);
});
test("filesystem import refuses an unverified or modified inspection object", async () => {
  await assert.rejects(importRomToFileSystem({supported:SUPPORTED_ROMS[0]},{}),/must pass inspectRom/);
});
