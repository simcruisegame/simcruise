"""Embed an ONNX model's external weights (a .data file) into the .onnx file itself.

Unreal's NNE takes one model buffer at runtime; models exported with external data
(Qualcomm AI Hub, large PyTorch exports) keep their weights beside the graph. This
rewrites the protobuf with only the standard library: every TensorProto initializer
whose data_location is EXTERNAL gets its bytes as raw_data; nothing else changes.

    python -I scripts/ai/embed_onnx_external.py model.onnx out.onnx
"""
import os
import sys


def varint(buf, i):
    shift = result = 0
    while True:
        b = buf[i]
        i += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, i
        shift += 7


def enc_varint(n):
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return bytes(out)


def fields(buf):
    """Yield (number, wire type, payload, raw bytes of the whole field)."""
    i = 0
    while i < len(buf):
        start = i
        key, i = varint(buf, i)
        num, wt = key >> 3, key & 7
        if wt == 0:
            _, i = varint(buf, i)
            payload = None
        elif wt == 1:
            i += 8
            payload = None
        elif wt == 5:
            i += 4
            payload = None
        elif wt == 2:
            n, j = varint(buf, i)
            payload = buf[j:j + n]
            i = j + n
        else:
            raise ValueError('unsupported wire type %d' % wt)
        yield num, wt, payload, buf[start:i]


def field_len(num, payload):
    return enc_varint(num << 3 | 2) + enc_varint(len(payload)) + payload


def fix_tensor(tensor, data):
    loc = {}
    keep = []
    external = False
    for num, wt, payload, raw in fields(tensor):
        if num == 13:
            k = v = b''
            for n2, _, p2, _ in fields(payload):
                if n2 == 1:
                    k = bytes(p2)
                elif n2 == 2:
                    v = bytes(p2)
            loc[k.decode()] = v.decode()
        elif num == 14:
            # data_location: 0 DEFAULT (raw data here), 1 EXTERNAL.
            external = varint(raw, 1)[0] == 1
            if not external:
                keep.append(raw)
        else:
            keep.append(raw)
    if not external and not loc:
        return tensor, 0
    offset = int(loc.get('offset', '0'))
    # 'length' is optional: without it the tensor runs to the end of the file.
    length = int(loc['length']) if 'length' in loc else len(data) - offset
    blob = data[offset:offset + length]
    if len(blob) != length:
        raise ValueError('external data out of range')
    return b''.join(keep) + field_len(9, blob), length


def fix_graph(graph, data):
    out = []
    total = 0
    for num, wt, payload, raw in fields(graph):
        if num == 5:
            t, n = fix_tensor(payload, data)
            total += n
            out.append(field_len(5, t))
        else:
            out.append(raw)
    return b''.join(out), total


def main(src, dst):
    model = open(src, 'rb').read()
    base = os.path.splitext(src)[0]
    data = open(base + '.data', 'rb').read() if os.path.exists(base + '.data') else b''
    out = []
    total = 0
    for num, wt, payload, raw in fields(model):
        if num == 7:
            g, total = fix_graph(payload, data)
            out.append(field_len(7, g))
        else:
            out.append(raw)
    open(dst, 'wb').write(b''.join(out))
    print('embedded %d bytes of weights; wrote %s (%d bytes)' % (total, dst, os.path.getsize(dst)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
