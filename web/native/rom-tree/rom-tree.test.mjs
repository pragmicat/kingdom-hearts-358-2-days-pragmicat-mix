import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';

const [factoryPath, importerPath] = process.argv.slice(2);
const { default: create } = await import(pathToFileURL(factoryPath));
const { parseHeader, parseFat, parseFnt, hashRom, inspectRom } =
    await import(pathToFileURL(importerPath));

const text = value => new TextEncoder().encode(value);
const cart = new Uint8Array(0x4800);
const view = new DataView(cart.buffer);
cart.set(text('SYNTHETIC   YKGP'));
view.setUint32(0x80, cart.length, true);

function section(data, offset) {
    cart.set(data, offset);
    return data.length;
}
function system(data, offset, addressField, sizeField) {
    view.setUint32(addressField, offset, true);
    view.setUint32(sizeField, section(data, offset), true);
}

system(Uint8Array.of(0xaa, 0xbb, 0xcc, 0xdd), 0x4000, 0x20, 0x2c);
system(Uint8Array.of(1, 2, 3, 4), 0x4040, 0x30, 0x3c);
system(new Uint8Array(32), 0x4020, 0x50, 0x54);

function fntName(name, directory = false) {
    const bytes = text(name);
    return [bytes.length | (directory ? 0x80 : 0), ...bytes];
}
const rootDir = [...fntName('folder', true), 0x01, 0xf0, ...fntName('compressed.z'), 0];
const childDir = [...fntName('note.bin'), 0];
const fnt = new Uint8Array(16 + rootDir.length + childDir.length);
const table = new DataView(fnt.buffer);
table.setUint32(0, 16, true);
table.setUint16(6, 2, true);
table.setUint32(8, 16 + rootDir.length, true);
table.setUint16(12, 1, true);
table.setUint16(14, 0xf000, true);
fnt.set(rootDir, 16);
fnt.set(childDir, 16 + rootDir.length);
system(fnt, 0x4080, 0x40, 0x44);

const files = [
    [0x4200, Uint8Array.of(0x11, 3, 0, 0, 0, 65, 66, 67)], // LZ11 literal "ABC"
    [0x4400, text('old!')],
    [0x4600, text('RAW')],
];
const fat = new Uint8Array(8 * files.length);
const entries = new DataView(fat.buffer);
for (const [id, [offset, bytes]] of files.entries()) {
    entries.setUint32(id * 8, offset, true);
    entries.setUint32(id * 8 + 4, offset + section(bytes, offset), true);
}
system(fat, 0x4100, 0x48, 0x4c);

const file = new Blob([cart]);
await assert.rejects(inspectRom(file), /does not match the supported Days cartridge hashes/);
const header = parseHeader(cart.subarray(0, 0x200), cart.length);
const read = (where, size) => cart.subarray(where, where + size);
const fatEntries = parseFat(read(header.fat_offset, header.fat_size), cart.length);
const paths = parseFnt(read(header.fnt_offset, header.fnt_size), fatEntries.length);
const { sha256 } = await hashRom(file);

const module = await create({ noInitialRun: true });
const { FS } = module;
const unpacked = `/data/extracted/${sha256.slice(0, 16)}`;
const write = (name, bytes) => {
    FS.mkdirTree(name.slice(0, name.lastIndexOf('/')));
    FS.writeFile(name, bytes);
};
// The synthetic fixture cannot pass the production hash gate. Stage its
// parsed bytes in the documented browser shape to exercise the native reader.
FS.mkdirTree('/data/extracted/0000000000000000.partial/nitrofs');
write(`${unpacked}/system/header.bin`, read(0, 0x200));
for (const [field, name] of [
    ['arm9', 'arm9.bin'], ['arm7', 'arm7.bin'],
    ['arm9_overlay', 'arm9_overlay_table.bin'], ['fnt', 'fnt.bin'], ['fat', 'fat.bin'],
]) {
    write(`${unpacked}/system/${name}`, read(header[`${field}_offset`], header[`${field}_size`]));
}
for (const { file_id, start, size } of fatEntries) {
    const name = paths.get(file_id) ?? `_unnamed/file_${String(file_id).padStart(5, '0')}.bin`;
    write(`${unpacked}/nitrofs/${name}`, read(start, size));
}
for (const name of ['Enabled', 'Ignored']) {
    const prefix = `/mods/${name}`;
    write(`${prefix}/mod.ini`, text(`id = fixture.${name.toLowerCase()}\nname = ${name}\nversion = 1\nkind = gameplay\napi = 2\n`));
    write(`${prefix}/files/folder/note.bin`, text(
        name === 'Enabled' ? 'longer mod payload' : 'wrong payload'));
}
write('/saves/mods/order.ini', text('Ignored = off\nEnabled = on\n'));
module.callMain([]);
