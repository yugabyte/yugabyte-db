//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "yb/rocksdb/db.h"

#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/column_family.h"
#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/utilities/write_batch_with_index.h"
#include "yb/rocksdb/table/scoped_arena_iterator.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/util/string_util.h"
#include "yb/util/test_macros.h"
#include "yb/rocksdb/util/testutil.h"

namespace rocksdb {

static std::string PrintContents(WriteBatch* b) {
  InternalKeyComparator cmp(BytewiseComparator());
  auto factory = std::make_shared<SkipListFactory>();
  Options options;
  options.memtable_factory = factory;
  ImmutableCFOptions ioptions(options);
  WriteBuffer wb(options.db_write_buffer_size);
  MemTable* mem =
      new MemTable(cmp, ioptions, MutableCFOptions(options, ioptions), &wb,
                   kMaxSequenceNumber);
  mem->Ref();
  std::string state;
  ColumnFamilyMemTablesDefault cf_mems_default(mem);
  Status s = WriteBatchInternal::InsertInto(b, &cf_mems_default, nullptr);
  size_t count = 0;
  int put_count = 0;
  int delete_count = 0;
  int single_delete_count = 0;
  int merge_count = 0;
  Arena arena;
  ScopedArenaIterator iter(mem->NewIterator(ReadOptions(), &arena));
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    ParsedInternalKey ikey;
    memset(static_cast<void*>(&ikey), 0, sizeof(ikey));
    EXPECT_TRUE(ParseInternalKey(iter->key(), &ikey));
    switch (ikey.type) {
      case kTypeValue:
        state.append("Put(");
        state.append(ikey.user_key.ToString());
        state.append(", ");
        state.append(iter->value().ToString());
        state.append(")");
        count++;
        put_count++;
        break;
      case kTypeDeletion:
        state.append("Delete(");
        state.append(ikey.user_key.ToString());
        state.append(")");
        count++;
        delete_count++;
        break;
      case kTypeSingleDeletion:
        state.append("SingleDelete(");
        state.append(ikey.user_key.ToString());
        state.append(")");
        count++;
        single_delete_count++;
        break;
      case kTypeMerge:
        state.append("Merge(");
        state.append(ikey.user_key.ToString());
        state.append(", ");
        state.append(iter->value().ToString());
        state.append(")");
        count++;
        merge_count++;
        break;
      default:
        assert(false);
        break;
    }
    state.append("@");
    state.append(NumberToString(ikey.sequence));
  }
  EXPECT_EQ(b->HasPut(), put_count > 0);
  EXPECT_EQ(b->HasDelete(), delete_count > 0);
  EXPECT_EQ(b->HasSingleDelete(), single_delete_count > 0);
  EXPECT_EQ(b->HasMerge(), merge_count > 0);
  if (!s.ok()) {
    state.append(s.ToString(false));
  } else if (count != WriteBatchInternal::Count(b)) {
    state.append("CountMismatch()");
  }
  delete mem->Unref();
  return state;
}

class WriteBatchTest : public RocksDBTest {};

TEST_F(WriteBatchTest, Empty) {
  WriteBatch batch;
  ASSERT_EQ("", PrintContents(&batch));
  ASSERT_EQ(0, WriteBatchInternal::Count(&batch));
  ASSERT_EQ(0, batch.Count());
}

TEST_F(WriteBatchTest, Multiple) {
  WriteBatch batch;
  batch.Put(Slice("foo"), Slice("bar"));
  batch.Delete(Slice("box"));
  batch.Put(Slice("baz"), Slice("boo"));
  WriteBatchInternal::SetSequence(&batch, 100);
  ASSERT_EQ(100U, WriteBatchInternal::Sequence(&batch));
  ASSERT_EQ(3, WriteBatchInternal::Count(&batch));
  ASSERT_EQ("Put(baz, boo)@102"
            "Delete(box)@101"
            "Put(foo, bar)@100",
            PrintContents(&batch));
  ASSERT_EQ(3, batch.Count());
}

