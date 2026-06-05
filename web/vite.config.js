import { defineConfig } from 'vite'

// GitHub Pages project sites serve from a subpath
// (https://<user>.github.io/<repo>/), so allow the base href to be set at
// build time via PAGES_BASE. Defaults to '/' for root hosting / local dev.
export default defineConfig({
  base: process.env.PAGES_BASE || '/',
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
