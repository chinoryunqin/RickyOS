import { defineConfig } from 'vite';
import { resolve } from 'node:path';

export default defineConfig({
  base: './',
  // No data: URIs: the pages' CSP only allows images from 'self'.
  build: { assetsInlineLimit: 0, rollupOptions: { input: {
    main: resolve(import.meta.dirname, 'index.html'),
    install: resolve(import.meta.dirname, 'install.html'),
    licenses: resolve(import.meta.dirname, 'licenses.html'),
    r27: resolve(import.meta.dirname, '27.html'),
    rickyos: resolve(import.meta.dirname, 'rickyos.html'),
    developers: resolve(import.meta.dirname, 'developers.html'),
    verse: resolve(import.meta.dirname, 'verse.html'),
  } } },
});
