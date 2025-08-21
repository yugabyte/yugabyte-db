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

#include "yb/yql/pggate/pg_fk_reference_cache.h"

#include <algorithm>
#include <string>
#include <utility>

#include "yb/gutil/port.h"

#include "yb/util/flags/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/scope_exit.h"

#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pg_ybctid_reader_provider.h"
#include "yb/yql/pggate/util/ybc_guc.h"

namespace yb::pggate {

namespace {

template<class Container, class Key>
void Erase(Container& container, const Key& key) {
  const auto it = container.find(key);
  if (it != container.end()) {
    container.erase(it);
  }
}

} // namespace

class PgFKReferenceCache::Impl {
 public:
  Impl(YbctidReaderProvider& reader_provider, const BufferingSettings& buffering_settings)
      : reader_provider_(reader_provider), buffering_settings_(buffering_settings) {
  }

  void Clear() {
    references_.clear();
    regular_intents_.clear();
    deferred_intents_.clear();
    region_local_tables_.clear();
    intents_ = &regular_intents_;
    references_cache_limit_.reset();
  }

  void DeleteReference(const LightweightTableYbctid& key) {
    Erase(references_, key);
  }

  void AddReference(const LightweightTableYbctid& key) {
    if (!references_cache_limit_) {
      references_cache_limit_ = yb_fk_references_cache_limit;
    }
    if (PREDICT_TRUE(references_.size() < *references_cache_limit_) &&
        !references_.contains(key)) {
      references_.emplace(key.table_id, key.ybctid);
    }
  }

  Result<bool> IsReferenceExists(PgOid database_id, const LightweightTableYbctid& key) {
    if (references_.contains(key)) {
      return true;
    }

    auto reader = reader_provider_();
    auto residual_capacity = std::min<size_t>(intents_->size(), buffering_settings_.max_batch_size);
    reader.Reserve(residual_capacity);

    // Check existence of required FK intent.
    const auto intents_end = intents_->end();
    if (const auto it = intents_->find(key); it == intents_end) {
      if (!IsDeferredTriggersProcessingStarted()) {
        // In case of processing non deferred intents absence means the key was checked by previous
        // batched request and was not found. We don't need to call the reader in this case.
        return false;
      }
      // In case of processing deferred intents absence of intent could be caused by
      // subtxnransaction rollback. In this case we have to make a read attempt.
    } else {
      intents_->erase(it);
    }
    reader.Add(LightweightTableYbctid{key.table_id, key.ybctid});
    --residual_capacity;

    auto it = intents_->begin();
    for (; it != intents_end && residual_capacity; --residual_capacity, ++it) {
      reader.Add(*it);
    }
    ScopeExit intents_cleanup([this, erase_end = it] {
      intents_->erase(intents_->begin(), erase_end);
    });
    // Add the keys found in docdb to the FK cache.
    auto ybctids = VERIFY_RESULT(reader.Read(
        database_id, region_local_tables_,
        make_lw_function([](YbcPgExecParameters& params) { params.rowmark = ROW_MARK_KEYSHARE; })));
    for (const auto& ybctid : ybctids) {
      references_.emplace(ybctid.table_id, ybctid.ybctid);
    }
    return references_.contains(key);
  }

  void AddIntent(const LightweightTableYbctid& key, const IntentOptions& options) {
    LOG_IF(DFATAL, IsDeferredTriggersProcessingStarted())
        << "AddIntent is not expected after deferred trigger processing start";

    if (references_.contains(key)) {
        return;
    }

    if (options.is_region_local) {
      region_local_tables_.insert(key.table_id);
    }
    LOG_IF(DFATAL, !options.is_region_local && region_local_tables_.contains(key.table_id))
        << "The " << key.table_id << " table was previously reported as region local";
    (options.is_deferred ? &deferred_intents_ : &regular_intents_)->emplace(
        key.table_id, std::string(key.ybctid));
  }

  void OnDeferredTriggersProcessingStarted() {
    LOG_IF(DFATAL, IsDeferredTriggersProcessingStarted())
        << "Multiple call of OnDeferredTriggersProcessingStarted is not expected";
    LOG_IF(DFATAL, !regular_intents_.empty())
        << "OnDeferredTriggersProcessingStarted implies all non deferred intents were processed";
    intents_ = &deferred_intents_;
  }

 private:
  [[nodiscard]] bool IsDeferredTriggersProcessingStarted() const {
    return intents_ == &deferred_intents_;
  }

  YbctidReaderProvider& reader_provider_;
  const BufferingSettings& buffering_settings_;
  MemoryOptimizedTableYbctidSet references_;
  TableYbctidSet regular_intents_;
  TableYbctidSet deferred_intents_;
  TableYbctidSet* intents_ = &regular_intents_;
  OidSet region_local_tables_;
  std::optional<size_t> references_cache_limit_;
};

PgFKReferenceCache::PgFKReferenceCache(
    YbctidReaderProvider& reader_provider,
    std::reference_wrapper<const BufferingSettings> buffering_settings)
    : impl_(new Impl(reader_provider, buffering_settings)) {}

PgFKReferenceCache::~PgFKReferenceCache() = default;

void PgFKReferenceCache::Clear() {
  impl_->Clear();
}

void PgFKReferenceCache::DeleteReference(const LightweightTableYbctid& key) {
  impl_->DeleteReference(key);
}

void PgFKReferenceCache::AddReference(const LightweightTableYbctid& key) {
  impl_->AddReference(key);
}

Result<bool> PgFKReferenceCache::IsReferenceExists(
    PgOid database_id, const LightweightTableYbctid& key) {
  return impl_->IsReferenceExists(database_id, key);
}

void PgFKReferenceCache::AddIntent(
    const LightweightTableYbctid& key, const IntentOptions& options) {
  impl_->AddIntent(key, options);
}

void PgFKReferenceCache::OnDeferredTriggersProcessingStarted() {
  impl_->OnDeferredTriggersProcessingStarted();
}

} // namespace yb::pggate
