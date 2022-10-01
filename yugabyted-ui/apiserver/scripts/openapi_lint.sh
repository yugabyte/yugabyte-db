#!/usr/bin/env bash

echo "Running lint on openapi spec"
export NPM_BIN=`npm bin -g 2>/dev/null`
$NPM_BIN/openapi lint --format=stylish --config=../conf/openapi_lint.conf --entrypoints=../conf/openapi.yml
