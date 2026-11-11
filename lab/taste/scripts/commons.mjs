// Wikimedia Commons lookups for reference photos (author, licence, page); images stay out of the repo.
// usage: bun commons.mjs search "query" | cat "Category:..." | info "File:..." [thumbwidth] [outdir]
const UA = {"User-Agent":"SimCruiseTasteTest/1.0 (reference research)"};
const api = "https://commons.wikimedia.org/w/api.php?format=json&";
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const _f=globalThis.fetch;globalThis.fetch=async(u,o)=>{for(let i=0;i<6;i++){const r=await _f(u,o);if(r.status!==429&&r.ok)return r;await sleep(4000*(i+1));}return _f(u,o)};
const [mode, arg, w, out] = process.argv.slice(2);
if (mode === "search") {
  const r = await (await fetch(api+"action=query&list=search&srnamespace=6&srlimit=25&srsearch="+encodeURIComponent(arg),{headers:UA})).json();
  for (const s of r.query.search) console.log(s.title);
} else if (mode === "cat") {
  const r = await (await fetch(api+"action=query&list=categorymembers&cmlimit=300&cmtitle="+encodeURIComponent(arg),{headers:UA})).json();
  for (const s of r.query.categorymembers) console.log(s.title);
} else {
  const r = await (await fetch(api+"action=query&prop=imageinfo&iiprop=url|extmetadata|size&iiurlwidth="+(w||1280)+"&titles="+encodeURIComponent(arg),{headers:UA})).json();
  const p = Object.values(r.query.pages)[0]; const ii = p.imageinfo[0]; const m = ii.extmetadata;
  const strip = s => (s||"").replace(/<[^>]+>/g,"").trim();
  console.log(JSON.stringify({title:p.title, page:ii.descriptionurl, author:strip(m.Artist?.value), licence:m.LicenseShortName?.value, date:strip(m.DateTimeOriginal?.value), desc:strip(m.ImageDescription?.value).slice(0,200), thumb:ii.thumburl}));
  if (out) { const b = await (await fetch(ii.thumburl,{headers:UA})).arrayBuffer(); const name = out+"/"+p.title.replace(/^File:/,"").replace(/[^A-Za-z0-9._-]/g,"_"); await Bun.write(name, b); console.log("saved", name); }
}
