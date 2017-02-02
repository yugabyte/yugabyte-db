var path = require('path');

module.exports = {
  eslint: {
    configFile: path.resolve(__dirname, '.eslintrc.yml')
  },
  resolve: {
    root: path.resolve(__dirname, './src')
  },
  sassLoader: {
    includePaths: [path.resolve(__dirname, "./src")]
  }
}
