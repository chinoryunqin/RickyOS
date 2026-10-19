import { defineConfig } from 'vite';
import { resolve } from 'node:path';

// Build only the isolated lab entry. Production catalogs/OTA are never inputs.
export default defineConfig({
  base: './', publicDir: false,
  build: { outDir: 'dist-lab', assetsInlineLimit: 0,
    rollupOptions: { input: resolve(import.meta.dirname, 'lab-eego.html') } },
});
