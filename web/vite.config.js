import { defineConfig } from 'vite'
import { execSync } from 'node:child_process'

// Cache-busting version for the fixed-name Emscripten artifacts (/wasm/tdfu.js
// and tdfu.wasm). Full `git describe` (not just the tag) so every commit gets a
// distinct value; 'dev' fallback for builds without git history.
let appVersion = 'dev'
try {
  appVersion = execSync('git describe --tags --always --dirty', { encoding: 'utf8' }).trim()
} catch { /* no git available - keep 'dev' */ }

// GitHub Pages project sites serve from a subpath
// (https://<user>.github.io/<repo>/), so allow the base href to be set at
// build time via PAGES_BASE. Defaults to '/' for root hosting / local dev.
export default defineConfig({
  base: process.env.PAGES_BASE || '/',
  define: {
    // Compiled into the bundle; app.js uses it to version the tdfu.wasm URL
    // (Module.locateFile) to match the ?v= on the glue script tag below.
    __APP_VERSION__: JSON.stringify(appVersion),
  },
  plugins: [
    {
      // /wasm/tdfu.js is a fixed name (not content-hashed like the /assets/*
      // bundles), and a stale cached copy demonstrably survived redeploys in
      // the field: a client kept v1.5.13 glue under a v1.5.15 page (an old
      // cache keyed on the bare URL). Version the URL so any such cache
      // misses after a deploy and falls through to the network.
      name: 'tdfu-cache-bust',
      transformIndexHtml: {
        order: 'pre',
        handler(html) {
          return html.replace('src="/wasm/tdfu.js"', `src="/wasm/tdfu.js?v=${appVersion}"`)
        },
      },
    },
  ],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
