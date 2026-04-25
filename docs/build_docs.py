#!/usr/bin/env python3

import os
import re
import sys

try:
    import markdown
except ImportError:
    print("Missing dependency: pip install markdown")
    raise

DEFS_H    = "../sw/Core/Src/plinky/defs.h"
ICONS_DIR = "img/params"
FN_ICONS_DIR = "img/function"
I_ICONS_DIR = "img/icons"

PARAM_LABELS = {
    "P_SHAPE":        "SHAPE",
    "P_DISTORTION":   "DIST",
    "P_PITCH":        "PITCH",
    "P_OCT":          "OCTAVE",
    "P_GLIDE":        "GLIDE",
    "P_INTERVAL":     "OSC<br>INTERVAL",
    "P_NOISE":        "NOISE",
    "P_RESO":         "RESO",
    "P_DEGREE":       "DEGREE",
    "P_SCALE":        "SCALE",
    "P_MICROTONE":    "MICRO<br>TONE",
    "P_COLUMN":       "COLUMN",
    "P_ENV_LVL1":     "SENS",
    "P_ATTACK1":      "ENV 1<br>ATTACK",
    "P_DECAY1":       "ENV 1<br>DECAY",
    "P_SUSTAIN1":     "ENV 1<br>SUSTAIN",
    "P_RELEASE1":     "ENV 1<br>RELEASE",
    "P_ROOT":         "ROOT",
    "P_ENV_LVL2":     "ENV 2<br>LEVEL",
    "P_ATTACK2":      "ENV 2<br>ATTACK",
    "P_DECAY2":       "ENV 2<br>DECAY",
    "P_SUSTAIN2":     "ENV 2<br>SUSTAIN",
    "P_RELEASE2":     "ENV 2<br>RELEASE",
    "P_ENV2_UNUSED":  "",
    "P_DLY_SEND":     "DELAY<br>SEND",
    "P_DLY_TIME":     "DELAY<br>TIME",
    "P_PING_PONG":    "DELAY<br>PING PONG",
    "P_DLY_WOBBLE":   "DELAY<br>WOBBLE",
    "P_DLY_FEEDBACK": "DELAY<br>FEEDBACK",
    "P_TEMPO":        "TEMPO",
    "P_RVB_SEND":     "REVERB<br>SEND",
    "P_RVB_TIME":     "REVERB<br>TIME",
    "P_SHIMMER":      "REVERB<br>SHIMMER",
    "P_RVB_WOBBLE":   "REVERB<br>WOBBLE",
    "P_RVB_UNUSED":   "",
    "P_SWING":        "SWING",
    "P_ARP_TGL":      "ARP<br>TOGGLE",
    "P_ARP_ORDER":    "ARP<br>ORDER",
    "P_ARP_CLK_DIV":  "ARP<br>CLOCK DIV",
    "P_ARP_CHANCE":   "ARP<br>CHANCE",
    "P_ARP_EUC_LEN":  "ARP<br>EUCLID LEN",
    "P_ARP_OCTAVES":  "ARP<br>OCTAVES",
    "P_LATCH_TGL":    "LATCH",
    "P_SEQ_ORDER":    "SEQ<br>ORDER",
    "P_SEQ_CLK_DIV":  "SEQ<br>CLOCK DIV",
    "P_SEQ_CHANCE":   "SEQ<br>CHANCE",
    "P_SEQ_EUC_LEN":  "SEQ<br>EUCLID LEN",
    "P_GATE_LENGTH":  "SEQ<br>GATE LEN",
    "P_SCRUB":        "SAMPLE<br>SCRUB",
    "P_GR_SIZE":      "SAMPLE<br>GRAIN SIZE",
    "P_PLAY_SPD":     "SAMPLE<br>PLAY SPD",
    "P_SMP_STRETCH":  "SAMPLE<br>STRETCH",
    "P_SAMPLE":       "SAMPLE ID",
    "P_PATTERN":      "PATTERN ID",
    "P_SCRUB_JIT":    "SAMPLE<br>SCRUB JITT",
    "P_GR_SIZE_JIT":  "SAMPLE<br>SIZE JITT",
    "P_PLAY_SPD_JIT": "SAMPLE<br>SPEED JITT",
    "P_SMP_UNUSED1":  "<UNUSED>",
    "P_SMP_UNUSED2":  "<UNUSED>",
    "P_STEP_OFFSET":  "SEQ STEP<br>OFFSET",
    "P_A_SCALE":      "CV A<br>LEVEL",
    "P_A_OFFSET":     "LFO A<br>OFFSET",
    "P_A_DEPTH":      "LFO A<br>DEPTH",
    "P_A_RATE":       "LFO A<br>RATE",
    "P_A_SHAPE":      "LFO A<br>SHAPE",
    "P_A_SYM":        "LFO A<br>SYMM",
    "P_B_SCALE":      "CV B<br>LEVEL",
    "P_B_OFFSET":     "LFO B<br>OFFSET",
    "P_B_DEPTH":      "LFO B<br>DEPTH",
    "P_B_RATE":       "LFO B<br>RATE",
    "P_B_SHAPE":      "LFO B<br>SHAPE",
    "P_B_SYM":        "LFO B<br>SYMM",
    "P_X_SCALE":      "CV X<br>LEVEL",
    "P_X_OFFSET":     "LFO X<br>OFFSET",
    "P_X_DEPTH":      "LFO X<br>DEPTH",
    "P_X_RATE":       "LFO X<br>RATE",
    "P_X_SHAPE":      "LFO X<br>SHAPE",
    "P_X_SYM":        "LFO X<br>SYMM",
    "P_Y_SCALE":      "CV Y<br>LEVEL",
    "P_Y_OFFSET":     "LFO Y<br>OFFSET",
    "P_Y_DEPTH":      "LFO Y<br>DEPTH",
    "P_Y_RATE":       "LFO Y<br>RATE",
    "P_Y_SHAPE":      "LFO Y<br>SHAPE",
    "P_Y_SYM":        "LFO Y<br>SYMM",
    "P_SYN_LVL":      "SYNTH<br>LEVEL",
    "P_SYN_WET_DRY":  "SYNTH<br>WET/DRY",
    "P_HPF":          "HPF",
    "P_MIX_UNUSED1":  "<UNUSED>",
    "P_SETTINGS1":    "SETTINGS",
    "P_VOLUME":       "VOLUME",
    "P_IN_LVL":       "INPUT<br>LEVEL",
    "P_IN_WET_DRY":   "INPUT<br>WET/DRY",
    "P_SYS_UNUSED1":  "<UNUSED>",
    "P_MIX_UNUSED2":  "<UNUSED>",
    "P_SETTINGS2":    "<UNUSED>",
    "P_MIX_WIDTH":    "STEREO<br>WIDTH",
}


