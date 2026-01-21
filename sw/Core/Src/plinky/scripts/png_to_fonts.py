#!/usr/bin/env python3
"""
Convert PNG back to fonts.h
Updates a specific font's data from a PNG file
Preserves font_offsets and file structure
"""

import os
import re
from PIL import Image

# Font names indexed by actual Font enum value
FONT_NAMES = {
    0: "F_8",
    1: "F_12",
    2: "F_16",
    3: "F_16_BOLD",
    4: "F_20_BOLD",
}

def parse_fonts_h(filename):
    """Parse fonts.h and extract font_offsets and font_data arrays"""
    with open(filename, 'r') as f:
        content = f.read()

    # Remove C comments for parsing
    content_no_comments = re.sub(r'/\*.*?\*/', '', content, flags=re.DOTALL)
    content_no_comments = re.sub(r'//.*?$', '', content_no_comments, flags=re.MULTILINE)

    # Find the font_offsets array
    font_offsets = None
    offsets_match = re.search(r'const\s+static\s+u16\s+font_offsets\[NUM_FONTS\]\s*=\s*\{(.*?)\};', content_no_comments, re.DOTALL)
    if offsets_match:
        # Parse flat font_offsets array
        offsets_str = offsets_match.group(1)
        font_offsets = [int(v.strip(), 0) for v in offsets_str.split(',') if v.strip()]

    if not font_offsets or len(font_offsets) != 5:
        raise ValueError("Could not find valid font_offsets array in fonts.h (expected 5 entries)")

    # Find the font_data array
    data_match = re.search(r'const\s+static\s+u32\s+font_data\[\d*\]\s*=\s*\{([^}]+)\}', content_no_comments, re.DOTALL)
    if not data_match:
        raise ValueError("Could not find font_data array in fonts.h")

    values_str = data_match.group(1)
    values = [int(v.strip().rstrip(','), 0) for v in values_str.split(',') if v.strip()]

    return font_offsets, values

def get_font_boundaries_for_index(font_offsets, font_index):
    """Get start and end u32 index for a specific font"""
    start_idx = font_offsets[font_index] // 4

    if font_index + 1 < len(font_offsets):
        end_idx = font_offsets[font_index + 1] // 4
    else:
        end_idx = None  # Last font, use total length

    return start_idx, end_idx

# Font dimensions lookup
FONT_DIMENSIONS = {
    0: (4, 8, 1),      # F_8
    1: (6, 12, 2),     # F_12
    2: (8, 16, 2),     # F_16
    3: (8, 16, 2),     # F_16_BOLD
    4: (10, 20, 3),    # F_20_BOLD
}

def get_font_dimensions(font_index):
    """Get font dimensions from lookup table"""
    return FONT_DIMENSIONS[font_index]

def extract_character_from_png(img, char_idx, xsize, ysize):
    """Extract a single character from the PNG"""
    x_offset = char_idx * xsize

    # Crop character region
    char_img = img.crop((x_offset, 0, x_offset + xsize, ysize))
    char_img = char_img.convert('L')  # Grayscale
    pixels = char_img.load()

    # Convert pixels to column-major byte array
    char_bytes = []
    for col_idx in range(xsize):
        # Build column value from pixels
        column_data = 0
        for row_idx in range(ysize):
            if pixels[col_idx, row_idx] > 128:  # Bright = white = set bit
                column_data |= (1 << row_idx)

        # Convert to little-endian bytes
        datap = (ysize + 7) // 8
        for byte_idx in range(datap):
            byte_val = (column_data >> (byte_idx * 8)) & 0xFF
            char_bytes.append(byte_val)

    return char_bytes

def import_font_from_png(png_path, font_index, fonts_h_path):
    """Import font data from PNG"""
    xsize, ysize, datap = get_font_dimensions(font_index)

    print(f"Loading {png_path}...")
    img = Image.open(png_path)

    # Verify dimensions
    num_chars = 95
    expected_width = xsize * num_chars
    if img.height != ysize:
        raise ValueError(f"Expected height {ysize}, got {img.height}")
    if img.width != expected_width:
        print(f"Warning: Expected width {expected_width}, got {img.width}")

    print(f"Extracting {num_chars} characters ({xsize}×{ysize} each)...")

    # Extract all characters
    all_bytes = []
    for char_idx in range(num_chars):
        char_bytes = extract_character_from_png(img, char_idx, xsize, ysize)
        all_bytes.extend(char_bytes)

    # Pad to 4-byte boundary if needed
    while len(all_bytes) % 4 != 0:
        all_bytes.append(0)

    # Convert bytes to u32 array
    font_u32 = []
    for i in range(0, len(all_bytes), 4):
        u32_val = 0
        for j in range(4):
            u32_val |= all_bytes[i + j] << (j * 8)
        font_u32.append(u32_val)

    # Get expected size from font_offsets and pad if needed
    font_offsets, _ = parse_fonts_h(fonts_h_path)
    start_idx, end_idx = get_font_boundaries_for_index(font_offsets, font_index)
    if end_idx is None:
        # Parse to get total length
        _, all_data = parse_fonts_h(fonts_h_path)
        end_idx = len(all_data)

    expected_size = end_idx - start_idx

    # Pad with zeros to match expected size
    while len(font_u32) < expected_size:
        font_u32.append(0)

    return font_u32

