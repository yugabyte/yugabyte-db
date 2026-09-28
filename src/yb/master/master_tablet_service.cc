// Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/master_tablet_service.h"

#include <algorithm>
#include <atomic>
#include <optional>

#include "yb/common/common_flags.h"
#include "yb/common/entity_ids.h"
#include "yb/common/wire_protocol.h"

#include "yb/common/pgsql_protocol.messages.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/sysinfo.h"

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/scoped_leader_shared_lock.h"
#include "yb/master/scoped_leader_shared_lock-internal.h"
#include "yb/master/ysql/ysql_manager.h"

#include "yb/rpc/rpc_context.h"

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/random_util.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

DEFINE_RUNTIME_AUTO_bool(enable_ysql_catalog_prefetch_admission, kLocalVolatile, false, true,
    "Whether the master leader bounds how many YSQL catalog prefetches it serves at a time. "
    "Until the upgrade is finalized nothing is bounded, because a tserver that has not been "
    "upgraded does not report that it is prefetching and the bound could then have covered only "
    "part of the cluster.");

DEFINE_RUNTIME_int32(master_max_concurrent_ysql_catalog_prefetches, -1,
    "Maximum number of YSQL catalog prefetches the master leader serves concurrently. Prefetches "
    "beyond this are rejected with an error the client retries after backing off, so that a fleet "
    "wide event such as a DDL does not queue every tserver on the leader at once. Ordinary "
    "catalog reads are never bounded. -1 (the default) is resolved at startup to a value derived "
    "from the number of cores available to the process, never less than 8 so that the bound does "
    "not bite on a small master; 0 means no limit, and so does -1 set at runtime since that "
    "resolution happens only at startup. Serving a prefetch is CPU bound, so a limit near the "
    "core count keeps the leader fully busy without letting the queue grow unbounded. Set it "
    "generously if you set it by hand: a limit below what the leader can actually serve makes "
    "callers wait out their deadline and fail rather than merely queue.");

DEFINE_RUNTIME_uint32(master_new_ysql_catalog_prefetch_pct, 50,
    "Percentage of master_max_concurrent_ysql_catalog_prefetches available to prefetches that are "
    "just starting. A prefetch is a long sequence of paged reads, so admitting new ones only up to "
    "a lower watermark leaves room for sequences already under way to finish, instead of many "
    "connections each completing part of their pages and then timing out. At least one new "
    "prefetch is always admissible, whatever this works out to.");

DEFINE_validator(master_new_ysql_catalog_prefetch_pct,
    FLAG_COND_VALIDATOR(_value <= 100, "Must be a percentage, so at most 100"));

METRIC_DEFINE_gauge_int64(server, ysql_catalog_prefetches_in_progress,
                          "YSQL Catalog Prefetches In Progress",
                          yb::MetricUnit::kRequests,
                          "Number of YSQL catalog prefetches the master is currently serving");
METRIC_DEFINE_counter(server, ysql_catalog_prefetch_rejections,
                      "YSQL Catalog Prefetch Rejections",
                      yb::MetricUnit::kRequests,
                      "Number of YSQL catalog prefetches rejected because "
                      "master_max_concurrent_ysql_catalog_prefetches was exceeded");

DEFINE_test_flag(int32, ysql_catalog_prefetch_rejections_to_inject, 0,
    "Reject about this many YSQL catalog prefetches with the same retryable error the concurrency "
    "limit produces, independently of master_max_concurrent_ysql_catalog_prefetches, counting "
    "down as they are injected. Lets a test verify that a client retries a rejected prefetch "
    "rather than failing the connection, and get a bounded number of rejections rather than a "
    "probabilistic one -- a prefetch small enough to fit in a single request would otherwise be "
    "rejected or not on the toss of a coin. The countdown is a plain decrement, so prefetches "
    "arriving together can take it past zero and inject one or two more than asked.");

DEFINE_test_flag(int32, ysql_catalog_write_rejection_percentage, 0,
    "Reject specified percentage of writes to the YSQL catalog tables.");

DEFINE_test_flag(bool, ysql_require_force_catalog_modifications, false,
    "Fail YSQL catalog writes requests if force_catalog_modifications is not set.");

using namespace std::chrono_literals;

