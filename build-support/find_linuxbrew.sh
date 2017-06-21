#!/usr/bin/env bash

. "${BASH_SOURCE%/*}/../build-support/common-build-env.sh"

detect_linuxbrew

if "$YB_USING_LINUXBREW"; then
  echo "$YB_LINUXBREW_DIR"
fi
