import {VectorTile} from '@mapbox/vector-tile';
import Pbf from 'pbf';

export function vectorTile(bytes){
 // pbf 4 reads fixed-width values against the backing ArrayBuffer. Node's
 // pooled Buffers and subarray views can start at a nonzero byte offset.
 return new VectorTile(new Pbf(bytes.byteOffset ? Uint8Array.from(bytes) : bytes));
}
