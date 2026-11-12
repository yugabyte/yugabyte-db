#!/bin/bash
# Same google-java-format version that sbt javafmtAll uses (managed/project/plugins.sbt).
# Prints nothing for a formatted file; .arclint (and build-support/lint.py in the pr-lint
# GitHub check) turns any "error:" output into a lint error.
file="$1"
# The JVM can write to stderr on success (e.g. "Picked up JAVA_TOOL_OPTIONS"), so keep stderr
# out of the formatted output and show it only when formatting fails.
errfile=$(mktemp)
trap 'rm -f "$errfile"' EXIT
if ! formatted=$(java -jar managed/lint/google-java-format-1.17.0-all-deps.jar "$file" 2>"$errfile"); then
  printf 'error: google-java-format failed on %s:\n%s\n' "$file" "$(cat "$errfile")"
elif ! delta=$(diff "$file" - <<< "$formatted"); then
  printf 'error: %s is not formatted; run "sbt javafmtAll" in managed/.\n%s\n' "$file" "$delta"
fi
# arc lint treats a non-zero exit as the linter itself failing, so always return 0.
exit 0
