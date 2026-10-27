#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.

set -euo pipefail

print_help() {
  cat <<-EOT
Builds the BYOC API proxy image from a release tarball, and optionally pushes it.
Usage: ${0##*/} <options>
Options:
  -h, --help
    Show usage.
  -t, --tarball <path>
    Release tarball byoc_api_proxy-<version>.tar.gz, as built by release.sh. Required.
  -r, --repository <name>
    Image repository, e.g. quay.io/yugabyte/byoc-api-proxy-itest for a release build. Required:
    releases are pushed to the -itest repository and reach the public one only by promotion.
  -b, --base-image <image>
    Base image override (default: the BASE_IMAGE in the Dockerfile).
  -p, --push
    Push the image after building it.
The image is tagged <repository>:<version>.
EOT
}

readonly docker_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

tarball=""
repository=""
base_image=""
push=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
      ;;
    -t|--tarball)
      tarball="$2"
      shift
      ;;
    -r|--repository)
      repository="$2"
      shift
      ;;
    -b|--base-image)
      base_image="$2"
      shift
      ;;
    -p|--push)
      push="true"
      ;;
    *)
      echo "Unknown option $1" >&2
      print_help >&2
      exit 1
      ;;
  esac
  shift
done

if [[ -z "${repository}" ]]; then
  echo "--repository is required" >&2
  print_help >&2
  exit 1
fi
if [[ ! -f "${tarball}" ]]; then
  echo "Release tarball not found: '${tarball}'" >&2
  exit 1
fi

tarball_name="$(basename "${tarball}")"
if [[ ! "${tarball_name}" =~ ^byoc_api_proxy-([0-9]+\.[0-9]+\.[0-9]+(-SNAPSHOT)?)\.tar\.gz$ ]]; then
  echo "Unexpected tarball name '${tarball_name}', expected byoc_api_proxy-<version>.tar.gz" >&2
  exit 1
fi
version="${BASH_REMATCH[1]}"
image="${repository}:${version}"

# The build context holds only the tarball, so nothing else from the checkout leaks into it.
context="$(mktemp -d)"
trap 'rm -rf "${context}"' EXIT
cp "${tarball}" "${context}/"

build_args=(--build-arg "VERSION=${version}")
if [[ -n "${base_image}" ]]; then
  build_args+=(--build-arg "BASE_IMAGE=${base_image}")
fi

echo "Building ${image}"
docker build --platform linux/amd64 \
  "${build_args[@]}" \
  --file "${docker_dir}/Dockerfile" \
  --tag "${image}" \
  "${context}"

if [[ "${push}" == "true" ]]; then
  echo "Pushing ${image}"
  docker push "${image}"
fi
