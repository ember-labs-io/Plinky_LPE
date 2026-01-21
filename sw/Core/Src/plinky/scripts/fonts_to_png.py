#!/usr/bin/env python3
"""
Convert Plinky fonts.h to PNG images
Exports raw font data exactly as stored - no cropping, skipping, or modifications
"""

import os
import re
from PIL import Image

# Font boundaries (indices in u32 array where each font starts)
FONT_BOUNDARIES = [0, 96, 384, 768, 1152, 1872]

# Font names indexed by actual Font enum value
FONT_NAMES = {
    0: "F_8",
    1: "F_12",
    2: "F_16",
    3: "F_16_BOLD",
    4: "F_20_BOLD",
}

# Font dimensions: (xsize, ysize, datap)
FONT_DIMENSIONS = {
    0: (4, 8, 1),      # F_8
    1: (6, 12, 2),     # F_12
    2: (8, 16, 2),     # F_16
    3: (8, 16, 2),     # F_16_BOLD
    4: (10, 20, 3),    # F_20_BOLD
}

def parse_fonts_h(filename):
    """Parse fonts.h and extract the font_data array"""
    with open(filename, 'r') as f:
        content = f.read()

    # Remove C comments
    content = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    content = re.sub(r'//.*?$', '', content, flags=re.MULTILINE)

    # Find the font_data array
    match = re.search(r'const\s+static\s+u32\s+font_data\[\d*\]\s*=\s*\{([^}]+)\}', content, re.DOTALL)
    if not match:
        raise ValueError("Could not find font_data array in fonts.h")

    values_str = match.group(1)
    values = [int(v.strip().rstrip(','), 0) for v in values_str.split(',') if v.strip()]

    return values

def get_font_dimensions(font_index):
    """Get font dimensions from lookup table"""
    return FONT_DIMENSIONS[font_index]

def extract_character(font_data, char_offset, xsize, ysize, datap):
    """Extract a single character's pixel data"""
    mask = (1 << ysize) - 1

    # Create image for this character
    char_img = Image.new('1', (xsize, ysize), 0)
    pixels = char_img.load()

    # Extract all columns (no skipping)
    for col_idx in range(xsize):
        # Read column as little-endian multi-byte value
        column_data = 0
        base = char_offset + col_idx * datap
        for byte_idx in range(datap):
            column_data |= font_data[base + byte_idx] << (byte_idx * 8)

        column_data &= mask

        # Draw column pixels
        for row_idx in range(ysize):
            if column_data & (1 << row_idx):
                pixels[col_idx, row_idx] = 1

    return char_img

def font_index_to_offset(font_index):
    """Convert font index to offset in font_data"""
    return FONT_BOUNDARIES[font_index]

def export_font_to_png(font_data_u32, font_index, output_path):
    """Export a font to PNG"""
    xsize, ysize, datap = get_font_dimensions(font_index)

    # Convert u32 array to byte array
    font_data_bytes = []
    for u32_val in font_data_u32:
        font_data_bytes.append(u32_val & 0xFF)
        font_data_bytes.append((u32_val >> 8) & 0xFF)
        font_data_bytes.append((u32_val >> 16) & 0xFF)
        font_data_bytes.append((u32_val >> 24) & 0xFF)

    # Get font offset
    font_offset_u32 = font_index_to_offset(font_index)
    font_offset_bytes = font_offset_u32 * 4  # Convert u32 offset to byte offset

    # ASCII printable characters: 32-126 (95 characters)
    # But stored starting at char 32, so we have chars 32-126
    num_chars = 95
    char_width = xsize * datap  # Bytes per character

    # Calculate total PNG width
    total_width = xsize * num_chars

    # Create output image
    output_img = Image.new('RGB', (total_width, ysize), 'black')

    print(f"Font {font_index} ({FONT_NAMES[font_index]}): {xsize}×{ysize} pixels, {datap} bytes/col")
    print(f"Extracting {num_chars} characters...")

    # Extract each character
    for char_idx in range(num_chars):
        char_offset = font_offset_bytes + char_idx * char_width
        char_img = extract_character(font_data_bytes, char_offset, xsize, ysize, datap)

        # Convert to RGB and paste
        char_rgb = char_img.convert('RGB')
        char_rgb = Image.eval(char_rgb, lambda p: 255 if p else 0)

        x_pos = char_idx * xsize
        output_img.paste(char_rgb, (x_pos, 0))

    output_img.save(output_path)
    print(f"Saved to {output_path}")

if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: python3 fonts_to_png.py <font_index>")
        print("\nFont enum indices:")
        for idx in sorted(FONT_NAMES.keys()):
            print(f"  {idx}: {FONT_NAMES[idx]}")
        sys.exit(1)

    font_index = int(sys.argv[1])
    if font_index not in FONT_NAMES:
        print(f"Error: Invalid font_index {font_index}")
        print("Valid indices:", sorted(FONT_NAMES.keys()))
        sys.exit(1)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    data_dir = os.path.join(script_dir, "..", "data")
    fonts_h_path = os.path.join(data_dir, "fonts.h")

    print(f"Parsing {fonts_h_path}...")
    font_data = parse_fonts_h(fonts_h_path)
    print(f"Loaded {len(font_data)} u32 values")

    output_file = os.path.join(data_dir, f"font_{FONT_NAMES[font_index]}.png")
    export_font_to_png(font_data, font_index, output_file)

    print("Done!")
