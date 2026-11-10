#!/usr/bin/env bun
// Sketchfab → SimCruise traffic models.
//
//   bun authoring/cars/sketchfab.mjs search "generic sedan" [--license=by,cc0] [--max-faces=120000] [--count=12]
//   bun authoring/cars/sketchfab.mjs fetch <uid> --kind=sedan --length=4.7 [--flip] [--allow-brand]
//   bun authoring/cars/sketchfab.mjs credits
//
// Search is keyless. Fetching needs your own Sketchfab API token in the
// environment (Settings → Password & API on sketchfab.com):
//   SKETCHFAB_API_TOKEN=...   (sent as "Authorization: Token …")
// The token never enters the repository, the game or the logs.
//
// Licence gate: only CC0 and CC BY models are fetched, because only those can be
// committed to an MIT repository and shipped inside the game. CC BY-SA, ND, NC
// and Store/Editorial licences are refused. Every fetched model is recorded in
// game/art/traffic/manifest.json with its author, licence and source URL, and
// `credits` regenerates licenses/Traffic models.txt from that manifest.
//
// Trademarks are not licensed by Creative Commons (CC BY 4.0 §2(b)(2)). The
// project uses made-up car names and no badges (docs/cars.md), so `fetch`
// refuses models whose title or tags name a car maker unless --allow-brand is
// given, and then reminds you to remove badges and lettering before committing.
//
// Models arrive in arbitrary scale and orientation. AmbientLife.gd normalises
// them at load: longest horizontal axis along Z, scaled to --length metres,
// wheels on the ground. --flip turns a model that drives backwards.

import { mkdirSync, writeFileSync, readFileSync, existsSync } from "node:fs";
import { join, dirname, resolve } from "node:path";
import { unzipSync } from "fflate";

const API = "https://api.sketchfab.com/v3";
const ROOT = resolve(dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Za-z]:)/, "$1")), "../..");
const OUT = join(ROOT, "game/art/traffic");
const MANIFEST = join(OUT, "manifest.json");
const ALLOWED = { cc0: "CC0 1.0", by: "CC BY 4.0" };
const BRANDS = ["toyota","honda","ford","chevrolet","chevy","gmc","dodge","ram ","jeep","chrysler","cadillac","buick","lincoln","tesla","nissan","subaru","mazda","mitsubishi","hyundai","kia","volkswagen","vw ","audi","bmw","mercedes","porsche","ferrari","lamborghini","volvo","lexus","acura","infiniti","oldsmobile","pontiac","plymouth","jaguar","land rover","mini ","fiat","alfa","peugeot","renault","skoda","seat ","opel","lada","rivian","lucid"];
const UA = { "User-Agent": "SimCruise-authoring" };

const args = process.argv.slice(2);
const flag = (name, fallback) => {
  const hit = args.find((a) => a === `--${name}` || a.startsWith(`--${name}=`));
  if (!hit) return fallback;
  return hit.includes("=") ? hit.slice(hit.indexOf("=") + 1) : true;
};
const positional = args.filter((a) => !a.startsWith("--"));

async function json(url, headers = {}) {
  const r = await fetch(url, { headers: { ...UA, ...headers } });
  if (!r.ok) throw new Error(`${r.status} ${r.statusText} for ${url.replace(/token=[^&]+/, "token=…")}`);
  return r.json();
}

function licenceSlug(model) {
  const l = model.license;
  return (typeof l === "string" ? l : l?.slug || l?.label || "").toLowerCase();
}

// Makers named in the title (blocking) and only in the tags (a warning: tags
// often cite the cars a generic body was styled after).
function branded(model) {
  const title = ` ${model.name} `.toLowerCase();
  return BRANDS.filter((b) => title.includes(b));
}
function taggedBrands(model) {
  const tags = ` ${(model.tags || []).map((t) => t.name || t).join(" ")} `.toLowerCase();
  return BRANDS.filter((b) => tags.includes(b));
}

function readManifest() {
  return existsSync(MANIFEST) ? JSON.parse(readFileSync(MANIFEST, "utf8")) : { models: [] };
}

