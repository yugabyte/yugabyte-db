# Copyright (c) YugabyteDB, Inc.
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

set(YB_PCH_PREFIX storage)

set(STORAGE_SRCS
    compaction.cc
    frontier.cc
    )

set(STORAGE_DEPS yb_common_proto yb_util)

ADD_YB_LIBRARY(yb_storage
        SRCS ${STORAGE_SRCS}
        DEPS ${STORAGE_DEPS}
        )

ADD_YB_LIBRARY(
    yb_storage_test_util
    SRCS storage_test_util.cc
    DEPS yb_storage)
