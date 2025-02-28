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

#include "yb/yql/pggate/pg_tools.h"

DEFINE_test_flag(bool, ysql_ignore_add_fk_reference, false,
                 "Don't fill YSQL's internal cache for FK check to force read row from a table");

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
      : reader_provider_(reader_provider), buffering_settings_(buffering_settings) {}

  void Clear() {
    references_.clear();
    intents_.clear();
    region_local_tables_.clear();
  }

  void DeleteReference(const LightweightTableYbctid& key) {
    Erase(references_, key);
  }

  void AddReference(const LightweightTableYbctid& key) {
    if (!references_.contains(key) && PREDICT_TRUE(!FLAGS_TEST_ysql_ignore_add_fk_reference)) {
      references_.emplace(key.table_id, key.ybctid);
    }
  }

  Result<bool> IsReferenceExists(PgOid database_id, const LightweightTableYbctid& key) {
    if (references_.contains(key)) {
      return true;
    }

    // Check existence of required FK intent.
    // Absence means the key was checked by previous batched request and was not found.
    // We don't need to call the reader in this case.
    auto it = intents_.find(key);
    if (it == intents_.end()) {
      return false;
    }

    auto reader = reader_provider_();
    auto available_capacity = std::min<size_t>(
        intents_.size(), buffering_settings_.max_batch_size);
    reader.Reserve(available_capacity);
    // If the reader fails to get the result, we fail the whole operation (and transaction).
    // Hence it's ok to extract (erase) the keys from intent before calling reader.
    reader.Add(std::move(intents_.extract(it).value()));
    --available_capacity;

    for (auto it = intents_.begin();
         it != intents_.end() && available_capacity; --available_capacity) {
      reader.Add(std::move(intents_.extract(it++).value()));
    }

    // Add the keys found in docdb to the FK cache.
    auto ybctids = VERIFY_RESULT(reader.Read(
        database_id, region_local_tables_,
        make_lw_function([](YbcPgExecParameters& params) { params.rowmark = ROW_MARK_KEYSHARE; })));
    for (const auto& ybctid : ybctids) {
      references_.emplace(ybctid.table_id, ybctid.ybctid);
    }
    return references_.contains(key);
  }

  void AddIntent(const LightweightTableYbctid& key, bool is_region_local) {
    if (references_.contains(key)) {
        return;
    }

    if (is_region_local) {
      region_local_tables_.insert(key.table_id);
    }
    DCHECK(is_region_local || !region_local_tables_.contains(key.table_id));
    intents_.emplace(key.table_id, std::string(key.ybctid));
  }

 private:
  YbctidReaderProvider& reader_provider_;
  const BufferingSettings& buffering_settings_;
  MemoryOptimizedTableYbctidSet references_;
  TableYbctidSet intents_;
  OidSet region_local_tables_;
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

void PgFKReferenceCache::AddIntent(const LightweightTableYbctid& key, bool is_region_local) {
  impl_->AddIntent(key, is_region_local);
}

} // namespace yb::pggate