TEST_F(WriteBatchTest, Corruption) {
  WriteBatch batch;
  batch.Put(Slice("foo"), Slice("bar"));
  batch.Delete(Slice("box"));
  WriteBatchInternal::SetSequence(&batch, 200);
  Slice contents = WriteBatchInternal::Contents(&batch);
  WriteBatchInternal::SetContents(&batch,
                                  Slice(contents.data(), contents.size() - 1));
  ASSERT_EQ("Put(foo, bar)@200"
            "Corruption: bad WriteBatch Delete",
            PrintContents(&batch));
}

TEST_F(WriteBatchTest, Append) {
  WriteBatch b1, b2;
  WriteBatchInternal::SetSequence(&b1, 200);
  WriteBatchInternal::SetSequence(&b2, 300);
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ("",
            PrintContents(&b1));
  ASSERT_EQ(0, b1.Count());
  b2.Put("a", "va");
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ("Put(a, va)@200",
            PrintContents(&b1));
  ASSERT_EQ(1, b1.Count());
  b2.Clear();
  b2.Put("b", "vb");
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ("Put(a, va)@200"
            "Put(b, vb)@201",
            PrintContents(&b1));
  ASSERT_EQ(2, b1.Count());
  b2.Delete("foo");
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ("Put(a, va)@200"
            "Put(b, vb)@202"
            "Put(b, vb)@201"
            "Delete(foo)@203",
            PrintContents(&b1));
  ASSERT_EQ(4, b1.Count());
}

TEST_F(WriteBatchTest, SingleDeletion) {
  WriteBatch batch;
  WriteBatchInternal::SetSequence(&batch, 100);
  ASSERT_EQ("", PrintContents(&batch));
  ASSERT_EQ(0, batch.Count());
  batch.Put("a", "va");
  ASSERT_EQ("Put(a, va)@100", PrintContents(&batch));
  ASSERT_EQ(1, batch.Count());
  batch.SingleDelete("a");
  ASSERT_EQ(
      "SingleDelete(a)@101"
      "Put(a, va)@100",
      PrintContents(&batch));
  ASSERT_EQ(2, batch.Count());
}

namespace {

struct TestHandler : public WriteBatch::Handler {
  std::string seen;
  virtual Status PutCF(uint32_t column_family_id, const SliceParts& key,
                       const SliceParts& value) override {
    if (column_family_id == 0) {
      seen += "Put(" + key.TheOnlyPart().ToDebugString() + ", " +
              value.TheOnlyPart().ToDebugString() + ")";
    } else {
      seen += "PutCF(" + ToString(column_family_id) + ", " +
              key.TheOnlyPart().ToDebugString() + ", " + value.TheOnlyPart().ToDebugString() + ")";
    }
    return Status::OK();
  }
  virtual Status DeleteCF(uint32_t column_family_id,
                          const Slice& key) override {
    if (column_family_id == 0) {
      seen += "Delete(" + key.ToDebugString() + ")";
    } else {
      seen += "DeleteCF(" + ToString(column_family_id) + ", " +
              key.ToDebugString() + ")";
    }
    return Status::OK();
  }
  virtual Status SingleDeleteCF(uint32_t column_family_id,
                                const Slice& key) override {
    if (column_family_id == 0) {
      seen += "SingleDelete(" + key.ToDebugString() + ")";
    } else {
      seen += "SingleDeleteCF(" + ToString(column_family_id) + ", " +
              key.ToDebugString() + ")";
    }
    return Status::OK();
  }
  virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                         const Slice& value) override {
    if (column_family_id == 0) {
      seen += "Merge(" + key.ToDebugString() + ", " + value.ToDebugString() + ")";
    } else {
      seen += "MergeCF(" + ToString(column_family_id) + ", " +
              key.ToDebugString() + ", " + value.ToDebugString() + ")";
    }
    return Status::OK();
  }
  void LogData(const Slice& blob) override {
    seen += "LogData(" + blob.ToDebugString() + ")";
  }
};

} // namespace

TEST_F(WriteBatchTest, PutNotImplemented) {
  WriteBatch batch;
  batch.Put(Slice("k1"), Slice("v1"));
  ASSERT_EQ(1, batch.Count());
  ASSERT_EQ("Put(k1, v1)@0", PrintContents(&batch));

  WriteBatch::Handler handler;
  ASSERT_OK(batch.Iterate(&handler));
}

