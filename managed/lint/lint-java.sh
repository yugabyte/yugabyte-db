#!/bin/bash
# Same google-java-format version that sbt javafmtAll uses (managed/project/plugins.sbt).
# Prints nothing for a formatted file; .arclint (and build-support/lint.py in the pr-lint
# GitHub check) turns any "error:" output into a lint error.
file="$1"
# google-java-format parses with the javac of the JDK that runs it, so a JDK older than 17
# reports managed/'s Java 17 syntax (switch arrows, records) as a parse error. The pr-lint
# image's default java is older, so also try the JDKs under /usr/lib/jvm.
java=""
for j in "${JAVA_HOME:+$JAVA_HOME/bin/java}" java /usr/lib/jvm/*/bin/java; do
  if [[ -z "$j" ]] || ! command -v "$j" > /dev/null; then
    continue
  fi
  version=$("$j" -XshowSettings:properties -version 2>&1 |
    sed -n 's/^ *java\.specification\.version = //p')
  if [[ "${version%%.*}" =~ ^[0-9]+$ ]] && (( ${version%%.*} >= 17 )); then
    java="$j"
    break
  fi
done
if [[ -z "$java" ]]; then
  printf 'error: no JDK 17 or later to run google-java-format on %s; set JAVA_HOME.\n' "$file"
  exit 0
fi
# The JVM can write to stderr on success (e.g. "Picked up JAVA_TOOL_OPTIONS"), so keep stderr
# out of the formatted output and show it only when formatting fails.
errfile=$(mktemp)
trap 'rm -f "$errfile"' EXIT
if ! formatted=$("$java" -jar managed/lint/google-java-format-1.17.0-all-deps.jar "$file" \
    2>"$errfile"); then
  printf 'error: google-java-format failed on %s:\n%s\n' "$file" "$(cat "$errfile")"
elif ! delta=$(diff "$file" - <<< "$formatted"); then
  printf 'error: %s is not formatted; run "sbt javafmtAll" in managed/.\n%s\n' "$file" "$delta"
fi
# arc lint treats a non-zero exit as the linter itself failing, so always return 0.
exit 0
