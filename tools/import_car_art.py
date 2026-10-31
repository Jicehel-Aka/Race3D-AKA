#!/usr/bin/env python3
"""Turn a sheet of rear-view car pictures (pink/magenta background) into main/game/car_art.cpp:
one 176x84 RGB565 (AKA native bit order) picture per driver, colour-keyed with 0xF81F, no palette reduction.
usage: import_car_art.py sheet.png out.cpp"""
import sys, numpy as np, cv2
from PIL import Image
from scipy import ndimage as ndi

W, H = 176, 84
W3 = 224                      # three-quarter pictures are wider
KEY = 0xF81F
MID = [k / 6 for k in range(1, 6)]   # intermediate frames between the rear view and the three-quarter view
# driver order in race.cpp: VIPER BLAZE NOVA RAVEN ACE ZED KAI YOU -> (picture index row-major in the sheet, hue shift degrees)
PICK = [(2, 0), (11, 0), (6, 0), (4, 0), (9, 0), (1, 0), (0, 0), (2, 150)]
# three-quarter rear pictures (rows 4-5 of the sheet, indices 12..19), nose pointing left; mirrored for the other side
# (picture, hue shift) ; 'white' desaturates the blue paint
PICK3 = [(13, 0), (16, 0), (14, 0), (18, 0), (17, 'white'), (17, 65), (16, (-22, 20, 70)), (13, 150)]   # (19) carries the sheet's watermark, (12) has a broken outline

def cars(path):
    im = Image.open(path).convert('RGB'); a = np.array(im).astype(int)
    R, G, B = a[..., 0], a[..., 1], a[..., 2]
    pink = (R > 165) & (R - G > 45) & (B - G > 10)
    lab, _ = ndi.label(pink)
    border = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    car = ndi.binary_opening(~np.isin(lab, list(border)), iterations=1)
    lab2, _ = ndi.label(car)
    out = []
    for i, sl in enumerate(ndi.find_objects(lab2)):
        m = lab2[sl] == i + 1
        if m.sum() > 3000: out.append((sl, m))
    out.sort(key=lambda o: (round(o[0][0].start / 100), o[0][1].start))
    return im, out

def picture(im, sl, m, shift=0, W=W):
    m = ndi.binary_fill_holes(m)
    m = ndi.binary_erosion(m, iterations=1)
    crop = im.crop((sl[1].start, sl[0].start, sl[1].stop, sl[0].stop))
    if shift:                                       # recolour the saturated (painted) parts only
        hsv = np.array(crop.convert('HSV')).astype(int)
        lo, hi = 120, 205                                                       # blue paint only (keeps lamps and glass)
        if isinstance(shift, tuple): shift, lo, hi = shift
        sel = (hsv[..., 1] > 90) & (hsv[..., 0] > lo) & (hsv[..., 0] < hi)
        if shift == 'white':
            hsv[..., 1][sel] = 25
            hsv[..., 2][sel] = np.minimum(255, hsv[..., 2][sel] * 1.2 + 25)
        else:
            hsv[..., 0][sel] = (hsv[..., 0][sel] + int(shift * 256 / 360)) % 256
        crop = Image.fromarray(hsv.astype('uint8'), 'HSV').convert('RGB')
    rgb = np.array(crop).copy()
    rgb[~m] = (12, 10, 16)                           # dark rim instead of pink fringe
    w, h = m.shape[1], m.shape[0]
    nh = min(H, max(1, round(h * W / w)))
    small = Image.fromarray(rgb).resize((W, nh), Image.LANCZOS)
    msk = Image.fromarray((m * 255).astype('uint8')).resize((W, nh), Image.BOX)
    big = np.zeros((H, W, 3), 'uint8'); bm = np.zeros((H, W), 'uint8')
    top = H - nh
    big[top:] = np.array(small); bm[top:] = np.array(msk)
    r, g, b = big[..., 0].astype(int), big[..., 1].astype(int), big[..., 2].astype(int)
    px = (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11)
    px[px == KEY] = KEY - 1
    px[bm < 128] = KEY
    return px.astype(np.uint16)

