"""Offline semantic mask preparation for an attributed KartaView frame manifest."""
import argparse
import hashlib
import json
from pathlib import Path
import time
import zipfile

from ai_edge_litert.interpreter import Interpreter
import numpy as np
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageOps

MODEL_SHA256 = "0acdf37d5db38e681646ce79557c2af516650ecca5377b15c9cb171f751556cb"
MODEL_URL = "https://www.kaggle.com/models/spsayakpaul/deeplabv3-mobilenetv2/TfLite/ade20k/2"
CLASS_NAMES = ["excluded", "building / wall", "tree / shrub", "grass", "road", "sidewalk"]
COLORS = [(54, 58, 61), (255, 162, 50), (33, 195, 116), (172, 210, 47), (206, 81, 189), (246, 206, 118)]
GROUPS = [{"building", "wall", "house", "skyscraper", "tower"}, {"tree", "plant", "palm"}, {"grass"}, {"road"}, {"sidewalk"}]
MIN_PROBABILITY = 0.50


def classify(interpreter, image):
    # TF-Slim MobileNetV2 normalizes actual RGB to [-1,1] and pads the image at
    # its bottom/right edges. Preserve camera aspect and ignore the padded area.
    plane = np.zeros((1, 513, 513, 3), np.float32)
    plane[0, :image.height, :image.width] = np.asarray(image, dtype=np.float32) / 127.5 - 1.
    interpreter.set_tensor(interpreter.get_input_details()[0]["index"], plane)
    interpreter.invoke()
    scores = interpreter.tensor(interpreter.get_output_details()[0]["index"])()[0, :image.height, :image.width]
    classes = np.empty((image.height, image.width), np.uint8)
    accepted = np.empty_like(classes, dtype=bool)
    # Chunk the softmax to avoid duplicating the entire 151-channel float tensor.
    for y in range(0, image.height, 8):
        band = scores[y:y+8]
        classes[y:y+8] = band.argmax(axis=-1)
        stabilised = band - band.max(axis=-1, keepdims=True)
        probability = 1. / np.exp(stabilised, out=stabilised).sum(axis=-1)
        accepted[y:y+8] = probability >= MIN_PROBABILITY
    return Image.fromarray(classes), accepted


def keep_mask(raw, accepted, labels, profile):
    classes = np.asarray(raw)
    output = Image.new("L", raw.size)
    for index, group in enumerate(GROUPS, 1):
        lut = np.array([name in group for name in labels])
        interior = Image.fromarray(255 * (lut[classes] & accepted).astype(np.uint8))
        output.paste(index, mask=interior.filter(ImageFilter.MinFilter(5)))
    draw = ImageDraw.Draw(output)
    for polygon in profile["blocked_polygons"]:
        points = [(round(x * (raw.width-1) / (profile["width"]-1)),
                   round(y * (raw.height-1) / (profile["height"]-1))) for x, y in polygon]
        draw.polygon(points, fill=0)
    return output


def compare(photo, keep, directory):
    photo.save(directory / "comparison-photo.webp", quality=87)
    display = ImageEnhance.Brightness(ImageOps.grayscale(photo).convert("RGB")).enhance(.55)
    for index, color in enumerate(COLORS[1:], 1):
        region = keep.point([255 if i == index else 0 for i in range(256)])
        tint = Image.blend(photo, Image.new("RGB", photo.size, color), .61)
        display.paste(tint, mask=region)
    display.save(directory / "comparison-semantic.webp", quality=87)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("model", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--camera-profile", type=Path)
    parser.add_argument("--compare-id")
    args = parser.parse_args()
    if hashlib.sha256(args.model.read_bytes()).hexdigest() != MODEL_SHA256:
        parser.error("The checkpoint differs from the validated ADE20K model in README.md")
    with zipfile.ZipFile(args.model) as model:
        labels = [line.split(";")[0] for line in model.read("labels.txt").decode().splitlines()]
    manifest = json.loads(args.manifest.read_text())
    profile = json.loads(args.camera_profile.read_text()) if args.camera_profile else None
    if args.compare_id and not profile:
        parser.error("A comparison requires a calibrated camera exclusion profile")
    args.output.mkdir(parents=True, exist_ok=True)
    engine = Interpreter(model_path=str(args.model), num_threads=1)
    engine.allocate_tensors()
    receipt = {"model": MODEL_URL, "modelSHA256": MODEL_SHA256, "modelLicense": "Apache-2.0",
               "classLabels": labels, "groupLabels": CLASS_NAMES, "palette": COLORS,
               "minimumTopProbability": MIN_PROBABILITY, "cameraProfile": profile,
               "attribution": manifest["attribution"], "license": manifest["licenseUrl"], "frames": []}
    for frame in manifest["frames"]:
        fid = str(frame["id"])
        if not fid.isdecimal():
            raise ValueError("Expected a numeric KartaView frame ID")
        if profile and str(frame["sequenceId"]) != profile["sequence"]:
            raise ValueError("Camera exclusion profile does not match this sequence")
        if not frame.get("author") or not frame.get("sourceUrl"):
            raise ValueError("Each frame must retain its author and source URL")
        with Image.open(args.manifest.parent / frame["imagePath"]) as source:
            source = ImageOps.exif_transpose(source).convert("RGB")
            ratio = 513 / max(source.size)
            photo = source.resize((round(source.width*ratio), round(source.height*ratio)), Image.Resampling.BILINEAR)
        if profile and abs(photo.width / photo.height - profile["width"] / profile["height"]) > .003:
            raise ValueError("Camera exclusion profile does not match the image aspect ratio")
        started = time.monotonic()
        raw, accepted = classify(engine, photo)
        raw.save(args.output / (fid + ".labels.png"))
        entry = {key: frame[key] for key in ("id", "sequenceId", "sourceUrl", "author")}
        entry["size"] = photo.size
        if fid == args.compare_id:
            receipt["comparisonFrame"] = fid
            receipt["comparisonChanges"] = "resized photograph and color overlay of retained semantic classes"
        entry["license"] = frame.get("licenseUrl", manifest["licenseUrl"])
        if profile:
            keep = keep_mask(raw, accepted, labels, profile)
            keep.save(args.output / (fid + ".keep.png"))
            entry["groupPixels"] = keep.histogram()[:len(CLASS_NAMES)]
            if fid == args.compare_id:
                compare(photo, keep, args.output)
        entry["seconds"] = round(time.monotonic() - started, 3)
        receipt["frames"].append(entry)
        print(fid, entry["seconds"], "s", flush=True)
    (args.output / "semantic-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    main()
