// Webpack 2 config overrides for react-app-rewired.

var path = require('path');
var srcPath = path.resolve(__dirname, './src');
var eslintConfigPath = path.resolve(__dirname, '.eslintrc.yml');

const ExtractTextPlugin = require("extract-text-webpack-plugin");

const extractSass = new ExtractTextPlugin({
    filename: "[name].[contenthash].css",
    disable: process.env.NODE_ENV === "development"
});

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
  if (urlLoaderRule.exclude) urlLoaderRule.exclude.push(/\.scss$/);

  var fileLoaderRule = config.module.rules.find(loaderMatch('file-loader'));
  if (fileLoaderRule.exclude) fileLoaderRule.exclude.push(/\.scss$/);

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
    sassRule.use = extractSass.extract({
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

  return config;
}

module.exports = function override(config, env) {
  config = rewireSass(config, env);

  if (!config.resolve) config.resolve = {};
  if (!config.resolve.modules) config.resolve.modules = [];
  config.resolve.modules.push(srcPath);

  const jsPreRule = config.module.rules.find(isJsPreRule);
  var eslintLoader = jsPreRule.use.find(loaderMatch('eslint-loader'));
  eslintLoader.options.useEslintrc = true;

  return config;
}
