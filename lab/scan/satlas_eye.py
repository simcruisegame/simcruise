"""The Satlas aerial eye: Satlas Aerial_SwinB_SI backbone + FPN (Allen Institute for AI, weights ODC-BY)
with a small segmentation head that we train on clean labels (scan_data.py), exported as one ONNX file.

    python -I lab/scan/satlas_eye.py features --data C:/aiwork/scan-data
    python -I lab/scan/satlas_eye.py train    --data C:/aiwork/scan-data --hold us-gilbert-az,us-seattle-wa,...
    python -I lab/scan/satlas_eye.py export   --data C:/aiwork/scan-data --out C:/aiwork/scan-eye-satlas.onnx
    python -I lab/scan/satlas_eye.py eval     --data C:/aiwork/scan-data --onnx C:/aiwork/scan-eye-satlas.onnx

The ONNX model: input "image" 1x3x512x512 RGB in 0..1 (a z16 tile, about 1 m/px); output "probs"
1x12x512x512 class probabilities in scan_data.CLASSES order.
"""
import argparse, glob, json, os, sys, time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_data as sd  # noqa: E402

K = len(sd.CLASSES)


class Head(nn.Module):
    """3x3 conv on the 1/4-resolution FPN features, then class logits; 0.08 M parameters."""

    def __init__(self, cin=128, mid=64, k=K):
        super().__init__()
        self.a = nn.Conv2d(cin, mid, 3, padding=1)
        self.b = nn.Conv2d(mid, mid, 3, padding=1)
        self.c = nn.Conv2d(mid, k, 1)

    def forward(self, f):
        return self.c(F.relu(self.b(F.relu(self.a(f)))))


class Eye(nn.Module):
    def __init__(self, backbone, fpn, head):
        super().__init__()
        self.backbone, self.fpn, self.head = backbone, fpn, head

    def forward(self, x):
        logits = self.head(self.fpn(self.backbone(x))[0])
        return torch.softmax(F.interpolate(logits, scale_factor=4, mode="bilinear", align_corners=False), 1)


def satlas():
    import satlaspretrain_models
    return satlaspretrain_models.Weights().get_pretrained_model("Aerial_SwinB_SI", fpn=True, device="cpu").eval()


def samples(data):
    idx = {}
    for f in glob.glob(os.path.join(data, "index-*.json")):
        for r in json.load(open(f)):
            idx[r["file"]] = r
    return list(idx.values())


def cmd_features(a):
    m = satlas()
    torch.set_num_threads(a.threads)
    os.makedirs(os.path.join(a.data, "feat"), exist_ok=True)
    rows = [{"file": os.path.basename(p)} for p in sorted(glob.glob(os.path.join(a.data, "samples", "*.npz")))]
    t0 = time.time()
    for i, r in enumerate(rows):
        out = os.path.join(a.data, "feat", r["file"].replace(".npz", ".d.f16.npy" if a.degrade else ".f16.npy"))
        if os.path.exists(out):
            continue
        img = np.load(os.path.join(a.data, "samples", r["file"]))["image"]
        if a.degrade:  # the game's own input: the zoom-14 photograph (2.4 m/px) resampled to 512
            from PIL import Image
            img = np.asarray(Image.fromarray(img).resize((256, 256), Image.BILINEAR).resize((512, 512), Image.BILINEAR))
        x = torch.from_numpy(img).permute(2, 0, 1)[None].float() / 255
        with torch.no_grad():
            f = m.fpn(m.backbone(x))[0][0].numpy().astype(np.float16)
        np.save(out, f)
        if i % 20 == 0:
            print(i, len(rows), round(time.time() - t0), "s", flush=True)


def load_set(a, rows, kinds=(".f16.npy", ".d.f16.npy")):
    fs, ls = [], []
    for r in rows:
        for kind in kinds:
            fp = os.path.join(a.data, "feat", r["file"].replace(".npz", kind))
            if not os.path.exists(fp):
                continue
            fs.append(np.load(fp))
            ls.append(np.load(os.path.join(a.data, "samples", r["file"]))["label"])
    return np.stack(fs), np.stack(ls)


def split(a):
    rows = samples(a.data)
    hold = set(a.hold.split(","))
    return [r for r in rows if r["place"] not in hold], [r for r in rows if r["place"] in hold]


def class_weights(labels):
    n = np.bincount(labels[labels != sd.IGNORE].ravel(), minlength=K).astype(np.float64)
    w = 1 / np.sqrt(np.maximum(n, 1) / n.sum())
    w[n == 0] = 0
    return torch.tensor(w / w[n > 0].mean(), dtype=torch.float32), n


def evaluate(pred, lab):
    ok = lab != sd.IGNORE
    p, l = pred[ok], lab[ok]
    res = {"pixel_acc": float((p == l).mean())}
    ious = {}
    for c in range(K):
        inter = np.sum((p == c) & (l == c)); union = np.sum((p == c) | (l == c))
        if np.sum(l == c) > 0:
            ious[sd.CLASSES[c]] = round(float(inter / max(union, 1)), 3)
    res["iou"] = ious
    res["miou"] = round(float(np.mean(list(ious.values()))), 3)
    return res


