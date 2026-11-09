# Street photograph masks

This offline authoring tool segments attributed KartaView frames on the CPU. It reads an atlas street-samples JSON file and exports pixel label PNGs, a provenance record and, for a matched camera profile, conservative masks for comparing multiple views. Godot does not load the model.

Install Python 3.11–3.14, Pillow, NumPy and the pinned LiteRT runtime in a separate environment. Download the 2.42 MB model from its publisher. The script checks the extracted model’s SHA-256.

```sh
python3 -m venv .semantic-venv
.semantic-venv/bin/python -m pip install 'ai-edge-litert==2.1.5' numpy Pillow
curl -fL 'https://api.kaggle.com/v1/models/spsayakpaul/deeplabv3-mobilenetv2/tfLite/ade20k/2/download' -o model.tar.gz
tar -xzf model.tar.gz 2.tflite
.semantic-venv/bin/python segment.py samples.json 2.tflite masks \
  --camera-profile kartaview-11591982.json --compare-id 2625241458
```

The manifest supplies an array of `frames` with `id`, `sequenceId`, `sourceUrl`, `author` and `imagePath` (absolute or relative to the manifest), plus `licenseUrl` and `attribution`. The checked-in profile was marked against Miami frames 216–218 and covers the reflecting camera car. For another sequence omit the profile to obtain raw labels, or supply a reviewed profile for that camera. Profile polygons use their recorded pixel coordinate system. Exclusions must account for the actual camera position and any timestamp.

`*.labels.png` contains the raw 0–150 label index. `*.keep.png` contains 0 for withheld pixels, 1 for structure, 2 for trees and shrubs, 3 for grass, 4 for road and 5 for sidewalk. A 0.50 minimum winning softmax score and a two-pixel boundary erosion reject uncertain pixels. This threshold is a model score and has no measured probability calibration on KartaView. The masks do not establish depth, identify a building footprint or license invented facade details. Use nearest-neighbor resampling for class masks and geometry checks before projecting pixels. Deriving a surface texture from these photos requires preserving the photo attribution.

## Rights and provenance

The publisher marks this exact converted checkpoint Apache-2.0. It derives from Google’s MobileNetV2 DeepLabv3 ADE20K release. The 151 classes include the background class. Google’s LiteRT source is Apache-2.0. The authoring tool uses separately installed runtime packages and separately downloaded weights. Distributors of a bundled binary or checkpoint must retain applicable license/NOTICE and dependency notices for their actual distribution. [Checkpoint](https://www.kaggle.com/models/spsayakpaul/deeplabv3-mobilenetv2/TfLite/ade20k/2) · [Conversion provenance](https://github.com/tensorflow/tfhub.dev/blob/master/assets/docs/sayakpaul/deeplabv3-mobilenetv2-ade20k/1/default/lite/2.md) · [LiteRT license](https://github.com/google-ai-edge/LiteRT/blob/main/LICENSE) · [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0).

MIT’s terms restrict obtaining original ADE20K photographs to research and education and give separate terms for annotations and software. This tool consumes the published model. It does not redistribute the training imagery. Independent rights in each underlying training image have not been audited. [ADE20K terms](https://ade20k.csail.mit.edu/terms/).

Miami comparison photographs and derived masks are adapted from frames 216–218 of sequence 11591982, captured by mrmappenburger, © Grab and KartaView contributors, under CC BY-SA 4.0. Changes include resizing, semantic inference, masking and color overlay. Retain the receipt and credit when sharing those assets. [Frame 216](https://kartaview.org/details/11591982/216) · [Frame 217](https://kartaview.org/details/11591982/217) · [Frame 218](https://kartaview.org/details/11591982/218) · [CC BY-SA 4.0](https://creativecommons.org/licenses/by-sa/4.0/).
