"""The shipped aerial eye: a small network of our own (MIT), trained from random init on the clean labels
(scan_data.py) and on the soft labels of the Satlas teacher (satlas_eye.py; Satlas weights ODC-BY).

    python -I lab/scan/student_eye.py teach  --data C:/aiwork/scan-data       # teacher probabilities per tile
    python -I lab/scan/student_eye.py train  --data C:/aiwork/scan-data --steps 4000
    python -I lab/scan/student_eye.py export --data C:/aiwork/scan-data --out data/ai/scan-eye-student.onnx

Same ONNX contract as the teacher: input "image" 1x3x512x512 RGB 0..1, output "probs" 1x12x512x512.
"""
import argparse, glob, json, os, sys, time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import scan_data as sd  # noqa: E402
import satlas_eye as se  # noqa: E402

K = len(sd.CLASSES)


def widths(m):
    return tuple(int(round(w * m / 8)) * 8 for w in (24, 40, 80, 160, 256))


def block(cin, cout, stride=1):
    """Depthwise-separable conv block (MobileNet style)."""
    return nn.Sequential(
        nn.Conv2d(cin, cin, 3, stride, 1, groups=cin, bias=False), nn.BatchNorm2d(cin), nn.ReLU6(inplace=True),
        nn.Conv2d(cin, cout, 1, bias=False), nn.BatchNorm2d(cout), nn.ReLU6(inplace=True))


class Student(nn.Module):
    """A small U-Net: encoder to 1/32 for context (a 512 px tile sees about 16 m per cell at the bottom),
    decoder to 1/4, class logits upsampled to the input size."""

    def __init__(self, w=(24, 40, 80, 160, 256)):
        super().__init__()
        self.stem = nn.Sequential(nn.Conv2d(3, w[0], 3, 2, 1, bias=False), nn.BatchNorm2d(w[0]), nn.ReLU6(inplace=True))  # 1/2
        self.e1 = nn.Sequential(block(w[0], w[1], 2), block(w[1], w[1]))          # 1/4
        self.e2 = nn.Sequential(block(w[1], w[2], 2), block(w[2], w[2]))          # 1/8
        self.e3 = nn.Sequential(block(w[2], w[3], 2), block(w[3], w[3]), block(w[3], w[3]))  # 1/16
        self.e4 = nn.Sequential(block(w[3], w[4], 2), block(w[4], w[4]), block(w[4], w[4]))  # 1/32
        self.d3 = block(w[4] + w[3], w[3])
        self.d2 = block(w[3] + w[2], w[2])
        self.d1 = block(w[2] + w[1], 64)
        self.out = nn.Conv2d(64, K, 1)

    def logits(self, x):
        s = self.stem(x); a = self.e1(s); b = self.e2(a); c = self.e3(b); d = self.e4(c)
        up = lambda t, ref: F.interpolate(t, size=ref.shape[-2:], mode="bilinear", align_corners=False)
        c2 = self.d3(torch.cat([up(d, c), c], 1))
        b2 = self.d2(torch.cat([up(c2, b), b], 1))
        a2 = self.d1(torch.cat([up(b2, a), a], 1))
        return self.out(a2)  # 1/4 resolution

    def forward(self, x):
        return torch.softmax(F.interpolate(self.logits(x), scale_factor=4, mode="bilinear", align_corners=False), 1)


class Lite(nn.Module):
    """The shippable eye: the first two stages of the Satlas Swin-v2-B backbone (2.1 M parameters, ODC-BY,
    kept and fine-tuned) for the 1/4 and 1/8 features, our own small blocks for context at 1/16 and 1/32
    (in place of Satlas's 84 M-parameter deep stages), and our decoder (MIT)."""

    def __init__(self, swin=None):
        super().__init__()
        if swin is None:
            import torchvision
            swin = torchvision.models.swin_v2_b()
        self.s1 = nn.Sequential(*swin.features[:2])   # 1/4, 128 channels (NHWC)
        self.s2 = nn.Sequential(*swin.features[2:4])  # 1/8, 256 channels (NHWC)
        self.e3 = nn.Sequential(block(256, 256, 2), block(256, 256))   # 1/16
        self.e4 = nn.Sequential(block(256, 320, 2), block(320, 320))   # 1/32
        self.d3 = block(320 + 256, 256)
        self.d2 = block(256 + 256, 160)
        self.d1 = block(160 + 128, 96)
        self.out = nn.Conv2d(96, K, 1)

    def logits(self, x):
        a = self.s1(x); b = self.s2(a)
        a = a.permute(0, 3, 1, 2); b = b.permute(0, 3, 1, 2)
        c = self.e3(b); d = self.e4(c)
        up = lambda t, ref: F.interpolate(t, size=ref.shape[-2:], mode="bilinear", align_corners=False)
        c2 = self.d3(torch.cat([up(d, c), c], 1))
        b2 = self.d2(torch.cat([up(c2, b), b], 1))
        a2 = self.d1(torch.cat([up(b2, a), a], 1))
        return self.out(a2)

    def forward(self, x):
        return torch.softmax(F.interpolate(self.logits(x), scale_factor=4, mode="bilinear", align_corners=False), 1)


