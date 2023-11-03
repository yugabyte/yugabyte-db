const { ESBuildMinifyPlugin } = require('esbuild-loader');
const { getLoaders, loaderByName, removeLoaders, addAfterLoader } = require('@craco/craco');

const throwError = (message) =>
  throwUnexpectedConfigError({
    packageName: 'craco',
    githubRepo: 'gsoft-inc/craco',
    message,
    githubIssueQuery: 'webpack'
  });

module.exports = {
  typescript: {
    enableTypeChecking: true /* (default value) */
  },
  webpack: {
    configure: (webpackConfig, { paths }) => {
      const { hasFoundAny, matches } = getLoaders(webpackConfig, loaderByName('babel-loader'));
      if (!hasFoundAny) throwError('failed to find babel-loader');

      console.log('removing babel-loader');
      const { hasRemovedAny, removedCount } = removeLoaders(
        webpackConfig,
        loaderByName('babel-loader')
      );
      if (!hasRemovedAny) throwError('no babel-loader to remove');
      if (removedCount !== 2) throwError('had expected to remove 2 babel loader instances');

      console.log('adding esbuild-loader');

      const tsLoader = {
        test: /\.(js|mjs|jsx|ts|tsx)$/,
        include: paths.appSrc,
        loader: require.resolve('esbuild-loader'),
        options: {
          loader: 'tsx',
          target: 'es2016'
        }
      };

      const { isAdded: tsLoaderIsAdded } = addAfterLoader(
        webpackConfig,
        loaderByName('url-loader'),
        tsLoader
      );
      if (!tsLoaderIsAdded) throwError('failed to add esbuild-loader');
      console.log('added esbuild-loader');

      console.log('adding non-application JS babel-loader back');
      const { isAdded: babelLoaderIsAdded } = addAfterLoader(
        webpackConfig,
        loaderByName('esbuild-loader'),
        matches[1].loader // babel-loader
      );
      if (!babelLoaderIsAdded) throwError('failed to add back babel-loader for non-application JS');
      console.log('added non-application JS babel-loader back');

      console.log('replacing TerserPlugin with ESBuildMinifyPlugin');
      webpackConfig.optimization.minimizer = [
        new ESBuildMinifyPlugin({
          target: 'es2016'
        })
      ];

      webpackConfig.optimization.nodeEnv = 'production';

      webpackConfig.module.rules.unshift({
        test: /\.svg$/,
        use: [
          {
            loader: '@svgr/webpack',
            options: {
              svgoConfig: {
                plugins: [
                  {
                    removeViewBox: false
                  }
                ]
              }
            }
          }
        ]
      });

      return webpackConfig;
    }
  }
};
