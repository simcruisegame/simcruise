"""Locate road users in licensed atlas street frames for offline reconstruction review."""
import argparse
import hashlib
import json
from pathlib import Path
import time

import cv2
import numpy as np

MODEL_SHA = "4b82da9944b88577175ee23a459dce2e26e6e4be573def65b1055dc2d9720186"
CLASSES = {2: "car", 5: "bus", 7: "truck"}
SIDE = 416


def letterbox(image):
    height, width = image.shape[:2]
    scale = SIDE / max(width, height)
    w, h = int(width * scale), int(height * scale)
    x, y = (SIDE-w)//2, (SIDE-h)//2
    square = np.zeros((SIDE, SIDE, 3), np.uint8)
    square[y:y+h, x:x+w] = cv2.resize(cv2.cvtColor(image, cv2.COLOR_BGR2RGB), (w, h))
    normalized = (square.astype(np.float32) - [103.53, 116.28, 123.675]) / [57.375, 57.12, 58.395]
    return cv2.dnn.blobFromImage(normalized.astype(np.float32)), (x,y,w,h)


def detections(net, image):
    blob, (ox, oy, width, height) = letterbox(image)
    net.setInput(blob)
    tensors = {t.shape[1:]: t[0] for t in net.forward(net.getUnconnectedOutLayersNames())}
    boxes, scores, labels = [], [], []
    for stride in (8,16,32):
        size = SIDE // stride
        classes = tensors[size*size,80]
        distances = tensors[size*size,32]
        labels_all = classes.argmax(axis=1)
        confidence = classes.max(axis=1)
        selected = np.flatnonzero(confidence >= .32)
        for pos in selected:
            logits = distances[pos].reshape(4,8)
            mass = np.exp(logits - logits.max(axis=1,keepdims=True))
            l,t,r,b = (mass @ np.arange(8)) / mass.sum(axis=1) * stride
            cx, cy = ((pos % size)*stride+(stride-1)/2, (pos//size)*stride+(stride-1)/2)
            x0,y0,x1,y1 = np.clip([(cx-l-ox)/width,(cy-t-oy)/height,(cx+r-ox)/width,(cy+b-oy)/height],0,1)
            if x1<=x0 or y1<=y0:
                continue
            boxes.append([x0,y0,x1-x0,y1-y0])
            scores.append(float(confidence[pos]))
            labels.append(int(labels_all[pos]))
    selected = np.array(cv2.dnn.NMSBoxes(boxes,scores,.32,.55)).ravel()
    result = []
    for i in selected:
        if labels[i] in CLASSES:
            x,y,w,h = boxes[i]
            result.append({"class":CLASSES[labels[i]],"score":round(scores[i],4),
                           "box":np.round([x,y,x+w,y+h],5).tolist()})
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("model", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if hashlib.sha256(args.model.read_bytes()).hexdigest()!=MODEL_SHA:
        parser.error("Expected the pinned OpenCV Zoo float NanoDet ONNX model")
    net = cv2.dnn.readNet(str(args.model))
    cv2.setNumThreads(2)
    selected = json.loads(args.manifest.read_text())["frames"]
    observations = []
    for frame in selected:
        path = args.manifest.parent / frame["imagePath"]
        image = cv2.imread(str(path))
        if image is None:
            parser.error(f"Cannot decode {path}")
        start = time.perf_counter()
        objects = detections(net,image)
        row = {k:frame.get(k) for k in ("id","lat","lon","heading","sequenceId","capturedAt","sourceUrl","author","licenseUrl","gpsAccuracyMeters")}
        row.update(objects=objects,elapsedMs=round((time.perf_counter()-start)*1000))
        observations.append(row)
    args.output.write_text(json.dumps({"modelSha256":MODEL_SHA,"frames":observations},indent=2)+"\n")


if __name__=="__main__":
    main()
