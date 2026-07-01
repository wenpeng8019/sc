#!/usr/bin/env python3
"""生成 TIFF-LZW 地面真值向量：raw 像素字节 + libtiff 压缩出的 LZW 子流。
用于校验 codec_lzw_raw_*（TIFF 变体：MSB-first + early change）与真实 libtiff 互通。"""
import os, struct
from PIL import Image, TiffImagePlugin

OUT = os.path.dirname(os.path.abspath(__file__))


def make_image(w, h, seed):
    data = bytearray(w * h)
    s = seed & 0xFFFFFFFF
    for i in range(w * h):
        s = (s * 1103515245 + 12345) & 0xFFFFFFFF
        data[i] = (s >> 16) & 0xFF
    img = Image.frombytes("L", (w, h), bytes(data))
    return img, bytes(data)


def extract_strip(path):
    img = Image.open(path)
    tags = img.tag_v2
    offsets = tags[273]            # StripOffsets
    counts = tags[279]             # StripByteCounts
    # 强制单 strip（见 save 参数）
    assert len(offsets) == 1, f"expected single strip, got {len(offsets)}"
    with open(path, "rb") as f:
        f.seek(offsets[0])
        return f.read(counts[0])


def emit(name, w, h, seed):
    img, raw = make_image(w, h, seed)
    tif = os.path.join(OUT, f"{name}.tif")
    # rowsperstrip = h → 单 strip；predictor=1 → 无差分，LZW 直接编码原始像素
    img.save(tif, format="TIFF", compression="tiff_lzw",
             tiffinfo={TiffImagePlugin.ROWSPERSTRIP: h})
    lzw = extract_strip(tif)
    with open(os.path.join(OUT, f"{name}.raw"), "wb") as f:
        f.write(raw)
    with open(os.path.join(OUT, f"{name}.lzw"), "wb") as f:
        f.write(lzw)
    # round-trip 自检：PIL 解回应等于 raw
    back = Image.open(tif).tobytes()
    assert back == raw, "PIL self round-trip mismatch"
    os.remove(tif)
    print(f"{name}: raw={len(raw)} lzw={len(lzw)} (w={w} h={h})")


emit("tiff_small", 16, 8, 0x1234)      # 128 字节，单一码宽
emit("tiff_mid", 64, 64, 0x9E3779B9)   # 4096 字节，跨码宽增长
emit("tiff_big", 256, 256, 0xDEADBEEF) # 65536 字节，触发满表 CLEAR
