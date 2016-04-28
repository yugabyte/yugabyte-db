// Copyright (c) YugaByte, Inc.

#include "rocksdb/db.h"

#include <memory>
#include <sstream>

#include "db/memtable.h"
#include "db/column_family.h"
#include "db/write_batch_internal.h"
#include "db/writebuffer.h"
#include "rocksdb/env.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/utilities/write_batch_with_index.h"
#include "table/scoped_arena_iterator.h"
#include "util/logging.h"
#include "util/string_util.h"
#include "util/testharness.h"

using std::string;

namespace rocksdb {

namespace {
  struct UserSeqNumTestHandler : public WriteBatch::Handler {
    std::stringstream out_;
    SequenceNumber user_sequence_number_;

    virtual Status PutCF(
        uint32_t column_family_id,
        const Slice& key,
        const Slice& value) override {
      StartOutputLine(__FUNCTION__);
      OutputField("key", key);
      OutputField("value", value);
      FinishOutputLine();
      return Status::OK();
    }

    virtual Status DeleteCF(
        uint32_t column_family_id,
        const Slice& key) override {
      StartOutputLine(__FUNCTION__);
      OutputField("key", key);
      FinishOutputLine();
      return Status::OK();
    }

    virtual Status SingleDeleteCF(
        uint32_t column_family_id,
        const Slice& key) override {
      StartOutputLine(__FUNCTION__);
      OutputField("key", key);
      FinishOutputLine();
      return Status::OK();
    }
    
    virtual Status MergeCF(
        uint32_t column_family_id,
        const Slice& key,
        const Slice& value) override {
      StartOutputLine(__FUNCTION__);
      OutputField("key", key);
      OutputField("value", value);
      FinishOutputLine();
      return Status::OK();
    }

    virtual void SetUserSequenceNumber(SequenceNumber user_sequence_number) override {
      user_sequence_number_ = user_sequence_number;
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
      out_ << "): seq_num=" << user_sequence_number_ << std::endl;
    }

    bool need_separator_;
  };

  static string WriteBatchToString(const WriteBatch& b) {
    auto handler = unique_ptr<UserSeqNumTestHandler>(new UserSeqNumTestHandler());
    b.Iterate(handler.get());
    return handler->out_.str();
  }
}

class UserSequenceNumberTest : public testing::Test {
 protected:
  WriteBatch CreateDummyWriteBatch() {
    WriteBatch b;
    b.AddUserSequenceNumber(123);
    b.Put("A", "B");
    b.AddUserSequenceNumber(124);
    b.Delete("C");
    return b;
  }
};

TEST_F(UserSequenceNumberTest, Empty) {
  WriteBatch batch;
  ASSERT_EQ(0, WriteBatchInternal::Count(&batch));
  ASSERT_EQ(0, batch.Count());
}


TEST_F(UserSequenceNumberTest, Append) {
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

TEST_F(UserSequenceNumberTest, DisallowStartingAddingUserSeqNumbersAfterAddingUpdates) {
  ASSERT_DEATH({
    WriteBatch b;
    b.Put("a", "b");
    // We don't allow specifying user-defined sequence numbers after we've already started adding
    // updates to the write batch.
    b.AddUserSequenceNumber(123);
  }, "Assertion.*Count.*user_sequence_numbers_");
}

TEST_F(UserSequenceNumberTest, SetUserSequenceNumber) {
  WriteBatch b;

  ASSERT_FALSE(b.HasUserSequenceNumbers());
  b.AddUserSequenceNumber(77701);
  b.Put("k1", "v1");
  ASSERT_TRUE(b.HasUserSequenceNumbers());

  b.AddUserSequenceNumber(77702);
  b.Put("k2", "v2");

  b.AddUserSequenceNumber(77703);
  b.Delete("k3");

  b.AddUserSequenceNumber(77704);
  b.Merge("k4", "v4");

  ASSERT_TRUE(b.HasUserSequenceNumbers());

  ASSERT_EQ(
    "PutCF(key='k1', value='v1'): seq_num=77701\n"
    "PutCF(key='k2', value='v2'): seq_num=77702\n"
    "DeleteCF(key='k3'): seq_num=77703\n"
    "MergeCF(key='k4', value='v4'): seq_num=77704\n",
    WriteBatchToString(b));
}

TEST_F(UserSequenceNumberTest, AppendBatchesWithUserSequenceNumbers) {
  WriteBatch dst;
  dst.AddUserSequenceNumber(1200);
  dst.Put("my_key", "my_value");
  ASSERT_OK(dst.ValidateUserSequenceNumbers());

  dst.AddUserSequenceNumber(1201);
  dst.Merge("my_merge_key", "my_merge_value");

  WriteBatch src;
  src.AddUserSequenceNumber(1210);
  src.Delete("my_key");
  ASSERT_OK(src.ValidateUserSequenceNumbers());

  WriteBatchInternal::Append(&dst, &src);
  ASSERT_EQ(
      "PutCF(key='my_key', value='my_value'): seq_num=1200\n"
      "MergeCF(key='my_merge_key', value='my_merge_value'): seq_num=1201\n"
      "DeleteCF(key='my_key'): seq_num=1210\n",
      WriteBatchToString(dst));
  ASSERT_OK(dst.ValidateUserSequenceNumbers());
}

// This is based on WriteBatchTest.SavePointsTest
TEST_F(UserSequenceNumberTest, SavePointTest) {
  Status s;
  WriteBatch batch;
  batch.SetSavePoint();

  batch.AddUserSequenceNumber(1000);
  batch.Put("A", "a");
  batch.AddUserSequenceNumber(1001);
  batch.Put("B", "b");
  batch.SetSavePoint();

  batch.AddUserSequenceNumber(1002);
  batch.Put("C", "c");
  batch.AddUserSequenceNumber(1003);
  batch.Delete("A");
  batch.SetSavePoint();
  batch.SetSavePoint();

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "PutCF(key='A', value='a'): seq_num=1000\n"
      "PutCF(key='B', value='b'): seq_num=1001\n"
      "PutCF(key='C', value='c'): seq_num=1002\n"
      "DeleteCF(key='A'): seq_num=1003\n",
      WriteBatchToString(batch));
  ASSERT_EQ(4, WriteBatchInternal::GetUserSequenceNumbers(batch).size());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(4, WriteBatchInternal::GetUserSequenceNumbers(batch).size());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "PutCF(key='A', value='a'): seq_num=1000\n"
      "PutCF(key='B', value='b'): seq_num=1001\n",
      WriteBatchToString(batch));
  ASSERT_EQ(2, WriteBatchInternal::GetUserSequenceNumbers(batch).size());

