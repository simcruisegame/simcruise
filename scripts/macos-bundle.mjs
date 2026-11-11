// Inspect and package a macOS app bundle on any system (run with Bun or Node).
//
//   bun scripts/macos-bundle.mjs verify SimCruise.app [arch]   # default arch: arm64
//   bun scripts/macos-bundle.mjs zip SimCruise.app OUT.zip
//   bun scripts/macos-bundle.mjs thin BINARY arm64              # like lipo -thin, in place
//
// verify  checks every Mach-O file in the bundle statically, without macOS:
//         - it is a thin 64-bit Mach-O of the expected architecture;
//         - it carries an embedded code signature whose CodeDirectory hashes
//           (SHA-256, one per page) match the file's bytes;
//         - for the main executable, the Info.plist and _CodeSignature/CodeResources
//           hashes in the signature match those files, and every file sealed in
//           CodeResources (files2) still has the recorded SHA-256 (nested code:
//           the recorded CodeDirectory hash);
//         and fails if a file in the bundle is not sealed. It prints one line per
//         binary. It cannot prove that macOS will run the app: only a Mac can.
// zip     writes a zip that keeps what macOS needs, even when the bundle sits on
//         a Windows file system that has no permission bits: entries carry Unix
//         modes (0755 for directories and Mach-O files, 0644 otherwise) and
//         symbolic links are stored as links. Archive Utility and `ditto -x -k`
//         restore them. Entries are sorted and dated from each file's mtime.
import { createHash } from "node:crypto";
import { readFileSync, readdirSync, lstatSync, readlinkSync, writeFileSync, existsSync } from "node:fs";
import { join, basename, relative, sep } from "node:path";
import { deflateRawSync } from "node:zlib";

const CPU = { 0x01000007: "x86_64", 0x0100000c: "arm64" };
const die = (msg) => { console.error(msg); process.exit(1); };

function walk(dir, out = []) {
  for (const name of readdirSync(dir).sort()) {
    const p = join(dir, name);
    const st = lstatSync(p);
    out.push({ path: p, st });
    if (st.isDirectory() && !st.isSymbolicLink()) walk(p, out);
  }
  return out;
}

const isMachO = (buf) => {
  if (buf.length < 4) return false;
  const le = buf.readUInt32LE(0), be = buf.readUInt32BE(0);
  return le === 0xfeedfacf || le === 0xfeedface || be === 0xcafebabe || be === 0xcafebabf;
};

function readHead(path, n = 4) {
  const buf = readFileSync(path);
  return buf.subarray(0, n);
}

