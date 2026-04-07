#!/bin/bash

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
echo "npm bin at: $NPM_BIN"

openapi_format_ver="1.17.1"
# Check if openapi-format is installed locally.
if command -v openapi-format > /dev/null
then
  local_version=$(openapi-format --version)
  if [ "$local_version" != "$openapi_format_ver" ];
  then
    echo "Warning: Local openapi-format version ($local_version) is not the required \
        ($openapi_format_ver). Installing..."
    set -e
    npm install -g openapi-format@$openapi_format_ver
    set +e
  else
    echo "Using local openapi-format version: $local_version"
  fi
else
  echo "Installing openapi-format version via $NPM_BIN/npx: $openapi_format_ver"
  set -e
  $NPM_BIN/npx openapi-format@$openapi_format_ver --version
  set +e
fi
