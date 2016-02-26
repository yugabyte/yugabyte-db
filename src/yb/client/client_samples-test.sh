#!/bin/bash -xe
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
# Tests that the Kudu client library can be installed outside
# the build tree, that the installed headers are sane, and that
# the sample code can be built and runs correctly.

# Clean up after the test. Must be idempotent.
cleanup() {
  if [ -n "$TS_PID" ]; then
      kill -9 "$TS_PID" || :
      wait $TS_PID || :
  fi
  if [ -n "$MASTER_PID" ]; then
      kill -9 "$MASTER_PID" || :
      wait $MASTER_PID || :
  fi
  if [ -n "$BASE_DIR" -a -d "$BASE_DIR" ]; then
      rm -rf "$BASE_DIR"
  fi
  if [ -n "$LIBRARY_DIR" -a -d "$LIBRARY_DIR" ]; then
      rm -rf "$LIBRARY_DIR"
  fi
}
trap cleanup EXIT

OUTPUT_DIR=$(cd $(dirname "$BASH_SOURCE"); pwd)

# Install the client library to a temporary directory.
# Try to detect whether we're building using Ninja or Make.
LIBRARY_DIR=$(mktemp -d -t kudu-samples-test.XXXXXXXXXXXXX)
PREFIX_DIR=$LIBRARY_DIR/usr/local
SAMPLES_DIR=$PREFIX_DIR/share/doc/kuduClient/samples
pushd $OUTPUT_DIR/..
NINJA=$(which ninja 2>/dev/null) || NINJA=""
if [ -r build.ninja -a -n "$NINJA" ]; then
  DESTDIR=$LIBRARY_DIR ninja install
else
  make -j$(getconf _NPROCESSORS_ONLN) DESTDIR=$LIBRARY_DIR install
fi
popd

# Test that all of the installed headers can be compiled on their own.
# This catches bugs where we've made a mistake in 'include-what-you-use'
# within the library.
for include_file in $(find $LIBRARY_DIR -name \*.h) ; do
  echo Checking standalone compilation of $include_file...
  if ! ${CXX:-g++} -o /dev/null -I$LIBRARY_DIR/usr/local/include $include_file ; then
    set +x
    echo
    echo -----------------------------------------
    echo $include_file fails to build on its own.
    echo See log above for details.
    echo -----------------------------------------
    exit 1
  fi
done
# Prefer the cmake on the system path, since we expect our client library
# to be usable with older versions of cmake. But if it isn't there,
# use the one from thirdparty.
CMAKE=$(which cmake || :)
if [ -z "$CMAKE" ]; then
  # TODO: temporary hack which assumes this script is in src/build/<type>/bin
  CMAKE=$OUTPUT_DIR/../../../thirdparty/installed/bin/cmake
fi

# Build the client samples using the client library.
# We can just always use Make here, since we're calling cmake ourselves.
pushd $SAMPLES_DIR
CMAKE_PREFIX_PATH=$PREFIX_DIR $CMAKE .
make -j$(getconf _NPROCESSORS_ONLN)
popd

# Pick a unique localhost IP address so this can run in parallel with other
# tests. This only works on Linux.
LOCALHOST_IP=127.0.0.1
if [ "$(uname)" == "Linux" ]; then
  LOCALHOST_IP=127.$[($$ >> 8) & 0xff].$[$$ & 0xff].1
  echo Using unique localhost IP $LOCALHOST_IP
fi


# Start master+ts
export TMPDIR=${TMPDIR:-/tmp}
export TEST_TMPDIR=${TEST_TMPDIR:-$TMPDIR/kudutest-$UID}
mkdir -p $TEST_TMPDIR
BASE_DIR=$(mktemp -d $TEST_TMPDIR/client_samples-test.XXXXXXXX)
$OUTPUT_DIR/kudu-master \
  --default_num_replicas=1 \
  --log_dir=$BASE_DIR \
  --fs_wal_dir=$BASE_DIR/master \
  --fs_data_dirs=$BASE_DIR/master \
  --webserver_interface=localhost \
  --webserver_port=0 \
  --rpc_bind_addresses=$LOCALHOST_IP &
MASTER_PID=$!
$OUTPUT_DIR/kudu-tserver \
  --log_dir=$BASE_DIR \
  --fs_wal_dir=$BASE_DIR/ts \
  --fs_data_dirs=$BASE_DIR/ts \
  --rpc_bind_addresses=$LOCALHOST_IP \
  --local_ip_for_outbound_sockets=$LOCALHOST_IP \
  --webserver_interface=localhost \
  --webserver_port=0 \
  --tserver_master_addrs=$LOCALHOST_IP &
TS_PID=$!

# Let them run for a bit.
sleep 5

# Run the samples.
$SAMPLES_DIR/sample $LOCALHOST_IP
