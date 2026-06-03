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

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"


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

  Status Add(
      const Info& info, const LightweightTableYbctid& key,
      const YbcPgTableLocalityInfo& locality_info,
      std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle> handle,
      std::optional<ErrorStatusAdditionalInfo>& error_info) {
    if (info_ && *info_ != info && HasPendingLocks()) {
      RETURN_NOT_OK(DoFlush(error_info));
    }

    if (!info_) {
      info_ = info;
    }

    auto ipair = intents_.emplace(key.table_id, key.ybctid);
    if (ipair.second) {
      table_locality_map_.Add(key.table_id, locality_info);
    }

    if (handle) {
      DCHECK(*handle == next_check_handle_ - 1);
      const auto* last = skippable_.empty() ? nullptr : &skippable_.back();
      const uint32_t handle_idx =  (!last || last->handle != *handle) ? 0 : (last->handle_idx + 1);
      skippable_.emplace_back(*handle, *ipair.first, handle_idx);
    }

    return narrow_cast<int>(intents_.size()) >= yb_explicit_row_locking_batch_size
        ? DoFlush(error_info) : Status::OK();
  }

  Status Flush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
    const auto has_pending = HasPendingLocks();
    VLOG(5) << "Flush has pending: " <<  has_pending << " intents size: " << intents_.size();
    return has_pending ? DoFlush(error_info) : Status::OK();
  }

  void Clear() {
    ClearIntents();
    skipped_.clear();
  }

  bool HasPendingLocks() const {
    return !intents_.empty();
  }

  Result<bool> IsSkipped(
      YbcIsExplicitlyLockedRowSkippedCheckHandle handle,
      std::optional<ErrorStatusAdditionalInfo>& error_info) {

    if (skipped_.erase(handle)) {
      return true;
    }

    if (skippable_.empty()) {
      return false;
    }

    RETURN_NOT_OK(DoFlush(error_info));
    return skipped_.erase(handle);
  }

  [[nodiscard]] YbcIsExplicitlyLockedRowSkippedCheckHandle AcquireCheckHandle() {
    return next_check_handle_++;
  }

 private:
  void ClearIntents() {
    info_.reset();
    intents_.clear();
    table_locality_map_.Clear();
    ybctids_set_.clear();
    skippable_.clear();
  }

  Status DoFlush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
    DCHECK(HasPendingLocks());
    auto status = DoFlushImpl();
    if (!status.ok()) {
      error_info.emplace(
          info_->pg_wait_policy,
          TransactionError(status).value() == TransactionErrorCode::kNone
              ? kInvalidOid
              : RelationOid::ValueFromStatus(status).value_or(kInvalidOid));
    }
    ClearIntents();
    return status;
  }

  auto DoRead(YbctidReader::BatchAccessor& batch) {
    return batch.Read(
        info_->database_id, table_locality_map_,
        {.rowmark = info_->rowmark,
        .pg_wait_policy = info_->pg_wait_policy,
        .docdb_wait_policy = info_->docdb_wait_policy,
        .run_marker = PgSessionRunOperationMarker::ExplicitRowLock});
  }

  Status DoFlushImpl() {
    return skippable_.empty() ? DoFlushRegular() : DoFlushSkippable();
  }

  Status DoFlushRegular() {
    DCHECK(skippable_.empty());
    const auto intents_count = intents_.size();
    auto batch = ybctid_reader_.StartNewBatch(intents_count);
    for (const auto& intent : intents_) {
      batch.Add(intent);
    }
    const auto ybctids_count = VERIFY_RESULT(DoRead(batch)).size();
    SCHECK_EQ(ybctids_count, intents_count, NotFound, "Some of the requested ybctids are missing");
    return Status::OK();
  }

  Status DoFlushSkippable() {
    DCHECK(!skippable_.empty());
    const auto intents_count = intents_.size();
    std::ranges::sort(
        skippable_,
        [](const auto& lhs, const auto& rhs) { return lhs.handle_idx < rhs.handle_idx; });
    for(auto i = skippable_.begin(); i != skippable_.end();) {
      auto batch = ybctid_reader_.StartNewBatch(intents_count);
      size_t batch_size = 0;
      const auto group_start = i;
      const auto group_handle_idx = group_start->handle_idx;
      for(; i != skippable_.end() && i->handle_idx == group_handle_idx; ++i) {
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
  std::optional<Info> info_;
  MemoryOptimizedTableYbctidSet intents_;
  TableLocalityMap table_locality_map_;

  YbcIsExplicitlyLockedRowSkippedCheckHandle next_check_handle_{0};
  LightweightYbctidSet ybctids_set_;
  std::unordered_set<YbcIsExplicitlyLockedRowSkippedCheckHandle> skipped_;
  std::vector<SkippableLockInfo> skippable_;
};

ExplicitRowLockBuffer::ExplicitRowLockBuffer(PgSession& session)
    : impl_(new Impl(session)) {}

ExplicitRowLockBuffer::~ExplicitRowLockBuffer() = default;

Status ExplicitRowLockBuffer::Add(
      const Info& info, const LightweightTableYbctid& key,
      const YbcPgTableLocalityInfo& locality_info,
      std::optional<YbcIsExplicitlyLockedRowSkippedCheckHandle> handle,
      std::optional<ErrorStatusAdditionalInfo>& error_info) {
  return impl_->Add(info, key, locality_info, handle, error_info);
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

YbcIsExplicitlyLockedRowSkippedCheckHandle ExplicitRowLockBuffer::AcquireCheckHandle() {
  return impl_->AcquireCheckHandle();
}

Result<bool> ExplicitRowLockBuffer::IsSkipped(
    YbcIsExplicitlyLockedRowSkippedCheckHandle handle,
    std::optional<ErrorStatusAdditionalInfo>& error_info) {
  return impl_->IsSkipped(handle, error_info);
}

} // namespace yb::pggate