TEST_F(WriteBatchTest, DeleteNotImplemented) {
  WriteBatch batch;
  batch.Delete(Slice("k2"));
  ASSERT_EQ(1, batch.Count());
  ASSERT_EQ("Delete(k2)@0", PrintContents(&batch));

  WriteBatch::Handler handler;
  ASSERT_OK(batch.Iterate(&handler));
}

TEST_F(WriteBatchTest, SingleDeleteNotImplemented) {
  WriteBatch batch;
  batch.SingleDelete(Slice("k2"));
  ASSERT_EQ(1, batch.Count());
  ASSERT_EQ("SingleDelete(k2)@0", PrintContents(&batch));

  WriteBatch::Handler handler;
  ASSERT_OK(batch.Iterate(&handler));
}

TEST_F(WriteBatchTest, MergeNotImplemented) {
  WriteBatch batch;
  batch.Merge(Slice("foo"), Slice("bar"));
  ASSERT_EQ(1, batch.Count());
  ASSERT_EQ("Merge(foo, bar)@0", PrintContents(&batch));

  WriteBatch::Handler handler;
  ASSERT_OK(batch.Iterate(&handler));
}

TEST_F(WriteBatchTest, Blob) {
  WriteBatch batch;
  batch.Put(Slice("k1"), Slice("v1"));
  batch.Put(Slice("k2"), Slice("v2"));
  batch.Put(Slice("k3"), Slice("v3"));
  batch.PutLogData(Slice("blob1"));
  batch.Delete(Slice("k2"));
  batch.SingleDelete(Slice("k3"));
  batch.PutLogData(Slice("blob2"));
  batch.Merge(Slice("foo"), Slice("bar"));
  ASSERT_EQ(6, batch.Count());
  ASSERT_EQ(
      "Merge(foo, bar)@5"
      "Put(k1, v1)@0"
      "Delete(k2)@3"
      "Put(k2, v2)@1"
      "SingleDelete(k3)@4"
      "Put(k3, v3)@2",
      PrintContents(&batch));

  TestHandler handler;
  ASSERT_OK(batch.Iterate(&handler));
  ASSERT_EQ(
      "Put(k1, v1)"
      "Put(k2, v2)"
      "Put(k3, v3)"
      "LogData(blob1)"
      "Delete(k2)"
      "SingleDelete(k3)"
      "LogData(blob2)"
      "Merge(foo, bar)",
      handler.seen);
}

// It requires more than 30GB of memory to run the test. With single memory
// allocation of more than 30GB.
// Not all platform can run it. Also it runs a long time. So disable it.
TEST_F(WriteBatchTest, DISABLED_ManyUpdates) {
  // Insert key and value of 3GB and push total batch size to 12GB.
  static const size_t kKeyValueSize = 4u;
  static const uint32_t kNumUpdates = 3 << 30;
  std::string raw(kKeyValueSize, 'A');
  WriteBatch batch(kNumUpdates * (4 + kKeyValueSize * 2) + 1024u);
  char c = 'A';
  for (uint32_t i = 0; i < kNumUpdates; i++) {
    if (c > 'Z') {
      c = 'A';
    }
    raw[0] = c;
    raw[raw.length() - 1] = c;
    c++;
    batch.Put(raw, raw);
  }

  ASSERT_EQ(kNumUpdates, batch.Count());

  struct NoopHandler : public WriteBatch::Handler {
    uint32_t num_seen = 0;
    char expected_char = 'A';
    virtual Status PutCF(uint32_t column_family_id, const SliceParts& key,
                         const SliceParts& value) override {
      EXPECT_EQ(kKeyValueSize, key.TheOnlyPart().size());
      EXPECT_EQ(kKeyValueSize, value.TheOnlyPart().size());
      EXPECT_EQ(expected_char, key.TheOnlyPart()[0]);
      EXPECT_EQ(expected_char, value.TheOnlyPart()[0]);
      EXPECT_EQ(expected_char, key.TheOnlyPart()[kKeyValueSize - 1]);
      EXPECT_EQ(expected_char, value.TheOnlyPart()[kKeyValueSize - 1]);
      expected_char++;
      if (expected_char > 'Z') {
        expected_char = 'A';
      }
      ++num_seen;
      return Status::OK();
    }
    virtual Status DeleteCF(uint32_t column_family_id,
                            const Slice& key) override {
      EXPECT_TRUE(false);
      return Status::OK();
    }
    virtual Status SingleDeleteCF(uint32_t column_family_id,
                                  const Slice& key) override {
      EXPECT_TRUE(false);
      return Status::OK();
    }
    virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                           const Slice& value) override {
      EXPECT_TRUE(false);
      return Status::OK();
    }
    void LogData(const Slice& blob) override { EXPECT_TRUE(false); }
    bool Continue() override { return num_seen < kNumUpdates; }
  } handler;

  ASSERT_OK(batch.Iterate(&handler));
  ASSERT_EQ(kNumUpdates, handler.num_seen);
}

