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

#include "yb/yql/pggate/pg_explicit_row_lock_buffer.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"


#include "yb/util/status_format.h"

#include "yb/yql/pggate/util/ybc_util.h"

namespace yb::pggate {
namespace {

struct SkippableLockInfo {
  YbcIsExplicitlyLockedRowSkippedCheckHandle handle;
  LightweightTableYbctid ybctid;
  uint32_t handle_idx;
};

using LightweightYbctidSet =
    std::unordered_set<LightweightTableYbctid, TableYbctidHasher, TableYbctidComparator>;

} // namespace

class ExplicitRowLockBuffer::Impl {
 public:
  explicit Impl(PgSession& session) : ybctid_reader_(session) {}

  Status Add(const AddLockData& data) {
    return DoAdd(data);
  }

  Result<YbcIsExplicitlyLockedRowSkippedCheckHandle> AddSkippable(
      const AddLockData& data, std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle> handle) {
    RETURN_NOT_OK(DoAdd(data, &handle));
    return *handle;
  }

  Status Flush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
    const auto has_pending = HasPendingLocks();
    VLOG(5) << "Flush has pending: " <<  has_pending
            << " intents size: " << pending_.intents.size();
    return has_pending ? DoFlush(error_info) : Status::OK();
  }

  void Clear() {
    pending_ = {};
    in_flight_ = {};
    ybctids_set_.clear();
    skipped_.clear();
    latest_known_check_handle_.reset();
  }

  bool HasPendingLocks() const {
    return !pending_.intents.empty();
  }

  Result<bool> IsSkipped(
      YbcIsExplicitlyLockedRowSkippedCheckHandle handle,
      std::optional<ErrorStatusAdditionalInfo>& error_info) {

    if (skipped_.erase(handle)) {
      return true;
    }

    if (pending_.skippable.empty()) {
      return false;
    }

    RETURN_NOT_OK(DoFlush(error_info));
    return skipped_.erase(handle);
  }

 private:
  struct Locks {
    std::optional<LockInfo> info;
    MemoryOptimizedTableYbctidSet intents;
    TableLocalityMap table_locality_map;
    std::vector<SkippableLockInfo> skippable;
  };

  Status DoAdd(
      const AddLockData& data,
      std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle>* handle = nullptr) {
    auto& info = pending_.info;
    if (info && *info != data.lock_info && HasPendingLocks()) {
      RETURN_NOT_OK(DoFlush(data.error_info));
    }

    if (!info) {
      info = data.lock_info;
    }
    const auto& ybctid = data.lock_key.ybctid;
    const auto& table_id = data.lock_key.table_id;

    auto ipair = pending_.intents.emplace(table_id, ybctid);
    if (ipair.second) {
      pending_.table_locality_map.Add(table_id, data.table_locality);
    }

    if (handle) {
      auto& skippable = pending_.skippable;
      size_t handle_idx = 0;
      // Handle is kind of a signature to indicate that multiple ybctids belongs to it.
      // It is expected that all the ybctids for same handle are added one after another,
      // so only the latest handle is valid.
      if (handle->has_value()) {
        RSTATUS_DCHECK(
            *handle == latest_known_check_handle_, IllegalState,
            "Latest known skip locked handle is only expected (got $0 instead of $1)",
            *handle, latest_known_check_handle_);
        // Skippable candidates could be empty because of previous flush.
        DCHECK(skippable.empty() || **handle == skippable.back().handle);
        if (!skippable.empty()) [[likely]] {
          handle_idx = skippable.back().handle_idx + 1;
        }
      } else {
        latest_known_check_handle_ =
            latest_known_check_handle_.has_value() ? (*latest_known_check_handle_ + 1) : 0;
        handle->emplace(*latest_known_check_handle_);
      }
      skippable.emplace_back(**handle, *ipair.first, handle_idx);
    }

    return narrow_cast<int>(pending_.intents.size()) >= yb_explicit_row_locking_batch_size
        ? DoFlush(data.error_info) : Status::OK();
  }

