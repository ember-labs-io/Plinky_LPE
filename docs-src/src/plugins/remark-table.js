import { visit, SKIP } from 'unist-util-visit';
import { toString } from 'mdast-util-to-string';

const MODIFIER_CLASSES = {
  center:        'table-center',
  rows:          'table-rows',
  'content-fixed': 'table-content-fixed',
};

export function remarkTable() {
  return (tree) => {
    visit(tree, 'containerDirective', (node, index, parent) => {
      if (node.name !== 'table') return;

      const labelNode = node.children.find(c => c.data?.directiveLabel);
      const label = labelNode ? toString(labelNode).trim() : '';
      const classes = label.split(/\s+/).filter(Boolean).map(m => MODIFIER_CLASSES[m]).filter(Boolean);

      const tableNode = node.children.find(c => c.type === 'table');
      if (tableNode && classes.length > 0) {
        tableNode.data = tableNode.data || {};
        tableNode.data.hProperties = tableNode.data.hProperties || {};
        tableNode.data.hProperties.className = [
          ...(tableNode.data.hProperties.className || []),
          ...classes,
        ];
      }

      parent.children.splice(index, 1, ...node.children.filter(c => !c.data?.directiveLabel));
      return [SKIP, index];
    });

    visit(tree, 'table', (node) => {
      const header = node.children[0];
      if (!header) return;
      const allEmpty = header.children.every(cell => toString(cell).trim() === '');
      if (!allEmpty) return;
      node.children.splice(0, 1);
      node.data = node.data || {};
      node.data.hProperties = node.data.hProperties || {};
      node.data.hProperties.dataHeaderless = true;
    });
  };
}
