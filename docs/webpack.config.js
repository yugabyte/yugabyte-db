const ESLintPlugin = require('eslint-webpack-plugin');
const path = require('path');

module.exports = {
  context: path.join(__dirname, 'src'),
  entry: {
    site: './index.js',
    search: './algolia-search.js',
    searchBanner: './search-banner.js',
  },
  output: {
    path: path.join(__dirname, 'static/js'),
    filename: '[name].js'
  },
  module: {
    rules: [
      {
        test: /\.(js|jsx)$/,
        include: path.join(__dirname, 'src'),
        use: {
          loader: 'babel-loader',
          options: {
            babelrc: true,
            cacheDirectory: true,
          }
        }
      },
    ],
  },
  plugins: [
    new ESLintPlugin()
  ],
};
