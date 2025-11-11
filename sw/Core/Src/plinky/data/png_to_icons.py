#!/usr/bin/env python3
"""
Convert icons_sheet.png back to icons.h
Reads 128x128 PNG (8x8 grid of 16x16 icons) and generates icons array
"""

import os
from PIL import Image

ICON_WIDTH = 16
ICON_HEIGHT = 16
ICONS_PER_ROW = 8
NUM_ICONS = 64

def extract_icon(img, icon_idx):
    """Extract a single icon from the sprite sheet"""
    row = icon_idx // ICONS_PER_ROW
    col = icon_idx % ICONS_PER_ROW

    x = col * ICON_WIDTH
    y = row * ICON_HEIGHT

    # Extract icon region
    icon_img = img.crop((x, y, x + ICON_WIDTH, y + ICON_HEIGHT))

    # Convert to grayscale/binary
    icon_img = icon_img.convert('L')
    pixels = icon_img.load()

    # Convert to array of u16 values (one per column)
    icon_data = []
    for col_idx in range(ICON_WIDTH):
        column_value = 0
        for row_idx in range(ICON_HEIGHT):
            # If pixel is bright (>128), consider it set
            if pixels[col_idx, row_idx] > 128:
                column_value |= (1 << row_idx)
        icon_data.append(column_value)

    return icon_data

def format_icon_line(icon_data):
    """Format an icon as a C array line"""
    hex_values = [f"0x{val:04x}" for val in icon_data]
    return "\t{" + ", ".join(hex_values) + "},"

def generate_icons_h(png_path, output_path):
    """Generate icons.h from PNG file"""
    print(f"Reading {png_path}...")
    img = Image.open(png_path)

    # Verify dimensions
    expected_width = ICONS_PER_ROW * ICON_WIDTH
    expected_height = (NUM_ICONS // ICONS_PER_ROW) * ICON_HEIGHT

    if img.width != expected_width or img.height != expected_height:
        raise ValueError(f"Expected {expected_width}x{expected_height} image, got {img.width}x{img.height}")

    print(f"Extracting {NUM_ICONS} icons...")
    icons = []
    for icon_idx in range(NUM_ICONS):
        icon_data = extract_icon(img, icon_idx)
        icons.append(icon_data)

    print(f"Writing {output_path}...")
    with open(output_path, 'w') as f:
        f.write("#pragma once\n")
        f.write('#include "utils.h"\n')
        f.write("\n")
        f.write("// clang-format off\n")
        f.write(f"const static u16 icons[{NUM_ICONS}][{ICON_WIDTH}]={{\n")

        for icon_data in icons:
            f.write(format_icon_line(icon_data) + "\n")

        f.write("};\n")
        f.write("// clang-format on\n")

    print("Done!")

if __name__ == "__main__":
    import sys

    script_dir = os.path.dirname(os.path.abspath(__file__))

    # Default input/output paths
    png_path = os.path.join(script_dir, "icons_sheet.png")
    output_path = os.path.join(script_dir, "icons.h")

    # Allow custom paths from command line
    if len(sys.argv) >= 2:
        png_path = sys.argv[1]
    if len(sys.argv) >= 3:
        output_path = sys.argv[2]

    if not os.path.exists(png_path):
        print(f"Error: {png_path} not found")
        sys.exit(1)

    generate_icons_h(png_path, output_path)
