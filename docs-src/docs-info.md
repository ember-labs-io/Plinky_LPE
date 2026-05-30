# Docs Setup

## Stack

- **Astro** static site generator (v6)
- **Tailwind CSS** v4 + **DaisyUI** v5 (`halloween` theme)
- **@tailwindcss/typography** (`prose` classes for markdown content)
- **Montserrat** (headings) + **Google Sans** (body) fonts

## Build

```
cd docs-src
npm run build   # deletes *.html in docs/, then runs astro build
npm run dev     # local dev server
```

Output goes to `docs/` (one level up), base URL `/Plinky_LPE/`.

## Content Collections

Defined in `src/content.config.ts`. Two collections, both using markdown files:

| Collection | Source dir | URL pattern |
|---|---|---|
| `release-notes` | `src/content/release-notes/` | `/Plinky_LPE/release-notes/<slug>` |
| `references` | `src/content/references/` | `/Plinky_LPE/references/<slug>` |

Content files have no frontmatter schema (empty `z.object({})`). Slug is the filename without extension.

## Routing

- `src/pages/index.astro` — renders the left nav and immediately redirects to `release-notes/v0-5-0`
- `src/pages/[...slug].astro` — single route that handles all content pages; renders the markdown `<Content />` inside the layout

## Layout (`src/layouts/Layout.astro`)

Three-column structure, fixed to viewport height:

```
┌─────────────────────────────────────────────────┐
│ Topbar (navbar)                                 │
├────────┬──────────────────────────┬─────────────┤
│        │                          │             │
│  Left  │     Main content         │  Right nav  │
│  nav   │     (scrollable)         │  (xl only)  │
│  (264px│                          │  (224px)    │
│  drawer│                          │             │
└────────┴──────────────────────────┴─────────────┘
```

- Left nav uses a DaisyUI drawer (toggleable on mobile via hamburger button)
- Right page-nav shows h3/h4 headings from the current page only
- Slots: `topbar`, `nav`, `page-nav`, default (main content)
- Smooth-scrolling for anchor links handled via inline `<script>`

## Remark Plugins (`src/plugins/`)

All plugins use `remark-directive` syntax (`:name` for inline, `::name` for leaf block).

### `remark-icon.js`
Inline PNG icons from `docs/img/icons/`.

```md
:I_TEMPO    →  <img src="/Plinky_LPE/img/icons/I_TEMPO.png" class="inline-icon">
```

Icons are displayed inline, vertically aligned, inverted to match the dark theme.

### `remark-param.js`
Synth parameter references with hover tooltip (SVG image from `docs/img/params/`).

```md
:p_shape    →  <span class="param-ref">shape <img class="param-img" ...></span>
```

- Label is lowercased (from `PARAM_LABELS` map)
- Icon lookup: uses the param's own SVG if it exists; otherwise falls back to the param two rows above (Plinky's pad layout has alternating label/value rows), or the first-row param in the same column for LFO params
- Throws a build error for unknown or unmapped params

### `remark-fn.js`
Function button references with hover tooltip (SVG from `docs/img/function/`).

```md
:fn_shift_a    →  <span class="fn-ref">shift a <img class="fn-img" ...></span>
```

Available keys: `FN_SHIFT_A`, `FN_SHIFT_B`, `FN_LOAD`, `FN_LEFT`, `FN_RIGHT`, `FN_CLEAR`, `FN_RECORD`, `FN_PLAY`.

### `remark-dinkus.js`
Section separator rendered as a centered asterism character.

```md
::dinkus    →  <p class="dinkus">⁕</p>
```

### `remark-links.js`
Rewrites `.md` links to site URLs so cross-references work in both the built site and raw markdown.

```md
[text](references/midi_implementation.md#anchor)
  →  href="/Plinky_LPE/references/midi_implementation#anchor"
```

## Static Assets (`docs/`)

| Path | Contents |
|---|---|
| `img/icons/` | Inline UI icons (PNG, ~70 files) — names `I_*` |
| `img/params/` | Synth parameter icons (SVG, one per visible param row) — names `P_*` |
| `img/function/` | Function button icons (SVG, 8 files) — names `FN_*` |
| `fonts/` | Google Sans variable font (regular + italic TTF) |
| `_astro/` | Astro-generated CSS bundle |

## CSS (`src/styles/global.css`)

