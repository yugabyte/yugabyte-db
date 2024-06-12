#!/usr/bin/env bash

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
echo "npm bin at: $NPM_BIN"

pushd ../src/main/resources/openapi

file=$1
echo "Formatting openapi file ${file}"

yaml_files=$(find . -name "*.yaml" -print)
mkdir -p tmp
tmp_yml="tmp/tmp.yml"
rm -f $tmp_yml

openapi_format_ver="1.17.1"
openapi_format_cmd=""
# Check if openapi-format is installed locally.
if command -v openapi-format > /dev/null
then
  local_version=$(openapi-format --version)
  if [ "$local_version" != "$openapi_format_ver" ];
  then
    echo "Warning: Local openapi-format version ($local_version) is not the required \
        ($openapi_format_ver). Installing..."
    npm install -g openapi-format@$openapi_format_ver
  else
    echo "Using local openapi-format version: $local_version"
  fi
  openapi_format_cmd="openapi-format"
else
  openapi_format_cmd="$NPM_BIN/npx openapi-format@$openapi_format_ver"
fi

# Format all the required YML files.
# TODO: Fix processing of this file.
if [ "${file}" == "./openapi/openapi.yaml" ];
then
  echo "${file}"
  exit 0
fi

$openapi_format_cmd \
  --sortFile ../openapi_sort_format.json \
  --sortComponentsFile ../openapi_sort_components.json \
  --output ${tmp_yml} ${file} # > /dev/null 2>&1
if [ $? -ne 0 ];
then
  echo "Error: Failed to format YML file: $file"
  exit 1
fi

cmp -s ${tmp_yml} $file
if [ $? -ne 0 ];
then
  cp ${tmp_yml} $file
fi

rm -f ${tmp_yml}

popd
