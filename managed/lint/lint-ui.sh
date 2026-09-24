#!/bin/bash
# eslint for managed/ui sources, invoked by the "eslint-ui" linter in .arclint.
#
# arc runs this from the repository root and appends the paths to lint. .eslintrc.js pins
# parserOptions.tsconfigRootDir to its own directory, so the TypeScript project resolves no
# matter where this is run from.
ESLINT=managed/ui/node_modules/eslint/bin/eslint.js
if [[ ! -x "${ESLINT}" ]]; then
  # Don't fail the whole lint run for someone who has not installed the UI dependencies.
  echo "lint-ui.sh: ${ESLINT} not found - run 'npm ci' in managed/ui to lint UI changes" >&2
  exit 0
fi
"${ESLINT}" -c managed/ui/.eslintrc.js -- "$@"
# eslint exits non-0 whenever it reports anything, and arc lint reads a non-0 exit as the lint
# command itself having failed rather than as lint output. Same reason as lint-java.sh.
exit 0
