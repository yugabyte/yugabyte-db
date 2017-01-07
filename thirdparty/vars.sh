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

if [ -z "$TP_DIR" ]; then
   echo "TP_DIR variable not set, check your scripts"
   exit 1
fi

TP_BUILD_DIR=$TP_DIR/build
TP_DOWNLOAD_DIR=$TP_DIR/download
TP_SOURCE_DIR=$TP_DIR/src

# This URL corresponds to the CloudFront Distribution for the S3
# bucket cloudera-thirdparty-libs which is directly accessible at
# http://cloudera-thirdparty-libs.s3.amazonaws.com/
# TODO: copy this to YugaByte's own S3 bucket. Ideally use authenticated S3 for downloads.
CLOUDFRONT_URL_PREFIX=http://d3dr9sfxru4sde.cloudfront.net

PREFIX_COMMON=$TP_DIR/installed/common
PREFIX_DEPS=$TP_DIR/installed/uninstrumented
PREFIX_DEPS_TSAN=$TP_DIR/installed/tsan

# libstdcxx needs its own prefix so that it is not inadvertently included in the library search path
# during non-TSAN builds.
PREFIX_LIBSTDCXX=$PREFIX_DEPS/gcc
PREFIX_LIBSTDCXX_TSAN=$PREFIX_DEPS_TSAN/gcc

GFLAGS_VERSION=2.1.2
GFLAGS_DIR=$TP_SOURCE_DIR/gflags-$GFLAGS_VERSION

GLOG_VERSION=0.3.4
GLOG_DIR=$TP_SOURCE_DIR/glog-$GLOG_VERSION

GMOCK_VERSION=1.7.0
GMOCK_DIR=$TP_SOURCE_DIR/gmock-$GMOCK_VERSION

GPERFTOOLS_VERSION=2.2.1
GPERFTOOLS_DIR=$TP_SOURCE_DIR/gperftools-$GPERFTOOLS_VERSION

PROTOBUF_VERSION=2.6.1
PROTOBUF_DIR=$TP_SOURCE_DIR/protobuf-$PROTOBUF_VERSION

CMAKE_VERSION=3.2.3
CMAKE_DIR=$TP_SOURCE_DIR/cmake-${CMAKE_VERSION}

SNAPPY_VERSION=1.1.0
SNAPPY_DIR=$TP_SOURCE_DIR/snappy-$SNAPPY_VERSION

LZ4_VERSION=r130
LZ4_DIR=$TP_SOURCE_DIR/lz4-lz4-$LZ4_VERSION

# from https://github.com/kiyo-masui/bitshuffle
# Hash of git: 55f9b4caec73fa21d13947cacea1295926781440
BITSHUFFLE_VERSION=55f9b4c
BITSHUFFLE_DIR=$TP_SOURCE_DIR/bitshuffle-${BITSHUFFLE_VERSION}

ZLIB_VERSION=1.2.8
ZLIB_DIR=$TP_SOURCE_DIR/zlib-$ZLIB_VERSION

LIBEV_VERSION=4.20
LIBEV_DIR=$TP_SOURCE_DIR/libev-$LIBEV_VERSION

RAPIDJSON_VERSION=0.11
RAPIDJSON_DIR=$TP_SOURCE_DIR/rapidjson-${RAPIDJSON_VERSION}

# Hash of the squeasel git revision to use.
# (from http://github.com/cloudera/squeasel)
#
# To re-build this tarball use the following in the squeasel repo:
#  export NAME=squeasel-$(git rev-parse HEAD)
#  git archive HEAD --prefix=$NAME/ -o /tmp/$NAME.tar.gz
#  s3cmd put -P /tmp/$NAME.tar.gz s3://cloudera-thirdparty-libs/$NAME.tar.gz
#
# File a HD ticket for access to the cloudera-dev AWS instance to push to S3.
SQUEASEL_VERSION=8ac777a122fccf0358cb8562e900f8e9edd9ed11
SQUEASEL_DIR=$TP_SOURCE_DIR/squeasel-${SQUEASEL_VERSION}

