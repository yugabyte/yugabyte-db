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

#include <memory>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/testutil.h"

#include "yb/util/test_macros.h"

using std::string;

namespace rocksdb {

namespace {

struct UserOpIdTestHandler : public WriteBatch::Handler {
  Status PutCF(
      uint32_t column_family_id,
      const SliceParts& key,
      const SliceParts& value) override {
    StartOutputLine(__FUNCTION__);
    OutputField("key", key.TheOnlyPart());
    OutputField("value", value.TheOnlyPart());
    FinishOutputLine();
    return Status::OK();
  }

  Status DeleteCF(
      uint32_t column_family_id,
      const Slice& key) override {
    StartOutputLine(__FUNCTION__);
    OutputField("key", key);
    FinishOutputLine();
    return Status::OK();
  }

  Status SingleDeleteCF(
      uint32_t column_family_id,
      const Slice& key) override {
    StartOutputLine(__FUNCTION__);
    OutputField("key", key);
    FinishOutputLine();
    return Status::OK();
  }

  Status MergeCF(
      uint32_t column_family_id,
      const Slice& key,
      const Slice& value) override {
    StartOutputLine(__FUNCTION__);
    OutputField("key", key);
    OutputField("value", value);
    FinishOutputLine();
    return Status::OK();
  }

  Status Frontiers(const UserFrontiers& frontier) override {
    out_ << "frontiers=" << frontier.ToString() << std::endl;
    return Status::OK();
  }

  std::string str() const {
    return out_.str();
  }

 private:
  void StartOutputLine(const char* name) {
    out_ << name << "(";
    need_separator_ = false;
  }
  void OutputField(const char* field_name, const Slice& value) {
    if (need_separator_) {
      out_ << ", ";
    }
    need_separator_ = true,
    out_ << field_name << "='" << value.ToString() << "'";
  }
  void FinishOutputLine() {
    out_ << ")" << std::endl;
  }

  std::stringstream out_;
  bool need_separator_ = false;
};

std::string WriteBatchToString(const WriteBatch& b) {
  UserOpIdTestHandler handler;
  EXPECT_OK(b.Iterate(&handler));
  return handler.str();
}

} // namespace

class UserOpIdTest : public RocksDBTest {
 protected:
  WriteBatch CreateDummyWriteBatch() {
    WriteBatch b;
    b.SetFrontiers(&frontiers_);
    b.Put("A", "B");
    b.Delete("C");
    return b;
  }

