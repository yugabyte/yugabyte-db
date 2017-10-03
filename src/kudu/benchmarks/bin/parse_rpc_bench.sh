#!/bin/bash -e
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
# Little script to parse the output from rpc-bench.


FILE=$1
if [[ -z $FILE || $FILE == "-h" || $FILE == "--help" ]]; then
  echo "Usage: $0 rpc-bench-output.log"
  echo
  echo 'Example:'
  echo
  echo '$ cd $KUDU_HOME'
  echo '$ BUILD_TYPE=RELEASE ./build-support/jenkins/build-and-test.sh'
  echo '$ KUDU_ALLOW_SLOW_TESTS=1 ./build/latest/bin/rpc-bench --gtest_repeat=10 2>&1 | tee rpc-bench-output.log'
  echo '$ ./src/kudu/benchmarks/bin/parse_rpc_bench.sh rpc-bench-output.log'
  echo
  echo 'Example output:'
  echo
  echo 'Reqs/sec: runs=10, avg=146661.6, max=147649'
  echo 'User CPU per req: runs=10, avg=16.3004, max=16.4745'
  echo 'Sys CPU per req: runs=10, avg=29.25029, max=29.5273'
  echo
  exit 1
fi

# Just some hacky one-liners to parse and summarize the output files.
# Don't forget to redirect stderr to stdout when teeing the rpc-bench output to the log file!
perl -ne '/ (Reqs\/sec):\s+(\d+(?:\.(?:\d+)?)?)/ or next; $lab = $1; $m = $2 if $2 > $m; $v += $2; $ct++; END { print "$lab: runs=$ct, avg=" . $v/$ct . ", max=$m\n"; }' < $FILE
perl -ne '/ (User CPU per req):\s+(\d+(?:\.(?:\d+)?)?)/ or next; $lab = $1; $m = $2 if $2 > $m; $v += $2; $ct++; END { print "$lab: runs=$ct, avg=" . $v/$ct . ", max=$m\n"; }' < $FILE
perl -ne '/ (Sys CPU per req):\s+(\d+(?:\.(?:\d+)?)?)/ or next; $lab = $1; $m = $2 if $2 > $m; $v += $2; $ct++; END { print "$lab: runs=$ct, avg=" . $v/$ct . ", max=$m\n"; }' < $FILE
