#!/bin/bash
java -jar managed/lint/google-java-format-1.17.0-all-deps.jar $@ | diff $@ -
# As diff returns non-0 exit code on difference - arc lint will think that
# the lint command failed unless we return 0 explicitly
exit 0
