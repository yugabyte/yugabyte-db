// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#include <gtest/gtest.h>

#include "yb/util/test_util.h"
#include "yb/tserver/tserver_shared_mem.h"

namespace yb {
namespace tserver {

class TServerSharedMemoryTest : public YBTest {};

TEST_F(TServerSharedMemoryTest, TestSharedCatalogVersion) {
  TServerSharedMemory tserver;
  TServerSharedMemory postgres(tserver.GetFd(), SharedMemorySegment::AccessMode::kReadOnly);

  // Default value is zero.
  ASSERT_EQ(0, tserver.GetYSQLCatalogVersion());
  ASSERT_EQ(0, postgres.GetYSQLCatalogVersion());

  // TServer can set catalog version.
  tserver.SetYSQLCatalogVersion(2);
  ASSERT_EQ(2, tserver.GetYSQLCatalogVersion());
  ASSERT_EQ(2, postgres.GetYSQLCatalogVersion());

  // TServer can update catalog version.
  tserver.SetYSQLCatalogVersion(4);
  ASSERT_EQ(4, tserver.GetYSQLCatalogVersion());
  ASSERT_EQ(4, postgres.GetYSQLCatalogVersion());
}

TEST_F(TServerSharedMemoryTest, TestMultipleTabletServers) {
  TServerSharedMemory tserver1;
  TServerSharedMemory postgres1(tserver1.GetFd(), SharedMemorySegment::AccessMode::kReadOnly);

  TServerSharedMemory tserver2;
  TServerSharedMemory postgres2(tserver2.GetFd(), SharedMemorySegment::AccessMode::kReadOnly);

  tserver1.SetYSQLCatalogVersion(22);
  tserver2.SetYSQLCatalogVersion(17);

  ASSERT_EQ(22, postgres1.GetYSQLCatalogVersion());
  ASSERT_EQ(17, postgres2.GetYSQLCatalogVersion());
}

TEST_F(TServerSharedMemoryTest, TestBadSegment) {
  // Pass an invalid file descriptor so open fails fatally.
  ASSERT_DEATH({
    TServerSharedMemory memory(-1, SharedMemorySegment::AccessMode::kReadWrite);
  }, "Error mapping shared memory segment");
}

}  // namespace tserver
}  // namespace yb
