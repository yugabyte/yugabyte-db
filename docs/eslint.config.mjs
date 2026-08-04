import { defineConfig } from "eslint/config";
import _import from "eslint-plugin-import";
import stylistic from "@stylistic/eslint-plugin";
import path from "node:path";
import { fileURLToPath } from "node:url";

export default defineConfig([
  {
    languageOptions: {
      parserOptions: {
        ecmaVersion: "latest",
        sourceType: "module",
        ecmaFeatures: {
          jsx: true,
        },
      },
    },

    plugins: {
      import: _import,
      stylistic,
    },

    settings: {
      "import/resolver": {
        node: {
          paths: ["./src"],
        },
      },
    },

    rules: {
      "no-console": "warn",
      "no-debugger": "warn",
      "no-unused-vars": "warn",
    },
  },
]);