def build_label_map(font_offsets):
    """Build a map of u32 index -> label for font boundaries"""
    label_map = {}

    for font_index in range(len(font_offsets)):
        u32_idx = font_offsets[font_index] // 4
        label_map[u32_idx] = FONT_NAMES[font_index]

    return label_map

def update_fonts_h(fonts_h_path, font_index, new_font_data):
    """Update fonts.h with new font data - preserves file structure"""
    print(f"Reading {fonts_h_path}...")

    # Read the entire file
    with open(fonts_h_path, 'r') as f:
        content = f.read()

    # Parse font_offsets and font_data
    font_offsets, all_font_data = parse_fonts_h(fonts_h_path)
    print(f"Current fonts.h has {len(all_font_data)} u32 values")

    # Get font name from enum value
    font_name = FONT_NAMES[font_index]

    # Get boundaries for this font
    start_idx, end_idx = get_font_boundaries_for_index(font_offsets, font_index)
    if end_idx is None:
        end_idx = len(all_font_data)

    old_size = end_idx - start_idx
    new_size = len(new_font_data)

    print(f"Replacing font {font_index} ({font_name}):")
    print(f"  Position: u32[{start_idx}:{end_idx}]")
    print(f"  Size: {old_size} → {new_size} u32 values")

    if old_size != new_size:
        raise ValueError(f"Font size mismatch! Expected {old_size} u32 values, got {new_size}")

    # Replace the font data
    all_font_data[start_idx:end_idx] = new_font_data

    # Build label map
    label_map = build_label_map(font_offsets)

    # Extract everything before font_data declaration
    prefix_match = re.search(r'(.*?)const\s+static\s+u32\s+font_data\[\d*\]\s*=\s*\{', content, re.DOTALL)
    if not prefix_match:
        raise ValueError("Could not find font_data declaration")

    prefix = prefix_match.group(1)

    # Extract everything after font_data array
    suffix_match = re.search(r'const\s+static\s+u32\s+font_data\[\d*\]\s*=\s*\{.*?\};(.*)', content, re.DOTALL)
    if not suffix_match:
        raise ValueError("Could not find end of font_data array")

    suffix = suffix_match.group(1)

    # Write output file
    print(f"Writing {fonts_h_path}...")
    with open(fonts_h_path, 'w') as f:
        # Write everything before font_data
        f.write(prefix)

        # Write font_data array
        f.write(f"const static u32 font_data[{len(all_font_data)}]={{\n")

        prev_label_idx = None
        for i in range(0, len(all_font_data), 8):
            # Check if we need to add a label before this line
            if i in label_map:
                # Add empty line before label (except for first label)
                if prev_label_idx is not None:
                    f.write("\n")
                f.write(f"\t// {label_map[i]}\n")
                prev_label_idx = i

            # Format up to 8 values
            row = all_font_data[i:i+8]
            hex_values = [f"0x{val:08x}" for val in row]
            line_values = [f"{hv}," for hv in hex_values]

            # Remove trailing comma from very last value
            if i + len(row) >= len(all_font_data):
                line_values[-1] = line_values[-1].rstrip(',')

            f.write("\t" + " ".join(line_values) + "\n")

        f.write("};")

        # Write everything after font_data (includes font_offsets)
        # Normalize to exactly one blank line
        f.write('\n\n' + suffix.lstrip('\n'))

    print("Done!")

if __name__ == "__main__":
    import sys

    if len(sys.argv) < 2:
        print("Usage: python3 png_to_font.py <font_index>")
        print("\nFont enum indices:")
        for idx in sorted(FONT_NAMES.keys()):
            print(f"  {idx}: {FONT_NAMES[idx]}")
        print("\nExample: python3 png_to_font.py 2")
        sys.exit(1)

    font_index = int(sys.argv[1])

    if font_index not in FONT_NAMES:
        print(f"Error: Invalid font_index {font_index}")
        print("Valid indices:", sorted(FONT_NAMES.keys()))
        sys.exit(1)

    # Auto-generate filename
    script_dir = os.path.dirname(os.path.abspath(__file__))
    data_dir = os.path.join(script_dir, "..", "data")
    png_path = os.path.join(data_dir, f"font_{FONT_NAMES[font_index]}.png")

    if not os.path.exists(png_path):
        print(f"Error: {png_path} not found")
        print(f"Run 'python3 fonts_to_png.py {font_index}' to generate it first")
        sys.exit(1)

    # Update fonts.h
    fonts_h_path = os.path.join(data_dir, "fonts.h")

    # Import font from PNG (needs fonts_h_path for sizing)
    new_font_data = import_font_from_png(png_path, font_index, fonts_h_path)

    update_fonts_h(fonts_h_path, font_index, new_font_data)
