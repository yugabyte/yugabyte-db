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
  echo >&2 "${0##*/}: generate truststore for unit tests"
  echo >&2 "Argument: TEST_CERT_DIR"
  exit 1
fi

out_dir="$1"

if [ -f "$out_dir/client.truststore" ] ; then
  echo "$out_dir/client.truststore already exists, not regenerating"
  exit 0
fi

if ! [ -f "$out_dir/ca.crt" ] ; then
  echo "generate_test_certificates.sh not run yet, running"
  "${BASH_SOURCE[0]%/*}/generate_test_certificates.sh" "$out_dir"
fi

# keytool comes installed with java, but may not be in PATH, so we first get JAVA_HOME.
JAVA_HOME=$(java -XshowSettings:properties -version 2>&1 >/dev/null \
             | grep 'java.home = ' \
             | sed -e 's,.* = \(.*\),\1,')
keytool_path="$JAVA_HOME"/bin/keytool

generate_truststore() {
  local dir="$1"
  "$keytool_path" -importcert \
          -file "$dir/ca.crt" \
          -alias 'caroot' \
          -keystore "$dir/client.truststore" \
          -storepass 'password' \
          -storetype JKS \
          -noprompt
}

generate_truststore "$out_dir"
