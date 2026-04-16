import * as path from 'path';
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import svgr from 'vite-plugin-svgr';
import { createHtmlPlugin } from 'vite-plugin-html';

const viteConfig = {
  plugins: [
    react({
      jsxRuntime: 'classic',
      exclude: /\.stories\.tsx?$/
    }),
    // use svg icons as react components
    svgr({
      exportAsDefault: true,
      svgrOptions: {
        plugins: ['@svgr/plugin-svgo', '@svgr/plugin-jsx']
      }
    }),
    // plugin to access env variables at index.html
    createHtmlPlugin({
      minify: false,
      inject: {
        data: {
          VITE_API_BASE_URL: process.env.VITE_API_BASE_URL,
          VITE_BUILD_VERSION: process.env.VITE_BUILD_VERSION,
          VITE_BUILD_COMMIT: process.env.VITE_BUILD_COMMIT
        }
      }
    })
  ],
  resolve: {
    alias: [{ find: '@app', replacement: path.resolve(__dirname, 'src') }]
  },
  publicDir: 'public',
  build: {
    sourcemap: false,
    outDir: 'ui',
    copyPublicDir: true,
    assetsDir: 'js',
    chunkSizeWarningLimit: 2000,
    rollupOptions: {
      output: {
        manualChunks: {
          'vendor': ['react', 'react-dom'],
          'ui': ['@material-ui/core', '@material-ui/icons'],
          'mui-datatables': ['mui-datatables'],
          'material-ui-lab': ['@material-ui/lab']
        }
      }
    }
  },
  server: {
    host: '0.0.0.0',
    port: 3000,
    proxy: {
      '/api': {
        target: 'http://localhost:15433',
        changeOrigin: true,
        secure: false,
      },
    },
  },
  preview: {
    port: 5000
  },
  // workaround for dependencies that reference "process.env" and "global"
  define: process.env.NODE_ENV === 'production'
    ? {
        'process.env.NODE_ENV': JSON.stringify('production')
      }
    : {
        'process.env.NODE_ENV': JSON.stringify('development'),
        global: 'window'
      }
};

export default defineConfig(viteConfig);
