import { visit } from 'unist-util-visit';

export function rehypeTable() {
  return (tree) => {
    visit(tree, 'element', (node) => {
      if (node.tagName !== 'table' || !node.properties?.dataHeaderless) return;

      const thead = node.children.find(c => c.type === 'element' && c.tagName === 'thead');
      let tbody = node.children.find(c => c.type === 'element' && c.tagName === 'tbody');

      if (!thead) return;

      thead.children.forEach(tr => {
        if (tr.type !== 'element') return;
        tr.children.forEach(cell => {
          if (cell.type === 'element' && cell.tagName === 'th') cell.tagName = 'td';
        });
      });

      if (!tbody) {
        tbody = { type: 'element', tagName: 'tbody', properties: {}, children: [] };
        node.children.push(tbody);
      }

      tbody.children.unshift(...thead.children);
      node.children = node.children.filter(c => !(c.type === 'element' && c.tagName === 'thead'));
      delete node.properties.dataHeaderless;
    });
  };
}
