import { defineConfig, loadEnv } from 'vite';
import react from '@vitejs/plugin-react';
import svgr from 'vite-plugin-svgr';
import dynamicImport from 'vite-plugin-dynamic-import';
import path from 'path';

// =============================================================================
// Code Transform Functions
// These transform code strings and are shared between Vite plugins and rolldown plugins
// =============================================================================

/**
 * Transform redux-form/es/util/isHotReloading.js
 *
 * The file references `module` which doesn't exist in ES modules:
 *   var castModule = module;  // ReferenceError!
 *
 * We add a shim that defines `module` as undefined so the code doesn't crash.
 */
const transformReduxForm = (code) => {
  // Only transform files that reference `module` directly
  if (code.includes('var castModule = module')) {
    // Add a module shim at the top - define module as undefined to prevent ReferenceError
    const moduleShim = 'var module = undefined;\n';
    code = moduleShim + code;
  }
  return code;
};

// =============================================================================
// Vite Plugins (for production build)
// =============================================================================

const createTransformPlugin = (name, filter, transform) => ({
  name,
  transform(code, id) {
    if (filter(id)) {
      return { code: transform(code), map: null };
    }
    return null;
  }
});

const reduxFormPlugin = () =>
  createTransformPlugin(
    'fix-redux-form-module-hot',
    (id) => id.includes('node_modules/redux-form'),
    transformReduxForm
  );

// =============================================================================
// Rolldown Plugins
// =============================================================================

const createRolldownLoadPlugin = (name, idFilter, transform) => ({
  name,
  async load(id) {
    if (!idFilter(id)) return null;
    const fs = await import('fs');
    const code = await fs.promises.readFile(id, 'utf8');
    return { code: transform(code) };
  }
});