// Parse a thin 64-bit Mach-O and its embedded signature.
function machO(buf, name) {
  const be = buf.readUInt32BE(0);
  if (be === 0xcafebabe || be === 0xcafebabf) {
    const n = buf.readUInt32BE(4), archs = [], step = be === 0xcafebabf ? 32 : 20;
    for (let i = 0; i < n; i++) archs.push(CPU[buf.readUInt32BE(8 + i * step)] ?? "unknown");
    return { fat: true, archs };
  }
  if (buf.readUInt32LE(0) !== 0xfeedfacf) die(`${name}: not a 64-bit Mach-O`);
  const arch = CPU[buf.readUInt32LE(4)] ?? `cpu 0x${buf.readUInt32LE(4).toString(16)}`;
  const filetype = buf.readUInt32LE(12), ncmds = buf.readUInt32LE(16);
  let off = 32, sig = null;
  for (let i = 0; i < ncmds; i++) {
    const cmd = buf.readUInt32LE(off), size = buf.readUInt32LE(off + 4);
    if (cmd === 0x1d) sig = { off: buf.readUInt32LE(off + 8), size: buf.readUInt32LE(off + 12) };
    off += size;
  }
  const r = { fat: false, archs: [arch], filetype, signed: false };
  if (!sig) return r;
  const blob = buf.subarray(sig.off, sig.off + sig.size);
  if (blob.readUInt32BE(0) !== 0xfade0cc0) die(`${name}: signature is not an embedded SuperBlob`);
  const count = blob.readUInt32BE(8);
  let cd = null, cdBytes = null, cms = 0;
  for (let i = 0; i < count; i++) {
    const type = blob.readUInt32BE(12 + i * 8), boff = blob.readUInt32BE(16 + i * 8);
    const len = blob.readUInt32BE(boff + 4);
    if (type === 0 && blob.readUInt32BE(boff) === 0xfade0c02) { cd = boff; cdBytes = blob.subarray(boff, boff + len); }
    if (type === 0x10000) cms = len - 8; // CMS wrapper: empty for ad-hoc signatures
  }
  if (cd === null) die(`${name}: signature has no CodeDirectory`);
  const c = cdBytes;
  const flags = c.readUInt32BE(12), hashOffset = c.readUInt32BE(16), identOffset = c.readUInt32BE(20);
  const nSpecial = c.readUInt32BE(24), nCode = c.readUInt32BE(28), codeLimit = c.readUInt32BE(32);
  const hashSize = c[36], hashType = c[37], pageShift = c[39];
  if (hashType !== 2 || hashSize !== 32) die(`${name}: CodeDirectory hash type ${hashType} is not SHA-256`);
  const ident = c.subarray(identOffset, c.indexOf(0, identOffset)).toString();
  const page = 1 << pageShift;
  let bad = 0;
  for (let i = 0; i < nCode; i++) {
    const start = i * page, end = Math.min(start + page, codeLimit);
    const want = c.subarray(hashOffset + i * 32, hashOffset + (i + 1) * 32);
    if (!createHash("sha256").update(buf.subarray(start, end)).digest().equals(want)) bad++;
  }
  if (bad) die(`${name}: ${bad} of ${nCode} code page hashes do not match the file`);
  const special = (slot) => (slot <= nSpecial ? c.subarray(hashOffset - slot * 32, hashOffset - (slot - 1) * 32) : null);
  const cdhash = createHash("sha256").update(cdBytes).digest().subarray(0, 20);
  return { ...r, signed: true, adhoc: (flags & 0x2) !== 0, flags, ident, nCode, cms, special, cdhash };
}

// Minimal reader for the files2 dictionary of an XML CodeResources plist.
function sealedFiles(xml) {
  const start = xml.indexOf("<key>files2</key>");
  if (start < 0) die("CodeResources has no files2 dictionary");
  const body = xml.slice(xml.indexOf("<dict>", start) + 6);
  const files = new Map();
  const re = /<key>([^<]*)<\/key>\s*<dict>([\s\S]*?)<\/dict>/y;
  const ws = /\s*/y;
  let pos = 0;
  for (;;) {
    ws.lastIndex = pos; ws.exec(body); pos = ws.lastIndex;
    re.lastIndex = pos;
    const m = re.exec(body);
    if (!m) break;
    pos = re.lastIndex;
    const field = (k) => { const f = m[2].match(new RegExp(`<key>${k}</key>\\s*<(data|string)>([^<]*)</`)); return f ? f[2].replace(/\s+/g, "") : null; };
    const key = m[1].replace(/&amp;/g, "&").replace(/&lt;/g, "<").replace(/&gt;/g, ">");
    files.set(key, { hash2: field("hash2"), cdhash: field("cdhash"), symlink: field("symlink") });
  }
  return files;
}

