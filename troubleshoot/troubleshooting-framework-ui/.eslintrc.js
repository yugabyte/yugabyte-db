module.exports = {
  // Global ESLint Settings
  // =================================
  root: true,
  env: {
    browser: true,
    es2020: true
  },
  ignorePatterns: ['build/*'],
  settings: {
    react: {
      version: 'detect'
    },
    'import/resolver': {
      node: {
        extensions: ['.js', '.jsx', '.ts', '.tsx'],
        paths: ['./src']
      }
    }
  },

  plugins: ['@typescript-eslint'],
  // ===========================================
  // Set up ESLint for .js / .jsx files
  // ===========================================
  // .js / .jsx uses babel-eslint
  parser: '@typescript-eslint/parser',
  parserOptions: {
    ecmaVersion: 2020,
    sourceType: 'module',
    tsconfigRootDir: __dirname,
    ecmaFeatures: {
      jsx: true
    },
    project: ['./tsconfig.json']
  },

  // Plugins
  // =================================
  // plugins: ["import"],

  // Extend Other Configs
  // =================================
  extends: [
    'eslint:recommended',
    'plugin:import/errors',
    // Disable rules that conflict with Prettier
    // Prettier must be last to override other configs
    'prettier'
  ],
  rules: {
    'react/prop-types': 0,
    'react-hooks/exhaustive-deps': 0,
    'react/react-in-jsx-scope': 0,
    'react/jsx-key': 0,
    'import/named': 0,
    'import/namespace': 0,
    'import/export': 0,
    semi: 2,
    'no-var': 2,
    'prefer-const': 2,
    'no-undef': 0,
    'one-var': [2, 'never'],
    'import/no-unresolved': [0, { caseSensitive: false }],
    'no-control-regex': 0,
    // TODO: Handle null check in a separate diff
    eqeqeq: 2,
    'no-useless-return': 2,
    'no-eval': 2,
    'no-dupe-else-if': 2,
    'no-lonely-if': 2,
    'eol-last': 2,
    'no-console': [2, { allow: ['warn', 'error'] }],
    'react/no-array-index-key': 0,
    'react/no-this-in-sfc': 0,
    'no-extra-boolean-cast': 0,
    'no-unused-vars': 0,
    '@typescript-eslint/no-unused-vars': [
      0,
      { vars: 'all', args: 'none', ignoreRestSiblings: true }
    ],
    '@typescript-eslint/ban-types': 0,
    '@typescript-eslint/no-empty-function': 0,
    '@typescript-eslint/no-var-requires': 0,
    '@typescript-eslint/ban-ts-comment': [
      0,
      {
        'ts-ignore': 'allow-with-description',
        minimumDescriptionLength: 4
      }
    ],
    'no-unused-expressions': 0,
    '@typescript-eslint/no-unused-expressions': 0,
    '@typescript-eslint/no-this-alias': 0,
    '@typescript-eslint/no-explicit-any': 0,
    '@typescript-eslint/explicit-module-boundary-types': 0,
    '@typescript-eslint/no-non-null-assertion': 0,
    '@typescript-eslint/no-unsafe-member-access': 0,
    // TODO: Fix no-shadow in a separate diff
    '@typescript-eslint/no-shadow': 0,
    '@typescript-eslint/prefer-includes': 2,
    // TODO: Fix prefer-nullish-coalescing in a separate diff as it needs more investigation
    '@typescript-eslint/prefer-nullish-coalescing': 2,
    '@typescript-eslint/prefer-optional-chain': 0,
    '@typescript-eslint/no-non-null-asserted-optional-chain': 2
  }
};
