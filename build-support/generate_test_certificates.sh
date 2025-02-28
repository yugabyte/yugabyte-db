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
  echo >&2 "${0##*/}: generate certificates for unit tests"
  echo >&2 "Argument: TEST_CERT_DIR"
  exit 1
fi

out_dir="$1"
openssl_bin="$YB_THIRDPARTY_DIR/installed/common/bin/openssl"

generate_ca() {
  local dir="$1"
  local common_name="$2"

  touch "$dir/index.txt"
  echo "01" > "$dir/serial.txt"

  cat > "$dir/ca.self.conf" <<-EOT
[ ca ]
default_ca = yugabyte_ca

[ yugabyte_ca ]
default_startdate = 00000101000000Z
default_enddate = 99991231235959Z

serial = $dir/serial.txt
database = $dir/index.txt
default_md = sha256
policy = yugabyte_policy

unique_subject = no
copy_extensions = copy

[ yugabyte_policy ]
commonName = supplied

[ req ]
prompt = no
distinguished_name = yugabyte_distinguished_name

[ yugabyte_distinguished_name ]
commonName = $common_name
EOT

  cat > "$dir/ca.conf" <<-EOT
[ ca ]
default_ca = yugabyte_ca

[ yugabyte_ca ]
default_startdate = 00000101000000Z
default_enddate = 99991231235959Z

serial = $dir/serial.txt
database = $dir/index.txt
default_md = sha256
policy = yugabyte_policy

unique_subject = no
copy_extensions = copy
x509_extensions = my_extensions

[ yugabyte_policy ]
organizationName = optional
commonName = supplied
userId = optional

[ req ]
prompt = no
distinguished_name = yugabyte_distinguished_name

[ yugabyte_distinguished_name ]
commonName = $common_name

[ my_extensions ]
basicConstraints = CA:false
extendedKeyUsage = clientAuth,serverAuth

[ v3_intermediate_ca ]
basicConstraints = CA:true
keyUsage = digitalSignature, keyCertSign, cRLSign
EOT

  "$openssl_bin" genrsa -out "$dir/ca.key" 2048
  "$openssl_bin" req -new \
                     -config "$dir/ca.self.conf" \
                     -key "$dir/ca.key" \
                     -out "$dir/ca.csr"
  "$openssl_bin" ca -config "$dir/ca.self.conf" \
                    -keyfile "$dir/ca.key" \
                    -selfsign \
                    -in "$dir/ca.csr" \
                    -out "$dir/ca.crt" \
                    -outdir "$dir" \
                    -batch
}

generate_intermediate_ca() {
  local dir="$1"
  local common_name="$2"

  touch "$dir/interm.index.txt"
  echo "01" > "$dir/interm.serial.txt"


  cat > "$dir/intermediate_ca.self.conf" <<-EOT
[ ca ]
default_ca = yugabyte_ca

[ yugabyte_ca ]
default_startdate = 00000101000000Z
default_enddate = 99991231235959Z

serial = $dir/interm.serial.txt
database = $dir/interm.index.txt
default_md = sha256
policy = yugabyte_policy

unique_subject = no
copy_extensions = copy

[ yugabyte_policy ]
organizationName = optional
commonName = supplied
userId = optional

[ req ]
prompt = no
distinguished_name = intermediate_distinguished_name

[ intermediate_distinguished_name ]
commonName = $common_name

EOT

  cat > "$dir/intermediate_ca.conf" <<-EOT
[ ca ]
default_ca = yugabyte_ca

[ yugabyte_ca ]
default_startdate = 00000101000000Z
default_enddate = 99991231235959Z

serial = $dir/interm.serial.txt
database = $dir/interm.index.txt
default_md = sha256
policy = yugabyte_policy

unique_subject = no
copy_extensions = copy
x509_extensions = my_extensions

[ yugabyte_policy ]
organizationName = optional
commonName = supplied
userId = optional

[ req ]
prompt = no
distinguished_name = intermediate_distinguished_name

[ intermediate_distinguished_name ]
commonName = $common_name

[ my_extensions ]
basicConstraints = CA:false
extendedKeyUsage = clientAuth,serverAuth
EOT

  "$openssl_bin" genrsa -out "$dir/intermediate_ca.key" 2048
  "$openssl_bin" req -new \
                     -config "$dir/intermediate_ca.self.conf" \
                     -key "$dir/intermediate_ca.key" \
                     -out "$dir/intermediate_ca.csr"
  "$openssl_bin" ca -config "$dir/ca.conf" \
                    -keyfile "$dir/ca.key" \
                    -cert "$dir/ca.crt" \
                    -extensions v3_intermediate_ca \
                    -in "$dir/intermediate_ca.csr" \
                    -out "$dir/intermediate_ca.crt" \
                    -outdir "$dir" \
                    -batch
}


