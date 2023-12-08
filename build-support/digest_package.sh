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
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.

# This code is sourced from $SRCROOT/build-support/jenkins/build.sh.  The function it provides
# create md5 and sha files for whatever package is passed in.

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
  fatal "File '${BASH_SOURCE[0]}' must be sourced, not invoked"
fi

log "${BASH_SOURCE[0]} is being sourced"

digest_package() {
  package_path=$1

  if [[ -z ${package_path:-} ]]; then
    log "No package_path was passed in."
    return
  fi

  if [[ ! -f $package_path ]]; then
    log "The passed in file ('$package_path') doesn't exist."
    return
  fi

  local md5
  if is_mac; then
    md5=$( md5 <"$package_path" )
  else
    md5=$( md5sum "$package_path" | awk '{print $1}' )
  fi
  if [[ ! $md5 =~ ^[0-9a-f]{32}$ ]]; then
    log "Failed to compute MD5 sum of '$package_path', got '$md5' (expected 32 hex digits)."
    return
  fi

  # Macs have shasum perl script while linux hosts with have sha<precision>sum binaries
  local sha
  if is_mac; then
    sha=$( shasum -a 256 <"$package_path" | awk '{print $1}' )
  else
    sha=$( sha256sum <"$package_path" | awk '{print $1}' )
  fi
  if [[ ! $sha =~ ^[0-9a-f]{64}$ ]]; then
    log "Failed to compute SHA sum of '$package_path', got '$sha' (expected 64 hex digits)."
    return
  fi

  # Generate checksum files.
  echo -n "$md5" >"$package_path.md5"
  echo -n "$sha" >"$package_path.sha"
}
