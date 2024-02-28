#!/usr/bin/env bash

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
echo "npm bin at: $NPM_BIN"

pushd ../src/main/resources/openapi

echo "Formatting openapi files"

yaml_files=$(find . -name "*.yaml" -print)
mkdir -p tmp
tmp_yml="tmp/tmp.yml"
rm -f $tmp_yml

# Format all the required YML files.
for file in $yaml_files
do

  # TODO: Fix processing of this file.
  if [ "${file}" == "./openapi/openapi.yaml" ];
  then
    continue
  fi

  $NPM_BIN/npx openapi-format \
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
done

rm -f ${tmp_yml}

popd