def _unpack(v):
    r = (v & 31) * 255 // 31; g = ((v >> 5) & 63) * 255 // 63; b = (v >> 11) * 255 // 31
    return np.stack([r, g, b], -1).astype(np.float32), (v != KEY).astype(np.float32)

def mids(rear, tq):
    """intermediate pictures: dense optical flow rear->3/4, both pictures warped part-way, dissolved only around the middle (keeps ghosting to a minimum)"""
    o = (W3 - W) // 2
    ra = np.zeros((H, W3, 3), np.float32); aa = np.zeros((H, W3), np.float32)
    r0, a0 = _unpack(rear); ra[:, o:o + W] = r0; aa[:, o:o + W] = a0
    rb, ab = _unpack(tq)
    def gray(r, a):
        x = r * a[..., None] + 128 * (1 - a[..., None])
        g = cv2.cvtColor(x.astype(np.uint8), cv2.COLOR_RGB2GRAY)
        return cv2.addWeighted(g, 0.6, (a * 255).astype(np.uint8), 0.4, 0)
    F = cv2.DISOpticalFlow_create(cv2.DISOPTICAL_FLOW_PRESET_MEDIUM).calc(gray(ra, aa), gray(rb, ab), None)
    F = cv2.GaussianBlur(F, (0, 0), 4)
    X, Y = np.meshgrid(np.arange(W3, dtype=np.float32), np.arange(H, dtype=np.float32))
    out = []
    for t in MID:
        wa = cv2.remap(np.dstack([ra * aa[..., None], aa]), X - t * F[..., 0], Y - t * F[..., 1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
        wb = cv2.remap(np.dstack([rb * ab[..., None], ab]), X + (1 - t) * F[..., 0], Y + (1 - t) * F[..., 1], cv2.INTER_LINEAR, borderMode=cv2.BORDER_CONSTANT)
        k = float(np.clip((t - 0.25) / 0.5, 0, 1)); k = k * k * (3 - 2 * k)   # narrow dissolve around the middle only
        w = (1 - k) * wa + k * wb
        a = w[..., 3]; rgb = np.where(a[..., None] > 1e-3, w[..., :3] / np.maximum(a[..., None], 1e-3), 0)
        rgb = rgb.clip(0, 255)
        px = ((rgb[..., 0].astype(int) >> 3) | ((rgb[..., 1].astype(int) >> 2) << 5) | ((rgb[..., 2].astype(int) >> 3) << 11))
        px[a < 0.5] = KEY
        px[(px == KEY) & (a >= 0.5)] = KEY - 1
        out.append(px.astype(np.uint16))
    return out

def main():
    im, lst = cars(sys.argv[1])
    with open(sys.argv[2], 'w') as f:
        f.write('// generated by tools/import_car_art.py - do not edit\n#include "car_art.h"\n')
        f.write('const uint16_t kCarArt[%d][%d] = {\n' % (len(PICK), W * H))
        for p, sh in PICK:
            f.write('{' + ','.join(str(int(v)) for v in picture(im, *lst[p], shift=sh).ravel()) + '},\n')
        f.write('};\n')
        f.write('const uint16_t kCarArt3q[%d][%d] = {\n' % (len(PICK3), W3 * H))
        for p, sh in PICK3:
            f.write('{' + ','.join(str(int(v)) for v in picture(im, *lst[p], shift=sh, W=W3).ravel()) + '},\n')
        f.write('};\n')
        f.write('const uint16_t kCarArtMid[%d][%d][%d] = {\n' % (len(PICK), len(MID), W3 * H))
        for (p, sh), (p3, sh3) in zip(PICK, PICK3):
            fr = mids(picture(im, *lst[p], shift=sh), picture(im, *lst[p3], shift=sh3, W=W3))
            f.write('{' + ','.join('{' + ','.join(str(int(v)) for v in q.ravel()) + '}' for q in fr) + '},\n')
        f.write('};\n')
main()
