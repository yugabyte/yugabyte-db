import { defineConfig, loadEnv } from 'vite';

// The C++ pages the master still serves itself; `npm run dev` proxies them with /api.
const MASTER_PATHS = [
  '/api',
  '/masters',
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