namespace yb {
namespace master {

// Only SysTablet 0 is bootstrapped on all master peers, and only the master leader
// reads other sys tablets. We check only for tablet 0 so as to have same readiness
// level across all masters.
// Note: If this value changes, then IsTabletServerReady has to be revisited.
constexpr int NUM_TABLETS_SYS_CATALOG = 1;

void MasterTabletServiceImpl::AutoInitCatalogPrefetchLimit() {
  if (FLAGS_master_max_concurrent_ysql_catalog_prefetches >= 0) {
    return;
  }
  // Twice the core count rather than the core count itself: a prefetch spends some of its time
  // reading from RocksDB rather than on CPU, so a little oversubscription keeps the cores busy.
  // Erring high is the safe direction -- a limit above what the leader can serve merely fails to
  // bind, while one below it turns queueing into connection failures.
  //
  // The floor keeps the bound from biting on a small master. It exists for a fan-in of hundreds of
  // nodes, which a master with one or two cores does not see, and a limit of 2 there would reject
  // the third connection that happens to start alongside two others. That connection then waits
  // out the client's minimum backoff, a few hundred milliseconds, for a prefetch that on a small
  // catalog would have taken a few.
  const auto limit = std::max(8, 2 * base::NumCPUs());
  CHECK_OK(SET_FLAG_DEFAULT_AND_CURRENT(master_max_concurrent_ysql_catalog_prefetches, limit));
  LOG(INFO) << "Auto setting FLAGS_master_max_concurrent_ysql_catalog_prefetches to " << limit;
}

MasterTabletServiceImpl::MasterTabletServiceImpl(MasterTabletServer* server, Master* master)
    : TabletServiceImpl(server),
      master_(master),
      ysql_catalog_prefetches_in_progress_(
          METRIC_ysql_catalog_prefetches_in_progress.Instantiate(master->metric_entity(), 0)),
      ysql_catalog_prefetch_rejections_(
          METRIC_ysql_catalog_prefetch_rejections.Instantiate(master->metric_entity())) {
  AutoInitCatalogPrefetchLimit();
}

namespace {

// Admissions currently held. This rather than the gauge decides admission, because taking a slot
// has to be a single operation and the gauge cannot report what an increment returned. It is
// process-wide rather than per-service: a yb-master hosts one service, and in a test process
// hosting several masters only the leader serves catalog prefetches, so the bound still applies
// per leader either way.
std::atomic<int32_t> prefetches_in_flight{0};

// Only catalog prefetches are bounded. A catalog cache miss, a relcache build and a systable scan
// cost a fraction of a prefetch and run inside a query that is already executing, so delaying them
// would add latency where it is most visible -- and would let prefetches crowd out the cheap reads
// they contend with.
[[nodiscard]] bool IsCatalogPrefetch(const tserver::ReadRequestPB& req) {
  return std::ranges::any_of(req.pgsql_batch(), [](const auto& op) {
    return op.catalog_prefetch_kind() != YSQL_CATALOG_PREFETCH_NONE;
  });
}

// Whether this prefetch is one that something is already blocked on: a backend refreshing its
// catalog cache, which cannot run anything until it completes, or the relcache-init backend, whose
// file every new connection on its node waits for. A connection that is still starting up has
// nothing invested and can afford to wait, so these are admitted on the same footing as work
// already under way.
[[nodiscard]] bool IsCacheRefreshPrefetch(const tserver::ReadRequestPB& req) {
  return std::ranges::any_of(req.pgsql_batch(), [](const auto& op) {
    return op.catalog_prefetch_kind() == YSQL_CATALOG_PREFETCH_CACHE_REFRESH;
  });
}

// Whether an operation carries a paging state at any nesting level. PgsqlReadOp::PrepareNextRequest
// walks to the innermost request before copying the paging state onto it, so for a table read
// through an index -- which is most of what the prefetcher registers -- the state of a continuation
// sits on index_request rather than on the operation itself.
[[nodiscard]] bool HasPagingState(const PgsqlReadRequestPB& op) {
  for (const auto* req = &op; ; req = &req->index_request()) {
    if (req->has_paging_state()) {
      return true;
    }
    if (!req->has_index_request()) {
      return false;
    }
  }
}

// Whether any operation is resuming a prefetch that is already under way. Only the first request
// of a sequence carries no paging state; the prefetcher drops operations as they finish, so later
// requests carry one on every surviving operation. The kind is checked as well so that this holds
// on its own, rather than only where the caller has already established that this is a prefetch.
//
// The two fields sit at opposite ends of a nested request: the prefetcher sets the kind on the
// top-level request only, while PrepareNextRequest puts the paging state on the innermost one only.
// That is why the kind is read off the operation directly and the paging state through
// HasPagingState.
[[nodiscard]] bool IsPrefetchContinuation(const tserver::ReadRequestPB& req) {
  return std::ranges::any_of(req.pgsql_batch(), [](const auto& op) {
    return op.catalog_prefetch_kind() != YSQL_CATALOG_PREFETCH_NONE && HasPagingState(op);
  });
}

}  // namespace

Result<std::shared_ptr<void>> MasterTabletServiceImpl::AdmitRead(
    const tserver::ReadRequestPB& req) {
  if (!FLAGS_enable_ysql_catalog_prefetch_admission || !IsCatalogPrefetch(req)) {
    return std::shared_ptr<void>();
  }

  // Both rejection paths go through here, so an injected rejection is indistinguishable from a
  // real one to the client -- which is the point of the test flag.
  const auto reject = [this](const std::string& reason) {
    ysql_catalog_prefetch_rejections_->Increment();
    return STATUS_FORMAT(ServiceUnavailable, "Rejecting catalog prefetch: $0", reason);
  };

  // Two prefetches can be in AdmitRead at once, so this countdown can fire once more than it was
  // asked for. The test asserts at least one rejection rather than exactly one, which is cheaper
  // than making the flag atomic.
  if (PREDICT_FALSE(FLAGS_TEST_ysql_catalog_prefetch_rejections_to_inject > 0)) {
    // Plain assignment: with annotations enabled the macro yields a writer object that has no
    // compound-assignment operator, so -= would not compile under TSAN.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_ysql_catalog_prefetch_rejections_to_inject) =
        FLAGS_TEST_ysql_catalog_prefetch_rejections_to_inject - 1;
    return reject("injected by TEST_ysql_catalog_prefetch_rejections_to_inject");
  }

  const auto limit = FLAGS_master_max_concurrent_ysql_catalog_prefetches;
  if (limit <= 0) {
    return std::shared_ptr<void>();
  }

  // Only the opening read of a connection-startup prefetch is held to the lower watermark. Once a
  // sequence is under way its later reads use the whole limit -- refusing one of them wastes every
  // read before it -- and so does a backend refreshing its catalog cache, which is blocked on the
  // data rather than merely waiting to begin. One new prefetch is always admissible, or a small
  // limit (<= 0) would admit nothing at all.
  const auto effective_limit = (IsPrefetchContinuation(req) || IsCacheRefreshPrefetch(req))
      ? limit
      : std::max<int32_t>(
            1, narrow_cast<int32_t>(
                   static_cast<int64_t>(limit) *
                   FLAGS_master_new_ysql_catalog_prefetch_pct / 100));

  class Admission {
   public:
    explicit Admission(scoped_refptr<AtomicGauge<int64_t>> in_progress)
        : in_progress_(std::move(in_progress)) {}

    ~Admission() {
      prefetches_in_flight.fetch_sub(1, std::memory_order_acq_rel);
      in_progress_->Decrement();
    }

   private:
    scoped_refptr<AtomicGauge<int64_t>> in_progress_;
  };

  // Take a slot and give it back if that put us over. fetch_add reports the slot this request
  // took, so at most effective_limit are ever admitted. Incrementing a counter and reading it back
  // instead would let two requests each see the other's increment and both give up while a slot
  // stood free.
  //
  // A request on its way to rejection is counted between its fetch_add and its fetch_sub, so one
  // arriving in that window can read more than is really held and be rejected while a slot was
  // free. Every increment has a matching decrement, so this does not accumulate; the window is a
  // few instructions and the client retries.
  const auto taken = prefetches_in_flight.fetch_add(1, std::memory_order_acq_rel);
  if (taken >= effective_limit) {
    prefetches_in_flight.fetch_sub(1, std::memory_order_acq_rel);
    return reject(Format("$0 already in progress, limit is $1", taken, effective_limit));
  }

  ysql_catalog_prefetches_in_progress_->Increment();
  return std::make_shared<Admission>(ysql_catalog_prefetches_in_progress_);
}

Result<std::shared_ptr<tablet::AbstractTablet>> MasterTabletServiceImpl::GetTabletForRead(
  const TabletId& tablet_id, tablet::TabletPeerPtr tablet_peer,
  YBConsistencyLevel consistency_level, tserver::AllowSplitTablet allow_split_tablet,
  tserver::ReadResponsePB* resp) {
  // Ignore looked_up_tablet_peer.
  return master_->catalog_manager()->GetSystemTablet(tablet_id);
}

void MasterTabletServiceImpl::AcquireObjectLocks(
    const tserver::AcquireObjectLockRequestPB* req, tserver::AcquireObjectLockResponsePB* resp,
    rpc::RpcContext context) {
  context.RespondRpcFailure(
      rpc::ErrorStatusPB::ERROR_APPLICATION,
      STATUS(NotSupported, "AcquireObjectLocks is not implemented at masters"));
}

void MasterTabletServiceImpl::ReleaseObjectLocks(
    const tserver::ReleaseObjectLockRequestPB* req, tserver::ReleaseObjectLockResponsePB* resp,
    rpc::RpcContext context) {
  context.RespondRpcFailure(
      rpc::ErrorStatusPB::ERROR_APPLICATION,
      STATUS(NotSupported, "ReleaseObjectLocks is not implemented at masters"));
}

Result<tserver::GetYSQLLeaseInfoResponsePB> MasterTabletServiceImpl::GetYSQLLeaseInfo(
    const tserver::GetYSQLLeaseInfoRequestPB& req, CoarseTimePoint deadline) {
  return STATUS(NotSupported, "GetYSQLLeaseInfo is not implemented at masters");
}

void MasterTabletServiceImpl::Read(const tserver::ReadRequestPB* req,
                                    tserver::ReadResponsePB* resp,
                                    rpc::RpcContext context) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  if (!l.CheckIsInitializedAndIsLeaderOrRespondTServer(resp, &context)) {
    return;
  }

