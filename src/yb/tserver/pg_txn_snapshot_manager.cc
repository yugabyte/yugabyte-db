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

#include "yb/tserver/pg_txn_snapshot_manager.h"

#include <mutex>
#include <optional>
#include <regex>
#include <shared_mutex>

#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/common/wire_protocol.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.pb.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/shared_lock.h"

DECLARE_int32(yb_client_admin_operation_timeout_sec);

using namespace std::literals;

namespace yb::tserver {
namespace {

struct FullPgTxnSnapshotId {
  std::string tserver_uuid;
  PgTxnSnapshotLocalId snapshot_id;

  static std::optional<FullPgTxnSnapshotId> TryDecode(const std::string& snapshot_id) {
    static const std::regex kPattern(R"(^([a-f0-9]{32})-([a-f0-9]{32})$)");
    std::smatch match;
    if (!std::regex_match(snapshot_id, match, kPattern)) {
      return std::nullopt;
    }
    auto tserver_uuid = match.str(1);
    if (!Uuid::FromHexStringBigEndian(tserver_uuid).ok()) {
      return std::nullopt;
    }
    auto snapshot_id_result = PgTxnSnapshotLocalIdFromString(match.str(2));
    if (!snapshot_id_result.ok()) {
      return std::nullopt;
    }
    return FullPgTxnSnapshotId(std::move(tserver_uuid), std::move(*snapshot_id_result));
  }

 private:
  FullPgTxnSnapshotId(std::string&& tserver_uuid_, PgTxnSnapshotLocalId&& snapshot_id_)
      : tserver_uuid(std::move(tserver_uuid_)), snapshot_id(std::move(snapshot_id_)) {}
};

} // namespace

class PgTxnSnapshotManager::Impl {
 public:
  Impl(const std::string& instance_id, TsProxyProvider&& proxy_provider)
      : instance_id_(instance_id), proxy_provider_(proxy_provider) {}

  Result<std::string> Register(uint64_t session_id, const PgTxnSnapshot& snapshot) {
    std::lock_guard lock(mutex_);
    for (size_t i = 0; i < 5; ++i) {
      auto ipair = snapshots_.emplace(PgTxnSnapshotInfo{
          .snapshot_id = PgTxnSnapshotLocalId(Uuid::Generate()),
          .snapshot = std::move(snapshot),
          .session_id = session_id});

      if (ipair.second) {
        return Format("$0-$1", instance_id_, ipair.first->snapshot_id.ToString());
      }
    }
    return STATUS(IllegalState, "Failed to generate UUID for snapshot");
  }

  void UnregisterAll(uint64_t session_id) {
    std::lock_guard lock(mutex_);
    snapshots_.get<SessionIdIndex>().erase(session_id);
  }

  Result<PgTxnSnapshot> Get(const std::string& encoded_snapshot_id) {
    auto decoded = FullPgTxnSnapshotId::TryDecode(encoded_snapshot_id);
    SCHECK(decoded.has_value(), InvalidArgument, "Invalid Snapshot Id $0", encoded_snapshot_id);
    auto result = decoded->tserver_uuid == instance_id_
        ? Get(decoded->snapshot_id) : GetFromRemote(decoded->tserver_uuid, decoded->snapshot_id);
    SCHECK_FORMAT(
        result.ok() || !result.status().IsNotFound(), NotFound, "Could not find Snapshot $0",
        encoded_snapshot_id);
    return result;
  }

  Result<PgTxnSnapshot> Get(const PgTxnSnapshotLocalId& snapshot_id) {
    SharedLock lock(mutex_);
    const auto& snapshot_id_index = snapshots_.get<PgTxnSnapshotLocalIdIndex>();
    auto it = snapshot_id_index.find(snapshot_id);
    SCHECK(it != snapshot_id_index.end(), NotFound, "Snapshot not found $0", snapshot_id);
    return it->snapshot;
  }

 private:
  Result<PgTxnSnapshot> GetFromRemote(
      const std::string& ts_uuid, const PgTxnSnapshotLocalId& snapshot_id) {
    auto proxy = VERIFY_RESULT(proxy_provider_(ts_uuid));
    tserver::GetLocalPgTxnSnapshotRequestPB req;
    tserver::GetLocalPgTxnSnapshotResponsePB resp;
    const auto snapshot_id_data = snapshot_id.AsSlice();
    req.set_snapshot_id(snapshot_id_data.data(), snapshot_id_data.size());
    rpc::RpcController controller;
    controller.set_timeout(FLAGS_yb_client_admin_operation_timeout_sec * 1s);
    RETURN_NOT_OK(proxy->GetLocalPgTxnSnapshot(req, &resp, &controller));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }

    return PgTxnSnapshot::Make(resp.snapshot(), ReadHybridTime::FromPB(resp.snapshot_read_time()));
  }

  const std::string& instance_id_;
  const TsProxyProvider proxy_provider_;

  std::shared_mutex mutex_;

  struct PgTxnSnapshotInfo {
    PgTxnSnapshotLocalId snapshot_id;
    PgTxnSnapshot snapshot;
    uint64_t session_id;
  };

  struct PgTxnSnapshotLocalIdIndex;
  struct SessionIdIndex;

  using PgTxnSnapshots = boost::multi_index::multi_index_container<
      PgTxnSnapshotInfo,
      boost::multi_index::indexed_by<
          // Index by PgTxnSnapshotLocalId
          boost::multi_index::hashed_unique<
              boost::multi_index::tag<PgTxnSnapshotLocalIdIndex>,
              boost::multi_index::member<
                  PgTxnSnapshotInfo, PgTxnSnapshotLocalId, &PgTxnSnapshotInfo::snapshot_id>>,
          // Index by session_id
          boost::multi_index::ordered_non_unique<
              boost::multi_index::tag<SessionIdIndex>,
              boost::multi_index::member<
                  PgTxnSnapshotInfo, uint64_t, &PgTxnSnapshotInfo::session_id>>>>;

  PgTxnSnapshots snapshots_ GUARDED_BY(mutex_);
};

PgTxnSnapshotManager::PgTxnSnapshotManager(
    std::reference_wrapper<const std::string> instance_id, TsProxyProvider&& proxy_provider)
    : impl_(new Impl(instance_id, std::move(proxy_provider))) {
}

PgTxnSnapshotManager::~PgTxnSnapshotManager() = default;

Result<std::string> PgTxnSnapshotManager::Register(
    uint64_t session_id, const PgTxnSnapshot& snapshot) {
  return impl_->Register(session_id, snapshot);
}

void PgTxnSnapshotManager::UnregisterAll(uint64_t session_id) {
  return impl_->UnregisterAll(session_id);
}

Result<PgTxnSnapshot> PgTxnSnapshotManager::Get(const std::string& snapshot_id) {
  return impl_->Get(snapshot_id);
}

Result<PgTxnSnapshot> PgTxnSnapshotManager::Get(const PgTxnSnapshotLocalId& snapshot_id) {
  return impl_->Get(snapshot_id);
}

} // namespace yb::tserver
