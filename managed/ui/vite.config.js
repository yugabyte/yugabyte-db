import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import svgr from 'vite-plugin-svgr';
import dynamicImport from 'vite-plugin-dynamic-import';
import { createHtmlPlugin } from 'vite-plugin-html';
import path from 'path';

// =============================================================================
// Code Transform Functions
// These transform code strings and are shared between Vite plugins and esbuild plugins
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

/**
 * Transform moment-precise-range-plugin/moment-precise-range.js
 *
 * The file has this CommonJS pattern that fails in ES modules:
 *   if (typeof moment === "undefined" && typeof require === 'function') {
 *       var moment = require('moment');
 *   }
 *   (function(moment) { ... }(moment));
 *
 * We replace the require block with an ES import.
 */
const transformMomentPreciseRange = (code) => {
  // Only transform if it has the moment require pattern
  if (code.includes("require('moment')") || code.includes('require("moment")')) {
    // Remove the if block that tries to require moment
    code = code.replace(/if\s*\([^)]*typeof\s+moment[^)]*typeof\s+require[^)]*\)\s*\{[^}]*\}/, '');
    // Add ES import at the top
    code = `import moment from 'moment';\n${code}`;
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

const momentPreciseRangePlugin = () =>
  createTransformPlugin(
    'fix-moment-precise-range-plugin',
    (id) => id.includes('moment-precise-range-plugin'),
    transformMomentPreciseRange
  );

const reduxFormPlugin = () =>
  createTransformPlugin(
    'fix-redux-form-module-hot',
    (id) => id.includes('node_modules/redux-form'),
    transformReduxForm
  );

// =============================================================================
// esbuild Plugins (for optimizeDeps pre-bundling)
// =============================================================================

const createEsbuildTransformPlugin = (name, filter, transform) => ({
  name,
  setup(build) {
    build.onLoad({ filter }, async (args) => {
      const fs = await import('fs');
      let contents = await fs.promises.readFile(args.path, 'utf8');
      contents = transform(contents);
      return { contents, loader: 'js' };
    });
  }
});

export default defineConfig({
  plugins: [
    momentPreciseRangePlugin(),
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
    createHtmlPlugin({
      minify: false,
      inject: {
        data: {
          VITE_BUILD_COMMIT: process.env.VITE_BUILD_COMMIT
        }
      }
    }),
    dynamicImport()
  ],
  resolve: {
    alias: {
      '@app': path.resolve(__dirname, './src')
    }
  },
  build: {
    outDir: 'build',
    sourcemap: false,
    target: 'es2022',
    rollupOptions: {
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
        const hash = Buffer.from(`${basename}_${name}_${filename}`).toString('base64').slice(0, 5);
        return `${basename}_${name}__${hash}`;
      }
    }
  },
  define: {
    'process.env': {
      REACT_APP_API_URL: 'http://localhost:90000'
    },
    global: 'globalThis',
    this: 'window',
    globalThis: 'window'
  },
  optimizeDeps: {
    include: ['moment-precise-range-plugin', 'redux-form'],
    esbuildOptions: {
      plugins: [
        createEsbuildTransformPlugin(
          'fix-moment-precise-range',
          /node_modules\/moment-precise-range-plugin\/.*\.js$/,
          transformMomentPreciseRange
        ),
        createEsbuildTransformPlugin(
          'fix-redux-form',
          /node_modules\/redux-form\/.*\.js$/,
          transformReduxForm
        )
      ]
    }
  },
  server: {
    port: 3000,
    host: '0.0.0.0',
    proxy: {
      '/api': {
        target: 'http://localhost:9000',
        changeOrigin: true
      }
    }
  },
  preview: {
    port: 3000,
    host: '0.0.0.0'
  }
});
