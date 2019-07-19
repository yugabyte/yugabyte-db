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

#include "yb/master/sys_catalog-test_base.h"

namespace yb {
namespace master {
namespace enterprise {

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

  Status Visit(const CDCStreamId& stream_id, const SysCDCStreamEntryPB& metadata) override {
    // Setup the CDC stream info.
    CDCStreamInfo* const stream = new CDCStreamInfo(stream_id);
    auto l = stream->LockForWrite();
    l->mutable_data()->pb.CopyFrom(metadata);
    l->Commit();
    stream->AddRef();
    streams.push_back(stream);
    return Status::OK();
  }

  vector<CDCStreamInfo*> streams;
};

// Test the sys-catalog CDC stream basic operations (add, delete, visit).
TEST_F(SysCatalogTest, TestSysCatalogCDCStreamOperations) {
  SysCatalogTable* const sys_catalog = master_->catalog_manager()->sys_catalog();

  std::unique_ptr<TestCDCStreamLoader> loader(new TestCDCStreamLoader());
  ASSERT_OK(sys_catalog->Visit(loader.get()));

  // 1. CHECK ADD_CDCSTREAM.
  scoped_refptr<CDCStreamInfo> stream(new CDCStreamInfo("deadbeafdeadbeafdeadbeafdeadbeaf"));
  {
    auto l = stream->LockForWrite();
    l->mutable_data()->pb.set_table_id("test_table");
    // Add the CDC stream.
    ASSERT_OK(sys_catalog->AddItem(stream.get(), kLeaderTerm));
    l->Commit();
  }

  // Verify it showed up.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(1, loader->streams.size());
  ASSERT_TRUE(MetadatasEqual(stream.get(), loader->streams[0]));

  // 2. CHECK DELETE_CDCSTREAM.
  ASSERT_OK(sys_catalog->DeleteItem(stream.get(), kLeaderTerm));

  // Verify the result.
  loader->Reset();
  ASSERT_OK(sys_catalog->Visit(loader.get()));
  ASSERT_EQ(0, loader->streams.size());
}

} // namespace enterprise
} // namespace master
} // namespace yb