  test::TestUserFrontiers frontiers_{1, 123};
};

TEST_F(UserOpIdTest, Empty) {
  WriteBatch batch;
  ASSERT_EQ(0, WriteBatchInternal::Count(&batch));
  ASSERT_EQ(0, batch.Count());
}


TEST_F(UserOpIdTest, Append) {
  WriteBatch b1, b2;
  WriteBatchInternal::SetSequence(&b1, 200);
  WriteBatchInternal::SetSequence(&b2, 300);
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ(0, b1.Count());
  b2.Put("a", "va");
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ(1, b1.Count());
  b2.Clear();
  b2.Put("b", "vb");
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ(2, b1.Count());
  b2.Delete("foo");
  WriteBatchInternal::Append(&b1, &b2);
  ASSERT_EQ(4, b1.Count());
}

TEST_F(UserOpIdTest, SetUserSequenceNumber) {
  WriteBatch b;

  ASSERT_FALSE(b.Frontiers());
  test::TestUserFrontiers range(1, 77701);
  b.SetFrontiers(&range);
  b.Put("k1", "v1");
  ASSERT_FALSE(!b.Frontiers());

  b.Put("k2", "v2");

  b.Delete("k3");

  b.Merge("k4", "v4");

  ASSERT_FALSE(!b.Frontiers());

  ASSERT_EQ(
    "frontiers={ smallest: { value: 1 } largest: { value: 77701 } }\n"
    "PutCF(key='k1', value='v1')\n"
    "PutCF(key='k2', value='v2')\n"
    "DeleteCF(key='k3')\n"
    "MergeCF(key='k4', value='v4')\n",
    WriteBatchToString(b));
}

TEST_F(UserOpIdTest, AppendBatchesWithUserSequenceNumbers) {
  WriteBatch dst;
  test::TestUserFrontiers range(1, 1200);
  dst.SetFrontiers(&range);
  dst.Put("my_key", "my_value");

  dst.Merge("my_merge_key", "my_merge_value");

  WriteBatch src;
  src.Delete("my_key");

  WriteBatchInternal::Append(&dst, &src);
  ASSERT_EQ(
      "frontiers={ smallest: { value: 1 } largest: { value: 1200 } }\n"
      "PutCF(key='my_key', value='my_value')\n"
      "MergeCF(key='my_merge_key', value='my_merge_value')\n"
      "DeleteCF(key='my_key')\n",
      WriteBatchToString(dst));
}

// This is based on WriteBatchTest.SavePointsTest
TEST_F(UserOpIdTest, SavePointTest) {
  Status s;
  WriteBatch batch;
  batch.SetSavePoint();

  test::TestUserFrontiers range(1, 1000);
  batch.SetFrontiers(&range);
  batch.Put("A", "a");
  batch.Put("B", "b");
  batch.SetSavePoint();

  batch.Put("C", "c");
  batch.Delete("A");
  batch.SetSavePoint();
  batch.SetSavePoint();

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "frontiers={ smallest: { value: 1 } largest: { value: 1000 } }\n"
      "PutCF(key='A', value='a')\n"
      "PutCF(key='B', value='b')\n"
      "PutCF(key='C', value='c')\n"
      "DeleteCF(key='A')\n",
      WriteBatchToString(batch));
  ASSERT_FALSE(!batch.Frontiers());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_FALSE(!batch.Frontiers());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "frontiers={ smallest: { value: 1 } largest: { value: 1000 } }\n"
      "PutCF(key='A', value='a')\n"
      "PutCF(key='B', value='b')\n",
      WriteBatchToString(batch));
  ASSERT_FALSE(!batch.Frontiers());

  batch.Delete("A");
  batch.Put("B", "bb");
  ASSERT_FALSE(!batch.Frontiers());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ("", WriteBatchToString(batch));
  ASSERT_FALSE(batch.Frontiers());

  s = batch.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", WriteBatchToString(batch));

  test::TestUserFrontiers range2(1, 1001);
  batch.SetFrontiers(&range2);
  batch.Put("D", "d");
  batch.Delete("A");

  batch.SetSavePoint();

  batch.Put("A", "aaa");

  ASSERT_EQ(range2, *batch.Frontiers());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(range2, *batch.Frontiers());

  ASSERT_EQ(
      "frontiers={ smallest: { value: 1 } largest: { value: 1001 } }\n"
      "PutCF(key='D', value='d')\n"
      "DeleteCF(key='A')\n",
      WriteBatchToString(batch));

  batch.SetSavePoint();

  batch.Put("D", "d");
  batch.Delete("A");

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "frontiers={ smallest: { value: 1 } largest: { value: 1001 } }\n"
      "PutCF(key='D', value='d')\n"
      "DeleteCF(key='A')\n",
      WriteBatchToString(batch));

  s = batch.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ(
      "frontiers={ smallest: { value: 1 } largest: { value: 1001 } }\n"
      "PutCF(key='D', value='d')\n"
      "DeleteCF(key='A')\n",
      WriteBatchToString(batch));
}

TEST_F(UserOpIdTest, SavePointTest2) {
  WriteBatch b;

  Status s = b.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", WriteBatchToString(b));

  test::TestUserFrontiers range2(1, 1002);
  b.SetFrontiers(&range2);
  b.Delete("A");
  b.SetSavePoint();

  s = b.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("frontiers={ smallest: { value: 1 } largest: { value: 1002 } }\nDeleteCF(key='A')\n",
            WriteBatchToString(b));

  b.Clear();
  ASSERT_EQ("", WriteBatchToString(b));

  b.SetSavePoint();

  test::TestUserFrontiers range3(1, 1003);
  b.SetFrontiers(&range3);
  b.Delete("B");
  ASSERT_EQ("frontiers={ smallest: { value: 1 } largest: { value: 1003 } }\nDeleteCF(key='B')\n",
            WriteBatchToString(b));

  b.SetSavePoint();
  s = b.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("frontiers={ smallest: { value: 1 } largest: { value: 1003 } }\nDeleteCF(key='B')\n",
            WriteBatchToString(b));

  s = b.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("", WriteBatchToString(b));

  s = b.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", WriteBatchToString(b));
}

TEST_F(UserOpIdTest, CopyConstructorAndAssignmentOperator) {
  WriteBatch b = CreateDummyWriteBatch();
  WriteBatch b_copy(b);
  WriteBatch b_assigned = b;
  auto expected_str =
      "frontiers={ smallest: { value: 1 } largest: { value: 123 } }\n"
      "PutCF(key='A', value='B')\n"
      "DeleteCF(key='C')\n";
  ASSERT_EQ(expected_str, WriteBatchToString(b_copy));
  ASSERT_EQ(expected_str, WriteBatchToString(b_assigned));
}

TEST_F(UserOpIdTest, MoveConstructor) {
  auto temp = CreateDummyWriteBatch();
  WriteBatch b_moved(std::move(temp));
  temp = CreateDummyWriteBatch();
  WriteBatch b_move_assigned = std::move(temp);
  auto expected_str =
      "frontiers={ smallest: { value: 1 } largest: { value: 123 } }\n"
      "PutCF(key='A', value='B')\n"
      "DeleteCF(key='C')\n";
  ASSERT_EQ(expected_str, WriteBatchToString(b_moved));
  ASSERT_EQ(expected_str, WriteBatchToString(b_move_assigned));
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
