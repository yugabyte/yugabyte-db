// Copyright (c) Yugabyte, Inc.
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

#include <gtest/gtest.h>

#include "yb/master/catalog_entity_info.h"
#include "yb/util/test_util.h"

namespace yb {
namespace master {

class CatalogEntityInfoTest : public YBTest {};

// Verify that data mutations are not available from metadata() until commit.
TEST_F(CatalogEntityInfoTest, TestNamespaceInfoCommit) {
  scoped_refptr<NamespaceInfo> ns(new NamespaceInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));

  // Mutate the namespace, under the write lock.
  auto writer_lock = ns->LockForWrite();
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_NE("foo", reader_lock->name());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    auto reader_lock = ns->LockForRead();
    ASSERT_EQ("foo", reader_lock->name());
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(CatalogEntityInfoTest, TestTableInfoCommit) {
  scoped_refptr<TableInfo> table =
      make_scoped_refptr<TableInfo>("123" /* table_id */, false /* colocated */);

  // Mutate the table, under the write lock.
  auto writer_lock = table->LockForWrite();
  writer_lock.mutable_data()->pb.set_name("foo");

  // Changes should not be visible to a reader.
  // The reader can still lock for read, since readers don't block
  // writers in the RWC lock.
  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->name());
  }
  writer_lock.mutable_data()->set_state(SysTablesEntryPB::RUNNING, "running");

  {
    auto reader_lock = table->LockForRead();
    ASSERT_NE("foo", reader_lock->pb.name());
    ASSERT_NE("running", reader_lock->pb.state_msg());
    ASSERT_NE(SysTablesEntryPB::RUNNING, reader_lock->pb.state());
  }

  // Commit the changes
  writer_lock.Commit();

  // Verify that the data is visible
  {
    auto reader_lock = table->LockForRead();
    ASSERT_EQ("foo", reader_lock->pb.name());
    ASSERT_EQ("running", reader_lock->pb.state_msg());
    ASSERT_EQ(SysTablesEntryPB::RUNNING, reader_lock->pb.state());
  }
}

// Verify that data mutations are not available from metadata() until commit.
TEST_F(CatalogEntityInfoTest, TestTabletInfoCommit) {
  scoped_refptr<TabletInfo> tablet(new TabletInfo(nullptr, "123"));

  // Mutate the tablet, the changes should not be visible
  auto l = tablet->LockForWrite();
  PartitionPB* partition = l.mutable_data()->pb.mutable_partition();
  partition->set_partition_key_start("a");
  partition->set_partition_key_end("b");
  l.mutable_data()->set_state(SysTabletsEntryPB::RUNNING, "running");
  {
    // Changes shouldn't be visible, and lock should still be
    // acquired even though the mutation is under way.
    auto read_lock = tablet->LockForRead();
    ASSERT_NE("a", read_lock->pb.partition().partition_key_start());
    ASSERT_NE("b", read_lock->pb.partition().partition_key_end());
    ASSERT_NE("running", read_lock->pb.state_msg());
    ASSERT_NE(SysTabletsEntryPB::RUNNING,
              read_lock->pb.state());
  }

  // Commit the changes
  l.Commit();

  // Verify that the data is visible
  {
    auto read_lock = tablet->LockForRead();
    ASSERT_EQ("a", read_lock->pb.partition().partition_key_start());
    ASSERT_EQ("b", read_lock->pb.partition().partition_key_end());
    ASSERT_EQ("running", read_lock->pb.state_msg());
    ASSERT_EQ(SysTabletsEntryPB::RUNNING,
              read_lock->pb.state());
  }
}

} // namespace master
} // namespace yb
