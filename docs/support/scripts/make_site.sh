#!/bin/bash
########################################################################
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
# This script generates site documentation and Javadocs.
#
# Usage: make_site.sh
########################################################################
set -e

BUILD_TYPE=release
SOURCE_ROOT=$(cd $(dirname $0)/../../..; pwd)
BUILD_ROOT="$SOURCE_ROOT/build/$BUILD_TYPE"
SITE_OUTPUT_DIR="$BUILD_ROOT/site"
set -x

cd "$SOURCE_ROOT"

# Build Kudu thirdparty
$SOURCE_ROOT/build-support/enable_devtoolset.sh $SOURCE_ROOT/thirdparty/build-if-necessary.sh
echo "Successfully built third-party dependencies."

# Build the binaries so we can auto-generate the command-line references
mkdir -p "$BUILD_ROOT"
cd "$BUILD_ROOT"
rm -rf CMakeCache CMakeFiles/
$SOURCE_ROOT/build-support/enable_devtoolset.sh \
    $SOURCE_ROOT/thirdparty/installed/bin/cmake \
    -DNO_TESTS=1 \
    -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    $SOURCE_ROOT
make -j$(getconf _NPROCESSORS_ONLN)

# Check out the gh-pages repo into $SITE_OUTPUT_DIR
git clone -q $(git config --get remote.origin.url) --reference $SOURCE_ROOT -b gh-pages --depth 1 "$SITE_OUTPUT_DIR"

# Build the docs using the styles from the Jekyll site
rm -Rf "$SITE_OUTPUT_DIR/docs"
$SOURCE_ROOT/docs/support/scripts/make_docs.sh --build_root $BUILD_ROOT --site "$SITE_OUTPUT_DIR"
if [ -f "$SITE_OUTPUT_DIR/docs/index.html" ]; then
  echo "Successfully built docs."
else
  echo "Docs failed to build."
  exit 1
fi

cd "$SOURCE_ROOT/java"
mvn clean install -DskipTests
mvn clean javadoc:aggregate

if [ -f "$SOURCE_ROOT/java/target/site/apidocs/index.html" ]; then
  echo "Successfully built Javadocs."
else
  echo "Javadocs failed to build."
  exit 1
fi

rm -Rf "$SITE_OUTPUT_DIR/apidocs"
cp -au "$SOURCE_ROOT/java/target/site/apidocs" "$SITE_OUTPUT_DIR/"

cd "$SITE_OUTPUT_DIR"
SITE_ARCHIVE="$SITE_OUTPUT_DIR/website_archive.zip"
zip -rq "$SITE_ARCHIVE" docs apidocs

echo "Generated web site at $SITE_OUTPUT_DIR"
echo "Docs zip generated at $SITE_ARCHIVE"
