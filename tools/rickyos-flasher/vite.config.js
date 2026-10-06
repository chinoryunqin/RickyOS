import { defineConfig } from 'vite';
import { resolve } from 'node:path';

export default defineConfig({
  base: './',
  build: { rollupOptions: { input: {
    main: resolve(import.meta.dirname, 'index.html'),
    install: resolve(import.meta.dirname, 'install.html'),
    licenses: resolve(import.meta.dirname, 'licenses.html'),
    r27: resolve(import.meta.dirname, '27.html'),
  } } },
});
