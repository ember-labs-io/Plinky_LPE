import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';

const releaseNotes = defineCollection({
  loader: glob({ pattern: '**/*.md', base: './src/content/release-notes' }),
  schema: z.object({}),
});

const references = defineCollection({
  loader: glob({ pattern: '**/*.md', base: './src/content/references' }),
  schema: z.object({}),
});

export const collections = { 'release-notes': releaseNotes, references };