- Tailwind + DaisyUI imported via `@import "tailwindcss"` and `@plugin` directives
- `prose` class colors mapped to DaisyUI theme tokens
- Custom classes:
  - `.param-ref` / `.fn-ref` — small-caps colored inline span; shows image tooltip on hover
  - `.param-img` / `.fn-img` — 48x48 px, absolutely positioned above the span, hidden until hover
  - `.inline-icon` — 1em height, inline, `filter: invert(1)`
  - `.dinkus` — centered, decorative separator
- Headings use Montserrat; h5 is italic, h6 uses `all-small-caps`
- Tables are block-scrollable (`overflow-x: auto`) and centered

## Sidebar Layout

Defined in `src/pages/[...slug].astro`, passed into the `nav` slot of `Layout.astro`.

Structure:

```
<ul class="menu p-4">
  <li class="menu-title">Release Notes</li>
  <li><a>...</a></li>   ← one per release-notes entry; class="active" on current page
  <li class="menu-title">References</li>
  <li><a>...</a></li>   ← one per references entry; label = first heading of file

[conditional: only if current page has h3/h4 headings]
  <hr class="border-[var(--color-yellow-dark)] mx-4" />
  <ul class="menu p-4">
    <li class="menu-title">On this page</li>
    <li style="padding-left: Npx"><a href="#slug">heading text</a></li>
      ↳ depth 3 → 0px indent, no bullet
      ↳ depth 4 → 12px indent, • prefix (opacity-50)
```

Notes:
- The `<hr>` color is hardcoded via Tailwind arbitrary value `border-[var(--color-yellow-dark)]` — it is not connected to `--tw-prose-hr`
- Nav labels for references are extracted at build time by splitting `entry.body` on newlines and stripping the leading `#` from the first line
- Nav labels for release notes are derived from the filename (`v0-5-0` → `v0.5.0` via `.replace(/-/g, '.')`)
- Active page link gets DaisyUI `active` class
- The sidebar `<aside>` in `Layout.astro` has no `prose` class — it is styled purely via DaisyUI `menu` utilities

## Adding Content

1. Drop a `.md` file into `src/content/release-notes/` or `src/content/references/`
2. No frontmatter required
3. The first heading line of a references file becomes its nav label
4. Release-notes nav label is derived from the filename (`v0-5-0` → `v0.5.0`)
5. Run `npm run build` — the new page appears automatically

## Colors

### DaisyUI Halloween Theme

The full set of semantic color tokens from `node_modules/daisyui/theme/halloween.css`:

| Token | Value | Approx |
|---|---|---|
| `--color-base-100` | `oklch(21% 0.006 56)` | very dark warm gray (page background) |
| `--color-base-200` | `oklch(14% 0.004 49)` | darker warm gray (sidebars) |
| `--color-base-300` | `oklch(0% 0 0)` | black (topbar) |
| `--color-base-content` | `oklch(85% 0 0)` | light gray (body text) |
| `--color-primary` | `oklch(77% 0.204 61)` | orange (links, active nav, param refs) |
| `--color-primary-content` | `oklch(20% 0.004 197)` | near-black (text on primary) |
| `--color-secondary` | `oklch(46% 0.248 305)` | purple |
| `--color-secondary-content` | `oklch(89% 0.049 305)` | light purple |
| `--color-accent` | `oklch(65% 0.223 136)` | green (inline code) |
| `--color-accent-content` | `oklch(0% 0 0)` | black (text on accent) |
| `--color-neutral` | `oklch(24% 0.046 66)` | dark warm brown |
| `--color-neutral-content` | `oklch(85% 0.009 66)` | light warm gray |
| `--color-info` | `oklch(55% 0.215 263)` | blue |
| `--color-info-content` | `oklch(91% 0.043 263)` | light blue |
| `--color-success` | `oklch(63% 0.169 149)` | green |
| `--color-success-content` | `oklch(13% 0.033 149)` | dark green |
| `--color-warning` | `oklch(67% 0.157 58)` | amber |
| `--color-warning-content` | `oklch(13% 0.031 58)` | dark amber |
| `--color-error` | `oklch(66% 0.199 27)` | red |
| `--color-error-content` | `oklch(13% 0.039 27)` | dark red |

### Custom Colors

Defined in `src/styles/global.css`:

| Token | Value | Used for |
|---|---|---|
| `--color-yellow` | `oklch(85% 0.17 75)` | warm yellow, links, `.param-ref`, `.fn-ref` default color |
| `--color-yellow-dark` | `oklch(60% 0.17 75)` | darker warm yellow, HR dividers |
| `--color-heading` | `#f2f2f2` | h1–h4 heading color, h5/h6 heading color, `.dinkus` color |
| `--color-border` | `oklch(42% 0 0)` | all table cell and header borders |
