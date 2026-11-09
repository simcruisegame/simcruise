// Run with Bun after installing the pinned upstream package in this directory.
// Split NIST's embedded wasm out of the generated JavaScript so the native
// helper carries one compressed copy. No archive bytes ever become source code.
import {fileURLToPath} from 'node:url';
import fs from 'node:fs';
import path from 'node:path';
import {brotliCompressSync,constants} from 'node:zlib';
import {createHash} from 'node:crypto';
const out=path.resolve(import.meta.dirname,'../../helper/vendor/h5wasm');
const upstream=path.dirname(fileURLToPath(import.meta.resolve('h5wasm')));
const wasmLoader=fs.readFileSync(path.join(upstream,'hdf5_util.js'),'utf8');
const entry=fs.readFileSync(path.join(upstream,'hdf5_hl.js'),'utf8');
const start=wasmLoader.indexOf('function findWasmBinary(){return binaryDecode(');
const end=wasmLoader.indexOf('function getBinarySync(',start);
if(start<0||end<0)throw Error('Unexpected upstream HDF5 glue');
let bytes;
const instantiate=WebAssembly.instantiate;
WebAssembly.instantiate=async function(binary,...args){
 if(binary instanceof Uint8Array && binary[0]===0 && binary[1]===97)bytes=Buffer.from(binary);
 return instantiate.call(WebAssembly,binary,...args);
};
try{await (await import('h5wasm')).default.ready;}finally{WebAssembly.instantiate=instantiate;}
if(!bytes||!WebAssembly.validate(bytes))throw Error('Upstream wasm unavailable');
fs.mkdirSync(out,{recursive:true});
const digest=createHash('sha256').update(bytes).digest('hex');
fs.writeFileSync(path.join(out,'hdf5.wasm.br'),brotliCompressSync(bytes,{params:{[constants.BROTLI_PARAM_QUALITY]:9}}));
const preamble="/* NIST h5wasm 0.10.3. Embedded library split into an unchanged Brotli asset, 2026-11-08. See LICENSE.txt. */\nimport asset from './hdf5.wasm.br' with {type:'file'};\nimport {brotliDecompressSync} from 'node:zlib';\nconst engine=brotliDecompressSync(await Bun.file(asset).arrayBuffer());\n";
fs.writeFileSync(path.join(out,'hdf5_util.js'),preamble+wasmLoader.slice(0,start)+'function findWasmBinary(){return engine;}'+wasmLoader.slice(end));
fs.writeFileSync(path.join(out,'hdf5_hl.js'),entry);
fs.copyFileSync(path.resolve(upstream,'../../LICENSE.txt'),path.join(out,'LICENSE.txt'));
console.log('libhdf5 wasm SHA-256 '+digest+' / '+bytes.length+' source bytes');
