/*
 * Minimal service worker: keep the fixed-name resources fresh on a normal reload
 * so a redeploy is picked up without a hard-reload. GitHub Pages serves everything
 * with cache-control: max-age=600, which otherwise sticks for ~10 minutes.
 *
 *  - Page navigations: network-first (cache: 'reload') so new HTML lands at once.
 *  - /wasm/* (the Emscripten glue tdfu.js + tdfu.wasm): these are NOT content-hashed
 *    like the Vite /assets/* bundles, so a normal reload would keep the stale cached
 *    WASM - and the app version string AND the C-side fixes live inside the WASM.
 *    Revalidate them on each load (cache: 'no-cache' => a conditional GET, so it's a
 *    cheap 304 when the WASM hasn't changed, a fresh fetch when it has).
 *    Since v1.5.17 these URLs also carry ?v=<git describe> (vite.config.js +
 *    locateFile in app.js) - the primary cache-buster; stale copies keyed on the
 *    bare URL survived redeploys in the field on clients with older SW versions.
 *    This revalidation stays as the second layer.
 *
 * Content-hashed /assets/* change name per build, so they're left to the normal HTTP
 * cache. The worker caches nothing itself, so it can't get stuck on a stale page. To
 * retire it, deploy a sw.js whose fetch handler is empty (or unregister()s).
 */
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', (event) => {
    const req = event.request;
    if (req.mode === 'navigate') {
        event.respondWith(fetch(req, { cache: 'reload' }).catch(() => fetch(req)));
        return;
    }
    const url = new URL(req.url);
    if (url.origin === self.location.origin && url.pathname.startsWith('/wasm/')) {
        event.respondWith(fetch(req, { cache: 'no-cache' }).catch(() => fetch(req)));
    }
});