def parse_params():
    """Return list of all P_ param names in enum order from defs.h."""
    with open(DEFS_H) as f:
        content = f.read()
    match = re.search(r'typedef enum Param \{(.+?)\} Param;', content, re.DOTALL)
    if not match:
        raise RuntimeError("Could not find Param enum in defs.h")
    return re.findall(r'\bP_\w+\b', match.group(1))


def get_icon_path(params, param_name):
    """
    Return the icon path for param_name.
    Step 1: look for PARAM_NAME.svg
    Step 2: if param is on an odd row (second of pair), look for (index - 6).svg
    Step 3: if param is in the LFO region (P_A_SCALE..P_Y_SYM), look for the same column in row A
    """
    direct = os.path.join(ICONS_DIR, param_name + ".svg")
    if os.path.exists(direct):
        return direct

    idx = params.index(param_name)

    row = idx // 6
    if row % 2 == 1:
        fallback = os.path.join(ICONS_DIR, params[idx - 6] + ".svg")
        if os.path.exists(fallback):
            return fallback

    lfo_start = params.index("P_A_SCALE")
    lfo_end   = params.index("P_Y_SYM")
    if lfo_start <= idx <= lfo_end:
        col = idx % 6
        fallback = os.path.join(ICONS_DIR, params[lfo_start + col] + ".svg")
        if os.path.exists(fallback):
            return fallback

    print(f"ERROR: No icon found for {param_name} (index {idx})", file=sys.stderr)
    sys.exit(1)


SECTIONS = [
    ("Release Notes", "release_notes"),
    ("References",    "references"),
]

PAGE_TEMPLATE = """\
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Plinky - Lucky Phoenix Edition</title>
    <link rel="icon" type="image/svg+xml" href="img/params/P_A_SHAPE.svg">
    <link rel="stylesheet" href="pico.min.css">
    <link rel="stylesheet" href="style.css">
</head>
<body>
    <div id="topbar">
        <a href="index.html"><img src="img/ember-labs-logo-100x100.png" alt="Ember Labs"></a>
        <a href="index.html" id="topbar-title"><h1>Plinky - Lucky Phoenix Edition</h1></a>
    </div>
    <div id="body">
        <div id="sidebar">
            <nav>
{nav}
            </nav>
        </div>
        <div id="content">
            <div id="content-inner">
{content}
            </div>
        </div>
    </div>
</body>
</html>
"""


