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

#pragma once

#include <algorithm>
#include <functional>
#include <vector>

#include <gtest/gtest.h>

#include "yb/client/table_handle.h"
#include "yb/common/ql_protocol.pb.h"
#include "yb/qlexpr/ql_rowblock.h"

#include "yb/server/server_fwd.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/util/result.h"
#include "yb/util/test_util.h"

namespace yb {
namespace client {

extern const client::YBTableName kTableName;

template <class MiniClusterType>
class QLDmlTestBase : public MiniClusterTestWithClient<MiniClusterType> {
 public:
  QLDmlTestBase();
  void SetUp() override;
  void DoTearDown() override;

  virtual ~QLDmlTestBase() {}

 protected:
  virtual void SetFlags();
  void StartCluster();

  using MiniClusterTestWithClient<MiniClusterType>::client_;
  typename MiniClusterType::Options mini_cluster_opt_;
};

YB_STRONGLY_TYPED_BOOL(Transactional);
YB_DEFINE_ENUM(WriteOpType, (INSERT)(UPDATE)(DELETE));
YB_STRONGLY_TYPED_BOOL(Flush);

namespace kv_table_test {

constexpr const auto kKeyColumn = "key";
constexpr const auto kValueColumn = "value";

void BuildSchema(test::Partitioning partitioning, Schema* schema);

Status CreateTable(
    const Schema& schema, int num_tablets, YBClient* client,
    TableHandle* table, const YBTableName& table_name = kTableName);

void CreateTable(
    Transactional transactional, int num_tablets, YBClient* client, TableHandle* table,
    const YBTableName& table_name = kTableName);

void CreateIndex(
    Transactional transactional, int indexed_column_index, bool use_mangled_names,
    const TableHandle& table, YBClient* client, TableHandle* index);

// Insert/update a full, single row, equivalent to the statement below. Return a YB write op that
// has been applied.
// op_type == WriteOpType::INSERT: insert into t values (key, value);
// op_type == WriteOpType::UPDATE: update t set v=value where k=key;
// op_type == WriteOpType::DELETE: delete from t where k=key; (parameter "value" is unused).
Result<YBqlWriteOpPtr> WriteRow(
    TableHandle* table, const YBSessionPtr& session, int32_t key, int32_t value,
    const WriteOpType op_type = WriteOpType::INSERT, Flush flush = Flush::kTrue);

Result<YBqlWriteOpPtr> DeleteRow(TableHandle* table, const YBSessionPtr& session, int32_t key);

Result<YBqlWriteOpPtr> UpdateRow(
    TableHandle* table, const YBSessionPtr& session, int32_t key, int32_t value);

// Select the specified columns of a row using a primary key, equivalent to the select statement
// below. Return a YB read op that has been applied.
//   select <columns...> from t where h1 = <h1> and h2 = <h2> and r1 = <r1> and r2 = <r2>;
Result<int32_t> SelectRow(
    TableHandle* table, const YBSessionPtr& session, int32_t key,
    const std::string& column = kValueColumn);

// Selects all rows from test table, returning map key => value.
Result<std::map<int32_t, int32_t>> SelectAllRows(TableHandle* table, const YBSessionPtr& session);

Result<YBqlWriteOpPtr> Increment(
    TableHandle* table, const YBSessionPtr& session, int32_t key, int32_t delta = 1,
    Flush flush = Flush::kFalse);

} // namespace kv_table_test

template <class MiniClusterType>
class KeyValueTableTest : public QLDmlTestBase<MiniClusterType> {
 protected:
  void CreateTable(Transactional transactional);

  Status CreateTable(const Schema& schema);

  void CreateIndex(Transactional transactional,
                   int indexed_column_index = 1,
                   bool use_mangled_names = true);

  void PrepareIndex(Transactional transactional,
                    const YBTableName& index_name,
                    int indexed_column_index = 1,
                    bool use_mangled_names = true);

  Result<YBqlWriteOpPtr> WriteRow(
      const YBSessionPtr& session, int32_t key, int32_t value,
      const WriteOpType op_type = WriteOpType::INSERT,
      Flush flush = Flush::kTrue) {
    return kv_table_test::WriteRow(&table_, session, key, value, op_type, flush);
  }

  Result<YBqlWriteOpPtr> DeleteRow(const YBSessionPtr& session, int32_t key) {
    return kv_table_test::DeleteRow(&table_, session, key);
  }

  Result<YBqlWriteOpPtr> UpdateRow(const YBSessionPtr& session, int32_t key, int32_t value) {
    return kv_table_test::UpdateRow(&table_, session, key, value);
  }

  // Select the specified columns of a row using a primary key, equivalent to the select statement
  // below. Return a YB read op that has been applied.
  //   select <columns...> from t where h1 = <h1> and h2 = <h2> and r1 = <r1> and r2 = <r2>;
  Result<int32_t> SelectRow(const YBSessionPtr& session, int32_t key,
                            const std::string& column = kValueColumn) {
    return kv_table_test::SelectRow(&table_, session, key, column);
  }

  YBSessionPtr CreateSession(const YBTransactionPtr& transaction = nullptr,
                             const server::ClockPtr& clock = nullptr);

  // Selects all rows from test table, returning map key => value.
  Result<std::map<int32_t, int32_t>> SelectAllRows(const YBSessionPtr& session) {
    return kv_table_test::SelectAllRows(&table_, session);
  }

  virtual int NumTablets();

  // Sets number of tablets to use for test table creation.
  void SetNumTablets(int num_tablets) {
    num_tablets_ = num_tablets;
  }

  using MiniClusterTestWithClient<MiniClusterType>::client_;

  static const std::string kKeyColumn;
  static const std::string kValueColumn;
  TableHandle table_;
  TableHandle index_;
  int num_tablets_ = CalcNumTablets(3);
};

extern template class KeyValueTableTest<MiniCluster>;
extern template class KeyValueTableTest<ExternalMiniCluster>;

Status CheckOp(YBqlOp* op);

// Select rows count without intermediate conversion of rows to string vector as CountTableRows
// does.
Result<size_t> CountRows(
    const YBSessionPtr& session, const TableHandle& table,
    MonoDelta timeout = MonoDelta::FromSeconds(10) * kTimeMultiplier);

}  // namespace client
}  // namespace yb
