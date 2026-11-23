// Where the app is mounted: '' on the master itself, `/universes/<uuid>/proxy/<host>:<port>`
// through YugabyteDB Anywhere's proxy. The proxy rewrites the URLs in index.html but not the ones
// the app builds, so fetches, links, history entries and imported assets all go through `at`. The
// built script is `<mount>/ui/assets/<name>.js`, so its own URL gives the mount.
const mounted = new URL(import.meta.url).pathname.match(/^(.*)\/ui\/assets\/[^/]+$/);
export const MOUNT = mounted ? mounted[1] : '';

// Through the YBA proxy: its prefix (`/universes/<uuid>/proxy/`) and the host:port it proxies.
const proxied = MOUNT.match(/^(.*\/proxy\/)([^/]+)$/);
export const PROXY_PREFIX = proxied ? proxied[1] : null;
export const PROXIED_SERVER = proxied ? proxied[2] : null;

// A path from the master's root as this page reaches it.
export function at(path: string): string {
  return path.startsWith('/') ? MOUNT + path : path;
}

// The master path of a browser path under the mount.
export function fromBrowserPath(pathname: string): string {
  if (!MOUNT || !pathname.startsWith(MOUNT)) return pathname;
  return pathname.slice(MOUNT.length) || '/';
}
