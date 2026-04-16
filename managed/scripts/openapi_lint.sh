#!/usr/bin/env bash

OPENAPI_PATH="../src/main/resources/openapi.yaml"
OPENAPI_PUBLIC_PATH="../src/main/resources/openapi_public.yaml"

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
echo "npm bin at: $NPM_BIN"

for file in $OPENAPI_PATH $OPENAPI_PUBLIC_PATH
do
  echo "Running lint on openapi spec: ${file}"
  # using redocly version 1.0.2 since 1.1.0 onwards requires node version >=14.19.0
  # but jenkins runs node v12.22.12.
  # Faced an issue with a newly released openapi-sampler@1.5.0 on node v12.22.12.
  # So picking the older version openapi-sampler@1.4.0 explicitly.
  $NPM_BIN/npx -p openapi-sampler@1.4.0 -p @redocly/cli@1.0.2 redocly lint \
    --format=stylish --config=../src/main/resources/openapi_lint.yaml ${file}
  if [ $? -ne 0 ];
  then
    echo "ERROR: openapi lint failed for {$file}"
    exit 1
  fi
done
