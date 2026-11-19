// Read a single cartridge from a player's ZIP without extracting archive paths.
// ZIP offsets and sizes below are the classic single-volume ZIP fields.
const END = 0x06054b50, CENTRAL = 0x02014b50, LOCAL = 0x04034b50;
const DIRECTORY_LIMIT = 16 * 1024 * 1024;
const crcTable = Uint32Array.from({ length: 256 }, (_, byte) => {
  let value = byte;
  for (let bit = 0; bit < 8; ++bit) value = (value >>> 1) ^ ((value & 1) ? 0xedb88320 : 0);
  return value;
});

async function readPart(file, offset, length) {
  if (offset < 0 || length < 0 || offset + length > file.size)
    throw new Error("The ZIP is incomplete.");
  const bytes = await file.slice(offset, offset + length).arrayBuffer();
  if (bytes.byteLength !== length) throw new Error("The ZIP changed while it was being opened.");
  return new DataView(bytes);
}

async function cartridgeEntry(file) {
  const tailSize = Math.min(file.size, 22 + 0xffff);
  const tailOffset = file.size - tailSize;
  const tail = await readPart(file, tailOffset, tailSize);
  let end = tailSize - 22;
  while (end >= 0 && (tail.getUint32(end, true) !== END ||
      end + 22 + tail.getUint16(end + 20, true) !== tailSize)) --end;
  if (end < 0) throw new Error("Could not find the end of the ZIP. Choose a complete archive.");
  const entries = tail.getUint16(end + 10, true);
  const length = tail.getUint32(end + 12, true);
  const directoryOffset = tail.getUint32(end + 16, true);
  if (tail.getUint32(end + 4, true) !== 0 || tail.getUint16(end + 8, true) !== entries ||
      entries === 0xffff || length === 0xffffffff || directoryOffset === 0xffffffff)
    throw new Error("Choose a single-volume, non-ZIP64 archive or the .nds file inside it.");
  if (length > DIRECTORY_LIMIT || directoryOffset + length > tailOffset + end)
    throw new Error("The ZIP directory is too large or extends past the end of the archive.");

  const dir = await readPart(file, directoryOffset, length);
  const decoder = new TextDecoder();
  let entry, pos = 0;
  for (let n = 0; n < entries; ++n) {
    if (pos + 46 > length || dir.getUint32(pos, true) !== CENTRAL)
      throw new Error("The ZIP directory is incomplete.");
    const nameSize = dir.getUint16(pos + 28, true);
    const next = pos + 46 + nameSize + dir.getUint16(pos + 30, true) + dir.getUint16(pos + 32, true);
    if (next > length) throw new Error("A ZIP entry extends past its directory.");
    const nameBytes = new Uint8Array(dir.buffer, pos + 46, nameSize);
    const path = decoder.decode(nameBytes).replaceAll("\\", "/");
    const parts = path.split("/");
    const name = parts.pop();
    const attributes = dir.getUint32(pos + 38, true);
    const unixType = (attributes >>> 28) & 15;
    if (/\.nds$/i.test(name) && !name.startsWith("._") && !parts.includes("__MACOSX") &&
        !(attributes & 0x10) && (unixType === 0 || unixType === 8)) {
      if (entry) throw new Error("This ZIP contains several .nds files. Choose the Days .nds file directly.");
      if (dir.getUint16(pos + 34, true)) throw new Error("The cartridge is on another ZIP volume.");
      entry = { name, nameBytes: nameBytes.slice(), flags: dir.getUint16(pos + 8, true),
        method: dir.getUint16(pos + 10, true), crc: dir.getUint32(pos + 16, true),
        packed: dir.getUint32(pos + 20, true), size: dir.getUint32(pos + 24, true),
        offset: dir.getUint32(pos + 42, true), directoryOffset };
    }
    pos = next;
  }
  if (!entry) throw new Error("Choose a ZIP containing one Kingdom Hearts 358/2 Days .nds file.");
  return entry;
}

