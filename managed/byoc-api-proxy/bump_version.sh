#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.

set -euo pipefail

readonly base_dir="$(cd "$(dirname "$0")" && pwd)"
readonly gradle_properties="${base_dir}/gradle.properties"
readonly semver_pattern='^([0-9]+)\.([0-9]+)\.([0-9]+)(-SNAPSHOT)?$'

show_help() {
  cat >&2 <<-'EOT'
Usage:
  ./bump_version.sh [--show]
  ./bump_version.sh <major|minor|patch>
  ./bump_version.sh --set X.Y.Z

Updates version in gradle.properties (always written as X.Y.Z-SNAPSHOT for dev).

Examples:
  ./bump_version.sh --show
  ./bump_version.sh patch
  ./bump_version.sh minor
  ./bump_version.sh --set 1.2.0
EOT
  exit 1
}

read_gradle_property() {
  local key=$1
  local value
  value="$(grep -E "^${key}=" "${gradle_properties}" | tail -1 | cut -d= -f2- | tr -d '[:space:]')"
  if [[ -z "${value}" ]]; then
    echo "Property ${key} not found in ${gradle_properties}" >&2
    exit 1
  fi
  echo "${value}"
}

write_gradle_property() {
  local key=$1
  local value=$2
  if grep -qE "^${key}=" "${gradle_properties}"; then
    sed -i.bak "s/^${key}=.*/${key}=${value}/" "${gradle_properties}"
    rm -f "${gradle_properties}.bak"
  else
    echo "${key}=${value}" >> "${gradle_properties}"
  fi
}

parse_version() {
  local version=$1
  if [[ ! "${version}" =~ ${semver_pattern} ]]; then
    echo "Invalid version '${version}': expected semver X.Y.Z or X.Y.Z-SNAPSHOT" >&2
    exit 1
  fi
  echo "${BASH_REMATCH[1]} ${BASH_REMATCH[2]} ${BASH_REMATCH[3]}"
}

read_version_parts() {
  parse_version "$(read_gradle_property version)"
}

write_snapshot_version() {
  local major=$1
  local minor=$2
  local patch=$3
  write_gradle_property version "${major}.${minor}.${patch}-SNAPSHOT"
}

bump_version() {
  local bump_type=$1
  local major minor patch
  read -r major minor patch <<< "$(read_version_parts)"

  case "${bump_type}" in
    major)
      major=$((major + 1))
      minor=0
      patch=0
      ;;
    minor)
      minor=$((minor + 1))
      patch=0
      ;;
    patch)
      patch=$((patch + 1))
      ;;
    *)
      echo "Unknown bump type '${bump_type}'" >&2
      show_help
      ;;
  esac

  echo "${major}.${minor}.${patch} ${major} ${minor} ${patch}"
}

action=""
set_version=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      show_help
      ;;
    --show)
      action="show"
      ;;
    --set)
      set_version=$2
      action="set"
      shift
      ;;
    major|minor|patch)
      if [[ -n "${action}" && "${action}" != "show" ]]; then
        echo "Specify only one version action." >&2
        show_help
      fi
      action="bump"
      bump_type=$1
      ;;
    *)
      echo "Unknown argument: $1" >&2
      show_help
      ;;
  esac
  shift
done

if [[ ! -f "${gradle_properties}" ]]; then
  echo "Missing ${gradle_properties}" >&2
  exit 1
fi

if [[ -z "${action}" ]]; then
  action="show"
fi

case "${action}" in
  show)
    echo "version=$(read_gradle_property version)"
    ;;
  set)
    parse_version "${set_version}-SNAPSHOT" >/dev/null
    read -r major minor patch <<< "$(parse_version "${set_version}-SNAPSHOT")"
    write_snapshot_version "${major}" "${minor}" "${patch}"
    echo "Updated ${gradle_properties}:"
    echo "  version=$(read_gradle_property version)"
    ;;
  bump)
    read -r new_version major minor patch <<< "$(bump_version "${bump_type}")"
    write_snapshot_version "${major}" "${minor}" "${patch}"
    echo "Updated ${gradle_properties}:"
    echo "  version=$(read_gradle_property version)"
    ;;
esac