def slug(filename):
    return os.path.splitext(filename)[0]


def page_title(filename):
    return slug(filename).replace("_", " ")


def file_heading(folder, filename):
    path = os.path.join(folder, filename)
    with open(path) as f:
        first_line = f.readline().strip()
    if first_line.startswith("#"):
        return first_line.lstrip("#").strip()
    return page_title(filename)


def out_filename(filename):
    return slug(filename).lower() + ".html"


FN_LABELS = {
    "FN_SHIFT_A": "SHIFT A",
    "FN_SHIFT_B": "SHIFT B",
    "FN_LOAD":    "LOAD",
    "FN_LEFT":    "LEFT",
    "FN_RIGHT":   "RIGHT",
    "FN_CLEAR":   "CLEAR",
    "FN_RECORD":  "RECORD",
    "FN_PLAY":    "PLAY",
}


def expand_dim_tags(text):
    return re.sub(r'\[DIM\](.*?)\[/DIM\]', r'<span class="dim">\1</span>', text, flags=re.DOTALL)


def expand_equalcols_tags(text):
    return re.sub(r'\[EQUALCOLS\](.*?)\[/EQUALCOLS\]', r'<div class="equalcols" markdown="1">\1</div>', text, flags=re.DOTALL)


def expand_leftfit_tags(text):
    def replace(m):
        table_text = m.group(1).strip()
        lines = [l for l in table_text.splitlines() if l.strip()]

        rows = []
        header_idx = None
        for line in lines:
            cells = [c.strip() for c in line.strip('|').split('|')]
            is_sep = (
                any(c.strip() for c in cells) and
                all(re.match(r'^[-:]*$', c.strip()) for c in cells if c.strip())
            )
            if is_sep:
                if rows:
                    header_idx = len(rows) - 1
                continue
            rows.append(cells)

        if not rows:
            return m.group(0)

        n_cols = max(len(r) for r in rows)
        col_template = f"auto repeat({n_cols - 1}, 1fr)" if n_cols > 1 else "auto"

        out = [f'<div class="leftfit" style="grid-template-columns: {col_template}">']
        for row_idx, row in enumerate(rows):
            is_header = row_idx == header_idx
            cls = 'leftfit-td leftfit-th' if is_header else 'leftfit-td'
            for cell in row:
                cell_html = markdown.markdown(cell).strip()
                cell_html = re.sub(r'^<p>(.*)</p>$', r'\1', cell_html, flags=re.DOTALL)
                out.append(f'<div class="{cls}">{cell_html}</div>')
            for _ in range(n_cols - len(row)):
                out.append(f'<div class="{cls}"></div>')
        out.append('</div>')
        return '\n'.join(out)

    return re.sub(r'\[LEFTFIT\](.*?)\[/LEFTFIT\]', replace, text, flags=re.DOTALL)


def expand_lefttable_tags(text):
    return re.sub(r'\[LEFTTABLE\](.*?)\[/LEFTTABLE\]', r'<div class="lefttable" markdown="1">\1</div>', text, flags=re.DOTALL)


def expand_indent_tags(text):
    def replace(m):
        n = int(m.group(1)) if m.group(1) else 1
        return f'<div style="margin-left: {n * 48}px" markdown="1">{m.group(2)}</div>'
    return re.sub(r'\[INDENT(?:=(\d+))?\](.*?)\[/INDENT\]', replace, text, flags=re.DOTALL)


def expand_fn_tags(text):
    def replace(m):
        name = m.group(1)
        path = os.path.join(FN_ICONS_DIR, name + ".svg")
        if not os.path.exists(path):
            print(f"ERROR: No icon found for {name}", file=sys.stderr)
            sys.exit(1)
        return f'<img src="{path}" class="inline-icon" style="position:relative;top:2px">'
    return re.sub(r'\[(FN_\w+)\]', replace, text)


