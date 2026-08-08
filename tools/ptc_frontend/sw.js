const CACHE_NAME = "ptc-frontend-v4";
const STATIC_PATHS = [
  "./",
  "./index.html",
  "./styles.css",
  "./app.js",
  "./token.js",
  "./storage.js",
  "./pairing.js",
  "./manifest.webmanifest",
  "./icon.svg",
  "./icon-maskable.svg",
];
const STATIC_URLS = new Set(STATIC_PATHS.map(path => new URL(path, self.location.href).href));

self.addEventListener("install", event => {
  event.waitUntil(caches.open(CACHE_NAME).then(cache => cache.addAll(STATIC_PATHS)));
  self.skipWaiting();
});

self.addEventListener("activate", event => {
  event.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(key => key !== CACHE_NAME).map(key => caches.delete(key))))
      .then(() => self.clients.claim()),
  );
});

self.addEventListener("fetch", event => {
  if (event.request.method !== "GET") return;
  const url = new URL(event.request.url);
  if (!STATIC_URLS.has(url.href)) return;
  event.respondWith(
    caches.match(event.request).then(cached => cached || fetch(event.request).then(response => {
      if (!response.ok || response.type !== "basic") return response;
      const copy = response.clone();
      caches.open(CACHE_NAME).then(cache => cache.put(event.request, copy));
      return response;
    })),
  );
});
