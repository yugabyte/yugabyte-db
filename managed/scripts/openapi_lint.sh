#!/usr/bin/env bash

OPENAPI_PATH="../src/main/resources/openapi.yaml"
OPENAPI_PUBLIC_PATH="../src/main/resources/openapi_public.yaml"

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
echo "npm bin at: $NPM_BIN"

for file in $OPENAPI_PATH $OPENAPI_PUBLIC_PATH
do
  echo "Running lint on openapi spec: ${file}"
  # using redocly version 1.0.2 since 1.1.0 onwards requires node version >=14.19.0
  # but jenkins runs node v12.22.12
  $NPM_BIN/npx @redocly/cli@1.0.2 lint \
    --format=stylish --config=../src/main/resources/openapi_lint.yaml ${file}
  if [ $? -ne 0 ];
  then
    echo "ERROR: openapi lint failed for {$file}"
    exit 1
  fi
done
