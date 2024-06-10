#!/bin/bash
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
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
#
# Script which, when run from Jenkins, will hunt and kill any other jenkins
# processes running on the same machine from a different build.
#
# This is done by looking for all of the processes owned by the jenkins user,
# checking their BUILD_ID environment variable, and killing any whose BUILD_ID
# does not match the current $BUILD_ID environment.  This assumes that there are
# no concurrent builds configured in Jenkins, for obvious reasons.
#
# Set $DRY_RUN before running this script to just see what would be killed.

set -e

JENKINS_USER="${JENKINS_USER:-jenkins}"
# shellcheck disable=SC2153
CURRENT_BUILD="$BUILD_ID"

if [ "$USER" != "$JENKINS_USER" ]; then
  echo "Not running as user '$JENKINS_USER'"
  exit 1
fi

if [ -z "$CURRENT_BUILD" ]; then
  echo Not running in the context of a Jenkins build
  exit 1
fi

JENKINS_PIDS=$(pgrep -u "$JENKINS_USER")
for pid in $JENKINS_PIDS; do
  cmdline=$(ps h -p "$pid" -o cmd || echo '[pid exited]')
  build_env=$( tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | grep -E '^BUILD_ID=' || :)
  if [ -z "$build_env" ]; then
    # Some Jenkins processes, like the slave itself, don't have a BUILD_ID
    # set. We shouldn't kill those.
    echo "Process $pid ($cmdline) not associated with any build. Skipping..."
    continue
  fi
  build_id=$(echo "$build_env" | cut -d= -f2)
  if [ "$build_id" != "$CURRENT_BUILD" ]; then
    echo "Killing zombie process $pid (from build $build_id)"
    ps -fww -p "$pid" || :
    if [ -z "$DRY_RUN" ]; then
      kill -9 "$pid" || :
    fi
    echo ----------
  else
    echo "pid $pid ($cmdline) is from the current build. Not killing"
  fi
done
