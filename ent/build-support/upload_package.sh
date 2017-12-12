#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

# This code is sourced from build-and-test.sh in case of success (other than unit test failures).
# Here we upload the build to S3.

if [[ $BASH_SOURCE == $0 ]]; then
  fatal "File '$BASH_SOURCE' must be sourced, not invoked"
fi

log "$BASH_SOURCE invoked, edition: $YB_EDITION"

# We upload packages that we build here. These will be tested using itest. It is important that this
# URL ends with a "/".
SNAPSHOT_PACKAGE_UPLOAD_URL=s3://no-such-url

upload_package() {
  package_uploaded=false

  # Check for errors.
  package_upload_skipped=false
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

    if is_linux && [[ $YB_COMPILER_TYPE != "gcc" ]]; then
      log "Skipping package upload for a non-gcc build on Linux (compiler type: $YB_COMPILER_TYPE)"
      return
    fi
  fi

  package_upload_skipped=false
  log "Uploading package '$YB_PACKAGE_PATH' to $SNAPSHOT_PACKAGE_UPLOAD_URL"
  if ( set -x; s3cmd put "$YB_PACKAGE_PATH"{.md5,.sha,} "$SNAPSHOT_PACKAGE_UPLOAD_URL" ); then
    log "Uploaded package '$YB_PACKAGE_PATH' to $SNAPSHOT_PACKAGE_UPLOAD_URL"
    package_uploaded=true
  else
    log "Failed to upload package '$YB_PACKAGE_PATH' to $SNAPSHOT_PACKAGE_UPLOAD_URL"
    package_uploaded=false
  fi
}

upload_package
