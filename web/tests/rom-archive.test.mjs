import assert from "node:assert/strict";
import { test } from "node:test";
import { crc32, deflateRawSync } from "node:zlib";
import { openCartridgeFile } from "../rom-archive.mjs";
import { inspectRom } from "../rom-import.mjs";

const limit = 512 * 1024 * 1024;
const cartridge = Buffer.alloc(8192);
cartridge.write("358/2 DAYS");
cartridge.write("YKGE", 12);
cartridge[0x14] = 255;
function archive(entries, { comment = "" } = {}) {
  const body = [], directory = [];
  let offset = 0;
  for (const { name, bytes = cartridge, method = 8, descriptor = false,
               flags = 0, crc = crc32(bytes), size = bytes.length } of entries) {
    const filename = Buffer.from(name);
    const packed = method === 8 ? deflateRawSync(bytes) : bytes;
    const local = Buffer.alloc(30), central = Buffer.alloc(46);
    local.writeUInt32LE(0x04034b50);
    local.writeUInt16LE(20, 4);
    local.writeUInt16LE(flags | (descriptor ? 8 : 0), 6);
    local.writeUInt16LE(method, 8);
    local.writeUInt16LE(filename.length, 26);
    if (!descriptor) {
      local.writeUInt32LE(crc, 14);
      local.writeUInt32LE(packed.length, 18);
      local.writeUInt32LE(size, 22);
    }
    central.writeUInt32LE(0x02014b50);
    local.copy(central, 6, 4, 14);
    central.writeUInt32LE(crc, 16);
    central.writeUInt32LE(packed.length, 20);
    central.writeUInt32LE(size, 24);
    central.writeUInt16LE(filename.length, 28);
    central.writeUInt32LE(offset, 42);
    const trailer = descriptor ? Buffer.alloc(16) : Buffer.alloc(0);
    if (descriptor) {
      trailer.writeUInt32LE(0x08074b50);
      trailer.writeUInt32LE(crc, 4);
      trailer.writeUInt32LE(packed.length, 8);
      trailer.writeUInt32LE(size, 12);
    }
    body.push(local, filename, packed, trailer);
    directory.push(central, filename);
    offset += local.length + filename.length + packed.length + trailer.length;
  }
  const end = Buffer.alloc(22), index = Buffer.concat(directory);
  end.writeUInt32LE(0x06054b50);
  end.writeUInt16LE(entries.length, 8); end.writeUInt16LE(entries.length, 10);
  end.writeUInt32LE(index.length, 12); end.writeUInt32LE(offset, 16);
  end.writeUInt16LE(Buffer.byteLength(comment), 20);
  return new Blob([...body, index, end, comment]);
}

test("opens only the cartridge, including nested stored and streaming deflated entries", async () => {
  for (const method of [0, 8]) {
    const file = archive([
      { name: "__MACOSX/._days.nds" },
      { name: "README.txt", bytes: Buffer.from("unused"), method: 99 },
      { name: "Folder/Days.NDS", method, descriptor: method === 8 },
    ], { comment: "archive comment" });
    const updates = [];
    const rom = await openCartridgeFile(file, limit, { onProgress: x => updates.push(x) });
    assert.deepEqual(Buffer.from(await rom.arrayBuffer()), cartridge);
    assert.deepEqual(updates.at(-1), { phase: "unzip", completed: cartridge.length, total: cartridge.length });
    await assert.rejects(inspectRom(file), /does not match the supported Days cartridge hashes/);
  }
});

test("refuses missing or ambiguous cartridges, unsupported entries, size and CRC mismatches", async () => {
  const named = { name: "Days.nds" };
  for (const [entries, error] of [
    [[{ name: "README.txt" }], /containing one/],
    [[named, { name: "other.nds" }], /several/],
    [[{ ...named, flags: 1 }], /unencrypted/],
    [[{ ...named, method: 99 }], /deflated ZIP/],
    [[{ ...named, size: limit + 1 }], /limit/],
    [[{ ...named, size: cartridge.length - 1 }], /expands beyond/],
    [[{ ...named, crc: crc32(cartridge) ^ 1 }], /checksum/],
  ]) {
    await assert.rejects(openCartridgeFile(archive(entries), limit), error);
  }
  const zip = archive([named]);
  const bytes = Buffer.from(await zip.arrayBuffer());
  bytes[30] ^= 1;
  await assert.rejects(openCartridgeFile(new Blob([bytes]), limit), /names disagree/);
  await assert.rejects(openCartridgeFile(zip.slice(0, -1), limit), /end of the ZIP/);
});

test("a raw cartridge stays a sliceable file and an unzip progress callback can cancel", async () => {
  const input = new Blob([cartridge]);
  assert.equal(await openCartridgeFile(input, limit), input);
  const controller = new AbortController();
  await assert.rejects(inspectRom(archive([{ name: "Days.nds" }]), {
    signal: controller.signal,
    onProgress({ phase }) { if (phase === "unzip") controller.abort(); },
  }), { name: "AbortError" });
});