async function memberData(file, entry, limit) {
  if (entry.flags & ~0x080e || (entry.method !== 0 && entry.method !== 8))
    throw new Error("Use an unencrypted stored or deflated ZIP, or choose its .nds file directly.");
  if (entry.size < 512 || entry.size > limit)
    throw new Error("The cartridge in the ZIP is outside the supported 512 MiB limit or is too small.");
  // DEFLATE can be slightly larger than its input. Bound compressed work too.
  if (entry.packed > limit + 1024 * 1024 || (entry.method === 0 && entry.packed !== entry.size))
    throw new Error("The cartridge's compressed size is invalid.");
  const local = await readPart(file, entry.offset, 30);
  const nameSize = local.getUint16(26, true);
  const start = entry.offset + 30 + nameSize + local.getUint16(28, true);
  if (local.getUint32(0, true) !== LOCAL || local.getUint16(6, true) !== entry.flags ||
      local.getUint16(8, true) !== entry.method || nameSize !== entry.nameBytes.length ||
      start + entry.packed > entry.directoryOffset)
    throw new Error("The ZIP cartridge header does not match its directory.");
  const variable = await readPart(file, entry.offset + 30, nameSize + local.getUint16(28, true));
  if (entry.nameBytes.some((byte, index) => variable.getUint8(index) !== byte))
    throw new Error("The ZIP cartridge names disagree.");
  // Streaming writers put sizes in the directory. Some seekable writers use
  // a ZIP64 local extra even for a small member in an otherwise classic ZIP.
  if (!(entry.flags & 8)) {
    let size = local.getUint32(22, true), packed = local.getUint32(18, true);
    if (size === 0xffffffff || packed === 0xffffffff) {
      for (let pos = nameSize; pos + 4 <= variable.byteLength;) {
        const tag = variable.getUint16(pos, true);
        const end = pos + 4 + variable.getUint16(pos + 2, true);
        if (end > variable.byteLength) throw new Error("The ZIP local size field is incomplete.");
        if (tag === 1) {
          let next = pos + 4;
          if (size === 0xffffffff && next + 8 <= end) {
            size = Number(variable.getBigUint64(next, true));
            next += 8;
          }
          if (packed === 0xffffffff && next + 8 <= end)
            packed = Number(variable.getBigUint64(next, true));
          break;
        }
        pos = end;
      }
    }
    if (local.getUint32(14, true) !== entry.crc || size !== entry.size || packed !== entry.packed)
      throw new Error("The ZIP cartridge size or checksum disagrees with its directory.");
  }
  return file.slice(start, start + entry.packed);
}

async function unpack(data, entry, { signal, onProgress }) {
  // Blob.stream() may hand the inflater one enormous chunk. Limit each pull
  // so even a highly compressed input reaches our output bound incrementally.
  let position = 0;
  let stream = new ReadableStream({
    async pull(controller) {
      signal?.throwIfAborted();
      if (position === data.size) { controller.close(); return; }
      const length = Math.min(64 * 1024, data.size - position);
      const chunk = await readPart(data, position, length);
      position += length;
      controller.enqueue(new Uint8Array(chunk.buffer));
    },
  });
  if (entry.method === 8) {
    let inflater;
    try { inflater = new DecompressionStream("deflate-raw"); }
    catch { throw new Error("This browser cannot decompress the ZIP. Choose the .nds file directly."); }
    stream = stream.pipeThrough(inflater, { signal });
  }
  let count = 0, crc = 0xffffffff, reported = 0;
  onProgress?.({ phase: "unzip", completed: 0, total: entry.size });
  const checked = stream.pipeThrough(new TransformStream({
    transform(bytes, controller) {
      signal?.throwIfAborted();
      count += bytes.length;
      if (count > entry.size) throw new Error("The cartridge expands beyond the size listed in the ZIP.");
      for (let i = 0; i < bytes.length; ++i) crc = crcTable[(crc ^ bytes[i]) & 255] ^ (crc >>> 8);
      if (count - reported >= 1024 * 1024) {
        reported = count;
        onProgress?.({ phase: "unzip", completed: count, total: entry.size });
      }
      if (entry.method === 8) controller.enqueue(bytes);
    },
    flush() {
      if (count !== entry.size || ((crc ^ 0xffffffff) >>> 0) !== entry.crc)
        throw new Error("The cartridge in the ZIP is incomplete or its checksum is incorrect.");
      if (reported !== count) onProgress?.({ phase: "unzip", completed: count, total: count });
    },
  }), { signal });
  // A Blob gives the native importer random access for hashing and the game
  // filesystem. Stored entries can reuse their original slice after CRC checks.
  const inflated = await new Response(checked).blob();
  signal?.throwIfAborted();
  return entry.method === 0 ? data : inflated;
}

export async function openCartridgeFile(file, sizeLimit, options = {}) {
  options.signal?.throwIfAborted();
  const signature = file.size >= 4 ? (await readPart(file, 0, 4)).getUint32(0, true) : 0;
  if (signature !== LOCAL && signature !== END && !/\.zip$/i.test(file.name ?? "")) return file;
  const entry = await cartridgeEntry(file);
  options.signal?.throwIfAborted();
  const compressed = await memberData(file, entry, sizeLimit);
  const contents = await unpack(compressed, entry, options);
  return typeof File === "function" ? new File([contents], entry.name) : contents;
}
