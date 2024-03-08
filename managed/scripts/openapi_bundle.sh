#!/usr/bin/env bash

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
echo "npm bin at: $NPM_BIN"
pushd ../src/main/resources/openapi
mkdir -p tmp

echo "Processing paths component in openapi ..."
pushd paths
tmp_out_file="../tmp/paths_index.yaml"
rm -f $tmp_out_file
GLOBIGNORE=_index.yaml && cat *.yaml > $tmp_out_file
cmp -s $tmp_out_file _index.yaml
if [ $? -ne 0 ]; then
  cp $tmp_out_file _index.yaml
fi
popd

echo "Running bundle on openapi spec ..."
tmp_out_file=tmp/openapi_bundle.yaml
rm -f $tmp_out_file
# using redocly version 1.0.2 since 1.1.0 onwards requires node version >=14.19.0
# but jenkins runs node v12.22.12
$NPM_BIN/npx @redocly/cli@1.0.2 bundle openapi_split.yaml --output $tmp_out_file
cmp -s $tmp_out_file ../openapi.yaml
if [ $? -ne 0 ]; then
  cp $tmp_out_file ../openapi.yaml
fi

rm -rf tmp
popd

# Call python script which takes the above generated openapi.yaml file
# and extracts specific APIs for Public and processes other x-yba-api extensions.
../devops/bin/run_in_virtualenv.sh pip install PyYAML==6.0.1
../devops/bin/run_in_virtualenv.sh python3 ./openapi_process_vendor_ext.py
