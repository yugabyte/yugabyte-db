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

#ifndef YB_MASTER_YQL_VIRTUAL_TABLE_H
#define YB_MASTER_YQL_VIRTUAL_TABLE_H

#include "yb/common/ql_rowblock.h"

#include "yb/docdb/ql_storage_interface.h"

#include "yb/master/ts_descriptor.h"
#include "yb/master/util/yql_vtable_helpers.h"

#include "yb/util/metrics_fwd.h"

namespace yb {
namespace master {

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
  virtual Result<std::shared_ptr<QLRowBlock>> RetrieveData(
      const QLReadRequestPB& request) const = 0;

  CHECKED_STATUS GetIterator(const QLReadRequestPB& request,
                             const Schema& projection,
                             const Schema& schema,
                             const TransactionOperationContext& txn_op_context,
                             CoarseTimePoint deadline,
                             const ReadHybridTime& read_time,
                             const QLScanSpec& spec,
                             std::unique_ptr<docdb::YQLRowwiseIteratorIf>* iter) const override;

  CHECKED_STATUS BuildYQLScanSpec(
      const QLReadRequestPB& request,
      const ReadHybridTime& read_time,
      const Schema& schema,
      bool include_static_columns,
      const Schema& static_projection,
      std::unique_ptr<QLScanSpec>* spec,
      std::unique_ptr<QLScanSpec>* static_row_spec) const override;

  //------------------------------------------------------------------------------------------------
  // PGSQL Support.
  //------------------------------------------------------------------------------------------------

  CHECKED_STATUS CreateIterator(const Schema& projection,
                                const Schema& schema,
                                const TransactionOperationContext& txn_op_context,
                                CoarseTimePoint deadline,
                                const ReadHybridTime& read_time,
                                std::unique_ptr<docdb::YQLRowwiseIteratorIf>* iter) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  CHECKED_STATUS InitIterator(docdb::YQLRowwiseIteratorIf* iter,
                              const PgsqlReadRequestPB& request,
                              const Schema& schema,
                              const QLValuePB& ybctid) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  CHECKED_STATUS GetIterator(const PgsqlReadRequestPB& request,
                             const Schema& projection,
                             const Schema& schema,
                             const TransactionOperationContext& txn_op_context,
                             CoarseTimePoint deadline,
                             const ReadHybridTime& read_time,
                             const docdb::DocKey& start_doc_key,
                             docdb::YQLRowwiseIteratorIf::UniPtr* iter) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

  CHECKED_STATUS GetIterator(uint64 stmt_id,
                             const Schema& projection,
                             const Schema& schema,
                             const TransactionOperationContext& txn_op_context,
                             CoarseTimePoint deadline,
                             const ReadHybridTime& read_time,
                             const QLValuePB& ybctid,
                             docdb::YQLRowwiseIteratorIf::UniPtr* iter) const override {
    LOG(FATAL) << "Postgresql virtual tables are not yet implemented";
    return Status::OK();
  }

 protected:
  // Finds the given column name in the schema and updates the specified column in the given row
  // with the provided value.
  template<class T>
  CHECKED_STATUS SetColumnValue(const std::string& col_name, const T& value, QLRow* row) const {
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
#endif // YB_MASTER_YQL_VIRTUAL_TABLE_H
