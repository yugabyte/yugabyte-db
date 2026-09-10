#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.

set -euo pipefail

readonly package_name="byoc_api_proxy"

readonly base_dir="$(cd "$(dirname "$0")" && pwd)"
readonly project_dir="${base_dir}"
readonly build_output_dir="${project_dir}/build"
readonly gradle_properties="${project_dir}/gradle.properties"
readonly semver_pattern='^[0-9]+\.[0-9]+\.[0-9]+$'
readonly package_version_pattern='^[0-9]+\.[0-9]+\.[0-9]+(-SNAPSHOT)?$'

show_help() {
  cat >&2 <<-'EOT'

Usage:
  ./build.sh <clean|build|test|package [X.Y.Z]>

Version:
  Uses Gradle version from gradle.properties as-is (e.g. X.Y.Z-SNAPSHOT for dev).
  Pass an explicit X.Y.Z to package a release build.

Examples:
  ./build.sh build
  ./build.sh test
  ./build.sh package
  ./build.sh package 1.2.0
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

read_gradle_version() {
  read_gradle_property version
}

validate_release_version() {
  local version=$1
  if [[ ! "${version}" =~ ${semver_pattern} ]]; then
    echo "Invalid release version '${version}': expected semver X.Y.Z" >&2
    exit 1
  fi
}

validate_package_version() {
  local version=$1
  if [[ ! "${version}" =~ ${package_version_pattern} ]]; then
    echo "Invalid package version '${version}': expected semver X.Y.Z or X.Y.Z-SNAPSHOT" >&2
    exit 1
  fi
}

read_package_version() {
  read_gradle_version
}

package_build_type() {
  local version=$1
  if [[ "${version}" == *-SNAPSHOT ]]; then
    echo "snapshot"
  else
    echo "release"
  fi
}

write_package_version_files() {
  local version_dir=$1
  local version=$2
  local build_type
  build_type="$(package_build_type "${version}")"

  echo "${version}" > "${version_dir}/version.txt"
  cat > "${version_dir}/version_metadata.json" <<EOF
{
  "schema": "v1",
  "build_type": "${build_type}",
  "version_number": "${version}"
}
EOF
}

find_boot_jar() {
  local jar
  jar="$(find "${project_dir}/build/libs" -maxdepth 1 -name '*.jar' ! -name '*-plain.jar' -print -quit)"
  if [[ -z "${jar}" ]]; then
    echo "Boot jar not found under ${project_dir}/build/libs" >&2
    echo "Run './build.sh build' first." >&2
    exit 1
  fi
  echo "${jar}"
}

clean_build() {
  rm -rf "${build_output_dir}"
  (cd "${project_dir}" && ./gradlew clean)
}

build_jar() {
  (cd "${project_dir}" && ./gradlew bootJar)
}

build_release_jar() {
  local release_version=$1
  (cd "${project_dir}" && ./gradlew bootJar -Pversion="${release_version}")
}

run_tests() {
  (cd "${project_dir}" && ./gradlew test)
}

package_release() {
  local version=$1
  local staging_dir_name="${package_name}-${version}"
  local version_dir="${build_output_dir}/${staging_dir_name}/${version}"
  local bin_dir="${version_dir}/bin"
  local systemd_dir="${version_dir}/systemd"

  echo "Packaging ${staging_dir_name}"
  find_boot_jar >/dev/null

  rm -rf "${build_output_dir}/${staging_dir_name}"
  mkdir -p "${bin_dir}" "${systemd_dir}"

  install -m 0644 "$(find_boot_jar)" "${bin_dir}/byoc-api-proxy.jar"
  cp -f "${project_dir}/systemd/byoc-api-proxy.service" "${systemd_dir}/"
  cp -f "${project_dir}/systemd/byoc-api-proxy.env.example" "${systemd_dir}/"
  cp -f "${project_dir}/systemd/application.yaml.example" "${systemd_dir}/"
  cp -f "${project_dir}/systemd/install.sh" "${systemd_dir}/"
  write_package_version_files "${version_dir}" "${version}"
  chmod 755 "${systemd_dir}/install.sh"

  (
    cd "${build_output_dir}"
    tar -zcf "${staging_dir_name}.tar.gz" -C "${staging_dir_name}" "${version}"
  )
}

clean=false
build=false
test=false
package=false
version=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    help|-h|--help)
      show_help
      ;;
    clean)
      clean=true
      ;;
    build)
      build=true
      ;;
    test)
      test=true
      ;;
    package)
      package=true
      shift
      if [[ $# -gt 0 ]]; then
        version=$1
      fi
      ;;
    *)
      echo "Unknown option $1" >&2
      exit 1
      ;;
  esac
  if [[ $# -gt 0 ]]; then
    shift
  fi
done

help_needed=true

if [[ "${clean}" == "true" ]]; then
  help_needed=false
  echo "Cleaning..."
  clean_build
fi

if [[ "${build}" == "true" ]]; then
  help_needed=false
  echo "Building..."
  build_jar
fi

if [[ "${test}" == "true" ]]; then
  help_needed=false
  echo "Running tests..."
  run_tests
fi

if [[ "${package}" == "true" ]]; then
  if [[ -z "${version}" ]]; then
    version="$(read_package_version)"
  fi
  validate_package_version "${version}"
  help_needed=false
  echo "Packaging version ${version}..."
  build_release_jar "${version}"
  package_release "${version}"
fi

if [[ "${help_needed}" == "true" ]]; then
  show_help
fi
