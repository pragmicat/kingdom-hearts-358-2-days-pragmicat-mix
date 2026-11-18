// Browser-local importer matching khdays-port v0.1.5 tools/extract_data.py.
// This module is an import component, not a browser game engine. The native
// Pragmicat Mix DLL still requires a genuine WebAssembly runtime port.
import { Sha256 } from "./sha256.mjs";

export const SUPPORTED_ROMS = Object.freeze([
  Object.freeze({ name: "358/2 DAYS (Europe)", game_code: "YKGP", rom_version: 0,
    sha256: "1ecf5e7a41a2ae48e7c5fd3f678e5a79e854d843f1a1fa3a952a41b14e51ec4f",
    sha256_header_free: "d9e103c6ccd39d5b91e44d865983badc5e2af1674db404b3768557712038a373" }),
  Object.freeze({ name: "358/2 DAYS (USA)", game_code: "YKGE", rom_version: 0,
    sha256: "3cbbe3f15b49fc26f43edaf0050899b3cc1091a25e7a496373409c7c8c6fd0cb",
    sha256_header_free: "f3ebe95bc6a4fb3b096cefc5cdb54147647889fa7f404818f91d1a226b35e34b" }),
]);
const CHUNK_SIZE = 1024 * 1024;
const MAX_ROM_SIZE = 512 * 1024 * 1024;
const ROOT_DIRECTORY_ID = 0xf000;
const encoder = new TextEncoder();
const verifiedInspections = new WeakSet();
const importingFilesystems = new WeakSet();
function ascii(bytes) { return Array.from(bytes, b => b < 128 ? String.fromCharCode(b) : "\ufffd").join(""); }
function text(bytes) { return ascii(bytes).split("\0", 1)[0].trim(); }
function abort(signal) { if (signal?.aborted) throw new DOMException("ROM import cancelled.", "AbortError"); }
function range(size, offset, count, label) {
  if (!Number.isSafeInteger(offset) || !Number.isSafeInteger(count) || offset < 0 || count < 0 || offset + count > size)
    throw new Error(`${label} points outside the ROM.`);
}
async function read(file, offset, count, label) {
  range(file.size, offset, count, label);
  const bytes = new Uint8Array(await file.slice(offset, offset + count).arrayBuffer());
  if (bytes.length !== count) throw new Error(`${label} was truncated while reading.`);
  return bytes;
}
function progress(callback, phase, completed, total) { callback?.({ phase, completed, total }); }

export function parseHeader(bytes, romSize) {
  if (bytes.length < 0x200) throw new Error("The file is too small to be a Nintendo DS ROM.");
  if (romSize > MAX_ROM_SIZE) throw new Error("The ROM exceeds the supported 512 MiB cartridge limit.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const result = { title: text(bytes.subarray(0,12)), game_code: text(bytes.subarray(12,16)),
    maker_code: text(bytes.subarray(16,18)), unit_code: bytes[0x12], rom_version: bytes[0x1e] };
  for (const [name, addressOffset, sizeOffset] of [
    ["arm9",0x20,0x2c], ["arm7",0x30,0x3c], ["fnt",0x40,0x44], ["fat",0x48,0x4c],
    ["arm9_overlay",0x50,0x54], ["arm7_overlay",0x58,0x5c],
  ]) {
    result[`${name}_offset`] = view.getUint32(addressOffset, true);
    result[`${name}_size`] = view.getUint32(sizeOffset, true);
    if (result[`${name}_size`]) range(romSize, result[`${name}_offset`], result[`${name}_size`], name);
  }
  if (result.fat_size % 8) throw new Error("The FAT size is not a multiple of eight.");
  return result;
}

