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

#include "yb/master/yql_virtual_table.h"

#include "yb/common/schema.h"

#include "yb/dockv/key_entry_value.h"
#include "yb/docdb/doc_ql_scanspec.h"

#include "yb/master/master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/ts_manager.h"
#include "yb/master/yql_vtable_iterator.h"

#include "yb/util/metrics.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"

namespace yb {
namespace master {

namespace  {
static const char* const kBaseMetricDescription = "Time spent querying YCQL system table: ";
static const char* const kMetricPrefixName = "ycql_queries_";
}

YQLVirtualTable::YQLVirtualTable(const TableName& table_name,
                                 const NamespaceName &namespace_name,
                                 const Master* const master,
                                 const Schema& schema)
    : master_(master),
      table_name_(table_name),
      schema_(std::make_unique<Schema>(schema)) {
    std::string metricDescription = kBaseMetricDescription + table_name_;
    std::string metricName = kMetricPrefixName + namespace_name + "_" + table_name;
    EscapeMetricNameForPrometheus(&metricName);

    std::unique_ptr<HistogramPrototype> prototype = std::make_unique<OwningHistogramPrototype>(
                "server", metricName, metricDescription,
                MetricUnit::kMicroseconds, metricDescription,
                MetricLevel::kInfo, 0, 10000000, 2);
    histogram_ = master->metric_entity()->FindOrCreateMetric<Histogram>(std::move(prototype));
}

YQLVirtualTable::~YQLVirtualTable() = default;

Status YQLVirtualTable::GetIterator(
    const QLReadRequestPB& request,
    const dockv::ReaderProjection& projection,
      std::reference_wrapper<const docdb::DocReadContext> doc_read_context,
    const TransactionOperationContext& txn_op_context,
    const docdb::ReadOperationData& read_operation_data,
    const qlexpr::QLScanSpec& spec,
    std::reference_wrapper<const ScopedRWOperation> pending_op,
    std::unique_ptr<docdb::YQLRowwiseIteratorIf>* iter,
    const docdb::DocDBStatistics* statistics) const {
  // Acquire shared lock on catalog manager to verify it is still the leader and metadata will
  // not change.
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  RETURN_NOT_OK(l.first_failed_status());

  MonoTime start_time = MonoTime::Now();
  iter->reset(new YQLVTableIterator(
      VERIFY_RESULT(RetrieveData(request)), request.hashed_column_values()));
  histogram_->Increment(MonoTime::Now().GetDeltaSince(start_time).ToMicroseconds());
  return Status::OK();
}

Status YQLVirtualTable::BuildYQLScanSpec(
    const QLReadRequestPB& request,
    const ReadHybridTime& read_time,
    const Schema& schema,
    const bool include_static_columns,
    std::unique_ptr<qlexpr::QLScanSpec>* spec,
    std::unique_ptr<qlexpr::QLScanSpec>* static_row_spec) const {
  // There should be no static columns in system tables so we are not handling it.
  if (include_static_columns) {
    return STATUS(IllegalState, "system table contains no static columns");
  }
  const dockv::KeyEntryValues empty_vec;
  spec->reset(new docdb::DocQLScanSpec(
      schema, /* hash_code = */ boost::none, /* max_hash_code = */ boost::none, empty_vec,
      request.has_where_expr() ? &request.where_expr().condition() : nullptr,
      request.has_if_expr() ? &request.if_expr().condition() : nullptr, rocksdb::kDefaultQueryId,
      request.is_forward_scan()));
  return Status::OK();
}

void YQLVirtualTable::GetSortedLiveDescriptors(std::vector<std::shared_ptr<TSDescriptor>>* descs)
    const {
  master_->ts_manager()->GetAllLiveDescriptors(descs);
  std::sort(
      descs->begin(), descs->end(),
      [](const std::shared_ptr<TSDescriptor>& a, const std::shared_ptr<TSDescriptor>& b) -> bool {
        return a->permanent_uuid() < b->permanent_uuid();
      });
}

CatalogManagerIf& YQLVirtualTable::catalog_manager() const {
  return *master_->catalog_manager();
}

Result<std::pair<int, DataType>> YQLVirtualTable::ColumnIndexAndType(
    const std::string& col_name) const {
  auto column_index = schema_->find_column(col_name);
  if (column_index == Schema::kColumnNotFound) {
    return STATUS_SUBSTITUTE(NotFound, "Couldn't find column $0 in schema", col_name);
  }
  const DataType data_type = schema_->column(column_index).type_info()->type;
  return std::make_pair(column_index, data_type);
}

const std::string kSystemTablesReleaseVersion = "3.9-SNAPSHOT";
const std::string kSystemTablesReleaseVersionColumn = "release_version";

}  // namespace master
}  // namespace yb
