#!/usr/bin/env bash

[[ "${_YB_GET_VERSION_INCLUDED:-}" == "true" ]] && return 0
_YB_GET_VERSION_INCLUDED="true"

# git rev-parse --abbrev-ref HEAD
# git branch --show-current
get_version_string() {
  if [[ -f version.txt ]]; then
    branch=$(sed 's/-b0//' < version.txt)
  else
    branch=${BRANCH_NAME:-$(git branch --show-current)}
    branch=${branch:-DETACHED}
  fi

  if [[ -n $YB_RELEASE_BUILD_NUMBER ]]; then
    build="b${YB_RELEASE_BUILD_NUMBER}"
  else
    build="SHA$(git rev-parse --short HEAD)"
  fi
  echo -n "${branch}-${build}"
}


if [[ ${BASH_SOURCE[0]} == "$0" ]]; then
  get_version_string
fi
