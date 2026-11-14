"""Fetch the open-licence AI models the development tools use, into Saved/AI.

The scene parser's open model is the semantic head of Detectron2's Panoptic FPN
(R50, COCO panoptic; Apache-2.0 code, CC BY-SA 3.0 weights per Detectron2's
MODEL_ZOO.md, COCO annotations CC BY 4.0),
exported to ONNX by Qualcomm AI Hub. Only its first part (backbone, FPN,
proposals and the 54-class semantic logits) is kept: 120 MB, so it is fetched,
not committed.

    python -I scripts/ai/fetch_open_models.py
"""
import io
import os
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import embed_onnx_external  # noqa: E402

URL = ('https://qaihub-public-assets.s3.us-west-2.amazonaws.com/qai-hub-models/models/'
       'detectron2_panoptic_seg/releases/v0.66.0/detectron2_panoptic_seg-onnx-float.zip')
OUT = os.path.join(ROOT, 'Saved', 'AI')
TARGET = os.path.join(OUT, 'detectron2-panoptic-semseg-coco.onnx')


def main():
    if os.path.exists(TARGET):
        print('already there:', TARGET)
        return
    os.makedirs(OUT, exist_ok=True)
    work = os.path.join(OUT, 'download')
    os.makedirs(work, exist_ok=True)
    req = urllib.request.Request(URL, headers={'User-Agent': 'SimCruise model fetch (+https://github.com/simcruisegame/simcruise)'})
    print('downloading', URL)
    data = urllib.request.urlopen(req, timeout=600).read()
    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for name in z.namelist():
            if 'proposal_generator' in name and not name.endswith('/'):
                dest = os.path.join(work, os.path.basename(name))
                with open(dest, 'wb') as f:
                    f.write(z.read(name))
    embed_onnx_external.main(os.path.join(work, 'proposal_generator.onnx'), TARGET)
    for f in os.listdir(work):
        os.remove(os.path.join(work, f))
    os.rmdir(work)
    print('ready:', TARGET)


if __name__ == '__main__':
    main()
