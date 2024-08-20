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
dest_dir="src/universal/ybc"
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
    -d|--dest)
      dest_dir="$2"
      shift
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
  grep ybc -A2 ${config_file} |  awk -F '= ' '/stable_version/ {print $2}' | tr -d \"
)

# Use http, so aws s3 command/credential setup is not required.
s3_url="https://s3.us-west-2.amazonaws.com/releases.yugabyte.com/ybc"
x86_file="ybc-${ybc_version}-linux-x86_64.tar.gz"
aarch_file="ybc-${ybc_version}-el8-aarch64.tar.gz"

mkdir -p "$dest_dir"

if [ "$ignore_if_exists" = "false" ] ||
   ! [ -f "${dest_dir}/${x86_file}" ] ||
   ! [ -f "${dest_dir}/${aarch_file}" ]; then
  rm -f "${dest_dir}/${x86_file}" "${dest_dir}/${aarch_file}"
  curl -s -o "${dest_dir}/${x86_file}" "${s3_url}/${ybc_version}/${x86_file}"
  curl -s -o "${dest_dir}/${aarch_file}" "${s3_url}/${ybc_version}/${aarch_file}"
fi

if [ "$should_copy_ybc" = "true" ]; then
  mkdir -p /opt/yugabyte/ybc/release
  mkdir -p /opt/yugabyte/ybc/releases
  cp -a "$dest_dir"/*  /opt/yugabyte/ybc/release/
fi
