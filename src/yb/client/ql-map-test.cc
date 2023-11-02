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

#include <thread>

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/util/random_util.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using namespace std::literals;

namespace yb {
namespace client {

namespace {

std::string To8LengthHex(uint32_t value) {
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%08x", value);
  return buffer;
}

template <class RequestPB>
void AddHash(int32_t hash_seed, RequestPB* req) {
  QLAddInt32HashValue(req, hash_seed);
  QLAddInt32HashValue(req, ~hash_seed);
  auto s1 = To8LengthHex(hash_seed);
  auto s2 = To8LengthHex(~hash_seed);
  // Need 40 chars long string.
  QLAddStringHashValue(req, s1 + s2 + s1 + s2 + s1);
}

}  // namespace

class QLMapTest : public QLDmlTestBase<MiniCluster> {
 public:
  QLMapTest() {}

  void SetUp() override {
    QLDmlTestBase::SetUp();

    YBSchemaBuilder b;
    b.AddColumn("h1")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    b.AddColumn("h2")->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
    b.AddColumn("h3")->Type(DataType::STRING)->HashPrimaryKey()->NotNull();
    b.AddColumn("r1")->Type(DataType::INT32)->PrimaryKey()->NotNull();
    b.AddColumn("l1")->Type(QLType::CreateTypeMap(DataType::STRING, DataType::STRING));

    ASSERT_OK(table_.Create(kTableName, CalcNumTablets(3), client_.get(), &b));
  }

  std::unique_ptr<qlexpr::QLRowBlock> ReadRows(YBSession* session, int32_t hash_seed) {
    auto op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    AddHash(hash_seed, req);
    table_.AddColumns(table_.AllColumnNames(), req);
    EXPECT_OK(session->TEST_ApplyAndFlush(op));
    EXPECT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);

    return ql::RowsResult(op.get()).GetRowBlock();
  }