function verify(app, wantArch) {
  if (!existsSync(join(app, "Contents", "Info.plist"))) die(`${app}: no Contents/Info.plist`);
  const plist = readFileSync(join(app, "Contents", "Info.plist"), "utf8");
  const exeName = plist.match(/<key>CFBundleExecutable<\/key>\s*<string>([^<]+)<\/string>/)?.[1];
  if (!exeName) die("Info.plist has no CFBundleExecutable");
  const entries = walk(app);
  const results = new Map();
  for (const { path, st } of entries) {
    if (!st.isFile()) continue;
    const buf = readFileSync(path);
    if (!isMachO(buf)) continue;
    const rel = relative(app, path).split(sep).join("/");
    const m = machO(buf, rel);
    if (m.fat) die(`${rel}: universal binary (${m.archs.join(", ")}); expected ${wantArch} only`);
    if (m.archs[0] !== wantArch) die(`${rel}: ${m.archs[0]}, expected ${wantArch}`);
    if (!m.signed) die(`${rel}: ${m.archs[0]} but has no code signature (Apple Silicon refuses to run it)`);
    results.set(rel, m);
    console.log(`ok   ${rel}: Mach-O ${m.archs[0]}, ${m.adhoc ? "ad-hoc" : "certificate"} signature, identifier ${m.ident}, ${m.nCode} page hashes match`);
  }
  const main = results.get(`Contents/MacOS/${exeName}`);
  if (!main) die(`main executable Contents/MacOS/${exeName} is missing or not Mach-O`);
  const sha = (p) => createHash("sha256").update(readFileSync(p)).digest();
  const infoSlot = main.special(1), resSlot = main.special(3);
  if (!infoSlot || !infoSlot.equals(sha(join(app, "Contents", "Info.plist")))) die("Info.plist does not match the main executable's signature");
  const resPath = join(app, "Contents", "_CodeSignature", "CodeResources");
  if (!existsSync(resPath)) die("no Contents/_CodeSignature/CodeResources (the bundle is not sealed)");
  if (!resSlot || !resSlot.equals(sha(resPath))) die("CodeResources does not match the main executable's signature");
  const sealed = sealedFiles(readFileSync(resPath, "utf8"));
  let checked = 0;
  for (const [rel, rec] of sealed) {
    const p = join(app, "Contents", ...rel.split("/"));
    if (rec.symlink !== null) { if (readlinkSync(p) !== rec.symlink) die(`${rel}: symlink target changed`); checked++; continue; }
    if (rec.cdhash) {
      const m = results.get(`Contents/${rel}`);
      if (!m || !m.cdhash.equals(Buffer.from(rec.cdhash, "base64"))) die(`${rel}: nested code does not match its sealed CodeDirectory hash`);
    } else if (!rec.hash2 || !sha(p).equals(Buffer.from(rec.hash2, "base64"))) die(`${rel}: contents differ from the sealed hash`);
    checked++;
  }
  const exempt = new Set([`MacOS/${exeName}`, "Info.plist", "_CodeSignature/CodeResources", "PkgInfo"]);
  const unsealed = entries.filter(({ path, st }) => !st.isDirectory())
    .map(({ path }) => relative(join(app, "Contents"), path).split(sep).join("/"))
    .filter((rel) => !exempt.has(rel) && !sealed.has(rel));
  if (unsealed.length) die(`not sealed by CodeResources: ${unsealed.slice(0, 5).join(", ")}${unsealed.length > 5 ? " ..." : ""}`);
  console.log(`ok   bundle seal: Info.plist and CodeResources match the main executable's signature; ${checked} sealed files verified, none unsealed`);
}

// --- zip writer --------------------------------------------------------------
const crcTable = new Int32Array(256).map((_, n) => { let c = n; for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; return c; });
const crc32 = (buf) => { let c = -1; for (let i = 0; i < buf.length; i++) c = crcTable[(c ^ buf[i]) & 0xff] ^ (c >>> 8); return (c ^ -1) >>> 0; };
const dosTime = (d) => ({
  time: (d.getHours() << 11) | (d.getMinutes() << 5) | (d.getSeconds() >> 1),
  date: (Math.max(d.getFullYear(), 1980) - 1980) << 9 | ((d.getMonth() + 1) << 5) | d.getDate(),
});