def cmd_train(a):
    torch.manual_seed(0)
    torch.set_num_threads(a.threads)
    tr, te = split(a)
    ftr, ltr = load_set(a, tr)
    fte, lte = load_set(a, te)
    print("train tiles", len(ftr), "test tiles", len(fte))
    w, n = class_weights(ltr)
    print("label pixels", dict(zip(sd.CLASSES, n.astype(int).tolist())))
    head = Head()
    opt = torch.optim.AdamW(head.parameters(), lr=2e-3, weight_decay=1e-4)
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=a.epochs)
    lossf = nn.CrossEntropyLoss(weight=w, ignore_index=sd.IGNORE)
    lt = torch.from_numpy(ltr.astype(np.int64))
    best = None
    for ep in range(a.epochs):
        perm = np.random.RandomState(ep).permutation(len(ftr))
        head.train(); tot = 0
        for i in range(0, len(perm), a.batch):
            b = perm[i:i + a.batch]
            f = torch.from_numpy(ftr[b].astype(np.float32))
            if np.random.rand() < 0.5:  # flips and 90 degree turns: aerial photos have no "up"
                f, lb = f.flip(-1), lt[b].flip(-1)
            else:
                lb = lt[b]
            r = np.random.randint(4)
            f, lb = torch.rot90(f, r, (-2, -1)), torch.rot90(lb, r, (-2, -1))
            logits = F.interpolate(head(f), scale_factor=4, mode="bilinear", align_corners=False)
            loss = lossf(logits, lb)
            opt.zero_grad(); loss.backward(); opt.step(); tot += float(loss) * len(b)
        sched.step()
        head.eval()
        with torch.no_grad():
            pred = np.concatenate([F.interpolate(head(torch.from_numpy(fte[i:i + 8].astype(np.float32))), scale_factor=4, mode="bilinear").argmax(1).numpy() for i in range(0, len(fte), 8)])
        ev = evaluate(pred, lte)
        print(f"epoch {ep} loss {tot / len(ftr):.3f} held-out acc {ev['pixel_acc']:.3f} miou {ev['miou']}", flush=True)
        if best is None or ev["miou"] > best[0]:
            best = (ev["miou"], {k: v.clone() for k, v in head.state_dict().items()}, ev)
    torch.save(best[1], os.path.join(a.data, "satlas-head.pt"))
    json.dump(best[2], open(os.path.join(a.data, "satlas-head-eval.json"), "w"), indent=1)
    print("best", json.dumps(best[2]))


def cmd_export(a):
    m = satlas()
    head = Head(); head.load_state_dict(torch.load(os.path.join(a.data, "satlas-head.pt")))
    eye = Eye(m.backbone, m.fpn, head).eval()
    torch.onnx.export(eye, torch.rand(1, 3, 512, 512), a.out, input_names=["image"], output_names=["probs"], opset_version=17, dynamo=False)
    if a.int8:
        from onnxruntime.quantization import quantize_dynamic, QuantType
        q = a.out.replace(".onnx", "-int8.onnx")
        quantize_dynamic(a.out, q, weight_type=QuantType.QInt8)
        print(q, round(os.path.getsize(q) / 1e6, 1), "MB")
    print(a.out, round(os.path.getsize(a.out) / 1e6, 1), "MB")


def cmd_eval(a):
    import onnxruntime as ort
    from PIL import Image
    so = ort.SessionOptions(); so.intra_op_num_threads = a.threads
    s = ort.InferenceSession(a.onnx, so, providers=["CPUExecutionProvider"])
    _, te = split(a)
    preds, labs, times = [], [], []
    os.makedirs(os.path.join(a.data, "eval"), exist_ok=True)
    for r in te:
        z = np.load(os.path.join(a.data, "samples", r["file"]))
        x = (z["image"].astype(np.float32) / 255).transpose(2, 0, 1)[None]
        t = time.perf_counter(); p = s.run(None, {"image": x})[0][0]; times.append(time.perf_counter() - t)
        cls = p.argmax(0).astype(np.uint8)
        preds.append(cls); labs.append(z["label"])
        Image.fromarray(np.concatenate([z["image"], sd.colour(z["label"]), sd.colour(cls)], 1)).save(os.path.join(a.data, "eval", r["file"].replace(".npz", ".jpg")), quality=85)
    ev = evaluate(np.stack(preds), np.stack(labs))
    ev["seconds_per_tile_median"] = round(float(np.median(times)), 3)
    ev["threads"] = a.threads
    print(json.dumps(ev, indent=1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("--data", required=True)
    ap.add_argument("--hold", default="us-gilbert-az,us-seattle-wa,us-jasper-ar,us-selma-al,us-hilo-hi,rand-7-00,rand-7-01,rand-7-02")
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--epochs", type=int, default=25)
    ap.add_argument("--batch", type=int, default=8)
    ap.add_argument("--out", default="")
    ap.add_argument("--onnx", default="")
    ap.add_argument("--int8", action="store_true")
    ap.add_argument("--degrade", action="store_true")
    a = ap.parse_args()
    {"features": cmd_features, "train": cmd_train, "export": cmd_export, "eval": cmd_eval}[a.cmd](a)


if __name__ == "__main__":
    main()
