import { visit } from 'unist-util-visit';
import { base } from '../site.js';

export function remarkIcon() {
  return (tree) => {
    visit(tree, 'textDirective', (node) => {
      if (!node.name.startsWith('I_')) return;
      node.type = 'image';
      node.url = `${base}/img/icons/${node.name}.png`;
      node.alt = node.name;
      node.data = { hProperties: { class: 'inline-icon' } };
    });
  };
}
