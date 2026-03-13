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
  const apiTarget = env.VITE_YUGAWARE_API_URL
    ? new URL(env.VITE_YUGAWARE_API_URL).origin
    : 'http://localhost:9000';

  return {
    plugins: [
      reduxFormPlugin(),
      svgr({
        exportAsDefault: true,
        svgrOptions: {
          plugins: ['@svgr/plugin-svgo', '@svgr/plugin-jsx'],
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
      exclude: ['node_modules/.cache'],
      include: ['redux-form'],
      rolldownOptions: {
        plugins: [reduxFormRolldownPlugin()]
      }
    },
    server: {
      port: 3000,
      host: '0.0.0.0',
      proxy: {
        '/api': {
          target: apiTarget,
          changeOrigin: true
        }
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
