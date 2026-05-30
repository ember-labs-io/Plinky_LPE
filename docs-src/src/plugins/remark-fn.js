import { visit } from 'unist-util-visit';
import { readdirSync } from 'fs';
import { join } from 'path';
import { base } from '../site.js';

const ICONS_DIR = join(process.cwd(), 'public/img/function');
const AVAILABLE_ICONS = new Set(
  readdirSync(ICONS_DIR)
    .filter(f => f.endsWith('.svg'))
    .map(f => f.slice(0, -4))
);

const FN_LABELS = {
  'FN_SHIFT_A': 'Shift A',
  'FN_SHIFT_B': 'Shift B',
  'FN_LOAD':    'Load',
  'FN_LEFT':    'Left',
  'FN_RIGHT':   'Right',
  'FN_CLEAR':   'Cross',
  'FN_RECORD':  'Record',
  'FN_PLAY':    'Play',
};

export function remarkFn() {
  return (tree) => {
    visit(tree, 'textDirective', (node) => {
      const name = node.name.toUpperCase();
      if (!name.startsWith('FN_')) return;
      if (!FN_LABELS[name]) throw new Error(`[remark-fn] No label for: ${name}`);
      if (!AVAILABLE_ICONS.has(name)) throw new Error(`[remark-fn] No icon for: ${name}`);
      const label = FN_LABELS[name].toLowerCase();
      node.data = {
        hName: 'span',
        hProperties: { class: 'fn-ref' },
      };
      node.children = [
        { type: 'text', value: label },
        {
          type: 'image',
          url: `${base}/img/function/${name}.svg`,
          alt: name,
          data: { hProperties: { class: 'fn-img' } },
        },
      ];
    });
  };
}
