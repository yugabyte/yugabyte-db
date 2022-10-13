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

# This code is sourced from build-and-test.sh in case of success (other than unit test failures).
# Here we upload the build to S3.

if [[ $BASH_SOURCE == $0 ]]; then
  fatal "File '$BASH_SOURCE' must be sourced, not invoked"
fi

log "$BASH_SOURCE is being sourced"

upload_package() {
  package_uploaded=false
  package_upload_skipped=true

  if [[ -z ${YB_SNAPSHOT_PACKAGE_UPLOAD_URL:-} ]]; then
    log "YB_SNAPSHOT_PACKAGE_UPLOAD_URL not set, skipping package upload"
    return
  fi

  # If we fail with an error, we don't consider the package upload "skipped".
  package_upload_skipped=false

  if [[ $YB_SNAPSHOT_PACKAGE_UPLOAD_URL != */ ]]; then
    fatal "YB_SNAPSHOT_PACKAGE_UPLOAD_URL must end in a forward slash, got:" \
          "$YB_SNAPSHOT_PACKAGE_UPLOAD_URL"
  fi

  if [[ -z ${YB_PACKAGE_PATH:-} ]]; then
    log "The YB_PACKAGE_PATH variable is not set"
    return
  fi

  if [[ ! -f $YB_PACKAGE_PATH ]]; then
    log "File pointed to by YB_PACKAGE_PATH ('$YB_PACKAGE_PATH') not found."
    return
  fi

  local md5
  if is_mac; then
    md5=$( md5 <"$YB_PACKAGE_PATH" )
  else
    md5=$( md5sum "$YB_PACKAGE_PATH" | awk '{print $1}' )
  fi
  if [[ ! $md5 =~ ^[0-9a-f]{32}$ ]]; then
    log "Failed to compute MD5 sum of '$YB_PACKAGE_PATH', got '$md5' (expected 32 hex digits)."
    return
  fi

  # Some systems have "shasum", and some have "sha1sum". Not clear if this maps one to one to
  # the Linux / macOS division.
  local sha_program=shasum
  if ! which "$sha_program" &>/dev/null; then
    sha_program=sha1sum
  fi
  local sha=$( "$sha_program" <"$YB_PACKAGE_PATH" | awk '{print $1}' )
  if [[ ! $sha =~ ^[0-9a-f]{40}$ ]]; then
    log "Failed to compute SHA sum of '$YB_PACKAGE_PATH', got '$sha' (expected 40 hex digits)."
    return
  fi

  if [[ ! -f ~/.s3cfg ]]; then
    log "$HOME/.s3cfg not found, cannot upload package"
    return
  fi

  # Generate checksum files.
  echo -n "$md5" >"$YB_PACKAGE_PATH.md5"
  echo -n "$sha" >"$YB_PACKAGE_PATH.sha"

  # Check for various non-error reasons to skip package upload.
  package_upload_skipped=true

  if [[ ${YB_FORCE_PACKAGE_UPLOAD:-} != "1" ]]; then
    if ! is_jenkins; then
      log "Not running on Jenkins, skipping package upload." \
          "Use YB_FORCE_PACKAGE_UPLOAD to override this behavior."
      return
    fi

    if ! is_jenkins_master_build; then
      log "This is not a master job (job name: ${JOB_NAME:-undefined}), skipping package upload." \
          "Use YB_FORCE_PACKAGE_UPLOAD to override this behavior."
      return
    fi

    if [[ $BUILD_TYPE != "release" ]]; then
      log "Skipping package upload for a non-release build (build type: $BUILD_TYPE)"
      return
    fi
  fi

  package_upload_skipped=false
  log "Uploading package '$YB_PACKAGE_PATH' to $YB_SNAPSHOT_PACKAGE_UPLOAD_URL"
  if ( set -x; s3cmd put "$YB_PACKAGE_PATH"{.md5,.sha,} "$YB_SNAPSHOT_PACKAGE_UPLOAD_URL" ); then
    log "Uploaded package '$YB_PACKAGE_PATH' to $YB_SNAPSHOT_PACKAGE_UPLOAD_URL"
    package_uploaded=true
  else
    log "Failed to upload package '$YB_PACKAGE_PATH' to $YB_SNAPSHOT_PACKAGE_UPLOAD_URL"
    package_uploaded=false
  fi
}

upload_package
