#!/usr/bin/env python3
"""
Convert Plinky icons.h to PNG image
Each icon is 16x16 pixels, stored as 16 u16 values (one per column)
"""

import re
from PIL import Image, ImageDraw

# Icon dimensions
ICON_WIDTH = 16  # Icons stored as 16 columns
ICON_HEIGHT = 16  # 16 bits per u16
ICONS_PER_ROW = 8  # Grid layout

def parse_icons_h(filename):
    """Parse icons.h and extract the icon data"""
    with open(filename, 'r') as f:
        lines = f.readlines()

    # Find the start of the icons array
    in_array = False
    icons = []

    for line in lines:
        if 'const static u16 icons[64][16]' in line:
            in_array = True
            continue

        if not in_array:
            continue

        # Look for lines with hex values in braces
        if '{' in line and '}' in line:
            # Extract values between braces
            match = re.search(r'\{([^}]+)\}', line)
            if match:
                values_str = match.group(1)
                values = [int(v.strip(), 0) for v in values_str.split(',') if v.strip()]
                if len(values) == 16:
                    icons.append(values)

        # Stop when we hit the closing brace and semicolon
        if line.strip().startswith('};'):
            break

    return icons

def render_icon(icon_data):
    """Render a single icon to a PIL Image"""
    img = Image.new('1', (ICON_WIDTH, ICON_HEIGHT), 0)  # 1-bit image, black background
    pixels = img.load()

    for col_idx, column_data in enumerate(icon_data[:ICON_WIDTH]):  # All 16 columns
        for bit in range(ICON_HEIGHT):
            if column_data & (1 << bit):  # Check if bit is set
                pixels[col_idx, bit] = 1

    return img

def create_icon_sheet(icons):
    """Create a sprite sheet with all icons"""
    num_icons = len(icons)
    rows = (num_icons + ICONS_PER_ROW - 1) // ICONS_PER_ROW

    sheet_width = ICONS_PER_ROW * ICON_WIDTH
    sheet_height = rows * ICON_HEIGHT

    sheet = Image.new('RGB', (sheet_width, sheet_height), 'black')

    for idx, icon_data in enumerate(icons):
        row = idx // ICONS_PER_ROW
        col = idx % ICONS_PER_ROW

        x = col * ICON_WIDTH
        y = row * ICON_HEIGHT

        icon_img = render_icon(icon_data)
        # Convert 1-bit to RGB (white on black)
        icon_rgb = icon_img.convert('RGB')
        icon_rgb = Image.eval(icon_rgb, lambda p: 255 if p else 0)

        sheet.paste(icon_rgb, (x, y))

    return sheet

def create_individual_icons(icons):
    """Create individual PNG files for each icon"""
    import os

    output_dir = "icons_png"
    os.makedirs(output_dir, exist_ok=True)

    for idx, icon_data in enumerate(icons):
        icon_img = render_icon(icon_data)
        # Convert to RGB
        icon_rgb = icon_img.convert('RGB')
        icon_rgb = Image.eval(icon_rgb, lambda p: 255 if p else 0)

        filename = os.path.join(output_dir, f"icon_{idx:02d}.png")
        icon_rgb.save(filename)

    print(f"Created {len(icons)} individual icon files in {output_dir}/")

if __name__ == "__main__":
    import sys
    import os

    # Get script directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    icons_h_path = os.path.join(script_dir, "icons.h")

    print(f"Parsing {icons_h_path}...")
    icons = parse_icons_h(icons_h_path)
    print(f"Found {len(icons)} icons")

    # Create sprite sheet
    print("Creating sprite sheet...")
    sheet = create_icon_sheet(icons)

    output_file = os.path.join(script_dir, "icons_sheet.png")
    sheet.save(output_file)
    print(f"Saved sprite sheet to {output_file}")

    # Optionally create individual icons
    if "--individual" in sys.argv:
        print("Creating individual icon files...")
        create_individual_icons(icons)

    print("Done!")