  tserver::TabletServiceImpl::Read(req, resp, std::move(context));
}

void MasterTabletServiceImpl::Write(const tserver::WriteRequestPB* req,
                                    tserver::WriteResponsePB* resp,
                                    rpc::RpcContext context) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  if (!l.CheckIsInitializedAndIsLeaderOrRespondTServer(resp, &context)) {
    return;
  }

  if (PREDICT_FALSE(FLAGS_TEST_ysql_catalog_write_rejection_percentage > 0) &&
      req->pgsql_write_batch_size() > 0 &&
      RandomUniformInt(1, 99) <= FLAGS_TEST_ysql_catalog_write_rejection_percentage) {
    context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
        STATUS(InternalError, "Injected random failure for testing."));
      return;
  }

  bool log_versions = false;
  std::unordered_set<uint32_t> db_oids;
  for (const auto &pg_req : req->pgsql_write_batch()) {
    if (FLAGS_TEST_ysql_require_force_catalog_modifications &&
        !pg_req.force_catalog_modifications()) {
      context.RespondRpcFailure(
          rpc::ErrorStatusPB::ERROR_APPLICATION,
          STATUS(
              InternalError,
              "Catalog update without force_catalog_modifications when "
              "TEST_ysql_require_force_catalog_modifications is set"));
      return;
    }

    {
      auto write_allowed = master_->ysql_manager_impl().ValidateWriteToCatalogTableAllowed(
          pg_req.table_id(), pg_req.force_catalog_modifications());
      if (!write_allowed.ok()) {
        VLOG(1) << "Write to catalog table while it is not allowed: " << pg_req.ShortDebugString();
        context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION, std::move(write_allowed));
        return;
      }
    }

    if (pg_req.is_ysql_catalog_change_using_protobuf()) {
      const auto& res = master_->catalog_manager()->IncrementYsqlCatalogVersion();
      if (!res.ok()) {
        context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
            STATUS(InternalError, "Failed to increment YSQL catalog version"));
      }
    } else if (FLAGS_log_ysql_catalog_versions && pg_req.table_id() == kPgYbCatalogVersionTableId) {
      log_versions = true;
      if (FLAGS_ysql_enable_db_catalog_version_mode) {
        // The contents of req->pgsql_write_batch() are freed after the next call to
        // tserver::TabletServiceImpl::Write, save db_oid to use for later debugging log.

        // The write op to increment the catalog version number is special and may not have
        // set any of ysql_catalog_version, ysql_db_catalog_version, and ysql_db_oid. Therefore
        // we need to get db oid by decoding from ybctid.
        dockv::DocKey doc_key;
        if (!pg_req.has_ybctid_column_value() ||
            !doc_key.FullyDecodeFrom(pg_req.ybctid_column_value().value().binary_value()).ok() ||
            doc_key.range_group().size() != 1) {
          context.RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
              STATUS(InternalError, "Failed to get db oid"));
        }
        const uint32_t db_oid = doc_key.range_group()[0].GetUInt32();
        // In per-db catalog version mode, one can run a SQL script to prepare the
        // pg_yb_catalog_version table to have one row per-database, there can be
        // multiple write ops that write to the table pg_yb_catalog_version for
        // different rows.
        // We do not expect multiple write ops that write to the same db_oid because
        // db_oid is the primary key and duplicate key error should be reported.
        if (!db_oids.insert(db_oid).second) {
          LOG(DFATAL) << "Unexpected multiple writes to db_oid "
                      << db_oid << ", req: " << req->ShortDebugString();
        }
      }
    }
  }

  VLOG(5) << "Master write req: " << req->ShortDebugString();
  tserver::TabletServiceImpl::Write(req, resp, std::move(context));

  if (log_versions) {
    uint64_t catalog_version;
    uint64_t last_breaking_version;
    // The above Write is async, so delay a bit to hopefully read the newly written values.  If the
    // delay was not sufficient, it's not a big deal since this is just for logging.
    SleepFor(100ms);
    if (!db_oids.empty()) {
      for (const auto db_oid : db_oids) {
        if (!master_->catalog_manager()->GetYsqlDBCatalogVersion(db_oid, &catalog_version,
                                                                 &last_breaking_version).ok()) {
          LOG_WITH_FUNC(DFATAL) << "failed to get db catalog version for "
                                << db_oid << ", ignoring";
        } else {
          LOG_WITH_FUNC(INFO) << "db catalog version for " << db_oid << ": "
                              << catalog_version << ", breaking version: "
                              << last_breaking_version;
        }
      }
    } else {
      if (!master_->catalog_manager()->GetYsqlCatalogVersion(&catalog_version,
                                                             &last_breaking_version).ok()) {
        LOG_WITH_FUNC(DFATAL) << "failed to get catalog version, ignoring";
      } else {
        LOG_WITH_FUNC(INFO) << "catalog version: " << catalog_version << ", breaking version: "
                            << last_breaking_version;
      }
    }
  }
}

