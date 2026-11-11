// Find licensed Panoramax frames near a point, or fetch one for viewing (images stay out of the repo).
// usage: bun panoramax-near.mjs search LAT LON HALF_DEG  |  bun panoramax-near.mjs get ID OUTDIR
const [mode, a, b, c] = process.argv.slice(2);
const H = {"User-Agent":"SimCruiseTasteTest/1.0"};
if (mode === "search") {
  const lat=+a, lon=+b, d=+(c||.002);
  const url = `https://api.panoramax.xyz/api/search?bbox=${lon-d},${lat-d},${lon+d},${lat+d}&limit=100`;
  const j = await (await fetch(url,{headers:H})).json();
  for (const f of j.features) {
    const p=f.properties, prod=(f.providers||[]).find(s=>s.roles?.includes("producer"))?.name;
    console.log([f.id, f.geometry.coordinates[1].toFixed(6), f.geometry.coordinates[0].toFixed(6), p["view:azimuth"], p["pers:interior_orientation"]?.field_of_view, p.datetime, p.license, prod, p["pers:interior_orientation"]?.camera_model].join(" | "));
  }
} else {
  const url = `https://api.panoramax.xyz/api/search?ids=${a}`;
  const j = await (await fetch(url,{headers:H})).json();
  const f = j.features[0];
  console.log(JSON.stringify({id:f.id, coords:f.geometry.coordinates, props:f.properties, providers:f.providers, assets:Object.fromEntries(Object.entries(f.assets).map(([k,v])=>[k,v.href]))}, null, 1));
  const href = (f.assets.sd||f.assets.hd||f.assets.thumb).href;
  const buf = await (await fetch(href,{headers:H})).arrayBuffer();
  await Bun.write(`${b}/pano-${a}.jpg`, buf); console.log("saved", `${b}/pano-${a}.jpg`, buf.byteLength);
}