generate_cert() {
  local dir="$1"
  local prefix="$2"
  local use_intermediate_ca="${3:-false}"


  "$openssl_bin" genrsa -out "$dir/$prefix.key" 2048
  "$openssl_bin" req -new \
                     -config "$dir/$prefix.conf" \
                     -key "$dir/$prefix.key" \
                     -out "$dir/$prefix.csr"
  if [[ "$use_intermediate_ca" == "true" ]]; then
    "$openssl_bin" ca -config "$dir/intermediate_ca.conf" \
                      -keyfile "$dir/intermediate_ca.key" \
                      -cert "$dir/intermediate_ca.crt" \
                      -policy yugabyte_policy \
                      -in "$dir/$prefix.csr" \
                      -out "$dir/$prefix.crt" \
                      -outdir "$dir" \
                      -batch
  else
    "$openssl_bin" ca -config "$dir/ca.conf" \
                      -keyfile "$dir/ca.key" \
                      -cert "$dir/ca.crt" \
                      -policy yugabyte_policy \
                      -in "$dir/$prefix.csr" \
                      -out "$dir/$prefix.crt" \
                      -outdir "$dir" \
                      -batch
    fi
}

generate_node_cert() {
  local dir="$1"
  local ip_end_octet="$2"
  local use_intermediate_ca="${3:-false}"

  local ip="127.0.0.$ip_end_octet"
  local prefix="node.$ip"

  cat > "$dir/$prefix.conf" <<-EOT
[ req ]
prompt=no
distinguished_name = node_distinguished_name
req_extensions = req_ext

[ node_distinguished_name ]
commonName = node.$ip_end_octet

[ req_ext ]
subjectAltName = IP:$ip, DNS:127.*.*.$((ip_end_octet + 1)).ip.yugabyte
EOT

  generate_cert "$dir" "$prefix" "$use_intermediate_ca"
}

generate_node_named_cert() {
  local dir="$1"
  local ip_end_octet="$2"
  local use_intermediate_ca="${3:-false}"
  local ip="127.0.0.$ip_end_octet"
  local prefix="node.$ip"

  cat > "$dir/$prefix.conf" <<-EOT
[ req ]
prompt=no
distinguished_name = node_distinguished_name
req_extensions = req_ext

[ node_distinguished_name ]
commonName = yugabyte-test
userId = uid.yb

[ req_ext ]
subjectAltName = IP:$ip, DNS:127.0.*.$((ip_end_octet + 1)).ip.yugabyte, \
                 otherName:1.2.3.4;UTF8:other_name.yb
EOT

  generate_cert "$dir" "$prefix" "$use_intermediate_ca"
}

generate_ysql_cert() {
  local dir="$1"
  local prefix="$2"
  local use_intermediate_ca="${3:-false}"

  cat > "$dir/$prefix.conf" <<-EOT
[ req ]
prompt=no
distinguished_name=yugabyte_distinguished_name

[ yugabyte_distinguished_name ]
organizationName = YugaByte
commonName = yugabyte
EOT

  generate_cert "$dir" "$prefix" "$use_intermediate_ca"
  "$openssl_bin" pkcs8 -topk8 \
                       -inform PEM \
                       -outform DER \
                       -in "$dir/$prefix.key" \
                       -out "$dir/$prefix.key.der" \
                       -nocrypt
}

