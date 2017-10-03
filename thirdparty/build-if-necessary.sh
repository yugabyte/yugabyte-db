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

# Script which downloads and builds the thirdparty dependencies
# only if necessary.
#
# In a git repo, this uses git checksum information on the thirdparty
# tree. Otherwise, it uses a 'stamp file' approach.

set -e
set -o pipefail

TP_DIR=$(dirname $BASH_SOURCE)
cd $TP_DIR

NEEDS_BUILD=

IS_IN_GIT=$(test -d ../.git && echo true || :)

if [ -n "$IS_IN_GIT" ]; then
  # Determine whether this subtree in the git repo has changed since thirdparty
  # was last built

  CUR_THIRDPARTY_HASH=$(cd .. && git ls-tree -d HEAD thirdparty | awk '{print $3}')
  LAST_BUILD_HASH=$(cat .build-hash || :)
  if [ "$CUR_THIRDPARTY_HASH" != "$LAST_BUILD_HASH" ]; then
    echo "Rebuilding thirdparty: the repository has changed since thirdparty was last built."
    echo "Old git hash: $LAST_BUILD_HASH"
    echo "New build hash: $CUR_THIRDPARTY_HASH"
    NEEDS_BUILD=1
  else
    # Determine whether the developer has any local changes
    if ! ( git diff --quiet .  && git diff --cached --quiet . ) ; then
      echo "Rebuilding thirdparty: There are local changes in the repository."
      NEEDS_BUILD=1
    else
      echo Not rebuilding thirdparty. No changes since last build.
    fi
  fi
else
  # If we aren't inside running inside a git repository (e.g. we are
  # part of a source distribution tarball) then we can't use git to find
  # out whether the build is clean. Instead, use a .build-stamp file, and
  # see if any files inside this directory have been modified since then.
  if [ -f .build-stamp ]; then
    CHANGED_FILE_COUNT=$(find . -cnewer .build-stamp | wc -l)
    echo "$CHANGED_FILE_COUNT file(s) been modified since thirdparty was last built."
    if [ $CHANGED_FILE_COUNT -gt 0 ]; then
      echo "Rebuilding."
      echo NEEDS_BUILD=1
    fi
  else
    echo "It appears that thirdparty was never built. Building."
    NEEDS_BUILD=1
  fi
fi

if [ -z "$NEEDS_BUILD" ]; then
  exit 0
fi

rm -f .build-hash .build-stamp
./download-thirdparty.sh
./build-thirdparty.sh

if [ -n "$IS_IN_GIT" ]; then
  echo $CUR_THIRDPARTY_HASH > .build-hash
else
  touch .build-stamp
fi