void MasterTabletServiceImpl::IsTabletServerReady(
    const tserver::IsTabletServerReadyRequestPB* req,
    tserver::IsTabletServerReadyResponsePB* resp,
    rpc::RpcContext context) {
  SCOPED_LEADER_SHARED_LOCK(l, master_->catalog_manager_impl());
  int total_tablets = NUM_TABLETS_SYS_CATALOG;
  resp->set_total_tablets(total_tablets);
  resp->set_num_tablets_not_running(total_tablets);

  // Tablet 0 being ready corresponds to state_ = kRunning in catalog manager.
  // If catalog_status_ in not OK, then catalog manager state_ is not kRunning.
  if (!l.CheckIsInitializedOrRespondTServer(resp, &context, false /* set_error */)) {
    LOG(INFO) << "Zero tablets not running out of " << total_tablets;
  } else {
    LOG(INFO) << "All " << total_tablets << " tablets running.";
    resp->set_num_tablets_not_running(0);
    context.RespondSuccess();
  }
}

namespace {

void HandleUnsupportedMethod(const char* method_name, rpc::RpcContext* context) {
  context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_APPLICATION,
                             STATUS_FORMAT(NotSupported, "$0 Not Supported!", method_name));
}

}  // namespace

void MasterTabletServiceImpl::ListMasterServers(
    const tserver::ListMasterServersRequestPB* req, tserver::ListMasterServersResponsePB* resp,
    rpc::RpcContext context) {
  HandleUnsupportedMethod("ListMasterServers", &context);
}