def expand_icon_tags(text):
    def replace(m):
        name = m.group(1)
        path = os.path.join(I_ICONS_DIR, name + ".png")
        if not os.path.exists(path):
            print(f"ERROR: No icon found for {name}", file=sys.stderr)
            sys.exit(1)
        return f'<img src="{path}" class="inline-icon icon" style="position:relative;top:4px">'
    return re.sub(r'\[(I_\w+)\]', replace, text)


def expand_param_tags(text, params):
    def replace(m):
        name = m.group(1)
        path = get_icon_path(params, name)
        idx  = params.index(name)
        row  = idx // 6
        img  = f'<img src="{path}" class="inline-icon" style="position:relative;top:2px">'
        label = f'<span class="param-label">{PARAM_LABELS.get(name, name)}</span>'
        label_text = PARAM_LABELS.get(name, name)
        has_br = '<br>' in label_text
        if has_br:
            lines = label_text.split('<br>')
            outer_longer_up   = len(lines[0])  >= len(lines[-1]) + 2
            outer_longer_down = len(lines[-1]) >= len(lines[0])  + 2
            margin_up   = '-2px' if outer_longer_up   else '1px'
            margin_down = '-2px' if outer_longer_down else '2px'
        else:
            margin_up = margin_down = '-2px'
        if row % 2 == 0:
            img_up = img.replace('top:2px', 'top:4px')
            label = f'<span class="param-label" style="margin-left:{margin_up};position:relative;top:4px">{label_text}</span>'
            return f'<span style="display:inline-flex;align-items:flex-start">{img_up}{label}</span>'
        else:
            img_down = img.replace('top:2px', 'top:1px')
            label_down = f'<span class="param-label-down" style="margin-right:{margin_down}">{label_text}</span>'
            return f'{label_down}{img_down}'
    return re.sub(r'\[(P_\w+)\]', replace, text)


def remove_empty_thead(html):
    def replace(m):
        ths = re.findall(r'<th>(.*?)</th>', m.group(0), re.DOTALL)
        if ths and all(not th.strip() for th in ths):
            return ''
        return m.group(0)
    return re.sub(r'<thead>.*?</thead>', replace, html, flags=re.DOTALL)


def convert_md(src_path, params):
    with open(src_path) as f:
        text = f.read()
    text = expand_param_tags(text, params)
    text = expand_fn_tags(text)
    text = expand_icon_tags(text)
    text = expand_dim_tags(text)
    text = expand_equalcols_tags(text)
    text = expand_leftfit_tags(text)
    text = expand_lefttable_tags(text)
    text = expand_indent_tags(text)
    html = markdown.markdown(text, extensions=["tables", "md_in_html", "toc"])
    html = re.sub(r'href="([^"]+)\.md(#[^"]*)??"', r'href="\1.html\2"', html)
    return remove_empty_thead(html)


def build():
    params = parse_params()

    # Collect all pages first so we can build the full nav for every page
    pages = []
    for section_label, folder in SECTIONS:
        if not os.path.isdir(folder):
            continue
        files = sorted(f for f in os.listdir(folder) if f.endswith(".md"))
        for filename in files:
            pages.append((section_label, folder, filename))

    if not pages:
        print("No markdown files found.")
        return

    def build_nav(active_out):
        lines = []
        current_section = None
        for section_label, folder, filename in pages:
            if section_label != current_section:
                if current_section is not None:
                    lines.append('                </ul>')
                lines.append(f'                <div class="section-title">{section_label}</div>')
                lines.append('                <ul>')
                current_section = section_label
            out = out_filename(filename)
            title = file_heading(folder, filename) if folder == "references" else page_title(filename)
            active = ' class="active"' if out == active_out else ''
            lines.append(f'                    <li><a href="{out}"{active}>{title}</a></li>')
        if current_section is not None:
            lines.append('                </ul>')
        return "\n".join(lines)

    for section_label, folder, filename in pages:
        out = out_filename(filename)
        title = page_title(filename)
        content = convert_md(os.path.join(folder, filename), params)
        nav = build_nav(out)
        html = PAGE_TEMPLATE.format(title=title, nav=nav, content=content)
        with open(out, "w") as f:
            f.write(html)

    # index.html redirects to the first page
    first_page = out_filename(pages[0][2])
    with open("index.html", "w") as f:
        f.write(f'<!DOCTYPE html><meta http-equiv="refresh" content="0; url={first_page}">\n')

    names = [out_filename(p[2]) for p in pages]
    print(f"Built {len(pages)} page(s): {', '.join(names)}")


if __name__ == "__main__":
    build()