export function parseFat(bytes, romSize) {
  if (bytes.length % 8) throw new Error("The FAT size is not a multiple of eight.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const entries = [];
  for (let offset = 0; offset < bytes.length; offset += 8) {
    const start = view.getUint32(offset, true), end = view.getUint32(offset + 4, true);
    if (end < start) throw new Error(`FAT file ${offset / 8} has a negative size.`);
    range(romSize, start, end - start, `FAT file ${offset / 8}`);
    entries.push({ file_id: offset / 8, start, end, size: end - start });
  }
  return entries;
}

function component(name, fallback) { return name.replace(/[\\/\0]/g, "_").trim().replace(/^\.+|\.+$/g, "") || fallback; }
export function parseFnt(bytes, fatCount) {
  if (bytes.length < 8) throw new Error("The FNT is too small.");
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const directoryCount = view.getUint16(6, true);
  if (!directoryCount || directoryCount > 0x1000 || directoryCount * 8 > bytes.length)
    throw new Error("The FNT directory table is invalid.");
  const directories = Array.from({ length: directoryCount }, (_, i) => {
    const cursor = view.getUint32(i * 8, true);
    if (cursor < directoryCount * 8 || cursor >= bytes.length) throw new Error("A directory subtable points outside the FNT.");
    return { cursor, fileId: view.getUint16(i * 8 + 4, true) };
  });
  const paths = new Map(), seenDirectories = new Set(), seenPaths = new Set();
  // Iterative traversal bounds malformed nesting without risking the JS call stack.
  const stack = [{ id: ROOT_DIRECTORY_ID, path: "", depth: 0 }];
  while (stack.length) {
    const { id, path, depth } = stack.pop(), index = id - ROOT_DIRECTORY_ID;
    if (depth > 128 || index < 0 || index >= directoryCount || seenDirectories.has(id))
      throw new Error("The FNT has a cyclic, repeated or invalid directory reference.");
    seenDirectories.add(id);
    let { cursor, fileId } = directories[index];
    while (true) {
      if (cursor >= bytes.length) throw new Error("An FNT directory has no terminator.");
      const descriptor = bytes[cursor++];
      if (!descriptor) break;
      const length = descriptor & 0x7f, isDirectory = Boolean(descriptor & 0x80);
      if (!length || cursor + length + (isDirectory ? 2 : 0) > bytes.length)
        throw new Error("An FNT filename or directory reference is truncated.");
      const name = ascii(bytes.subarray(cursor, cursor + length)); cursor += length;
      if (isDirectory) {
        const child = view.getUint16(cursor, true); cursor += 2;
        const childPath = path + component(name, `dir_${child.toString(16).padStart(4,"0")}`) + "/";
        if (seenPaths.has(childPath) || seenPaths.has(childPath.slice(0,-1))) throw new Error("The FNT contains colliding paths.");
        seenPaths.add(childPath);
        stack.push({ id: child, path: childPath, depth: depth + 1 });
      } else {
        if (fileId >= fatCount || paths.has(fileId)) throw new Error("The FNT references an invalid or repeated FAT file.");
        const filePath = path + component(name, `file_${String(fileId).padStart(5,"0")}.bin`);
        if (seenPaths.has(filePath) || seenPaths.has(filePath + "/")) throw new Error("The FNT contains colliding paths.");
        seenPaths.add(filePath); paths.set(fileId++, filePath);
      }
    }
  }
  return paths;
}

// Hashes exactly as upstream: trimmed cartridges gain virtual FF bytes up to
// their declared capacity; the alternate digest zeros header free bytes 200-FFF.
export async function hashRom(file, { signal, onProgress } = {}) {
  const header = await read(file, 0, 0x200, "ROM header");
  if (file.size > MAX_ROM_SIZE) throw new Error("The ROM exceeds the supported 512 MiB cartridge limit.");
  const view = new DataView(header.buffer), used = view.getUint32(0x80, true);
  const capacity = header[0x14] <= 12 ? 0x20000 * 2 ** header[0x14] : file.size;
  const hashSize = 0x200 <= used && used <= file.size && file.size < capacity ? capacity : file.size;
  const full = new Sha256(), headerFree = new Sha256();
  for (let offset = 0; offset < hashSize; offset += CHUNK_SIZE) {
    abort(signal);
    const count = Math.min(CHUNK_SIZE, hashSize - offset);
    const bytes = new Uint8Array(count); bytes.fill(0xff);
    if (offset < file.size) bytes.set(await read(file, offset, Math.min(count, file.size - offset), "ROM data"));
    full.update(bytes);
    if (offset < 0x1000 && offset + count > 0x200) bytes.fill(0, Math.max(0,0x200-offset), Math.min(count,0x1000-offset));
    headerFree.update(bytes);
    progress(onProgress, "verify", Math.min(offset + count, hashSize), hashSize);
    // Allow paint, cancellation and keyboard handling between hash chunks.
    await new Promise(resolve => setTimeout(resolve, 0));
  }
  return { sha256: full.hex(), sha256_header_free: headerFree.hex(), hash_size: hashSize };
}

export async function inspectRom(file, options = {}) {
  abort(options.signal);
  const headerBytes = await read(file, 0, 0x200, "ROM header");
  const header = parseHeader(headerBytes, file.size);
  if (!SUPPORTED_ROMS.some(entry => entry.game_code === header.game_code && entry.rom_version === header.rom_version))
    throw new Error("Choose an unmodified North American or European Kingdom Hearts 358/2 Days ROM (revision 0).");
  const hashes = await hashRom(file, options);
  const supported = SUPPORTED_ROMS.find(entry => entry.sha256 === hashes.sha256 || entry.sha256_header_free === hashes.sha256_header_free);
  if (!supported) throw new Error("This ROM does not match the supported Days cartridge hashes. Choose an unmodified NA or EU copy.");
  const fat = parseFat(await read(file, header.fat_offset, header.fat_size, "FAT"), file.size);
  const paths = parseFnt(await read(file, header.fnt_offset, header.fnt_size, "FNT"), fat.length);
  const inspection = Object.freeze({ file, header:Object.freeze(header), ...hashes, supported, files:Object.freeze(fat.map(entry => Object.freeze({ ...entry,
    path: `nitrofs/${paths.get(entry.file_id) ?? `_unnamed/file_${String(entry.file_id).padStart(5,"0")}.bin`}` }))),
    named_file_count: paths.size });
  verifiedInspections.add(inspection);
  return inspection;
}

// FS is Emscripten's filesystem API. Import only after the selected runtime's
// before-start gate; callers must never send the File or extracted bytes online.
export async function importRomToFileSystem(inspection, FS, { signal, onProgress, root = "/data/extracted" } = {}) {
  if (!verifiedInspections.has(inspection)) throw new Error("The ROM must pass inspectRom before import.");
  if (!/^\/[a-zA-Z0-9_/-]+$/.test(root) || root.split("/").includes("..")) throw new Error("Invalid import directory.");
  const output = `${root.replace(/\/$/,"")}/${inspection.sha256.slice(0,16)}`;
  abort(signal);
  if (importingFilesystems.has(FS)) throw new Error("This game instance is already importing a ROM.");
  if (FS.analyzePath(output).exists) throw new Error("This ROM is already imported. Start a fresh game instance to reimport it.");
  importingFilesystems.add(FS);
  try {
    // The native VFS explicitly ignores .partial directories. Keep streaming
    // writes invisible until the entire extraction and its manifest are ready.
    const staging = output + ".partial";
    const { metadata, manifest } = await writeExtraction(inspection, FS, staging, { signal, onProgress });
    abort(signal);
    FS.rename(staging, output);
    return { output, metadata, manifest };
  } finally {
    importingFilesystems.delete(FS);
  }
}

async function writeExtraction(inspection, FS, output, { signal, onProgress }) {
  const { file, header, sha256, supported, files, named_file_count } = inspection;
  if (FS.analyzePath(`${output}/manifest.json`).exists) FS.unlink(`${output}/manifest.json`);
  const system = [ ["header.bin",0,0x200], ["arm9.bin",header.arm9_offset,header.arm9_size],
    ["arm7.bin",header.arm7_offset,header.arm7_size],
    ["arm9_overlay_table.bin",header.arm9_overlay_offset,header.arm9_overlay_size],
    ["arm7_overlay_table.bin",header.arm7_overlay_offset,header.arm7_overlay_size],
    ["fnt.bin",header.fnt_offset,header.fnt_size], ["fat.bin",header.fat_offset,header.fat_size] ];
  const records = [...system.filter(([, , size]) => size).map(([name,start,size]) => ({ path:`system/${name}`,start,size })), ...files];
  const total = records.reduce((sum, record) => sum + record.size, 0), manifest = { schema_version:1,system:{},files:[] };
  let completed = 0;
  for (const record of records) {
    abort(signal);
    const destination = `${output}/${record.path}`;
    FS.mkdirTree(destination.slice(0, destination.lastIndexOf("/")));
    const stream = FS.open(destination, "w"), hash = new Sha256();
    try {
      for (let position = 0; position < record.size; position += CHUNK_SIZE) {
        abort(signal);
        const bytes = await read(file, record.start + position, Math.min(CHUNK_SIZE, record.size - position), record.path);
        hash.update(bytes);
        if (FS.write(stream, bytes, 0, bytes.length, position) !== bytes.length) throw new Error(`Could not import ${record.path}.`);
        completed += bytes.length;
        progress(onProgress, "extract", completed, total);
      }
    } finally { FS.close(stream); }
    if (record.file_id === undefined) manifest.system[record.path.slice(7)] = { path:record.path,offset:record.start,size:record.size,sha256:hash.hex() };
    else manifest.files.push({ ...record,sha256:hash.hex() });
  }
  abort(signal);
  const metadata = { schema_version:1,source:{ filename:file.name ?? "local-rom.nds",file_size:file.size,sha256,supported:true,supported_entry:supported },
    header,extraction:{ nitrofs_file_count:files.length,named_file_count,unnamed_file_count:files.length-named_file_count } };
  FS.writeFile(`${output}/metadata.json`, encoder.encode(JSON.stringify(metadata, null, 2) + "\n"));
  FS.writeFile(`${output}/manifest.json`, encoder.encode(JSON.stringify(manifest, null, 2) + "\n"));
  return { metadata, manifest };
}
