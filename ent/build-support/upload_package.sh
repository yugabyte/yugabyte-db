#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

# This code is sourced from build-and-test.sh in case of success (other than unit test failures).
# Here we upload the build to S3.

if [[ $BASH_SOURCE == $0 ]]; then
  fatal "File '$BASH_SOURCE' must be sourced, not invoked"
fi

log "$BASH_SOURCE invoked, edition: $YB_EDITION"

RELEASE_PACKAGE_UPLOAD_URL=s3://snapshots.yugabyte.com/yugabyte

upload_package() {
  package_uploaded=false
  package_upload_skipped=false
  if [[ ${YB_FORCE_PACKAGE_UPLOAD:-} != "1" ]]; then
    if ! is_jenkins; then
      package_upload_skipped=true
      log "Not running on Jenkins, skipping package upload." \
          "Use YB_FORCE_PACKAGE_UPLOAD to override this behavior."
      return
    fi

    if ! is_jenkins_master_build; then
      package_upload_skipped=true
      log "This is not a master job (job name: ${JOB_NAME:-undefined}), skipping package upload." \
          "Use YB_FORCE_PACKAGE_UPLOAD to override this behavior."
      return
    fi
  fi


  if [[ ! -f ~/.s3cfg ]]; then
    log "$HOME/.s3cfg not found, cannot upload the release package"
    return 1
  fi

  if [[ -z ${YB_PACKAGE_PATH:-} ]]; then
    log "The YB_PACKAGE_PATH variable is not set"
    return 1
  fi

  if [[ ! -f $YB_PACKAGE_PATH ]]; then
    log "File pointed to by YB_PACKAGE_PATH ('$YB_PACKAGE_PATH') not found."
    return 1
  fi

  log "Uploading package '$YB_PACKAGE_PATH' to $RELEASE_PACKAGE_UPLOAD_URL"
  if ( set -x; s3cmd put "$YB_PACKAGE_PATH" "$RELEASE_PACKAGE_UPLOAD_URL" ); then
    log "Uploaded package '$YB_PACKAGE_PATH' to $RELEASE_PACKAGE_UPLOAD_URL"
    package_uploaded=true
  else
    log "Failed to upload package '$YB_PACKAGE_PATH' to $RELEASE_PACKAGE_UPLOAD_URL"
    package_uploaded=false
  fi
}

upload_package
