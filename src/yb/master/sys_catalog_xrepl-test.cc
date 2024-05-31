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

#include "yb/master/catalog_entity_info.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog-test_base.h"

using std::vector;

namespace yb {
namespace master {

class TestCDCStreamLoader : public Visitor<PersistentCDCStreamInfo> {
 public:
  TestCDCStreamLoader() {}
  ~TestCDCStreamLoader() { Reset(); }

  void Reset() {
    for (CDCStreamInfo* stream : streams) {
      stream->Release();
    }
    streams.clear();
  }

  Status Visit(const std::string& stream_id, const SysCDCStreamEntryPB& metadata) override {
    // Setup the CDC stream info.
    CDCStreamInfo* const stream =
        new CDCStreamInfo(VERIFY_RESULT(xrepl::StreamId::FromString(stream_id)));
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    stream->AddRef();
    streams.push_back(stream);
    return Status::OK();
  }

  vector<CDCStreamInfo*> streams;
};

class TestUniverseReplicationLoader : public Visitor<PersistentUniverseReplicationInfo> {
 public:
  TestUniverseReplicationLoader() {}
  ~TestUniverseReplicationLoader() { Reset(); }

  void Reset() {
    for (UniverseReplicationInfo* universe : universes) {
      universe->Release();
    }
    universes.clear();
  }

  Status Visit(
      const std::string& replication_group_id,
      const SysUniverseReplicationEntryPB& metadata) override {
    // Setup the universe replication info.
    UniverseReplicationInfo* const universe =
        new UniverseReplicationInfo(xcluster::ReplicationGroupId(replication_group_id));
    auto l = universe->LockForWrite();
    l.mutable_data()->pb.CopyFrom(metadata);
    l.Commit();
    universe->AddRef();
    universes.push_back(universe);
    return Status::OK();
  }

  vector<UniverseReplicationInfo*> universes;
};

// Test the sys-catalog CDC stream basic operations (add, delete, visit).
TEST_F(SysCatalogTest, TestSysCatalogCDCStreamOperations) {
  SysCatalogTable* const sys_catalog = &master_->sys_catalog();

  auto loader = std::make_unique<TestCDCStreamLoader>();
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  // 1. CHECK ADD_CDCSTREAM.
  auto stream = make_scoped_refptr<CDCStreamInfo>(xrepl::StreamId::GenerateRandom());
  {
    auto l = stream->LockForWrite();
    l.mutable_data()->pb.add_table_id("test_table");
    // Add the CDC stream.
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, stream));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1, loader->streams.size());
  ASSERT_METADATA_EQ(stream.get(), loader->streams[0]);

  // 2. CHECK DELETE_CDCSTREAM.
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, stream));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(0, loader->streams.size());
}

// Test the sys-catalog universe replication basic operations (add, delete, visit).
TEST_F(SysCatalogTest, TestSysCatalogUniverseReplicationOperations) {
  SysCatalogTable* const sys_catalog = &master_->sys_catalog();

  auto loader = std::make_unique<TestUniverseReplicationLoader>();
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  // 1. CHECK ADD_UNIVERSE_REPLICATION.
  auto universe = make_scoped_refptr<UniverseReplicationInfo>(
      xcluster::ReplicationGroupId("deadbeafdeadbeafdeadbeafdeadbeaf"));
  {
    auto l = universe->LockForWrite();
    l.mutable_data()->pb.add_tables("producer_table_id");
    // Add the universe replication info.
    ASSERT_OK(sys_catalog->Upsert(kLeaderTerm, universe));
    l.Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1, loader->universes.size());
  ASSERT_METADATA_EQ(universe.get(), loader->universes[0]);

  // 2. CHECK DELETE_UNIVERSE_REPLICATION.
  ASSERT_OK(sys_catalog->Delete(kLeaderTerm, universe));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(0, loader->universes.size());
}

} // namespace master
} // namespace yb
