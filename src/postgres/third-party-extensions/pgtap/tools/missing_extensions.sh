#!/bin/sh

MISSING_EXTENSIONS=$1
EXTENSION_TEST_FILES=$2

# Doesn't seem worth pulling all of util.sh in for just this, but if you need
# anything else please just pull it in!
stderr() {
    echo "$@" >&2
}

if [ -n "$MISSING_EXTENSIONS" ]; then
    if [ -n "$ALLOW_MISSING_EXTENSIONS" ]; then
        stderr
        stderr '***************************'
        stderr "WARNING: Some mandatory extensions ($MISSING_EXTENSIONS) are not installed, ignoring tests: $EXTENSION_TEST_FILES"
        stderr '***************************'
        stderr
    else
        stderr
        stderr '***************************'
        stderr "ERROR: Missing extensions required for testing: $MISSING_EXTENSIONS"
        stderr
        stderr "You may over-ride by setting \$ALLOW_MISSING_EXTENSIONS to a value."
        stderr '***************************'
        stderr
        exit 1
    fi
fi
