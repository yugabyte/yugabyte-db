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

#include "yb/docdb/docdb_test_base.h"

#include "yb/docdb/docdb.h"
#include "yb/docdb/docdb_test_util.h"

namespace yb {
namespace docdb {

DocDBTestBase::DocDBTestBase() {
}

DocDBTestBase::~DocDBTestBase() {
}

void DocDBTestBase::SetUp() {
  YBTest::SetUp();
  ASSERT_OK(InitRocksDBOptions());
  ASSERT_OK(InitRocksDBDir());
  ASSERT_OK(OpenRocksDB());
  ResetMonotonicCounter();
}

void DocDBTestBase::TearDown() {
  ASSERT_OK(DestroyRocksDB());
  YBTest::TearDown();
}

Status DocDBTestBase::CaptureLogicalSnapshot() {
  logical_snapshots_.emplace_back();
  return logical_snapshots_.back().Capture(rocksdb());
}

void DocDBTestBase::ClearLogicalSnapshots() {
  logical_snapshots_.clear();
}

Status DocDBTestBase::RestoreToRocksDBLogicalSnapshot(size_t snapshot_index) {
  CHECK_LE(0, snapshot_index);
  CHECK_LT(snapshot_index, logical_snapshots_.size());
  return logical_snapshots_[snapshot_index].RestoreTo(rocksdb());
}

}  // namespace docdb
}  // namespace yb
