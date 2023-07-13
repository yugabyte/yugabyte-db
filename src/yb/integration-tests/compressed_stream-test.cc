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

#include "yb/client/ql-dml-test-base.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"

#include "yb/rpc/compressed_stream.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/tcp_stream.h"

#include "yb/util/size_literals.h"

#include "yb/yql/cql/ql/util/statement_result.h"

using namespace std::literals;

DECLARE_int32(stream_compression_algo);
DECLARE_bool(enable_stream_compression);

namespace yb {

class CompressedStreamTest : public client::KeyValueTableTest<MiniCluster> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_stream_compression) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_stream_compression_algo) = 1;
    KeyValueTableTest::SetUp();
  }

  void TestSimpleOps();
};

void CompressedStreamTest::TestSimpleOps() {
  CreateTable(client::Transactional::kFalse);

  const int32_t kKey = 1;
  const int32_t kValue = 2;

  {
    auto session = NewSession();
    auto op = ASSERT_RESULT(WriteRow(session, kKey, kValue));
    ASSERT_EQ(op->response().status(), QLResponsePB::YQL_STATUS_OK);
  }

  {
    auto value = ASSERT_RESULT(SelectRow(NewSession(), kKey));
    ASSERT_EQ(kValue, value);
  }
}

TEST_F(CompressedStreamTest, Simple) {
  TestSimpleOps();
}

TEST_F(CompressedStreamTest, BigWrite) {
  client::YBSchemaBuilder builder;
  builder.AddColumn(kKeyColumn)->Type(DataType::INT32)->HashPrimaryKey()->NotNull();
  builder.AddColumn(kValueColumn)->Type(DataType::STRING);

  ASSERT_OK(table_.Create(client::kTableName, 1, client_.get(), &builder));

  const int32_t kKey = 1;
  const std::string kValue(64_KB, 'X');

  auto session = NewSession();
  {
    const auto op = table_.NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, kKey);
    table_.AddStringColumnValue(req, kValueColumn, kValue);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_OK(CheckOp(op.get()));
  }

  {
    const auto op = table_.NewReadOp();
    auto* const req = op->mutable_request();
    QLAddInt32HashValue(req, kKey);
    table_.AddColumns({kValueColumn}, req);
    ASSERT_OK(session->TEST_ApplyAndFlush(op));
    ASSERT_OK(CheckOp(op.get()));
    auto rowblock = yb::ql::RowsResult(op.get()).GetRowBlock();
    ASSERT_EQ(rowblock->row_count(), 1);
    ASSERT_EQ(kValue, rowblock->row(0).column(0).string_value());
  }
}

} // namespace yb