def make_net(a, pretrained=True):
    if a.arch == "lite":
        swin = se.satlas().backbone.backbone if pretrained else None
        return Lite(swin)
    return Student(widths(a.width))


def cmd_teach(a):
    """Teacher probabilities at 1/4 resolution (12 x 128 x 128, fp16) for each tile and its degraded copy."""
    head = se.Head(); head.load_state_dict(torch.load(os.path.join(a.data, "satlas-head.pt"))); head.eval()
    os.makedirs(os.path.join(a.data, "teach"), exist_ok=True)
    for fp in sorted(glob.glob(os.path.join(a.data, "feat", "*.npy"))):
        out = os.path.join(a.data, "teach", os.path.basename(fp))
        if os.path.exists(out):
            continue
        with torch.no_grad():
            p = torch.softmax(head(torch.from_numpy(np.load(fp).astype(np.float32))[None]), 1)[0]
        np.save(out, p.numpy().astype(np.float16))


def cmd_teach_unlabelled(a):
    """Teacher probabilities for the unlabelled photos (scan_data.py --aerial-only), full and game resolution."""
    import onnxruntime as ort
    from PIL import Image
    so = ort.SessionOptions(); so.intra_op_num_threads = a.threads
    s = ort.InferenceSession(a.onnx, so, providers=["CPUExecutionProvider"])
    os.makedirs(os.path.join(a.unlab, "teach"), exist_ok=True)
    files = sorted(glob.glob(os.path.join(a.unlab, "aerial", "*.jpg")))
    t0 = time.time()
    for i, fp in enumerate(files):
        name = os.path.basename(fp)[:-4]
        img = np.asarray(Image.open(fp).convert("RGB"))
        if img.shape != (512, 512, 3) or img.std() < 4:
            continue
        for kind, im in ((".f16.npy", img), (".d.f16.npy", np.asarray(Image.fromarray(img).resize((256, 256), Image.BILINEAR).resize((512, 512), Image.BILINEAR)))):
            out = os.path.join(a.unlab, "teach", name + kind)
            if os.path.exists(out):
                continue
            p = s.run(None, {"image": (im.astype(np.float32) / 255).transpose(2, 0, 1)[None]})[0][0]
            p = p.reshape(K, 128, 4, 128, 4).mean((2, 4))
            np.save(out, p.astype(np.float16))
        if i % 50 == 0:
            print(i, len(files), round(time.time() - t0), "s", flush=True)


def load_unlabelled(a, limit):
    from PIL import Image
    imgs, teach = [], []
    for tp in sorted(glob.glob(os.path.join(a.unlab, "teach", "*.npy")))[:limit]:
        base = os.path.basename(tp)
        name = base.split(".")[0]
        img = np.asarray(Image.open(os.path.join(a.unlab, "aerial", name + ".jpg")).convert("RGB"))
        if ".d." in base:
            img = np.asarray(Image.fromarray(img).resize((256, 256), Image.BILINEAR).resize((512, 512), Image.BILINEAR))
        imgs.append(img); teach.append(np.load(tp))
    return imgs, teach


def load(a, rows):
    imgs, labs, teach = [], [], []
    for r in rows:
        z = np.load(os.path.join(a.data, "samples", r["file"]))
        for kind in (".f16.npy", ".d.f16.npy"):
            tp = os.path.join(a.data, "teach", r["file"].replace(".npz", kind))
            if not os.path.exists(tp):
                continue
            img = z["image"]
            if kind.startswith(".d"):
                from PIL import Image
                img = np.asarray(Image.fromarray(img).resize((256, 256), Image.BILINEAR).resize((512, 512), Image.BILINEAR))
            imgs.append(img); labs.append(z["label"]); teach.append(np.load(tp))
    return np.stack(imgs), np.stack(labs), np.stack(teach)