void MasterTabletServiceImpl::ListTablets(const tserver::ListTabletsRequestPB* req,
                                          tserver::ListTabletsResponsePB* resp,
                                          rpc::RpcContext context)  {
  HandleUnsupportedMethod("ListTablets", &context);
}

void MasterTabletServiceImpl::ListTabletsForTabletServer(
    const tserver::ListTabletsForTabletServerRequestPB* req,
    tserver::ListTabletsForTabletServerResponsePB* resp,
    rpc::RpcContext context)  {
  HandleUnsupportedMethod("ListTabletsForTabletServer", &context);
}

void MasterTabletServiceImpl::GetLogLocation(const tserver::GetLogLocationRequestPB* req,
                                             tserver::GetLogLocationResponsePB* resp,
                                             rpc::RpcContext context)  {
  HandleUnsupportedMethod("GetLogLocation", &context);
}

void MasterTabletServiceImpl::Checksum(const tserver::ChecksumRequestPB* req,
                                       tserver::ChecksumResponsePB* resp,
                                       rpc::RpcContext context)  {
  HandleUnsupportedMethod("Checksum", &context);
}

void MasterTabletServiceImpl::AdminExecutePgsql(
    const tserver::AdminExecutePgsqlRequestPB* req, tserver::AdminExecutePgsqlResponsePB* resp,
    rpc::RpcContext context) {
  HandleUnsupportedMethod("AdminExecutePgsql", &context);
}

} // namespace master
} // namespace yb
