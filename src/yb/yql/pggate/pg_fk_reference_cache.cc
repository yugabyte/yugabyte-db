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
#include "yb/yql/pggate/pg_ybctid_reader.h"
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
  Impl(const PgSessionPtr& pg_session, const BufferingSettings& buffering_settings)
      : ybctid_reader_(pg_session), buffering_settings_(buffering_settings) {}

  void Clear() {
    references_.clear();
    regular_intents_.clear();
    deferred_intents_.clear();
    table_locality_map_.Clear();
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

  Result<bool> IsReferenceExists(
      PgOid database_id, const LightweightTableYbctid& key, YbcPgTableLocalityInfo locality_info) {
    if (references_.contains(key)) {
      return true;
    }

    // Check existence of required FK intent.
    auto requested_key_intent_it = intents_->find(key);
    if (requested_key_intent_it == intents_->end()) {
      if (!IsDeferredTriggersProcessingStarted()) {
        // In case of processing non deferred intents absence means the key was checked by previous
        // batched request and was not found. We don't need to call the reader in this case.
        return false;
      }
      // In case of processing deferred intents absence of intent could be caused by
      // subtransaction rollback. In this case we have to make a read attempt.
      requested_key_intent_it = intents_->emplace(key.table_id, key.ybctid).first;
      table_locality_map_.Add(key.table_id, locality_info);
    }
    RETURN_NOT_OK(ReadBatch(database_id, requested_key_intent_it));
    return references_.contains(key);
  }

  Status AddIntent(
      PgOid database_id, const LightweightTableYbctid& key, const IntentOptions& options) {
    LOG_IF(DFATAL, IsDeferredTriggersProcessingStarted())
        << "AddIntent is not expected after deferred trigger processing start";

    if (references_.contains(key)) {
        return Status::OK();
    }

    table_locality_map_.Add(key.table_id, options.locality_info);
    if (!options.is_deferred) {
      regular_intents_.emplace(key.table_id, key.ybctid);
      if (intents_->size() >= buffering_settings_.max_batch_size) {
        return ReadBatch(database_id, intents_->begin());
      }
    } else {
      deferred_intents_.emplace(key.table_id, key.ybctid);
    }
    return Status::OK();
  }

  void OnDeferredTriggersProcessingStarted() {
    LOG_IF(DFATAL, IsDeferredTriggersProcessingStarted())
        << "Multiple call of OnDeferredTriggersProcessingStarted is not expected";
    LOG_IF(DFATAL, !regular_intents_.empty())
        << "OnDeferredTriggersProcessingStarted implies all non deferred intents were processed";
    intents_ = &deferred_intents_;
  }

 private:
  Status ReadBatch(
      PgOid database_id, const MemoryOptimizedTableYbctidSet::iterator& mandatory_intent_it) {
    DCHECK(!intents_->empty() && mandatory_intent_it != intents_->end());
    const auto read_count_limit =
        std::min<size_t>(intents_->size(), buffering_settings_.max_batch_size);
    auto batch = ybctid_reader_.StartNewBatch(read_count_limit);
    batch.Add(*mandatory_intent_it);
    size_t requested_read_count = 1;

    auto it = intents_->begin();
    auto* mandatory_intent_it_ptr_for_separate_handling = &mandatory_intent_it;
    for (auto end = intents_->end(); it != end && requested_read_count < read_count_limit; ++it) {
      if (it != mandatory_intent_it) {
        batch.Add(*it);
        ++requested_read_count;
      } else {
        mandatory_intent_it_ptr_for_separate_handling = nullptr;
      }
    }
    const auto requested_intents_end_it = it;
    CancelableScopeExit intents_cleanup(
        [this, &requested_intents_end_it, mandatory_intent_it_ptr_for_separate_handling] {
          intents_->erase(intents_->begin(), requested_intents_end_it);
          if (mandatory_intent_it_ptr_for_separate_handling) {
            intents_->erase(*mandatory_intent_it_ptr_for_separate_handling);
          }
    });
    const auto ybctids = VERIFY_RESULT(batch.Read(
        database_id, table_locality_map_, {.rowmark = ROW_MARK_KEYSHARE}));
    // In case all FK has been read successfully it is reasonable to move requested intents into
    // references instead of cleanup intents and create new elements in references.
    if (ybctids.size() == requested_read_count) {
      intents_cleanup.Cancel();
      for (auto i = intents_->begin(); i != requested_intents_end_it;) {
          references_.insert(intents_->extract(i++));
      }
      if (mandatory_intent_it_ptr_for_separate_handling) {
        references_.insert(intents_->extract(*mandatory_intent_it_ptr_for_separate_handling));
      }
    } else {
      for (const auto& ybctid : ybctids) {
        references_.emplace(ybctid.table_id, ybctid.ybctid);
      }
    }
    return Status::OK();
  }

  [[nodiscard]] bool IsDeferredTriggersProcessingStarted() const {
    return intents_ == &deferred_intents_;
  }

  YbctidReader ybctid_reader_;
  const BufferingSettings& buffering_settings_;
  MemoryOptimizedTableYbctidSet references_;
  MemoryOptimizedTableYbctidSet regular_intents_;
  MemoryOptimizedTableYbctidSet deferred_intents_;
  MemoryOptimizedTableYbctidSet* intents_ = &regular_intents_;
  TableLocalityMap table_locality_map_;
  std::optional<size_t> references_cache_limit_;
};

PgFKReferenceCache::PgFKReferenceCache(
    const PgSessionPtr& pg_session,
    std::reference_wrapper<const BufferingSettings> buffering_settings)
    : impl_(new Impl(pg_session, buffering_settings)) {}

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
    PgOid database_id, const LightweightTableYbctid& key, YbcPgTableLocalityInfo locality_info) {
  return impl_->IsReferenceExists(database_id, key, locality_info);
}

Status PgFKReferenceCache::AddIntent(
    PgOid database_id, const LightweightTableYbctid& key, const IntentOptions& options) {
  return impl_->AddIntent(database_id, key, options);
}

void PgFKReferenceCache::OnDeferredTriggersProcessingStarted() {
  impl_->OnDeferredTriggersProcessingStarted();
}

} // namespace yb::pggate