// The test requires more than 18GB memory to run it, with single memory
// allocation of more than 12GB. Not all the platform can run it. So disable it.
TEST_F(WriteBatchTest, DISABLED_LargeKeyValue) {
  // Insert key and value of 3GB and push total batch size to 12GB.
  static const size_t kKeyValueSize = 3221225472u;
  std::string raw(kKeyValueSize, 'A');
  WriteBatch batch(12884901888u + 1024u);
  for (char i = 0; i < 2; i++) {
    raw[0] = 'A' + i;
    raw[raw.length() - 1] = 'A' - i;
    batch.Put(raw, raw);
  }

  ASSERT_EQ(2, batch.Count());

  struct NoopHandler : public WriteBatch::Handler {
    int num_seen = 0;
    virtual Status PutCF(uint32_t column_family_id, const SliceParts& key,
                         const SliceParts& value) override {
      EXPECT_EQ(kKeyValueSize, key.TheOnlyPart().size());
      EXPECT_EQ(kKeyValueSize, value.TheOnlyPart().size());
      EXPECT_EQ('A' + num_seen, key.TheOnlyPart()[0]);
      EXPECT_EQ('A' + num_seen, value.TheOnlyPart()[0]);
      EXPECT_EQ('A' - num_seen, key.TheOnlyPart()[kKeyValueSize - 1]);
      EXPECT_EQ('A' - num_seen, value.TheOnlyPart()[kKeyValueSize - 1]);
      ++num_seen;
      return Status::OK();
    }
    virtual Status DeleteCF(uint32_t column_family_id,
                            const Slice& key) override {
      EXPECT_TRUE(false);
      return Status::OK();
    }
    virtual Status SingleDeleteCF(uint32_t column_family_id,
                                  const Slice& key) override {
      EXPECT_TRUE(false);
      return Status::OK();
    }
    virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                           const Slice& value) override {
      EXPECT_TRUE(false);
      return Status::OK();
    }
    void LogData(const Slice& blob) override { EXPECT_TRUE(false); }
    bool Continue() override { return num_seen < 2; }
  } handler;

  ASSERT_OK(batch.Iterate(&handler));
  ASSERT_EQ(2, handler.num_seen);
}

TEST_F(WriteBatchTest, Continue) {
  WriteBatch batch;

  struct Handler : public TestHandler {
    int num_seen = 0;
    virtual Status PutCF(uint32_t column_family_id, const SliceParts& key,
                         const SliceParts& value) override {
      ++num_seen;
      return TestHandler::PutCF(column_family_id, key, value);
    }
    virtual Status DeleteCF(uint32_t column_family_id,
                            const Slice& key) override {
      ++num_seen;
      return TestHandler::DeleteCF(column_family_id, key);
    }
    virtual Status SingleDeleteCF(uint32_t column_family_id,
                                  const Slice& key) override {
      ++num_seen;
      return TestHandler::SingleDeleteCF(column_family_id, key);
    }
    virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                           const Slice& value) override {
      ++num_seen;
      return TestHandler::MergeCF(column_family_id, key, value);
    }
    void LogData(const Slice& blob) override {
      ++num_seen;
      TestHandler::LogData(blob);
    }
    bool Continue() override { return num_seen < 5; }
  } handler;

  batch.Put(Slice("k1"), Slice("v1"));
  batch.Put(Slice("k2"), Slice("v2"));
  batch.PutLogData(Slice("blob1"));
  batch.Delete(Slice("k1"));
  batch.SingleDelete(Slice("k2"));
  batch.PutLogData(Slice("blob2"));
  batch.Merge(Slice("foo"), Slice("bar"));
  auto status = batch.Iterate(&handler);
  LOG(INFO) << "Iterate result: " << status;
  ASSERT_EQ(
      "Put(k1, v1)"
      "Put(k2, v2)"
      "LogData(blob1)"
      "Delete(k1)"
      "SingleDelete(k2)",
      handler.seen);
}

