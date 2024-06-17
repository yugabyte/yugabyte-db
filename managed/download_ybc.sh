#/usr/local/env bash

set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
Options:
  --config <config_file>
    REQUIRED. Path of reference conf to get ybc version.
  --should_copy
    OPTIONAL. To copy the ybc packages to local ybc release directory
EOT
}

#default_parameters
config_file=""
should_copy_ybc="false"
ignore_if_exists="false"

POSITIONAL_ARGS=()

while [[ $# -gt 0 ]]; do
  case $1 in
    -c|--config)
      config_file="$2"
      shift
      shift
      ;;
    -s|--should_copy)
      should_copy_ybc="true"
      shift
      ;;
    -i|--ignore-if-exists)
      ignore_if_exists="true"
      shift
      ;;
    -h|--help)
      print_help
      exit 0
      ;;
    -*|--*)
      echo "Invalid option: $1"
      exit 1
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done

ybc_version=$(
  grep ybc -A2 ${config_file} |  grep stable_version | awk -F '= ' '{print $2}' | tr -d \"
)

mkdir -p src/universal/ybc

if [ "$ignore_if_exists" = "false" ] ||
   ! [ -f src/universal/ybc/ybc-${ybc_version}-linux-x86_64.tar.gz ] ||
   ! [ -f src/universal/ybc/ybc-${ybc_version}-el8-aarch64.tar.gz ]; then
  aws s3 cp \
    s3://releases.yugabyte.com/ybc/${ybc_version}/ybc-${ybc_version}-linux-x86_64.tar.gz \
    src/universal/ybc
  aws s3 cp \
    s3://releases.yugabyte.com/ybc/${ybc_version}/ybc-${ybc_version}-el8-aarch64.tar.gz \
    src/universal/ybc
fi

if [ "$should_copy_ybc" = "true" ]; then
  mkdir -p /opt/yugabyte/ybc/release
  mkdir -p /opt/yugabyte/ybc/releases
  cp -a src/universal/ybc/*  /opt/yugabyte/ybc/release/
fi
