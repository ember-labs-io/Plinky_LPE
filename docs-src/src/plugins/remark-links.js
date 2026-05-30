import { visit } from 'unist-util-visit';
import { base } from '../site.js';

export function remarkLinks() {
  return (tree) => {
    visit(tree, 'link', (node) => {
      if (!node.url.includes('.md')) return;
      node.url = base + '/' + node.url.replace('.md', '');
    });
  };
}