TEST_F(WriteBatchTest, PutGatherSlices) {
  WriteBatch batch;
  batch.Put(Slice("foo"), Slice("bar"));

  {
    // Try a write where the key is one slice but the value is two
    Slice key_slice("baz");
    Slice value_slices[2] = { Slice("header"), Slice("payload") };
    batch.Put(SliceParts(&key_slice, 1),
              SliceParts(value_slices, 2));
  }

  {
    // One where the key is composite but the value is a single slice
    Slice key_slices[3] = { Slice("key"), Slice("part2"), Slice("part3") };
    Slice value_slice("value");
    batch.Put(SliceParts(key_slices, 3),
              SliceParts(&value_slice, 1));
  }

  WriteBatchInternal::SetSequence(&batch, 100);
  ASSERT_EQ("Put(baz, headerpayload)@101"
            "Put(foo, bar)@100"
            "Put(keypart2part3, value)@102",
            PrintContents(&batch));
  ASSERT_EQ(3, batch.Count());
}

namespace {
class ColumnFamilyHandleImplDummy : public ColumnFamilyHandleImpl {
 public:
  explicit ColumnFamilyHandleImplDummy(int id)
      : ColumnFamilyHandleImpl(nullptr, nullptr, nullptr), id_(id) {}
  uint32_t GetID() const override { return id_; }
  const Comparator* user_comparator() const override {
    return BytewiseComparator();
  }

 private:
  uint32_t id_;
};

}  // anonymous namespace

TEST_F(WriteBatchTest, ColumnFamiliesBatchTest) {
  WriteBatch batch;
  ColumnFamilyHandleImplDummy zero(0), two(2), three(3), eight(8);
  batch.Put(&zero, Slice("foo"), Slice("bar"));
  batch.Put(&two, Slice("twofoo"), Slice("bar2"));
  batch.Put(&eight, Slice("eightfoo"), Slice("bar8"));
  batch.Delete(&eight, Slice("eightfoo"));
  batch.SingleDelete(&two, Slice("twofoo"));
  batch.Merge(&three, Slice("threethree"), Slice("3three"));
  batch.Put(&zero, Slice("foo"), Slice("bar"));
  batch.Merge(Slice("omom"), Slice("nom"));

  TestHandler handler;
  ASSERT_OK(batch.Iterate(&handler));
  ASSERT_EQ(
      "Put(foo, bar)"
      "PutCF(2, twofoo, bar2)"
      "PutCF(8, eightfoo, bar8)"
      "DeleteCF(8, eightfoo)"
      "SingleDeleteCF(2, twofoo)"
      "MergeCF(3, threethree, 3three)"
      "Put(foo, bar)"
      "Merge(omom, nom)",
      handler.seen);
}