const reduxFormRolldownPlugin = () =>
  createRolldownLoadPlugin('fix-redux-form', (id) => id.includes('redux-form'), transformReduxForm);

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), '');
  // Fail loudly on a malformed URL. new URL('htt://host').origin silently returns the string
  // "null", which makes http-proxy fall back to "base.invalid" (getaddrinfo ENOTFOUND base.invalid).
  const toOrigin = (value, varName) => {
    let url;
    try {
      url = new URL(value);
    } catch {
      throw new Error(`${varName} is not a valid URL: "${value}". Expected e.g. http://10.152.0.78`);
    }
    if (url.protocol !== 'http:' && url.protocol !== 'https:') {
      throw new Error(`${varName} must start with http:// or https:// (got "${value}").`);
    }
    return url.origin;
  };
  // When VITE_YUGAWARE_API_URL is set, run in "remote backend" dev mode (see `npm run start:remote`):
  // the browser only talks to the Vite dev server (same origin, so no browser CORS) and Vite proxies
  // /api to the remote YBA. Only the origin of VITE_YUGAWARE_API_URL is used (any /api path is
  // ignored); the client uses relative API roots (see src/config.js and YBAxios.ts) and we rewrite
  // request/response headers below.
  const apiUrl = env.VITE_YUGAWARE_API_URL;
  const apiTarget = apiUrl ? toOrigin(apiUrl, 'VITE_YUGAWARE_API_URL') : 'http://localhost:9000';
  const apiProxy = {
    target: apiTarget,
    changeOrigin: true,
    ...(apiUrl
      ? {
          // Allow proxying to remote backends with self-signed TLS certs.
          secure: false,
          configure: (proxy) => {
            proxy.on('proxyReq', (proxyReq) => {
              // Play's CSRF filter rejects POSTs whose Origin (http://localhost:3000) doesn't match
              // the upstream Host (the remote YBA). Strip Origin so requests look same-origin.
              proxyReq.removeHeader('origin');
              // Play uses a double-submit CSRF token: the value in the `csrfCookie` cookie must also
              // be echoed in the Csrf-Token header on state-changing requests. The SPA sets this
              // header itself, but on first load (index.html is served by Vite, not the backend) the
              // cookie may not have existed when the app captured it. Derive it here on every request
              // so proxied POSTs always carry a valid token.
              const cookieHeader = proxyReq.getHeader('cookie');
              if (typeof cookieHeader === 'string' && !proxyReq.getHeader('csrf-token')) {
                const match = cookieHeader.match(/(?:^|;\s*)csrfCookie=([^;]+)/);
                if (match) {
                  proxyReq.setHeader('Csrf-Token', decodeURIComponent(match[1]));
                }
              }
            });
            proxy.on('proxyRes', (proxyRes) => {
              // The remote sets its auth/CSRF cookies (e.g. csrfCookie, PLAY_SESSION) with Domain=
              // and/or Secure attributes. Over http://localhost:3000 the browser would drop those,
              // so the app can't read csrfCookie and Play then rejects POSTs with
              // "No CSRF token found". Rewrite Set-Cookie so the cookies are stored on localhost.
              const setCookie = proxyRes.headers['set-cookie'];
              if (setCookie) {
                proxyRes.headers['set-cookie'] = setCookie.map((cookie) =>
                  cookie
                    .replace(/;\s*Domain=[^;]*/i, '')
                    .replace(/;\s*Secure/i, '')
                    .replace(/;\s*SameSite=None/i, '; SameSite=Lax')
                );
              }
            });
          }
        }
      : {})
  };

  return {
    plugins: [
      reduxFormPlugin(),
      svgr({
        exportAsDefault: true,
        svgrOptions: {
          plugins: ['@svgr/plugin-jsx'],
          ref: true,
          svgo: false,
          titleProp: true
        },
        include: '**/*.svg',
        exclude: ['node_modules/**', '**/*.svg?img']
      }),
      react({
        exclude: /\.stories\.tsx?$/
      }),
      dynamicImport()
    ],
    resolve: {
      alias: {
        '@app': path.resolve(__dirname, './src'),
        // Polyfill Node.js 'events' module for browser compatibility
        // react-bootstrap-table (used by perf-advisor-ui) requires EventEmitter
        events: 'events'
      }
    },
    build: {
      outDir: 'build',
      sourcemap: false,
      target: 'es2022',
      rolldownOptions: {
        output: {
          entryFileNames: 'static/js/[name]-[hash].js',
          chunkFileNames: 'static/js/chunk-[name]-[hash].js',
          assetFileNames: (assetInfo) => {
            // CSS files go to static/css, other assets to static/assets
            if (assetInfo.name && assetInfo.name.endsWith('.css')) {
              return 'static/css/[name]-[hash][extname]';
            }
            return 'static/assets/[name]-[hash][extname]';
          }
        }
      }
    },
    publicDir: 'public',
    css: {
      modules: {
        // Match CRA's CSS Modules naming convention for Cypress test compatibility
        // Custom function to strip '.module' from filename (e.g., ProviderList.module.scss -> ProviderList)
        generateScopedName: (name, filename) => {
          const basename = path.basename(filename).replace(/\.module\.(scss|css|sass|less)$/, '');
          const hash = Buffer.from(`${basename}_${name}_${filename}`)
            .toString('base64')
            .slice(0, 5);
          return `${basename}_${name}__${hash}`;
        }
      }
    },
    define: {
      'process.env': {},
      global: 'globalThis'
    },
    optimizeDeps: {
      // `@yugabytedb/perf-advisor-ui` is locally linked via yalc during dev.
      // Excluding it from pre-bundling makes Vite serve its ESM straight from node_modules,
      // so every push is picked up on a plain restart.
      exclude: ['node_modules/.cache', '@yugabytedb/perf-advisor-ui'],
      include: ['redux-form'],
      rolldownOptions: {
        plugins: [reduxFormRolldownPlugin()]
      }
    },
    server: {
      port: 3000,
      host: '0.0.0.0',
      proxy: {
        '/api': apiProxy
      }
    },
    preview: {
      port: 3000,
      host: '0.0.0.0'
    },
    test: {
      include: ['src/**/*.test.{ts,tsx,js,jsx}'],
      globals: true,
      css: false,
      environment: 'jsdom',
      environmentOptions: {
        jsdom: {
          url: 'http://localhost'
        }
      },
      setupFiles: ['./vitest.setup.ts'],
      server: {
        deps: {
          inline: ['@yugabyte-ui-library/core', '@yugabytedb/perf-advisor-ui']
        }
      }
    }
  };
});
