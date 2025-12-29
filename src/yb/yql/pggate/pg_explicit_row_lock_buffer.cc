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

#include <string>
#include <utility>

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"

#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/util/ybc_util.h"

namespace yb::pggate {

Status ExplicitRowLockBuffer::Add(
    const Info& info, const LightweightTableYbctid& key,
    const YbcPgTableLocalityInfo& locality_info,
    std::optional<ErrorStatusAdditionalInfo>& error_info) {
  if (info_ && *info_ != info) {
    RETURN_NOT_OK(DoFlush(error_info));
  }
  if (!info_) {
    info_ = info;
  } else if (intents_.contains(key)) {
    return Status::OK();
  }

  table_locality_map_.Add(key.table_id, locality_info);
  intents_.emplace(key.table_id, key.ybctid);
  return narrow_cast<int>(intents_.size()) >= yb_explicit_row_locking_batch_size
      ? DoFlush(error_info) : Status::OK();
}

Status ExplicitRowLockBuffer::Flush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
  return IsEmpty() ? Status::OK() : DoFlush(error_info);
}

Status ExplicitRowLockBuffer::DoFlush(std::optional<ErrorStatusAdditionalInfo>& error_info) {
  DCHECK(!IsEmpty());
  auto scope = ScopeExit([this] { Clear(); });
  auto status = DoFlushImpl();
  if (!status.ok()) {
    error_info.emplace(
        info_->pg_wait_policy, TransactionError(status).value() == TransactionErrorCode::kNone
                                   ? kInvalidOid
                                   : RelationOid::ValueFromStatus(status).value_or(kInvalidOid));
  }
  return status;
}

Status ExplicitRowLockBuffer::DoFlushImpl() {
  const auto intents_count = intents_.size();
  auto batch = ybctid_reader_.StartNewBatch(intents_count);
  // The reader accepts Slice. It is required to keep data alive.
  MemoryOptimizedTableYbctidSet intents;
  intents.swap(intents_);
  for (const auto& intent : intents) {
    batch.Add(intent);
  }
  const auto existing_ybctids_count = VERIFY_RESULT(batch.Read(
      info_->database_id, table_locality_map_,
      make_lw_function(
          [&info = *info_](YbcPgExecParameters& params) {
            params.rowmark = info.rowmark;
            params.pg_wait_policy = info.pg_wait_policy;
            params.docdb_wait_policy = info.docdb_wait_policy;
          }))).size();
  // Make a swap back to preserve memory allocated for buckets
  intents.clear();
  intents_.swap(intents);
  SCHECK_EQ(
      existing_ybctids_count, intents_count, NotFound, "Some of the requested ybctids are missing");
  return Status::OK();
}

void ExplicitRowLockBuffer::Clear() {
  intents_.clear();
  info_.reset();
  table_locality_map_.Clear();
}

bool ExplicitRowLockBuffer::IsEmpty() const {
  return !info_;
}

} // namespace yb::pggate
