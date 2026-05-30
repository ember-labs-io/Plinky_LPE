// @ts-check
import { defineConfig } from 'astro/config';

import tailwindcss from '@tailwindcss/vite';
import remarkDirective from 'remark-directive';
import { remarkDinkus } from './src/plugins/remark-dinkus.js';
import { remarkIcon } from './src/plugins/remark-icon.js';
import { remarkParam } from './src/plugins/remark-param.js';
import { remarkFn } from './src/plugins/remark-fn.js';
import { remarkLinks } from './src/plugins/remark-links.js';
import { remarkTable } from './src/plugins/remark-table.js';
import { rehypeTable } from './src/plugins/rehype-table.js';

// https://astro.build/config
export default defineConfig({
  outDir: '../docs',
  base: '/Plinky_LPE/',
  markdown: {
    remarkPlugins: [remarkDirective, remarkDinkus, remarkIcon, remarkParam, remarkFn, remarkLinks, remarkTable],
    rehypePlugins: [rehypeTable],
  },
  vite: {
    plugins: [tailwindcss()]
  }
});