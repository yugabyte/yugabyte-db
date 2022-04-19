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

#ifndef YB_DOCDB_DOCDB_TEST_BASE_H
#define YB_DOCDB_DOCDB_TEST_BASE_H

#include <string>
#include <vector>

#include "yb/rocksdb/db.h"

#include "yb/docdb/docdb_test_util.h"
#include "yb/docdb/key_bytes.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

namespace yb {
namespace docdb {

class DocDBTestBase : public YBTest, public DocDBRocksDBFixture {
 public:
  DocDBTestBase();
  ~DocDBTestBase() override;
  void SetUp() override;
  void TearDown() override;

 protected:

  // Captures a "logical snapshot" of the underlying RocksDB database. By "logical snapshot" we mean
  // a straightforward copy of all key/values stored, not a RocksDB-level snapshot. This is an easy
  // way to go back to an old state of RocksDB in tests so we can make some more changes and take
  // on a different path.
  void CaptureLogicalSnapshot();

  // Clears the internal vector of logical RocksDB snapshots. The next snapshot to be captured will
  // again have the index 0.
  void ClearLogicalSnapshots();

  // Restore the state of RocksDB to the previously taken "logical snapshot" with the given index.
  //
  // @param snapshot_index The snapshot index to restore the state to RocksDB to, with the first
  //                       snapshot having index 0.
  void RestoreToRocksDBLogicalSnapshot(size_t snapshot_index);

  void RestoreToLastLogicalRocksDBSnapshot() {
    RestoreToRocksDBLogicalSnapshot(logical_snapshots_.size() - 1);
  }

  size_t num_logical_snapshots() { return logical_snapshots_.size(); }

  const std::vector<LogicalRocksDBDebugSnapshot>& logical_snapshots() {
    return logical_snapshots_;
  }

 private:
  std::vector<LogicalRocksDBDebugSnapshot> logical_snapshots_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_DOCDB_TEST_BASE_H