  TableHandle table_;
};

TEST_F(QLMapTest, SingleKeyUpdate) {
  auto session = NewSession();
  // First insert a row
  auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* req = op->mutable_request();
  AddHash(1, req);
  QLAddInt32RangeValue(req, 0);
  auto l1 = table_.PrepareColumn(req, "l1")->mutable_map_value();
  for (int i = 1; i <= 3; ++i) {
    l1->add_keys()->set_string_value(std::to_string(i));
    l1->add_values()->set_string_value(std::to_string(i * 10));
  }
  // Map is: 1 => 10, 2 => 20, 3 => 30
  ASSERT_OK(session->TEST_ApplyAndFlush(op));

  auto rowblock = ReadRows(session.get(), 1);
  ASSERT_EQ(1, rowblock->row_count());
  auto& row = rowblock->row(0);
  ASSERT_EQ(0, row.column(3).int32_value());
  auto& map_pb = row.column(4).map_value();
  LOG(INFO) << "Inserted Map: " << map_pb.DebugString();
  ASSERT_EQ(3, map_pb.keys_size());
  ASSERT_EQ(3, map_pb.values_size());
  for (int i = 0; i < map_pb.keys_size(); ++i) {
    ASSERT_EQ(map_pb.keys(i).int32_value() * 10, map_pb.values(i).int32_value());
  }

  // Now perform an update
  op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  req = op->mutable_request();
  AddHash(1, req);
  QLAddInt32RangeValue(req, 0);
  int column_id = table_.ColumnId("l1");
  client::UpdateMapUpsertKeyValue(req, column_id, "1", "100");
  ASSERT_OK(session->TEST_ApplyAndFlush(op));
  // Map should be: 1 => 100, 2 => 20, 3 => 30

  // Check if key value has been updated
  auto new_rowblock = ReadRows(session.get(), 1);
  ASSERT_EQ(1, new_rowblock->row_count());
  auto& new_row = new_rowblock->row(0);
  ASSERT_EQ(0, new_row.column(3).int32_value());
  auto& updated_map_pb = new_row.column(4).map_value();
  LOG(INFO) << "Updated Map: " << updated_map_pb.DebugString();
  ASSERT_EQ(3, updated_map_pb.keys_size());
  ASSERT_EQ(3, updated_map_pb.values_size());
  ASSERT_EQ("100", updated_map_pb.values(0).string_value());
  // Ensure old values are intact
  for (int i = 1; i < updated_map_pb.keys_size(); ++i) {
    ASSERT_EQ(updated_map_pb.keys(i).int32_value() * 10, updated_map_pb.values(i).int32_value());
  }
}

TEST_F(QLMapTest, TwoKeyUpdate) {
  auto session = NewSession();
  // First insert a row
  auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* req = op->mutable_request();
  AddHash(1, req);
  QLAddInt32RangeValue(req, 0);
  auto l1 = table_.PrepareColumn(req, "l1")->mutable_map_value();
  for (int i = 1; i <= 3; ++i) {
    l1->add_keys()->set_string_value(std::to_string(i));
    l1->add_values()->set_string_value(std::to_string(i * 15));
  }
  // Map is: 1 => 15, 2 => 30, 3 => 45
  ASSERT_OK(session->TEST_ApplyAndFlush(op));

  auto rowblock = ReadRows(session.get(), 1);
  ASSERT_EQ(1, rowblock->row_count());
  auto& row = rowblock->row(0);
  ASSERT_EQ(0, row.column(3).int32_value());
  auto& map_pb = row.column(4).map_value();
  LOG(INFO) << "Inserted Map: " << map_pb.DebugString();
  ASSERT_EQ(3, map_pb.keys_size());
  ASSERT_EQ(3, map_pb.values_size());
  for (int i = 0; i < map_pb.keys_size(); ++i) {
    ASSERT_EQ(map_pb.keys(i).int32_value() * 15, map_pb.values(i).int32_value());
  }

  // Now perform an update
  op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  req = op->mutable_request();
  AddHash(1, req);
  QLAddInt32RangeValue(req, 0);
  int column_id = table_.ColumnId("l1");
  client::UpdateMapUpsertKeyValue(req, column_id, "1", "17");
  client::UpdateMapUpsertKeyValue(req, column_id, "2", "37");
  ASSERT_OK(session->TEST_ApplyAndFlush(op));
  // Map should be: 1 => 17, 2 => 37, 3 => 30

  // Check if values have been updated
  auto new_rowblock = ReadRows(session.get(), 1);
  ASSERT_EQ(1, new_rowblock->row_count());
  auto& new_row = new_rowblock->row(0);
  ASSERT_EQ(0, new_row.column(3).int32_value());
  auto& updated_map_pb = new_row.column(4).map_value();
  LOG(INFO) << "Updated Map: " << updated_map_pb.DebugString();
  ASSERT_EQ(3, updated_map_pb.keys_size());
  ASSERT_EQ(3, updated_map_pb.values_size());
  ASSERT_EQ("17", updated_map_pb.values(0).string_value());
  ASSERT_EQ("37", updated_map_pb.values(1).string_value());
  // Old value should be intact
  ASSERT_EQ("45", updated_map_pb.values(2).string_value());
}

TEST_F(QLMapTest, TwoKeyUpdateWithNewKeyInsert) {
  auto session = NewSession();
  // First insert a row
  auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
  auto* req = op->mutable_request();
  AddHash(1, req);
  QLAddInt32RangeValue(req, 0);
  auto l1 = table_.PrepareColumn(req, "l1")->mutable_map_value();
  for (int i = 1; i <= 3; ++i) {
    l1->add_keys()->set_string_value(std::to_string(i));
    l1->add_values()->set_string_value(std::to_string(i * 15));
  }
  // Map is: 1 => 15, 2 => 30, 3 => 45
  ASSERT_OK(session->TEST_ApplyAndFlush(op));

  auto rowblock = ReadRows(session.get(), 1);
  ASSERT_EQ(1, rowblock->row_count());
  auto& row = rowblock->row(0);
  ASSERT_EQ(0, row.column(3).int32_value());
  auto& map_pb = row.column(4).map_value();
  LOG(INFO) << "Inserted Map: " << map_pb.DebugString();
  ASSERT_EQ(3, map_pb.keys_size());
  ASSERT_EQ(3, map_pb.values_size());
  for (int i = 0; i < map_pb.keys_size(); ++i) {
    ASSERT_EQ(map_pb.keys(i).int32_value() * 15, map_pb.values(i).int32_value());
  }

  // Now perform an update
  op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_UPDATE);
  req = op->mutable_request();
  AddHash(1, req);
  QLAddInt32RangeValue(req, 0);
  int column_id = table_.ColumnId("l1");
  client::UpdateMapUpsertKeyValue(req, column_id, "1", "17");
  client::UpdateMapUpsertKeyValue(req, column_id, "2", "37");
  client::UpdateMapUpsertKeyValue(req, column_id, "4", "73");
  ASSERT_OK(session->TEST_ApplyAndFlush(op));
  // Map should be: 1 => 17, 2 => 37, 3 => 30, 4 => 73

  // Check if values have been updated
  auto new_rowblock = ReadRows(session.get(), 1);
  ASSERT_EQ(1, new_rowblock->row_count());
  auto& new_row = new_rowblock->row(0);
  ASSERT_EQ(0, new_row.column(3).int32_value());
  auto& updated_map_pb = new_row.column(4).map_value();
  LOG(INFO) << "Updated Map: " << updated_map_pb.DebugString();
  ASSERT_EQ(4, updated_map_pb.keys_size());
  ASSERT_EQ(4, updated_map_pb.values_size());
  ASSERT_EQ("17", updated_map_pb.values(0).string_value());
  ASSERT_EQ("37", updated_map_pb.values(1).string_value());
  // Old value should be intact
  ASSERT_EQ("45", updated_map_pb.values(2).string_value());
  // New key-value pair must be correctly added
  ASSERT_EQ("4", updated_map_pb.keys(3).string_value());
  ASSERT_EQ("73", updated_map_pb.values(3).string_value());
}

}  // namespace client
}  // namespace yb
