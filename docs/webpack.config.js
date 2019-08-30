const path = require('path');

const config = {
  context: path.join(__dirname, 'src'),
  entry: {
    site: ['./index.js'],
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
        enforce: 'pre',
        use: [
          {
            loader: path.join(__dirname, 'node_modules/eslint-loader/index.js'),
            options: { eslintPath: path.join(__dirname, 'node_modules/eslint/lib/api.js'),
              ignore: false,
              useEslintrc: true,
            },
          }
        ],
      },
      {
        test: /\.(js|jsx)$/,
        include: path.join(__dirname, 'src'),
        loader: path.join(__dirname, 'node_modules/babel-loader/lib/index.js'),
        options: {
          babelrc: true,
          cacheDirectory: true,
        }
      },
    ],
  },
};

module.exports = config;
