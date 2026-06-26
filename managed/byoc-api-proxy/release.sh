#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.

set -euo pipefail

print_help() {
  cat <<-EOT
Generates the BYOC API proxy package in destination.
Usage: ${0##*/} <options>
Options:
  -h, --help
    Show usage.
  -d, --destination
    Directory into which the package should be copied.
  -l, --local
    Package using the Gradle version as-is (including -SNAPSHOT) into build/releases/.
    Without -l, builds a release package (X.Y.Z), stripping -SNAPSHOT from gradle.properties.
EOT
}

readonly byoc_api_proxy_home="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

destination=""
local=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
      ;;
    -d|--destination)
      destination="$2"
      shift
      ;;
    -l|--local)
      local="true"
      ;;
  esac
  shift
done

if [[ "${local}" == "true" ]]; then
  if [[ -z "${destination}" || ! -d "${destination}" ]]; then
    destination="${byoc_api_proxy_home}/build/releases"
    mkdir -p "${destination}"
  fi
elif [[ ! -d "${destination}" ]]; then
  echo "No destination directory found ('${destination}')" >&2
  exit 1
fi

exec python3 "${byoc_api_proxy_home}/release.py" \
  --source_dir "${byoc_api_proxy_home}" \
  --destination "${destination}" \
  $([[ "${local}" == "true" ]] && echo --local)
