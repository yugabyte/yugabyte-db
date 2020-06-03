// Webpack 2 config overrides for react-app-rewired.

var path = require('path');
var srcPath = path.resolve(__dirname, './src');
var eslintConfigPath = path.resolve(__dirname, '.eslintrc.yml');
const HtmlWebpackPlugin = require("html-webpack-plugin");

const ExtractTextPlugin = require("extract-text-webpack-plugin");

// module.exports = {
//   eslint: {
//     configFile: path.resolve(__dirname, '.eslintrc.yml')
//   },
//   resolve: {
//     root: path.resolve(__dirname, './src')
//   },
//   sassLoader: {
//     includePaths: [path.resolve(__dirname, "./src")]
//   }
// }

// get git info from command line
var commitHash = require('child_process')
  .execSync('git rev-parse HEAD')
  .toString().trim();

function loaderMatch(matchString) {
  return function (conf) {
    return conf.loader && conf.loader.match(matchString);
  };
}

function isJsPreRule(conf) {
  return conf.test && conf.test.toString().match('js|jsx') && conf.enforce === 'pre';
}

function rewireSass(config, env) {
  var urlLoaderRule = config.module.rules.find(loaderMatch('url-loader'));
  if (urlLoaderRule && urlLoaderRule.exclude) urlLoaderRule.exclude.push(/\.scss$/);

  var fileLoaderRule = config.module.rules.find(loaderMatch('file-loader'));
  if (fileLoaderRule && fileLoaderRule.exclude) fileLoaderRule.exclude.push(/\.scss$/);

  var sassRule = {
    test: /\.(scss|sass)$/
  };

  if (env === 'development') {
    sassRule.use = [{
      loader: "style-loader"
    }, {
      loader: "css-loader",
      options: {
        sourceMap: true
      }
    }, {
      loader: "sass-loader",
      options: {
        includePaths: [srcPath],
        sourceMap: true
      }
    }];
  } else {
    sassRule.use = ExtractTextPlugin.extract({
      use: [{
        loader: "css-loader"
      }, {
        loader: "sass-loader",
        options: {
          includePaths: [srcPath]
        }
      }],
      // Use style-loader in development.
      fallback: "style-loader"
    });
  }

  config.module.rules.unshift(sassRule);
  config.plugins.push(
    new ExtractTextPlugin({
      filename: "[name].[contenthash].css",
      disable: process.env.NODE_ENV === "development",
    })
  );

  return config;
}

module.exports = function override(config, env) {
  config = rewireSass(config, env);

  config.plugins.push(
    new HtmlWebpackPlugin({
      template: 'public/index.html',
      version: commitHash
    })
  );

  if (!config.resolve) config.resolve = {};
  if (!config.resolve.modules) config.resolve.modules = [];
  config.resolve.modules.push(srcPath);

  const jsPreRule = config.module.rules.find(isJsPreRule);
  var eslintLoader = jsPreRule.use.find(loaderMatch('eslint-loader'));
  eslintLoader.options.useEslintrc = true;

  return config;
}