TEST_F(WriteBatchTest, ColumnFamiliesBatchWithIndexTest) {
  WriteBatchWithIndex batch;
  ColumnFamilyHandleImplDummy zero(0), two(2), three(3), eight(8);
  batch.Put(&zero, Slice("foo"), Slice("bar"));
  batch.Put(&two, Slice("twofoo"), Slice("bar2"));
  batch.Put(&eight, Slice("eightfoo"), Slice("bar8"));
  batch.Delete(&eight, Slice("eightfoo"));
  batch.SingleDelete(&two, Slice("twofoo"));
  batch.Merge(&three, Slice("threethree"), Slice("3three"));
  batch.Put(&zero, Slice("foo"), Slice("bar"));
  batch.Merge(Slice("omom"), Slice("nom"));

  std::unique_ptr<WBWIIterator> iter;

  iter.reset(batch.NewIterator(&eight));
  iter->Seek("eightfoo");
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kPutRecord, iter->Entry().type);
  ASSERT_EQ("eightfoo", iter->Entry().key.ToString());
  ASSERT_EQ("bar8", iter->Entry().value.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kDeleteRecord, iter->Entry().type);
  ASSERT_EQ("eightfoo", iter->Entry().key.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(!iter->Valid());

  iter.reset(batch.NewIterator(&two));
  iter->Seek("twofoo");
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kPutRecord, iter->Entry().type);
  ASSERT_EQ("twofoo", iter->Entry().key.ToString());
  ASSERT_EQ("bar2", iter->Entry().value.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kSingleDeleteRecord, iter->Entry().type);
  ASSERT_EQ("twofoo", iter->Entry().key.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(!iter->Valid());

  iter.reset(batch.NewIterator());
  iter->Seek("gggg");
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kMergeRecord, iter->Entry().type);
  ASSERT_EQ("omom", iter->Entry().key.ToString());
  ASSERT_EQ("nom", iter->Entry().value.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(!iter->Valid());

  iter.reset(batch.NewIterator(&zero));
  iter->Seek("foo");
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kPutRecord, iter->Entry().type);
  ASSERT_EQ("foo", iter->Entry().key.ToString());
  ASSERT_EQ("bar", iter->Entry().value.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kPutRecord, iter->Entry().type);
  ASSERT_EQ("foo", iter->Entry().key.ToString());
  ASSERT_EQ("bar", iter->Entry().value.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ(WriteType::kMergeRecord, iter->Entry().type);
  ASSERT_EQ("omom", iter->Entry().key.ToString());
  ASSERT_EQ("nom", iter->Entry().value.ToString());

  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(!iter->Valid());

  TestHandler handler;
  ASSERT_OK(batch.GetWriteBatch()->Iterate(&handler));
  ASSERT_EQ(
      "Put(foo, bar)"
      "PutCF(2, twofoo, bar2)"
      "PutCF(8, eightfoo, bar8)"
      "DeleteCF(8, eightfoo)"
      "SingleDeleteCF(2, twofoo)"
      "MergeCF(3, threethree, 3three)"
      "Put(foo, bar)"
      "Merge(omom, nom)",
      handler.seen);
}

TEST_F(WriteBatchTest, SavePointTest) {
  Status s;
  WriteBatch batch;
  batch.SetSavePoint();

  batch.Put("A", "a");
  batch.Put("B", "b");
  batch.SetSavePoint();

  batch.Put("C", "c");
  batch.Delete("A");
  batch.SetSavePoint();
  batch.SetSavePoint();

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "Delete(A)@3"
      "Put(A, a)@0"
      "Put(B, b)@1"
      "Put(C, c)@2",
      PrintContents(&batch));

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "Put(A, a)@0"
      "Put(B, b)@1",
      PrintContents(&batch));

  batch.Delete("A");
  batch.Put("B", "bb");

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ("", PrintContents(&batch));

  s = batch.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", PrintContents(&batch));

  batch.Put("D", "d");
  batch.Delete("A");

  batch.SetSavePoint();

  batch.Put("A", "aaa");

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "Delete(A)@1"
      "Put(D, d)@0",
      PrintContents(&batch));

  batch.SetSavePoint();

  batch.Put("D", "d");
  batch.Delete("A");

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "Delete(A)@1"
      "Put(D, d)@0",
      PrintContents(&batch));

  s = batch.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ(
      "Delete(A)@1"
      "Put(D, d)@0",
      PrintContents(&batch));

  WriteBatch batch2;

  s = batch2.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", PrintContents(&batch2));

  batch2.Delete("A");
  batch2.SetSavePoint();

  s = batch2.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("Delete(A)@0", PrintContents(&batch2));

  batch2.Clear();
  ASSERT_EQ("", PrintContents(&batch2));

  batch2.SetSavePoint();

  batch2.Delete("B");
  ASSERT_EQ("Delete(B)@0", PrintContents(&batch2));

  batch2.SetSavePoint();
  s = batch2.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("Delete(B)@0", PrintContents(&batch2));

  s = batch2.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("", PrintContents(&batch2));

  s = batch2.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", PrintContents(&batch2));
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
