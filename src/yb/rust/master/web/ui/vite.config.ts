import { defineConfig, loadEnv, Plugin } from 'vite';

// YugabyteDB Anywhere's proxy (ApiHelper.replaceProxyLinks) replaces every "http://" in a proxied
// body with its own prefix, scripts included, which breaks React's namespace URIs and any URL the
// app builds. In a JS string, template or regex "http:\/\/" is the same text and is left alone.
function noHttpLiterals(): Plugin {
  return {
    name: 'no-http-literals',
    generateBundle(_, bundle) {
      for (const file of Object.values(bundle)) {
        if (file.type === 'chunk') file.code = file.code.replaceAll('http://', 'http:\\/\\/');
      }
    },
  };
}

// The C++ pages the master still serves itself; `npm run dev` proxies them with /api.
const MASTER_PATHS = [
  '/api',
  '/ObjectLockManager',
  '/varz',
  '/logs',
  '/mem-trackers',
  '/threadz',
  '/rpcz',
  '/metrics',
  '/prometheus-metrics',
  '/memz',
];

export default defineConfig(({ command, mode }) => {
  const master = loadEnv(mode, '.', '').MASTER_URL || 'http://127.0.0.1:7000';
  return {
    // The master serves the built files under /ui/ and index.html at every page URL. The dev
    // server serves the app at / so the page URLs work there too.
    base: command === 'build' ? '/ui/' : '/',
    plugins: [noHttpLiterals()],
    build: {
      outDir: 'dist',
      emptyOutDir: true,
      assetsInlineLimit: 0,
    },
    server: {
      proxy: Object.fromEntries(
        MASTER_PATHS.map((p) => [p, { target: master, changeOrigin: true }]),
      ),
    },
  };
});
