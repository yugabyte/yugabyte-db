const packageJson = require('./package.json');
const proxy = require('http2-proxy');

module.exports = {
  buildOptions: {
    out: "ui"
  },
  mount: {
    public: { url: '/', static: true },
    src: { url: '/dist' }
  },
  plugins: [
    '@snowpack/plugin-react-refresh',
    ['@snowpack/plugin-typescript', { args: '--project ./tsconfig.json' }],
    'snowpack-plugin-svgr', // converts svg imports into react components
    // don't minify index.html as uptime monitoring ID put there as a comment get stripped off
    ['@snowpack/plugin-webpack', { htmlMinifierOptions: false }]
  ],
  alias: {
    '@app': './src'
  },
  routes: [
    {
      src: '/api/.*',
      dest: (req, res) => {
        return proxy.web(req, res, {
          hostname: 'localhost',
          port: 15433,
        })
      }
    },
    // web server needs to serve the same page at all URLs that are managed client-side by <BrowserRouter>
    { match: 'routes', src: '.*', dest: '/index.html' },
    
  ],
  packageOptions: {
    // no need to build storybook, prettier and other unrelated deps with snowpack
    external: Object.keys(packageJson.devDependencies)
  },
  devOptions: {
    port: 3000
  },
};