async function search() {
  const q = positional[1] || "generic car";
  const licences = String(flag("license", "by,cc0")).split(",");
  const maxFaces = Number(flag("max-faces", 120000));
  const count = Number(flag("count", 12));
  for (const license of licences) {
    const url = `${API}/search?type=models&q=${encodeURIComponent(q)}&downloadable=true&license=${license}&categories=cars-vehicles&archives_flavours=true&max_face_count=${maxFaces}&count=${count}`;
    const j = await json(url);
    console.log(`\n${ALLOWED[license] || license} · "${q}"`);
    for (const m of j.results) {
      const glb = m.archives?.glb?.size ? `${(m.archives.glb.size / 1e6).toFixed(1)} MB glb` : "";
      const brands = branded(m), tagged = taggedBrands(m);
      const note = brands.length ? "  ✗ maker in title: " + brands.join(",") : tagged.length ? "  (tags cite: " + tagged.join(",").replace(/ /g, "") + ")" : "";
      console.log(`  ${m.uid}  ${m.name.slice(0, 44).padEnd(44)}  ${m.user.username.padEnd(18)}  ${String(m.faceCount).padStart(7)} faces  ${glb}${note}`);
    }
  }
}

async function fetchModel() {
  const uid = positional[1];
  if (!uid || !/^[0-9a-f]{32}$/.test(uid)) throw new Error("fetch needs a 32-character Sketchfab model uid");
  const token = process.env.SKETCHFAB_API_TOKEN;
  if (!token) throw new Error("set SKETCHFAB_API_TOKEN (sketchfab.com → Settings → Password & API) to download");
  const kind = String(flag("kind", "sedan"));
  const length = Number(flag("length", kind === "pickup" ? 5.9 : kind === "suv" ? 4.9 : 4.7));
  const model = await json(`${API}/models/${uid}`);
  const slug = licenceSlug(model);
  const licence = ALLOWED[slug] || (slug.includes("cc0") ? ALLOWED.cc0 : slug === "cc attribution" ? ALLOWED.by : null);
  if (!licence) throw new Error(`refused: licence "${slug}" is not CC0 or CC BY`);
  const brands = branded(model);
  if (brands.length && !flag("allow-brand", false)) throw new Error(`refused: names a car maker (${brands.join(", ")}); pick a generic model or pass --allow-brand and remove badges`);
  const dl = await json(`${API}/models/${uid}/download`, { Authorization: `Token ${token}` });
  if (!dl.gltf?.url) throw new Error("no glTF archive offered for this model");
  const r = await fetch(dl.gltf.url, { headers: UA });   // temporary signed URL, not cached
  if (!r.ok) throw new Error(`archive download failed: ${r.status}`);
  const files = unzipSync(new Uint8Array(await r.arrayBuffer()));
  const id = `${kind}-${uid.slice(0, 8)}`;
  const dir = join(OUT, id);
  for (const [name, data] of Object.entries(files)) {
    if (name.endsWith("/")) continue;
    const safe = name.replace(/\\/g, "/").split("/").filter((p) => p && p !== ".." && p !== ".").join("/");
    mkdirSync(dirname(join(dir, safe)), { recursive: true });
    writeFileSync(join(dir, safe), data);
  }
  const manifest = readManifest();
  manifest.models = manifest.models.filter((m) => m.uid !== uid);
  manifest.models.push({
    id, uid, kind, length_m: length, flip: Boolean(flag("flip", false)),
    scene: `res://art/traffic/${id}/scene.gltf`,
    title: model.name, author: model.user.displayName || model.user.username,
    author_url: model.user.profileUrl, licence, source_url: model.viewerUrl,
    faces: model.faceCount, fetched: new Date().toISOString().slice(0, 10),
    branded: brands,
  });
  mkdirSync(OUT, { recursive: true });
  writeFileSync(MANIFEST, JSON.stringify(manifest, null, 2) + "\n");
  writeCredits(manifest);
  console.log(`${model.name} by ${model.user.username} (${licence}) → game/art/traffic/${id}/`);
  if (brands.length) console.log("Remember: remove badges, grille logos and lettering before committing.");
  console.log("Open the project once in Godot (or run --headless --import) so the glTF is imported.");
}

function writeCredits(manifest = readManifest()) {
  const lines = ["Traffic models (game/art/traffic), fetched from Sketchfab with authoring/cars/sketchfab.mjs.", "Each is used under the licence named; trademarks, if any, are not licensed.", ""];
  for (const m of manifest.models) lines.push(`${m.id}: "${m.title}" by ${m.author} (${m.author_url}), ${m.licence}, ${m.source_url}`);
  writeFileSync(join(ROOT, "licenses/Traffic models.txt"), lines.join("\n") + "\n");
}

const command = positional[0];
try {
  if (command === "search") await search();
  else if (command === "fetch") await fetchModel();
  else if (command === "credits") writeCredits();
  else console.log(readFileSync(new URL(import.meta.url), "utf8").split("\n").filter((l) => l.startsWith("//")).map((l) => l.slice(3)).join("\n"));
} catch (e) {
  console.error(e.message);
  process.exit(1);
}
