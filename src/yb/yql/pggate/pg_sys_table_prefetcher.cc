//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_sys_table_prefetcher.h"

#include <algorithm>
#include <functional>
#include <ostream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/pg_system_attr.h"
#include "yb/common/pg_types.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/outbound_call.h"

#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/status_fwd.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"

namespace yb {
namespace pggate {
namespace {

using ColumnIdsContainer = std::vector<int>;
using DataHolder = std::shared_ptr<std::remove_const_t<PrefetchedDataHolder::element_type>>;

struct OperationInfo {
  OperationInfo(
    const PgsqlReadOpPtr& operation_, const PgTableDescPtr& table_, const PgTableDescPtr& index_)
      : operation(operation_), table(table_), index(index_) {
  }

  PgsqlReadOp& ReadOperation() {
    return *down_cast<PgsqlReadOp*>(operation.get());
  }

  PgsqlOpPtr operation;
  PgTableDescPtr table;
  ColumnIdsContainer targets;
  PgTableDescPtr index;
  ColumnIdsContainer index_targets;
};

struct PrefetchedInfo {
  ColumnIdsContainer targets;
  PgObjectId index_id;
  ColumnIdsContainer index_targets;
  DataHolder data;
};

using DataContainer = std::unordered_map<PgObjectId, PrefetchedInfo, PgObjectIdHash>;

void InsertData(DataContainer* container,
                const PgObjectId& table_id,
                ColumnIdsContainer* targets,
                const PgObjectId& index_id,
                ColumnIdsContainer* index_targets,
                rpc::SidecarHolder rows_data) {
  auto i = container->find(table_id);
  if (i == container->end()) {
    i = container->emplace(table_id, PrefetchedInfo{
      .targets = std::move(*targets),
      .index_id = index_id,
      .index_targets = std::move(*index_targets),
      .data = std::make_shared<DataHolder::element_type>()
    }).first;
    targets->clear();
    index_targets->clear();
  }
  i->second.data->push_back(std::move(rows_data));
}

// Helper function to produce sequence of columns ordered in same way like the ybcSetupTargets
// function does. There is no easy way to reuse the ybcSetupTargets function itself or use some
// common code by both functions. The reason is because postgres need to open relation before
// initiate a scan but for this purpose it has to load data from the `pg_class` and some other
// system tables. But current code is used for preloading `pg_class` tables as well.
// As a result current code can't use the ybcSetupTargets function and it has no access to the
// postgres table schema.
// Instead of this current function reimplements the logic of ybcSetupTargets
// with respect to the following assumptions:
// - scans from sys tables reads all of the columns from postgres table
// - all the attributes in sys table are requested in asc order
// - special attributes ObjectIdAttributeNumber and YBTupleIdAttributeNumber
//   are added after the table attributes
//
// As far as in future the list of targets columns in sys table scan (including the column order)
// produced by the ybcSetupTargets function may be changed request targets preload targets are
// checked at runtime in the PgSysTablePrefetcher::GetData method
std::vector<const PgColumn*> OrderColumns(const std::vector<PgColumn>& cols) {
  const PgColumn* objCol = nullptr;
  const PgColumn* ybctidCol = nullptr;
  std::vector<const PgColumn*> result;
  result.reserve(cols.size());
  for (const auto& c : cols) {
    const auto attr = c.attr_num();
    switch(attr) {
      case to_underlying(PgSystemAttrNum::kYBTupleId):
        ybctidCol = &c;
        break;
      case to_underlying(PgSystemAttrNum::kObjectId):
        objCol = &c;
        break;
      default:
        if (attr > 0) {
          result.push_back(&c);
        }
        break;
    }
  }
  std::sort(result.begin(), result.end(), [](auto lhs, auto rhs) {
      return lhs->attr_num() < rhs->attr_num(); });
  if (objCol) {
    result.push_back(objCol);
  }
  if (ybctidCol) {
    result.push_back(ybctidCol);
  }
  return result;
}

// Helper class to convert Result<T> functor into T functor.
template<class T, class... Args>
class ResultFunctorAdapter {
  using Functor = std::function<Result<T>(Args&&...)>;

 public:
  ResultFunctorAdapter(Status* status, Functor functor, T bad_status_value)
      : status_(status),
        functor_(std::move(functor)),
        bad_status_value_(std::move(bad_status_value)) {
  }

