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

#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/util/scope_exit.h"

namespace yb::pggate {

ExplicitRowLockBuffer::ExplicitRowLockBuffer(
    TableYbctidVectorProvider& ybctid_container_provider,
    std::reference_wrapper<const YbctidReader> ybctid_reader)
    : ybctid_container_provider_(ybctid_container_provider), ybctid_reader_(ybctid_reader) {
}

Status ExplicitRowLockBuffer::Add(
    Info&& info, const LightweightTableYbctid& key, bool is_region_local) {
  if (info_ && *info_ != info) {
    RETURN_NOT_OK(DoFlush());
  }
  if (!info_) {
    info_.emplace(std::move(info));
  } else if (intents_.contains(key)) {
    return Status::OK();
  }

  if (is_region_local) {
    region_local_tables_.insert(key.table_id);
  }
  DCHECK(is_region_local || !region_local_tables_.contains(key.table_id));
  intents_.emplace(key.table_id, std::string(key.ybctid));
  return narrow_cast<int>(intents_.size()) >= yb_explicit_row_locking_batch_size
      ? DoFlush() : Status::OK();
}

Status ExplicitRowLockBuffer::Flush() {
  return IsEmpty() ? Status::OK() : DoFlush();
}

Status ExplicitRowLockBuffer::DoFlush() {
  DCHECK(!IsEmpty());
  auto scope = ScopeExit([this] { Clear(); });
  auto ybctids = ybctid_container_provider_.Get();
  auto initial_intents_size = intents_.size();
  ybctids->reserve(initial_intents_size);
  for (auto it = intents_.begin(); it != intents_.end();) {
    auto node = intents_.extract(it++);
    ybctids->push_back(std::move(node.value()));
  }
  RETURN_NOT_OK(ybctid_reader_(
      info_->database_id, ybctids, region_local_tables_,
      make_lw_function(
          [&info = *info_](PgExecParameters& params) {
            params.rowmark = info.rowmark;
            params.pg_wait_policy = info.pg_wait_policy;
            params.docdb_wait_policy = info.docdb_wait_policy;
          })));
  SCHECK(
      initial_intents_size == ybctids->size(), NotFound,
      "Some of the requested ybctids are missing");
  return Status::OK();
}

void ExplicitRowLockBuffer::Clear() {
  intents_.clear();
  info_.reset();
  region_local_tables_.clear();
}

bool ExplicitRowLockBuffer::IsEmpty() const {
  return !info_;
}

} // namespace yb::pggate
