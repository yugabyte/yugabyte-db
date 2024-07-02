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
#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "yb/common/pg_system_attr.h"
#include "yb/common/pg_types.h"
#include "yb/common/pgsql_protocol.pb.h"
#include "yb/common/schema.h"

#include "yb/gutil/casts.h"

#include "yb/rpc/outbound_call.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/status_fwd.h"
#include "yb/util/tostring.h"

#include "yb/yql/pggate/pg_column.h"
#include "yb/yql/pggate/pg_op.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/pggate_flags.h"

DEFINE_NON_RUNTIME_bool(ysql_enable_read_request_caching, true, "Enable read request caching");
DEFINE_NON_RUNTIME_uint32(
    pg_cache_response_renew_soft_lifetime_limit_ms, 3 * 60 * 1000,
    "Lifetime limit for response cache soft renewing process");

namespace yb::pggate {
namespace {

using ColumnIdsContainer = std::vector<int>;
using DataHolder = std::shared_ptr<std::remove_const_t<PrefetchedDataHolder::element_type>>;

struct OperationInfo {
  OperationInfo(
    const PgsqlReadOpPtr& operation_, const PgTableDescPtr& table_, const PgTableDescPtr& index_)
      : operation(operation_), table(table_), index(index_) {
  }

  PgsqlReadOpPtr operation;
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
// - special attribute YBTupleIdAttributeNumber is added after the table attributes
//
// As far as in future the list of targets columns in sys table scan (including the column order)
// produced by the ybcSetupTargets function may be changed request targets preload targets are
// checked at runtime in the PgSysTablePrefetcher::GetData method
std::vector<const PgColumn*> OrderColumns(const std::vector<PgColumn>& cols) {
  const PgColumn* ybctidCol = nullptr;
  std::vector<const PgColumn*> result;
  result.reserve(cols.size());
  for (const auto& c : cols) {
    const auto attr = c.attr_num();
    switch(attr) {
      case to_underlying(PgSystemAttrNum::kYBTupleId):
        ybctidCol = &c;
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

void AddTargetColumn(LWPgsqlReadRequestPB* req, const PgColumn& column) {
  const auto cid = column.id();
  auto* expr_pb = req->add_targets();
  expr_pb->set_column_id(cid);
  if (!column.is_virtual_column()) {
    req->mutable_column_refs()->mutable_ids()->push_back(cid);
  }
}

using google::protobuf::io::CodedOutputStream;

template<class PB>
uint8_t* WritePBWithSize(uint8_t* out, const PB* pb) {
  if (!pb) {
    return CodedOutputStream::WriteVarint32ToArray(0U, out);
  }
  out = CodedOutputStream::WriteVarint32ToArray(narrow_cast<uint32_t>(pb->SerializedSize()), out);
  return pb->SerializeToArray(out);
}

class VersionInfoWriter {
 public:
  explicit VersionInfoWriter(PrefetcherOptions::VersionInfo version_info)
      : version_info_(version_info) {}

  [[nodiscard]] size_t GetSize() const {
    return CodedOutputStream::VarintSize64(version_info_.version) + 1;
  }

  uint8_t* Write(uint8_t* out) const {
    constexpr auto kTrue = '1';
    constexpr auto kFalse = '0';
    out = CodedOutputStream::WriteRawToArray(
        version_info_.is_db_catalog_version_mode ? &kTrue : &kFalse, 1, out);
    return CodedOutputStream::WriteVarint64ToArray(version_info_.version, out);
  }

 private:
  PrefetcherOptions::VersionInfo version_info_;
};

[[nodiscard]] std::string BuildCacheKey(
    yb::ThreadSafeArena* arena, const ReadHybridTime& catalog_read_time,
    const std::vector<OperationInfo>& ops, PrefetcherOptions::VersionInfo version_info) {
  constexpr auto kMaxFieldSize =
      CodedOutputStream::StaticVarintSize32<std::numeric_limits<uint32_t>::max()>::value;
  const VersionInfoWriter version_writer(version_info);
  auto total_size = version_writer.GetSize() + (ops.size() + 1) * kMaxFieldSize;
  std::optional<LWReadHybridTimePB> read_time_pb;
  if (catalog_read_time) {
    read_time_pb.emplace(arena);
    catalog_read_time.ToPB(&*read_time_pb);
    total_size += read_time_pb->SerializedSize();
  }
  for (const auto& o : ops) {
    total_size += o.operation->read_request().SerializedSize();
  }
  std::string result;
  result.resize(total_size);
  auto* start = pointer_cast<uint8_t*>(result.data());
  auto* out = version_writer.Write(start);
  out = WritePBWithSize(out, read_time_pb ? &*read_time_pb : nullptr);
  for (const auto& o : ops) {
    auto& req = o.operation->read_request();
    std::optional<uint64_t> stmt_id;
    if (req.has_stmt_id()) {
      stmt_id = req.stmt_id();
      req.clear_stmt_id();
    }
    out = WritePBWithSize(out, &o.operation->read_request());
    if (stmt_id) {
      req.set_stmt_id(*stmt_id);
    }
  }
  const auto actual_size = out - start;
  DCHECK_LE(actual_size, total_size);
  result.resize(actual_size);
  return result;
}

[[nodiscard]] std::optional<uint32_t> GetCacheLifetimeThreshold(PrefetchingCacheMode mode) {
  switch(mode) {
    case PrefetchingCacheMode::TRUST_CACHE:
      return std::nullopt;
    case PrefetchingCacheMode::RENEW_CACHE_SOFT:
      return FLAGS_pg_cache_response_renew_soft_lifetime_limit_ms;
    case PrefetchingCacheMode::RENEW_CACHE_HARD:
      return 0;
  }
  FATAL_INVALID_ENUM_VALUE(PrefetchingCacheMode, mode);
}

[[nodiscard]] PgSession::CacheOptions BuildCacheOptions(
    yb::ThreadSafeArena* arena, const ReadHybridTime& catalog_read_time,
    const std::vector<OperationInfo>& ops, const PrefetcherOptions::CachingInfo& caching_info) {
  return {
      .key_group = caching_info.db_oid,
      .key_value = BuildCacheKey(arena, catalog_read_time, ops, caching_info.version_info),
      .lifetime_threshold_ms = GetCacheLifetimeThreshold(caching_info.mode)
  };
}

auto MakeGenerator(const std::vector<OperationInfo>& ops) {
  return
      [i = ops.begin(), end = ops.end()]() mutable {
        PgSession::TableOperation<PgsqlReadOpPtr> result;
        if (i != end) {
          result.operation = &i->operation;
          result.table = i->table.get();
          ++i;
        }
        return result;
      };
}

Result<rpc::CallResponsePtr> Run(
    yb::ThreadSafeArena* arena, PgSession* session,
    const std::vector<OperationInfo>& ops, const PrefetcherOptions& options) {
  auto result = VERIFY_RESULT(options.caching_info
      ? session->RunAsync(
          make_lw_function(MakeGenerator(ops)),
          BuildCacheOptions(arena, session->catalog_read_time(), ops, *options.caching_info))
      : session->RunAsync(make_lw_function(MakeGenerator(ops)), HybridTime()));
  return VERIFY_RESULT(result.Get()).response;
}

struct RegisteredItem {
  RegisteredItem(PgObjectId table_id_, PgObjectId index_id_, int row_oid_filtering_attr_)
      : table_id(table_id_), index_id(index_id_), row_oid_filtering_attr(row_oid_filtering_attr_) {}

  PgObjectId table_id;
  PgObjectId index_id;
  int row_oid_filtering_attr;
};

void ApplySystemItemsFilter(LWPgsqlReadRequestPB* req, int oid_column_id) {
  auto* cond = req->add_where_clauses()->mutable_condition();
  *cond->mutable_op() = QL_OP_LESS_THAN;
  cond->add_operands()->set_column_id(oid_column_id);
  cond->add_operands()->mutable_value()->set_uint32_value(kFirstNormalColocationId);
}

// Helper class to load data from all registered tables
class Loader {
 public:
  Loader(PgSession* session, const std::shared_ptr<ThreadSafeArena>& arena,
         size_t estimated_size, const PrefetcherOptions& options)
      : session_(session),
        arena_(arena),
        options_(options) {
    op_info_.reserve(estimated_size);
  }

  // Prepare operation for read from particular table
  Status Apply(const RegisteredItem& item) {
    const auto table = VERIFY_RESULT(session_->LoadTable(item.table_id));
    const auto index = item.index_id.IsValid()
        ? VERIFY_RESULT(session_->LoadTable(item.index_id)) : PgTableDescPtr();

    VLOG(2) << "Loader::Apply"
            << " table_id=" << item.table_id << " (" << table->table_name().table_name() << ")"
            << " index_id=" << item.index_id
            << " row_oid_filtering_attr=" << item.row_oid_filtering_attr;
    RSTATUS_DCHECK(
        table->schema().table_properties().is_ysql_catalog_table(),
        InternalError,
        Format("$0 $1 is not a catalog table", item.table_id, table->table_name().table_name()));
    // System tables are not region local.
    op_info_.emplace_back(
        ArenaMakeShared<PgsqlReadOp>(arena_, &*arena_, *table, false /* is_region_local */,
                                     session_->metrics().metrics_capture()),
        table, index);
    auto& info = op_info_.back();
    auto& req = info.operation->read_request();
    SetupPaging(&req);
    PgTable target(table);
    auto ordered_columns = OrderColumns(target.columns());
    info.targets.reserve(ordered_columns.size());
    const PgColumn* oid_filtering_column = nullptr;
    for (const auto& column : ordered_columns) {
      AddTargetColumn(&req, *column);
      info.targets.push_back(column->id());
      if (column->attr_num() == item.row_oid_filtering_attr) {
        oid_filtering_column = column;
      }
    }
    if (oid_filtering_column) {
      ApplySystemItemsFilter(&req, oid_filtering_column->id());
    }
    if (index) {
      const PgTable index_target(index);
      for (const auto& column : index_target.columns()) {
        if (column.attr_num() == to_underlying(PgSystemAttrNum::kYBIdxBaseTupleId)) {
          auto& index_req = *req.mutable_index_request();
          index_req.dup_table_id(index->relfilenode_id().GetYbTableId());
          SetupPaging(&index_req);
          AddTargetColumn(&index_req, column);
          info.index_targets.push_back(column.id());
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
      auto response = VERIFY_RESULT(Run(arena_.get(), session_, op_info_, options_));
      Status remove_predicate_status;
      ResultFunctorAdapter<bool, OperationInfo&> remove_predicate(
          &remove_predicate_status,
          [&response, data_container](OperationInfo& op_info) -> Result<bool> {
            auto sidecar = VERIFY_RESULT(response->GetSidecarHolder(
                op_info.operation->response()->rows_data_sidecar()));
            InsertData(data_container,
                       op_info.table->relfilenode_id(),
                       &op_info.targets,
                       op_info.index ? op_info.index->relfilenode_id() : PgObjectId(),
                       &op_info.index_targets,
                       std::move(sidecar));
            return !VERIFY_RESULT(PrepareNextRequest(*op_info.table, op_info.operation.get()));
          }, true /* bad_status_value */);
      std::erase_if(op_info_, remove_predicate);
      RETURN_NOT_OK(remove_predicate_status);
    }
    return Status::OK();
  }

 private:
  void SetupPaging(LWPgsqlReadRequestPB* req) const {
    req->set_return_paging_state(true);
    req->set_is_forward_scan(true);
    req->set_limit(options_.fetch_row_limit);
  }

  PgSession* session_;
  std::vector<OperationInfo> op_info_;
  std::shared_ptr<ThreadSafeArena> arena_;
  const PrefetcherOptions options_;
};

} // namespace

std::string PrefetcherOptions::VersionInfo::ToString() const {
  return YB_STRUCT_TO_STRING(version, is_db_catalog_version_mode);
}

std::string PrefetcherOptions::CachingInfo::ToString() const {
  return YB_STRUCT_TO_STRING(version_info, mode);
}

std::string PrefetcherOptions::ToString() const {
  return YB_STRUCT_TO_STRING(caching_info, fetch_row_limit);
}

class PgSysTablePrefetcher::Impl {
 public:
  explicit Impl(const PrefetcherOptions& options)
      : arena_(SharedArena()), options_(options) {
    VLOG(1) << "Starting prefetcher with " << options_.ToString();
  }

  void Register(
      const PgObjectId& table_id, const PgObjectId& index_id, int row_oid_filtering_attr) {
    VLOG(1) << "Register table_id=" << table_id
            << " index_id=" << index_id
            << " row_oid_filtering_attr=" << row_oid_filtering_attr;
    registered_for_loading_.emplace_back(table_id, index_id, row_oid_filtering_attr);
  }

  PrefetchedDataHolder GetData(const LWPgsqlReadRequestPB& read_req, bool index_check_required) {
    LOG_IF(DFATAL, !registered_for_loading_.empty())
        << "All registered table must be prefetched first";
    const PgObjectId table_id(read_req.table_id());
    auto i = data_.find(table_id);
    if (i != data_.end()) {
      auto data = GetDataWithTargetsCheck(table_id, i->second, read_req, index_check_required);
      if (data.ok()) {
        return std::move(*data);
      }
      LOG(DFATAL) << data.status();
    } else {
      LOG(DFATAL) << "Sys table prefetching is enabled but table "
                  << table_id
                  << " was not prefetched. Prefetched tables are: "
                  << CollectionToString(data_, [](const auto& item) { return item.first; });
     }
    return PrefetchedDataHolder();
  }

  Status Prefetch(PgSession* session) {
    DCHECK(!registered_for_loading_.empty());
    std::sort(registered_for_loading_.begin(),
              registered_for_loading_.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.table_id < rhs.table_id; });
    Loader loader(session, arena_, registered_for_loading_.size(), options_);
    PgObjectId prev_table_id;
    for (const auto& item : registered_for_loading_) {
      // Load table once in spite of the fact it might be registered for preloading multiple times.
      if (prev_table_id != item.table_id) {
        RETURN_NOT_OK(loader.Apply(item));
        prev_table_id = item.table_id;
      }
    }
    registered_for_loading_.clear();
    return loader.Load(&data_);
  }

  const PrefetcherOptions& options() const {
    return options_;
  }

 private:
  std::shared_ptr<ThreadSafeArena> arena_;
  boost::container::small_vector<RegisteredItem, 64> registered_for_loading_;
  DataContainer data_;
  const PrefetcherOptions options_;
};

PgSysTablePrefetcher::PgSysTablePrefetcher(const PrefetcherOptions& options)
    : impl_(new Impl(options)) {}

PgSysTablePrefetcher::~PgSysTablePrefetcher() = default;

void PgSysTablePrefetcher::Register(
    const PgObjectId& table_id, const PgObjectId& index_id, int row_oid_filtering_attr) {
  impl_->Register(table_id, index_id, row_oid_filtering_attr);
}

Status PgSysTablePrefetcher::Prefetch(PgSession* session) {
  auto status = impl_->Prefetch(session);
  if (!status.ok()) {
    // Reset the state in case of failure to prevent using of incomplete data in future calls.
    impl_.reset(new Impl(impl_->options()));
  }
  return status;
}

PrefetchedDataHolder PgSysTablePrefetcher::GetData(
    const LWPgsqlReadRequestPB& read_req, bool index_check_required) {
  return impl_->GetData(read_req, index_check_required);
}

} // namespace yb::pggate