function zip(app, out) {
  const top = basename(app);
  const items = [{ path: app, st: lstatSync(app) }, ...walk(app)];
  const chunks = [], central = [];
  let offset = 0;
  for (const { path, st } of items) {
    let name = join(top, relative(app, path)).split(sep).join("/");
    let data, mode;
    if (st.isSymbolicLink()) { data = Buffer.from(readlinkSync(path)); mode = 0o120755; }
    else if (st.isDirectory()) { data = Buffer.alloc(0); mode = 0o040755; name += "/"; }
    else { data = readFileSync(path); mode = isMachO(data) || (st.mode & 0o111 && process.platform !== "win32") ? 0o100755 : 0o100644; }
    const crc = crc32(data);
    let method = 0, body = data;
    if (data.length && !st.isSymbolicLink()) {
      const d = deflateRawSync(data, { level: 9 });
      if (d.length < data.length) { method = 8; body = d; }
    }
    if (offset + body.length > 0xfffffff0) die("bundle too large for a zip without Zip64");
    const nameBuf = Buffer.from(name, "utf8");
    const { time, date } = dosTime(st.mtime);
    const local = Buffer.alloc(30);
    local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4); local.writeUInt16LE(0x0800, 6);
    local.writeUInt16LE(method, 8); local.writeUInt16LE(time, 10); local.writeUInt16LE(date, 12);
    local.writeUInt32LE(crc, 14); local.writeUInt32LE(body.length, 18); local.writeUInt32LE(data.length, 22);
    local.writeUInt16LE(nameBuf.length, 26); local.writeUInt16LE(0, 28);
    const cen = Buffer.alloc(46);
    cen.writeUInt32LE(0x02014b50, 0); cen.writeUInt16LE((3 << 8) | 20, 4); cen.writeUInt16LE(20, 6);
    cen.writeUInt16LE(0x0800, 8); cen.writeUInt16LE(method, 10); cen.writeUInt16LE(time, 12); cen.writeUInt16LE(date, 14);
    cen.writeUInt32LE(crc, 16); cen.writeUInt32LE(body.length, 20); cen.writeUInt32LE(data.length, 24);
    cen.writeUInt16LE(nameBuf.length, 28); cen.writeUInt32LE(((mode << 16) | (st.isDirectory() ? 0x10 : 0)) >>> 0, 38);
    cen.writeUInt32LE(offset, 42);
    chunks.push(local, nameBuf, body);
    central.push(cen, nameBuf);
    offset += 30 + nameBuf.length + body.length;
  }
  const cd = Buffer.concat(central);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(items.length, 8); end.writeUInt16LE(items.length, 10);
  end.writeUInt32LE(cd.length, 12); end.writeUInt32LE(offset, 16);
  writeFileSync(out, Buffer.concat([...chunks, cd, end]));
  console.log(`Wrote ${out} (${items.length} entries)`);
}

// Keep one architecture of a universal binary, in place (what `lipo -thin` does).
function thin(file, arch) {
  const buf = readFileSync(file);
  const m = machO(buf, file);
  if (!m.fat) { if (m.archs[0] !== arch) die(`${file} is ${m.archs[0]} only`); return; }
  const wide = buf.readUInt32BE(0) === 0xcafebabf, step = wide ? 32 : 20;
  for (let i = 0; i < buf.readUInt32BE(4); i++) {
    const e = 8 + i * step;
    if (CPU[buf.readUInt32BE(e)] !== arch) continue;
    const offset = wide ? Number(buf.readBigUInt64BE(e + 8)) : buf.readUInt32BE(e + 8);
    const size = wide ? Number(buf.readBigUInt64BE(e + 16)) : buf.readUInt32BE(e + 12);
    writeFileSync(file, buf.subarray(offset, offset + size));
    console.log(`Thinned ${basename(file)} to ${arch} (${(size / 1048576).toFixed(1)} of ${(buf.length / 1048576).toFixed(1)} MiB)`);
    return;
  }
  die(`${file} has no ${arch} slice (${m.archs.join(", ")})`);
}

const [cmd, a, b] = process.argv.slice(2);
if (cmd === "verify" && a) verify(a.replace(/[\\/]+$/, ""), b || "arm64");
else if (cmd === "zip" && a && b) zip(a.replace(/[\\/]+$/, ""), b);
else if (cmd === "thin" && a && b) thin(a, b);
else die("Usage: bun scripts/macos-bundle.mjs verify APP [arch] | zip APP OUT.zip | thin BINARY arch");
