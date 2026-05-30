import { visit } from 'unist-util-visit';

export function remarkDinkus() {
  return (tree) => {
visit(tree, 'leafDirective', (node) => {
      if (node.name !== 'dinkus') return;
      node.type = 'html';
      node.value = '<p class="dinkus">⁕</p>';
    });
  };
}
