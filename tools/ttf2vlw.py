#!/usr/bin/env python3
"""Convert TTF/OTF font to VLW format for TFT_eSPI smooth fonts.

VLW format (reverse-engineered from TFT_eSPI Smooth_font.cpp):
  Header: 6 x int32 big-endian
    gCount, version(11), fontSize, 0, ascent, descent
  Per glyph: 7 x int32 big-endian
    unicode, height, width, xAdvance, dY, dX, 0
  Bitmaps: 8-bit alpha, row-major, one per glyph in order
"""

import struct
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont


def render_glyph(font, char, font_size):
    ascent, descent = font.getmetrics()
    bbox = font.getbbox(char)
    if bbox is None:
        return None

    left, top, right, bottom = bbox
    w = right - left
    h = bottom - top
    if w == 0 or h == 0:
        return None

    # xAdvance
    x_advance = font.getlength(char)

    # dY = distance from baseline to top of bitmap (positive = up)
    # baseline is at y=ascent in the image coordinate system
    # top of bbox in font coords: top pixels from origin
    dy = ascent - top

    # dX = left bearing
    dx = left

    # Render glyph as white on black, extract alpha
    pad = 4
    img_w = w + pad * 2
    img_h = h + pad * 2
    img = Image.new('L', (img_w, img_h), 0)
    draw = ImageDraw.Draw(img)
    draw.text((pad - left, pad - top), char, font=font, fill=255)

    # Crop to actual content
    img_bbox = img.getbbox()
    if img_bbox is None:
        return None

    cropped = img.crop(img_bbox)
    cw, ch = cropped.size

    # Adjust dX and dY for the crop offset
    crop_dx = img_bbox[0] - pad + left
    crop_dy = ascent - (img_bbox[1] - pad + top)

    pixels = list(cropped.getdata())

    return {
        'unicode': ord(char),
        'height': ch,
        'width': cw,
        'xAdvance': int(round(x_advance)),
        'dY': crop_dy,
        'dX': crop_dx,
        'bitmap': bytes(pixels),
    }


def generate_vlw(ttf_path, font_size, output_path, char_ranges=None):
    font = ImageFont.truetype(str(ttf_path), font_size)
    ascent, descent = font.getmetrics()

    if char_ranges is None:
        # Default: Basic Latin (printable ASCII)
        char_ranges = [(0x0021, 0x007E)]

    chars = []
    for start, end in char_ranges:
        for cp in range(start, end + 1):
            chars.append(chr(cp))

    glyphs = []
    for ch in chars:
        g = render_glyph(font, ch, font_size)
        if g:
            glyphs.append(g)

    gcount = len(glyphs)

    # Build file
    data = bytearray()
    # Header
    data += struct.pack('>i', gcount)       # gCount
    data += struct.pack('>i', 11)           # version
    data += struct.pack('>i', font_size)    # font size
    data += struct.pack('>i', 0)            # deprecated
    data += struct.pack('>i', ascent)       # ascent
    data += struct.pack('>i', descent)      # descent

    # Glyph metrics
    for g in glyphs:
        data += struct.pack('>i', g['unicode'])
        data += struct.pack('>i', g['height'])
        data += struct.pack('>i', g['width'])
        data += struct.pack('>i', g['xAdvance'])
        data += struct.pack('>i', g['dY'])
        data += struct.pack('>i', g['dX'])
        data += struct.pack('>i', 0)  # padding

    # Bitmaps
    for g in glyphs:
        data += g['bitmap']

    Path(output_path).write_bytes(data)
    print(f"Created {output_path} ({len(data)} bytes, {gcount} glyphs)")
    return len(data)


def main():
    parser = argparse.ArgumentParser(description='Convert TTF to VLW for TFT_eSPI')
    parser.add_argument('ttf', help='Input TTF/OTF file')
    parser.add_argument('size', type=int, help='Font size in pixels')
    parser.add_argument('-o', '--output', help='Output VLW file (default: <name><size>.vlw)')
    parser.add_argument('--chars', default='basic_latin',
                        help='Character set: basic_latin, latin1, digits_plus, or range like 0x20-0x7E')
    args = parser.parse_args()

    ttf_path = Path(args.ttf)
    if args.output:
        out_path = args.output
    else:
        stem = ttf_path.stem.replace(' ', '')
        out_path = f"{stem}{args.size}.vlw"

    char_sets = {
        'basic_latin': [(0x0021, 0x007E)],
        'latin1': [(0x0021, 0x007E), (0x00A0, 0x00FF)],
        'digits_plus': [(0x0020, 0x003A)],  # space, digits, colon
    }

    if args.chars in char_sets:
        ranges = char_sets[args.chars]
    else:
        # Parse custom range like 0x20-0x7E
        parts = args.chars.split('-')
        ranges = [(int(parts[0], 0), int(parts[1], 0))]

    generate_vlw(ttf_path, args.size, out_path, ranges)


if __name__ == '__main__':
    main()