  Status DoFlush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
    DCHECK(HasPendingLocks());
    // Detach the batch before sending it, so a flush of this buffer during the send finds nothing
    // pending: the Perform for a catalog batch switches to the catalog read point, which flushes
    // all buffered entities. PG code runs during the send, and an ereport there longjmps past
    // this frame; the batch is then dropped by the next flush or Clear() instead of staying
    // pending.
    in_flight_ = std::exchange(pending_, {});
    auto status = DoFlushImpl();
    if (!status.ok()) {
      error_info.emplace(
          in_flight_.info->pg_wait_policy,
          TransactionError(status).value() == TransactionErrorCode::kNone
              ? kInvalidOid
              : RelationOid::ValueFromStatus(status).value_or(kInvalidOid));
    }
    in_flight_ = {};
    ybctids_set_.clear();
    return status;
  }

  auto DoRead(YbctidReader::BatchAccessor& batch) {
    const auto& info = *in_flight_.info;
    return batch.Read(
        info.database_id, in_flight_.table_locality_map,
        {.rowmark = info.rowmark,
        .pg_wait_policy = info.pg_wait_policy,
        .docdb_wait_policy = info.docdb_wait_policy});
  }

  Status DoFlushImpl() {
    return in_flight_.skippable.empty() ? DoFlushRegular() : DoFlushSkippable();
  }

  Status DoFlushRegular() {
    const auto& intents = in_flight_.intents;
    DCHECK(in_flight_.skippable.empty());
    const auto intents_count = intents.size();
    auto batch = ybctid_reader_.StartNewBatch(intents_count);
    for (const auto& intent : intents) {
      batch.Add(intent);
    }
    const auto ybctids_count = VERIFY_RESULT(DoRead(batch)).size();
    SCHECK_EQ(ybctids_count, intents_count, NotFound, "Some of the requested ybctids are missing");
    return Status::OK();
  }

  Status DoFlushSkippable() {
    auto& skippable = in_flight_.skippable;
    DCHECK(!skippable.empty());
    const auto intents_count = in_flight_.intents.size();
    std::ranges::sort(
        skippable,
        [](const auto& lhs, const auto& rhs) { return lhs.handle_idx < rhs.handle_idx; });
    for(auto i = skippable.begin(); i != skippable.end();) {
      auto batch = ybctid_reader_.StartNewBatch(intents_count);
      size_t batch_size = 0;
      const auto group_start = i;
      const auto group_handle_idx = group_start->handle_idx;
      for(; i != skippable.end() && i->handle_idx == group_handle_idx; ++i) {
        if (!skipped_.contains(i->handle)) {
          batch.Add(i->ybctid);
          ++batch_size;
        }
      }
      if (!batch_size) {
        break;
      }

      auto ybctids = VERIFY_RESULT(DoRead(batch));
      if (ybctids.size() != batch_size) {
        ybctids_set_.clear();
        std::ranges::copy(ybctids, std::inserter(ybctids_set_, ybctids_set_.begin()));
        for (auto j = group_start; j != i; ++j) {
          if (!ybctids_set_.contains(j->ybctid)) {
            skipped_.insert(j->handle);
          }
        }
        ybctids_set_.clear();
      }
    }
    return Status::OK();
  }

  YbctidReader ybctid_reader_;
  Locks pending_;
  Locks in_flight_;

  std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle> latest_known_check_handle_;
  LightweightYbctidSet ybctids_set_;
  std::unordered_set<YbcIsExplicitlyLockedRowSkippedCheckHandle> skipped_;
};

ExplicitRowLockBuffer::ExplicitRowLockBuffer(PgSession& session)
    : impl_(new Impl(session)) {}

ExplicitRowLockBuffer::~ExplicitRowLockBuffer() = default;

Status ExplicitRowLockBuffer::Add(const AddLockData& data) {
  return impl_->Add(data);
}

Result<YbcIsExplicitlyLockedRowSkippedCheckHandle> ExplicitRowLockBuffer::AddSkippable(
    const AddLockData& data,
    std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle> handle) {
  return impl_->AddSkippable(data, handle);
}

Status ExplicitRowLockBuffer::Flush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
  return impl_->Flush(error_info);
}

void ExplicitRowLockBuffer::Clear() {
  impl_->Clear();
}

bool ExplicitRowLockBuffer::HasPendingLocks() const {
  return impl_->HasPendingLocks();
}

Result<bool> ExplicitRowLockBuffer::IsSkipped(
    YbcIsExplicitlyLockedRowSkippedCheckHandle handle,
    std::optional<ErrorStatusAdditionalInfo>& error_info) {
  return impl_->IsSkipped(handle, error_info);
}

} // namespace yb::pggate
