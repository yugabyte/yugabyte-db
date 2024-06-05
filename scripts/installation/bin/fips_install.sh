#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations under
# the License.

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo >&2 "${0##*/}: run FIPS self-tests and generate config files"
  echo >&2 "Argument: CONFIG_DIR"
  exit 1
fi

out_dir=$(realpath "$1")

bin_dir="${YB_BIN_DIR:=${BASH_SOURCE%/*}/../bin}"
provider_dir="${YB_OSSL_PROVIDER_DIR:=${BASH_SOURCE%/*}/../lib/ossl-modules}"

openssl_bin="$bin_dir/openssl"

if [ -f "$provider_dir/fips.so" ]; then
  fips_provider="$provider_dir/fips.so"
else
  fips_provider="$provider_dir/fips.dylib"
fi

echo "OpenSSL binary: $openssl_bin"
echo "FIPS module: $fips_provider"

mkdir -p "$out_dir"

"$openssl_bin" fipsinstall \
    -out "$out_dir/fipsmodule.cnf" \
    -module "$fips_provider"

cat > "$out_dir"/openssl-fips.cnf <<-EOT
config_diagnostics = 1
openssl_conf = openssl_init

.include $out_dir/fipsmodule.cnf

[openssl_init]
providers = provider_sect
alg_section = algorithm_sect

[provider_sect]
fips = fips_sect
base = base_sect

[base_sect]
activate = 1

[algorithm_sect]
default_properties = fips=yes
EOT