  T operator()(Args&&... args) {
    if (status_->ok()) {
      auto res = functor_(std::forward<Args>(args)...);
      if (res.ok()) {
        return *res;
      } else {
        *status_ = res.status();
      }
    }
    return bad_status_value_;
  }

 private:
  Status* status_;
  Functor functor_;
  const T bad_status_value_;
};

Status CheckRequestTargets(const PgObjectId& table_id,
                                   const ColumnIdsContainer& targets,
                                   const LWPgsqlReadRequestPB& req) {
  SCHECK_EQ(table_id,
            PgObjectId(req.table_id()),
            IllegalState,
            "Request table differs from prefetch table");
  SCHECK_EQ(targets.size(),
            implicit_cast<size_t>(req.targets().size()),
            IllegalState,
            Format("Different number of targets detected for the $0 table", table_id));
  auto request_column_it = req.targets().begin();
  for (const auto& prefetched_column : targets) {
    SCHECK(request_column_it->has_column_id(),
           IllegalState,
           Format("Target without column id for the $0 table found", table_id));
    SCHECK_EQ(prefetched_column,
              request_column_it->column_id(),
              IllegalState,
              Format("Unexpected target for the $0 table found", table_id));
    ++request_column_it;
  }
  return Status::OK();
}

Result<PrefetchedDataHolder> GetDataWithTargetsCheck(
    const PgObjectId& table_id, const PrefetchedInfo& info,
    const LWPgsqlReadRequestPB& read_req, bool index_check_required) {
  RETURN_NOT_OK(CheckRequestTargets(table_id, info.targets, read_req));
  if (index_check_required && read_req.has_index_request()) {
    RETURN_NOT_OK_PREPEND(
      CheckRequestTargets(info.index_id, info.index_targets, read_req.index_request()),
      Format("GetData for $0 table failed: ", table_id));
  }
  return info.data;
}

void AddTargerColumn(LWPgsqlReadRequestPB* req, const PgColumn& column) {
  const auto cid = column.id();
  auto* expr_pb = req->add_targets();
  expr_pb->set_column_id(cid);
  if (!column.is_virtual_column()) {
    req->mutable_column_refs()->mutable_ids()->push_back(cid);
  }
}

void SetupPaging(LWPgsqlReadRequestPB* req) {
  req->set_return_paging_state(true);
  req->set_is_forward_scan(true);
  req->set_limit(FLAGS_ysql_prefetch_limit);
}

// Helper class to load data from all registered tables
class Loader {
 public:
  Loader(PgSession* session, size_t estimated_size)
      : session_(session), arena_(std::make_shared<Arena>()) {
    op_info_.reserve(estimated_size);
  }

  // Prepare operation for read from particular table
  Status Apply(const PgObjectId& table_id, const PgObjectId& index_id) {
    const auto table = VERIFY_RESULT(session_->LoadTable(table_id));
    const auto index = index_id.IsValid() ? VERIFY_RESULT(session_->LoadTable(index_id))
                                          : PgTableDescPtr();

    VLOG(2) << "Loader::Apply "
            << "table_id=" << table_id << " (" << table->table_name().table_name() << ") "
            << "index_id=" << index_id;
    CHECK(table->schema().table_properties().is_ysql_catalog_table())
        << table_id << " " << table->table_name().table_name() << " is not a catalog table";
    // System tables are not region local.
    op_info_.emplace_back(
        ArenaMakeShared<PgsqlReadOp>(arena_, &*arena_, *table, false /* is_region_local */),
        table, index);
    auto& info = op_info_.back();
    auto& req = info.ReadOperation().read_request();
    SetupPaging(&req);
    PgTable target(table);
    auto ordered_columns = OrderColumns(target.columns());
    info.targets.reserve(ordered_columns.size());
    for (const auto& c : ordered_columns) {
      AddTargerColumn(&req, *c);
      info.targets.push_back(c->id());
    }
    if (index) {
      const PgTable index_target(index);
      for (const auto& c : index_target.columns()) {
        if (c.attr_num() == to_underlying(PgSystemAttrNum::kYBIdxBaseTupleId)) {
          auto& index_req = *req.mutable_index_request();
          index_req.dup_table_id(index->id().GetYbTableId());
          SetupPaging(&index_req);
          AddTargerColumn(&index_req, c);
          info.index_targets.push_back(c.id());
          break;
        }
      }
    }
    return Status::OK();
  }