def cmd_train(a):
    torch.manual_seed(0); np.random.seed(0)
    torch.set_num_threads(a.threads)
    tr, te = se.split(a)
    xi, xl, xt = load(a, tr)
    vi, vl, _ = load(a, te)
    if a.unlab:
        ui, ut = load_unlabelled(a, a.unlab_limit)
        if ui:
            xi = np.concatenate([xi, np.stack(ui)]); xt = np.concatenate([xt, np.stack(ut)])
            xl = np.concatenate([xl, np.full((len(ui), 512, 512), sd.IGNORE, np.uint8)])
            del ui, ut
    print("train", len(xi), "held-out", len(vi), flush=True)
    w, _ = se.class_weights(xl)
    net = make_net(a)
    if a.resume:
        net.load_state_dict(torch.load(os.path.join(a.data, "student.pt")))
    print("params", sum(p.numel() for p in net.parameters()), flush=True)
    if a.arch == "lite":  # the pretrained Satlas stages learn slowly, our layers fast
        pre = list(net.s1.parameters()) + list(net.s2.parameters())
        ids = {id(p) for p in pre}
        groups = [{"params": pre, "lr": a.lr * 0.1}, {"params": [p for p in net.parameters() if id(p) not in ids], "lr": a.lr}]
        opt = torch.optim.AdamW(groups, weight_decay=1e-4)
        sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=[a.lr * 0.1, a.lr], total_steps=a.steps, pct_start=0.1)
    else:
        opt = torch.optim.AdamW(net.parameters(), lr=a.lr, weight_decay=1e-4)
        sched = torch.optim.lr_scheduler.OneCycleLR(opt, max_lr=a.lr, total_steps=a.steps, pct_start=0.1)
    # Rare classes (water, roads, roofs, bare) weigh more in the teacher loss too.
    kd_w = (w / w.mean()).view(1, -1, 1, 1)
    ce = nn.CrossEntropyLoss(weight=w, ignore_index=sd.IGNORE)
    best = (-1, None, None)
    C = a.crop
    # Tiles with rare classes (in the teacher's eyes) are drawn more often: water most.
    top = xt.argmax(1)
    share = lambda c: (top == sd.C[c]).mean((1, 2))
    pick = 1 + 12 * share("water") + 3 * (share("paved_road") + share("roof") + share("bare"))
    pick = pick / pick.sum()
    print("draw weight max/mean", round(float(pick.max() / pick.mean()), 1), flush=True)
    t0 = time.time()
    ckpt = os.path.join(a.data, f"train-{a.arch}.ckpt")
    start = 0
    if a.cont and os.path.exists(ckpt):
        st = torch.load(ckpt, weights_only=False)
        net.load_state_dict(st["net"]); opt.load_state_dict(st["opt"]); sched.load_state_dict(st["sched"])
        start, best = st["step"], (st["best_miou"], torch.load(os.path.join(a.data, "student.pt")), st["best_eval"])
        print("continue at step", start, flush=True)
    pause = os.path.join(os.path.dirname(a.data.rstrip("/\\")), "PAUSE")
    for step in range(start, a.steps):
        # Quiet windows on the shared PC: create C:/aiwork/PAUSE to hold training, delete it to go on.
        while os.path.exists(pause):
            time.sleep(10)
        if step % 200 == 0 and step > start:
            torch.save({"net": net.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(), "step": step,
                        "best_miou": best[0], "best_eval": best[2]}, ckpt)
        net.train()
        b = np.random.choice(len(xi), a.batch, p=pick)
        ys, xs = np.random.randint(0, 512 - C + 1, a.batch) // 4 * 4, np.random.randint(0, 512 - C + 1, a.batch) // 4 * 4
        img = np.stack([xi[i, y:y + C, x:x + C] for i, y, x in zip(b, ys, xs)])
        lab = np.stack([xl[i, y:y + C, x:x + C] for i, y, x in zip(b, ys, xs)])
        tea = np.stack([xt[i, :, y // 4:(y + C) // 4, x // 4:(x + C) // 4] for i, y, x in zip(b, ys, xs)])
        x = torch.from_numpy(img).permute(0, 3, 1, 2).float() / 255
        # Photometric jitter: aerial mosaics differ in brightness and white balance from state to state.
        x = (x * torch.empty(len(b), 3, 1, 1).uniform_(0.85, 1.15) + torch.empty(len(b), 1, 1, 1).uniform_(-0.06, 0.06)).clamp(0, 1)
        y = torch.from_numpy(lab.astype(np.int64)); t = torch.from_numpy(tea.astype(np.float32))
        r = np.random.randint(4); fl = np.random.rand() < 0.5
        aug = lambda u: torch.rot90(u.flip(-1) if fl else u, r, (-2, -1))
        x, y, t = aug(x), aug(y), aug(t)
        lg = net.logits(x)
        loss_t = (kd_w * t * (torch.log(t.clamp_min(1e-6)) - F.log_softmax(lg, 1))).sum() / (lg.shape[0] * lg.shape[-1] * lg.shape[-2])
        # A batch of only unlabelled photos has no label pixels: cross-entropy would be NaN.
        loss_l = ce(F.interpolate(lg, scale_factor=4, mode="bilinear", align_corners=False), y) if (y != sd.IGNORE).any() else lg.sum() * 0
        loss = a.alpha * loss_t + (1 - a.alpha) * loss_l
        opt.zero_grad(); loss.backward(); opt.step(); sched.step()
        if step % 100 == 0 or step == a.steps - 1:
            net.eval()
            with torch.no_grad():
                pred = np.concatenate([net(torch.from_numpy(vi[i:i + 4]).permute(0, 3, 1, 2).float() / 255).argmax(1).numpy() for i in range(0, len(vi), 4)])
            ev = se.evaluate(pred, vl)
            # The game takes water outlines from the map (fusion: map geometry first); score that too.
            ev_map = se.evaluate(np.where(vl == sd.C["water"], sd.C["water"], pred), vl)
            ev["with_map_water"] = {"pixel_acc": round(ev_map["pixel_acc"], 3), "miou": ev_map["miou"]}
            print(f"step {step} loss {float(loss):.3f} (teacher {float(loss_t):.3f} labels {float(loss_l):.3f}) held-out acc {ev['pixel_acc']:.3f} miou {ev['miou']} {round(time.time() - t0)} s", flush=True)
            if ev["miou"] > best[0]:
                best = (ev["miou"], {k: v.clone() for k, v in net.state_dict().items()}, ev)
                torch.save(best[1], os.path.join(a.data, "student.pt"))
                json.dump(best[2], open(os.path.join(a.data, "student-eval.json"), "w"), indent=1)
    print("best", json.dumps(best[2]))


def cmd_export(a):
    net = make_net(a, pretrained=False); net.load_state_dict(torch.load(os.path.join(a.data, "student.pt"))); net.eval()
    torch.onnx.export(net, torch.rand(1, 3, 512, 512), a.out, input_names=["image"], output_names=["probs"], opset_version=17, dynamo=False)
    print(a.out, round(os.path.getsize(a.out) / 1e6, 2), "MB")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd")
    ap.add_argument("--data", required=True)
    ap.add_argument("--hold", default="us-gilbert-az,us-seattle-wa,us-jasper-ar,us-selma-al,us-hilo-hi,rand-7-00,rand-7-01,rand-7-02")
    ap.add_argument("--threads", type=int, default=6)
    ap.add_argument("--steps", type=int, default=4000)
    ap.add_argument("--batch", type=int, default=12)
    ap.add_argument("--crop", type=int, default=256)
    ap.add_argument("--alpha", type=float, default=0.6)
    ap.add_argument("--out", default="")
    ap.add_argument("--onnx", default="C:/aiwork/scan-eye-satlas.onnx")
    ap.add_argument("--unlab", default="")
    ap.add_argument("--unlab-limit", type=int, default=100000)
    ap.add_argument("--width", type=float, default=1.0)
    ap.add_argument("--resume", action="store_true")
    ap.add_argument("--arch", default="small", choices=["small", "lite"])
    ap.add_argument("--cont", action="store_true", help="continue from the last checkpoint of this arch")
    ap.add_argument("--lr", type=float, default=3e-3)
    a = ap.parse_args()
    {"teach": cmd_teach, "teach-unlabelled": cmd_teach_unlabelled, "train": cmd_train, "export": cmd_export}[a.cmd](a)


if __name__ == "__main__":
    main()
