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

#include "yb/qlexpr/ql_rowblock.h"

#include "yb/docdb/ql_storage_interface.h"

#include "yb/master/ts_descriptor.h"
#include "yb/master/util/yql_vtable_helpers.h"

#include "yb/util/metrics_fwd.h"

namespace yb {
namespace master {

using VTableDataPtr = std::shared_ptr<qlexpr::QLRowBlock>;

// A YQL virtual table which is based on in memory data.
class YQLVirtualTable : public docdb::YQLStorageIf {
 public:
  explicit YQLVirtualTable(const TableName& table_name,
                           const NamespaceName &namespace_name,
                           const Master* const master,
                           const Schema& schema);
  ~YQLVirtualTable();

  // Access methods.
  const Schema& schema() const { return *schema_; }

  const TableName& table_name() const { return table_name_; }

  //------------------------------------------------------------------------------------------------
  // CQL Support.
  //------------------------------------------------------------------------------------------------

  // Retrieves all the data for the yql virtual table in form of a QLRowBlock. This data is then
  // used by the iterator.
  virtual Result<VTableDataPtr> RetrieveData(const QLReadRequestPB& request) const = 0;

  Status GetIterator(
      const QLReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const docdb::DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const docdb::ReadOperationData& read_operation_data,
      const qlexpr::QLScanSpec& spec,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<docdb::YQLRowwiseIteratorIf>* iter,
      const docdb::DocDBStatistics* statistics = nullptr) const override;

  Status BuildYQLScanSpec(
      const QLReadRequestPB& request,
      const ReadHybridTime& read_time,
      const Schema& schema,
      bool include_static_columns,
      std::unique_ptr<qlexpr::QLScanSpec>* spec,
      std::unique_ptr<qlexpr::QLScanSpec>* static_row_spec) const override;

  //------------------------------------------------------------------------------------------------
  // PGSQL Support.
  //------------------------------------------------------------------------------------------------

  Status CreateIterator(
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const docdb::DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const docdb::ReadOperationData& read_operation_data,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      std::unique_ptr<docdb::YQLRowwiseIteratorIf>* iter,
      const docdb::DocDBStatistics* statistics = nullptr) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  Status InitIterator(docdb::DocRowwiseIterator* iter,
                      const PgsqlReadRequestPB& request,
                      const Schema& schema,
                      const QLValuePB& ybctid) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  Status GetIterator(
      const PgsqlReadRequestPB& request,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const docdb::DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const docdb::ReadOperationData& read_operation_data,
      const dockv::DocKey& start_doc_key,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      docdb::YQLRowwiseIteratorIf::UniPtr* iter,
      const docdb::DocDBStatistics* statistics = nullptr) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  Status GetIteratorForYbctid(
      uint64 stmt_id,
      const dockv::ReaderProjection& projection,
      std::reference_wrapper<const docdb::DocReadContext> doc_read_context,
      const TransactionOperationContext& txn_op_context,
      const docdb::ReadOperationData& read_operation_data,
      const QLValuePB& min_ybctid,
      const QLValuePB& max_ybctid,
      std::reference_wrapper<const ScopedRWOperation> pending_op,
      docdb::YQLRowwiseIteratorIf::UniPtr* iter,
      const docdb::DocDBStatistics* statistics = nullptr,
      docdb::SkipSeek skip_seek = docdb::SkipSeek::kFalse) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  std::string ToString() const override { return Format("YQLVirtualTable $0", table_name_); }

 protected:
  // Finds the given column name in the schema and updates the specified column in the given row
  // with the provided value.
  template<class T>
  Status SetColumnValue(const std::string& col_name, const T& value, qlexpr::QLRow* row) const {
    auto p = VERIFY_RESULT(ColumnIndexAndType(col_name));
    row->SetColumn(p.first, util::GetValue(value, p.second));
    return Status::OK();
  }

  Result<std::pair<int, DataType>> ColumnIndexAndType(const std::string& col_name) const;

  // Get all live tserver descriptors sorted by their UUIDs. For cases like system.local and
  // system.peers tables to return the token map of each tserver node so that each maps to a
  // consistent token.
  void GetSortedLiveDescriptors(std::vector<std::shared_ptr<TSDescriptor>>* descs) const;

  CatalogManagerIf& catalog_manager() const;

  const Master* const master_;
  TableName table_name_;
  std::unique_ptr<Schema> schema_;
  scoped_refptr<Histogram> histogram_;
};

extern const std::string kSystemTablesReleaseVersion;
extern const std::string kSystemTablesReleaseVersionColumn;

}  // namespace master
}  // namespace yb