  // Load data from all prepared operations and place the result into data_container
  Status Load(DataContainer* data_container) {
    VLOG(2) << "Loader::Load";
    while (!op_info_.empty()) {
      auto response = VERIFY_RESULT(session_->RunAsync(
          make_lw_function([i = op_info_.begin(), end = op_info_.end()]() mutable {
            PgSession::TableOperation result;
            if (i != end) {
              result.operation = &i->operation;
              result.table = i->table.get();
              ++i;
            }
            return result;
          }),
          nullptr /* read_time */, false /* force_non_bufferable */));
      auto call_resp = VERIFY_RESULT(response.Get());
      Status remove_predicate_status;
      ResultFunctorAdapter<bool, OperationInfo&> remove_predicate(
          &remove_predicate_status,
          [&call_resp, data_container](OperationInfo& op_info) -> Result<bool> {
            auto& op = op_info.ReadOperation();
            auto sidecar = VERIFY_RESULT(call_resp->GetSidecarHolder(
                op.response()->rows_data_sidecar()));
            InsertData(data_container,
                       op_info.table->id(),
                       &op_info.targets,
                       op_info.index ? op_info.index->id() : PgObjectId(),
                       &op_info.index_targets,
                       std::move(sidecar));
            return !PrepareNextRequest(&op);
          }, true /* bad_status_value */);
      op_info_.erase(std::remove_if(op_info_.begin(), op_info_.end(), remove_predicate),
                     op_info_.end());
      RETURN_NOT_OK(remove_predicate_status);
    }
    return Status::OK();
  }

 private:
  PgSession* session_;
  std::vector<OperationInfo> op_info_;
  std::shared_ptr<Arena> arena_;
};

} // namespace

class PgSysTablePrefetcher::Impl {
 public:
  void Register(const PgObjectId& table_id, const PgObjectId& index_id) {
    VLOG(1) << "Register " << table_id << " " << index_id;
    if (data_.find(table_id) == data_.end()) {
      registered_for_loading_[table_id] = index_id;
    }
  }

  Result<PrefetchedDataHolder> GetData(
      PgSession* session, const LWPgsqlReadRequestPB& read_req, bool index_check_required) {
    const PgObjectId table_id(read_req.table_id());
    auto i = data_.find(table_id);
    if (i != data_.end()) {
      return GetDataWithTargetsCheck(table_id, i->second, read_req, index_check_required);
    }
    // Check that current table is registered for loading.
    // Absence of the table in the list means that this table must be registered first in our code.
    // DLOG(FATAL) is used instead of SCHECK to let user on release build proceed by reading
    // data from a master in a non efficient way (by using separate RPC).
    if (registered_for_loading_.find(table_id) == registered_for_loading_.end()) {
      DLOG(FATAL) << "Sys table prefetching is enabled but requested table "
                  << table_id
                  << " was not registered. The list of tables ready for prefetching is: "
                  << CollectionToString(registered_for_loading_,
                                        [](const auto& item) { return item.first; });
      return PrefetchedDataHolder();
    }
    Loader loader(session, registered_for_loading_.size());
    for (const auto& t : registered_for_loading_) {
      RETURN_NOT_OK(loader.Apply(t.first, t.second));
    }
    registered_for_loading_.clear();
    RETURN_NOT_OK(loader.Load(&data_));
    return GetDataWithTargetsCheck(table_id, data_[table_id], read_req, index_check_required);
  }

 private:
  std::unordered_map<PgObjectId, PgObjectId, PgObjectIdHash> registered_for_loading_;
  DataContainer data_;
};

PgSysTablePrefetcher::PgSysTablePrefetcher()
    : impl_(new Impl()) {
}

PgSysTablePrefetcher::~PgSysTablePrefetcher() = default;

void PgSysTablePrefetcher::Register(const PgObjectId& table_id, const PgObjectId& index_id) {
  impl_->Register(table_id, index_id);
}

Result<PrefetchedDataHolder> PgSysTablePrefetcher::GetData(
    PgSession* session, const LWPgsqlReadRequestPB& read_req, bool index_check_required) {
  auto result = impl_->GetData(session, read_req, index_check_required);
  if (!result.ok()) {
    // Reset the state in case of failure to prevent using of incomplete data in future calls.
    impl_.reset(new Impl());
  }
  return result;
}

} // namespace pggate
} // namespace yb