generate_test_certificates() {
  local out_dir="$1"

  set -euo pipefail

  temp_dir="$(mktemp -d)"
  mkdir -p "$temp_dir/CA1" "$temp_dir/CA2" "$temp_dir/named" "$out_dir" \
           "$temp_dir/intermediate1"  "$out_dir/intermediate1" \
           "$temp_dir/intermediate2"  "$out_dir/intermediate2"

  generate_ca "$temp_dir/CA1" 'YugabyteDB CA 1'
  for i in $(seq 2 2 254); do
    generate_node_cert "$temp_dir/CA1" "$i"
  done
  generate_ysql_cert "$temp_dir/CA1" ysql

  cp "$temp_dir/CA1/ca.crt" \
     "$temp_dir/CA1/node."*".crt" \
     "$temp_dir/CA1/node."*".key" \
     "$temp_dir/CA1/ysql.crt" \
     "$temp_dir/CA1/ysql.key" \
     "$temp_dir/CA1/ysql.key.der" \
     "$out_dir/"

  generate_ca "$temp_dir/CA2" 'YugabyteDB CA 2'
  for i in $(seq 2 2 254); do
    generate_node_cert "$temp_dir/CA2" "$i"
  done

  cat "$temp_dir/CA2/ca.crt" "$temp_dir/CA1/ca.crt" > "$temp_dir/combinedCA.crt"

  mkdir -p "$out_dir/CA2"
  cp "$temp_dir/CA2/ca.crt" \
     "$temp_dir/CA2/node."*".crt" \
     "$temp_dir/CA2/node."*".key" \
     "$temp_dir/combinedCA.crt" \
     "$out_dir/CA2"

  generate_ca "$temp_dir/named" 'YugabyteDB CA'
  for i in 2 4 6 52 ; do
    generate_node_named_cert "$temp_dir/named" $i
  done

  mkdir -p "$out_dir/named"
  cp "$temp_dir/named/ca.crt" \
     "$temp_dir/named/node."*".crt" \
     "$temp_dir/named/node."*".key" \
     "$out_dir/named"

  # Generate root + intermediate CA
  generate_ca "$temp_dir/intermediate1" 'YugabyteDB CA'
  generate_intermediate_ca "$temp_dir/intermediate1" 'Intermediate YugabyteDB CA'
  for i in 2 4 6 52 100 ; do
    generate_node_cert "$temp_dir/intermediate1" "$i" "true"
  done
  generate_ysql_cert "$temp_dir/intermediate1" ysql "true"

  cp "$temp_dir/intermediate1/node."*".crt" \
     "$temp_dir/intermediate1/node."*".key" \
     "$temp_dir/intermediate1/ysql.crt" \
     "$temp_dir/intermediate1/ysql.key" \
     "$temp_dir/intermediate1/ysql.key.der" \
     "$out_dir/intermediate1/"

  # intermediate1
  # CA crt = intermediate + root CA
  # node crt is just the server cert
  cat "$temp_dir/intermediate1/intermediate_ca.crt" \
      "$temp_dir/intermediate1/ca.crt" \
      > "$out_dir/intermediate1/ca.crt"

  # intermediate2
  # CA crt = just root CA
  # node crt = server cert + intermediate CA
  cp "$temp_dir/intermediate1/ca.crt" \
     "$temp_dir/intermediate1/node."*".key" \
     "$temp_dir/intermediate1/ysql.key" \
     "$temp_dir/intermediate1/ysql.key.der" \
     "$out_dir/intermediate2/"

  for i in 2 4 6 52 100 ; do
    cat "$temp_dir/intermediate1/node.127.0.0.$i.crt" \
        "$temp_dir/intermediate1/intermediate_ca.crt" \
        > "$out_dir/intermediate2/node.127.0.0.$i.crt"
    cat   "$temp_dir/intermediate1/ysql.crt" \
        "$temp_dir/intermediate1/intermediate_ca.crt" \
        > "$out_dir/intermediate2/ysql.crt"
  done

  rm -rf "$temp_dir"
}

report_error() {
  local error=$?
  local output="$1"
  echo >&2 "Failed to generate test certificates. Command output:"
  echo >&2
  echo >&2 "$output"
  exit $error
}
trap 'report_error "$out"' ERR
out=$(generate_test_certificates "$out_dir" 2>&1)
trap - ERR
