/*
 * Minimal service worker: serve page navigations network-first so a redeploy is
 * picked up on a normal reload. GitHub Pages serves the HTML with max-age=600,
 * which otherwise sticks for ~10 minutes; `cache: 'reload'` bypasses that for
 * the document fetch. Hashed /assets/* are content-addressed (their name changes
 * each build), so we leave them to the normal HTTP cache.
 *
 * It caches nothing itself and only intercepts navigations, so it can't get the
 * app "stuck" on a stale page. To retire it, deploy a sw.js whose fetch handler
 * is empty (or that calls self.registration.unregister()).
 */
self.addEventListener('install', () => self.skipWaiting());
self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
self.addEventListener('fetch', (event) => {
    const req = event.request;
    if (req.mode === 'navigate') {
        event.respondWith(fetch(req, { cache: 'reload' }).catch(() => fetch(req)));
    }
});