  batch.AddUserSequenceNumber(1004);
  batch.Delete("A");
  batch.AddUserSequenceNumber(1005);
  batch.Put("B", "bb");
  ASSERT_EQ(4, WriteBatchInternal::GetUserSequenceNumbers(batch).size());

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ("", WriteBatchToString(batch));
  ASSERT_EQ(0, WriteBatchInternal::GetUserSequenceNumbers(batch).size());

  s = batch.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", WriteBatchToString(batch));

  batch.AddUserSequenceNumber(1006);
  batch.Put("D", "d");
  batch.AddUserSequenceNumber(1007);
  batch.Delete("A");

  batch.SetSavePoint();

  batch.AddUserSequenceNumber(1008);
  batch.Put("A", "aaa");

  ASSERT_EQ(
      "[1006, 1007, 1008]",
      VectorToString(WriteBatchInternal::GetUserSequenceNumbers(batch)));

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "[1006, 1007]",
      VectorToString(WriteBatchInternal::GetUserSequenceNumbers(batch)));

  ASSERT_EQ(
      "PutCF(key='D', value='d'): seq_num=1006\n"
      "DeleteCF(key='A'): seq_num=1007\n",
      WriteBatchToString(batch));

  batch.SetSavePoint();

  batch.AddUserSequenceNumber(1009);
  batch.Put("D", "d");
  batch.AddUserSequenceNumber(1010);
  batch.Delete("A");

  ASSERT_OK(batch.RollbackToSavePoint());
  ASSERT_EQ(
      "PutCF(key='D', value='d'): seq_num=1006\n"
      "DeleteCF(key='A'): seq_num=1007\n",
      WriteBatchToString(batch));

  s = batch.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ(
      "PutCF(key='D', value='d'): seq_num=1006\n"
      "DeleteCF(key='A'): seq_num=1007\n",
      WriteBatchToString(batch));
}

TEST_F(UserSequenceNumberTest, SavePointTest2) {
  WriteBatch b;

  Status s = b.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", WriteBatchToString(b));

  b.AddUserSequenceNumber(1011);
  b.Delete("A");
  b.AddUserSequenceNumber(1012);
  b.SetSavePoint();

  s = b.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("DeleteCF(key='A'): seq_num=1011\n", WriteBatchToString(b));

  b.Clear();
  ASSERT_EQ("", WriteBatchToString(b));

  b.SetSavePoint();

  b.AddUserSequenceNumber(1013);
  b.Delete("B");
  ASSERT_EQ("DeleteCF(key='B'): seq_num=1013\n", WriteBatchToString(b));

  b.SetSavePoint();
  s = b.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("DeleteCF(key='B'): seq_num=1013\n", WriteBatchToString(b));

  s = b.RollbackToSavePoint();
  ASSERT_OK(s);
  ASSERT_EQ("", WriteBatchToString(b));

  s = b.RollbackToSavePoint();
  ASSERT_TRUE(s.IsNotFound());
  ASSERT_EQ("", WriteBatchToString(b));
}

TEST_F(UserSequenceNumberTest, CopyConstructorAndAssignmentOperator) {
  WriteBatch b = CreateDummyWriteBatch();
  WriteBatch b_copy(b);
  WriteBatch b_assigned = b;
  auto expected_str = 
      "PutCF(key='A', value='B'): seq_num=123\n"
      "DeleteCF(key='C'): seq_num=124\n";
  ASSERT_EQ(expected_str, WriteBatchToString(b_copy));
  ASSERT_EQ(expected_str, WriteBatchToString(b_assigned));
}

// On clang we get a -Wpessimizing-move warning (treated as an error in our build configuration)
// when we try to use std::move below.
#ifndef __clang__
TEST_F(UserSequenceNumberTest, MoveConstructor) {
  WriteBatch b_moved(std::move(CreateDummyWriteBatch()));
  WriteBatch b_move_assigned = std::move(CreateDummyWriteBatch());
  auto expected_str = 
      "PutCF(key='A', value='B'): seq_num=123\n"
      "DeleteCF(key='C'): seq_num=124\n";
  ASSERT_EQ(expected_str, WriteBatchToString(b_moved));
  ASSERT_EQ(expected_str, WriteBatchToString(b_move_assigned));
}
#endif

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
