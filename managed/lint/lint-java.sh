#!/bin/bash
# Same google-java-format version that sbt javafmtAll uses (managed/project/plugins.sbt).
# Prints nothing for a formatted file; .arclint (and build-support/lint.py in the pr-lint
# GitHub check) turns any "error:" output into a lint error.
file="$1"
if ! formatted=$(java -jar managed/lint/google-java-format-1.17.0-all-deps.jar "$file" 2>&1); then
  printf 'error: google-java-format failed on %s:\n%s\n' "$file" "$formatted"
elif ! delta=$(diff "$file" - <<< "$formatted"); then
  printf 'error: %s is not formatted; run "sbt javafmtAll" in managed/.\n%s\n' "$file" "$delta"
fi
# arc lint treats a non-zero exit as the linter itself failing, so always return 0.
exit 0
