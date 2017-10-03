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
# Reports a test run to the central test server, which records
# the results in a database. This is what drives our "flaky test dashboard".
# This script does blocking network IO, so if you are running it from the
# context of a build, you may want to run it in the background.
#
# Note that this may exit with a non-zero code if the network is flaky or the
# test result server is down.

set -e

ROOT=$(dirname $BASH_SOURCE)/..

# Verify and parse command line and options
if [ $# -ne 3 ]; then
  echo "usage: $0 <path/to/test> <path/to/log> <exit-code>"
  echo
  echo The \$TEST_RESULT_SERVER environment variable may be used
  echo to specify where to report the tests.
  exit 1
fi
TEST_EXECUTABLE=$1
LOGFILE=$2
STATUS=$3
TEST_RESULT_SERVER=${TEST_RESULT_SERVER:-localhost:8080}
REPORT_TIMEOUT=${REPORT_TIMEOUT:-10}

# On Jenkins, we'll have this variable set. Otherwise,
# report the build ID as non-jenkins.
BUILD_ID=${BUILD_TAG:-non-jenkins}

# Figure out the current git revision, and append a "-dirty" tag if it's
# not a pristine checkout
REVISION=$(cd $ROOT && git rev-parse HEAD)
if ! ( cd $ROOT && git diff --quiet .  && git diff --cached --quiet . ) ; then
  REVISION="${REVISION}-dirty"
fi

# Parse out our "build config" - a space-separated list of tags
# which include the cmake build type as well as the list of configured
# sanitizers

CMAKECACHE=$ROOT/CMakeCache.txt
BUILD_CONFIG=$(grep '^CMAKE_BUILD_TYPE:' $CMAKECACHE | cut -f 2 -d=)
if grep -q "KUDU_USE_ASAN:UNINITIALIZED=1" $CMAKECACHE ; then
  BUILD_CONFIG="$BUILD_CONFIG asan"
fi
if grep -q "KUDU_USE_TSAN:UNINITIALIZED=1" $CMAKECACHE ; then
  BUILD_CONFIG="$BUILD_CONFIG tsan"
fi
if grep -q "KUDU_USE_UBSAN:UNINITIALIZED=1" $CMAKECACHE ; then
  BUILD_CONFIG="$BUILD_CONFIG ubsan"
fi

# We sometimes have flaky infrastructure where NTP is broken. In that case
# do not report it as a failed test.
if grep -q 'Clock considered unsynchronized' $LOGFILE ; then
  echo Not reporting test that failed due to NTP issues.
  exit 1
fi

# Only upload a log if the test failed.
# This saves some space on S3, network bandwidth, etc, and we don't
# have a lot of use for the logs of successful tests anyway.
if [ "$STATUS" -ne 0 ]; then
  LOG_PARAM="-F log=@$LOGFILE"
else
  LOG_PARAM=""
fi

curl -s \
    --max-time $REPORT_TIMEOUT \
    $LOG_PARAM \
    -F "build_id=$BUILD_ID" \
    -F "hostname=$(hostname)" \
    -F "test_name=$(basename $TEST_EXECUTABLE)" \
    -F "status=$STATUS" \
    -F "revision=$REVISION" \
    -F "build_config=$BUILD_CONFIG" \
    http://$TEST_RESULT_SERVER/add_result