# git revision of google style guide:
# https://github.com/google/styleguide
# git archive --prefix=google-styleguide-$(git rev-parse HEAD)/ -o /tmp/google-styleguide-$(git rev-parse HEAD).tgz HEAD
GSG_VERSION=7a179d1ac2e08a5cc1622bec900d1e0452776713
GSG_DIR=$TP_SOURCE_DIR/google-styleguide-${GSG_VERSION}

GCOVR_VERSION=3.0
GCOVR_DIR=$TP_SOURCE_DIR/gcovr-${GCOVR_VERSION}

CURL_VERSION=7.32.0
CURL_DIR=$TP_SOURCE_DIR/curl-${CURL_VERSION}

# Hash of the crcutil git revision to use.
# (from http://github.mtv.cloudera.com/CDH/crcutil)
#
# To re-build this tarball use the following in the crcutil repo:
#  export NAME=crcutil-$(git rev-parse HEAD)
#  git archive HEAD --prefix=$NAME/ -o /tmp/$NAME.tar.gz
#  s3cmd put -P /tmp/$NAME.tar.gz s3://cloudera-thirdparty-libs/$NAME.tar.gz
CRCUTIL_VERSION=440ba7babeff77ffad992df3a10c767f184e946e
CRCUTIL_DIR=$TP_SOURCE_DIR/crcutil-${CRCUTIL_VERSION}

LIBUNWIND_VERSION=1.1a
LIBUNWIND_DIR=$TP_SOURCE_DIR/libunwind-${LIBUNWIND_VERSION}

# Our llvm tarball includes clang, extra clang tools, lld, and compiler-rt.
#
# See http://clang.llvm.org/get_started.html and http://lld.llvm.org/ for
# details on how they're laid out in the llvm tarball.
#
# Summary:
# 1. Unpack the llvm tarball
# 2. Unpack the clang tarball as tools/clang (rename from cfe-<version> to clang)
# 3. Unpack the extra clang tools tarball as tools/clang/tools/extra
# 4. Unpack the lld tarball as tools/lld
# 5. Unpack the compiler-rt tarball as projects/compiler-rt
# 6. Unpack the libc++ tarball as projects/libcxx
# 7. Unpack the libc++abi tarball as projects/libcxxabi
# 8. Create new tarball from the resulting source tree
#
LLVM_VERSION=3.9.0
# This is the naming pattern Kudu is now using for all source directories instead of _DIR.
# TODO: migrate to this pattern for all other third-party dependencies.
# Note: we have the ".src" suffix at the end because that's what comes out of the tarball.
LLVM_SOURCE=$TP_SOURCE_DIR/llvm-$LLVM_VERSION.src

# Python 2.7 is required to build LLVM 3.6+. It is only built and installed if
# the system Python version is not 2.7.
PYTHON_VERSION=2.7.10
PYTHON_DIR=$TP_SOURCE_DIR/python-${PYTHON_VERSION}

GCC_VERSION=4.9.3
GCC_DIR=$TP_SOURCE_DIR/gcc-${GCC_VERSION}

# Our trace-viewer repository is separate since it's quite large and
# shouldn't change frequently. We upload the built artifacts (HTML/JS)
# when we need to roll to a new revision.
#
# The source can be found at https://github.com/cloudera/kudu-trace-viewer
# and built with "kudu-build.sh" included within the repository.
TRACE_VIEWER_VERSION=45f6525d8aa498be53e4137fb73a9e9e036ce91d
TRACE_VIEWER_DIR=$TP_SOURCE_DIR/kudu-trace-viewer-${TRACE_VIEWER_VERSION}

NVML_VERSION=0.4-b2
NVML_DIR=$TP_SOURCE_DIR/nvml-$NVML_VERSION
