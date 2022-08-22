#/usr/local/env bash

set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
Options:
  --config <config_file>
    REQUIRED. Path of reference conf to get ybc version.
EOT
}

#default_parameters
config_file=""

while [[ $# -gt 0 ]]; do
  case ${1//_/-} in
    -c|--config)
      config_file=$2
      shift
    ;;
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      echo "Invalid option: $1"
      exit 1
  esac
  shift
done

ybc_version=$(
  grep ybc -A2 ${config_file} |  grep stable_version | awk -F '= ' '{print $2}' | tr -d \"
)

mkdir -p src/universal/ybc

aws s3 cp \
  s3://releases.yugabyte.com/ybc/${ybc_version}/ybc-${ybc_version}-linux-x86_64.tar.gz \
  src/universal/ybc
aws s3 cp \
  s3://releases.yugabyte.com/ybc/${ybc_version}/ybc-${ybc_version}-el8-aarch64.tar.gz \
  src/universal/ybc
