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

#pragma once

#include "yb/common/common.pb.h"
#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/schema.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol.h"

#include "yb/dockv/schema_packing.h"

#include "yb/util/locks.h"

namespace yb::docdb {

YB_STRONGLY_TYPED_BOOL(Index);

struct DocReadContext {
  DocReadContext(
      const std::string& log_prefix, TableType table_type, Index is_index,
      dockv::SchemaPackingRegistryPtr registry);

  DocReadContext(
      const std::string& log_prefix, TableType table_type, Index is_index,
      dockv::SchemaPackingRegistryPtr registry, const Schema& schema, SchemaVersion schema_version);

  // Copies schema/packing identity only. Tombstone-cache fields are reset to construction
  // defaults (unarmed watermark, empty cache) so same-schema-version TableInfo rebuilds fail
  // closed instead of carrying a warm cache into the new context.
  DocReadContext(const DocReadContext& rhs);

  DocReadContext(const DocReadContext& rhs, const Schema& schema, SchemaVersion schema_version);

  DocReadContext(const DocReadContext& rhs, const Schema& schema);

  DocReadContext(const DocReadContext& rhs, SchemaVersion min_schema_version);

  template <class PB>
  Status LoadFromPB(const PB& pb) {
    RETURN_NOT_OK(SchemaFromPB(pb.schema(), &schema_));
    RETURN_NOT_OK(schema_packing_storage.LoadFromPB(pb.old_schema_packings()));
    schema_packing_storage.AddSchema(pb.schema_version(), schema_);
    UpdateKeyPrefix();
    LogAfterLoad();
    return Status::OK();
  }

  template <class PB>
  Status MergeWithRestored(const PB& pb, dockv::OverwriteSchemaPacking overwrite) {
    RETURN_NOT_OK(schema_packing_storage.MergeWithRestored(
        pb.schema_version(), pb.schema(), pb.old_schema_packings(), overwrite));
    LogAfterMerge(overwrite);
    UpdateKeyPrefix();
    return Status::OK();
  }

  template <class PB>
  void ToPB(SchemaVersion schema_version, PB* out) const {
    DCHECK(schema_.has_column_ids());
    SchemaToPB(schema_, out->mutable_schema());
    schema_packing_storage.ToPB(schema_version, out->mutable_old_schema_packings());
  }

  const Schema& schema() const {
    return schema_;
  }

  Schema* mutable_schema() {
    return &schema_;
  }

  void SetCotableId(const Uuid& cotable_id);

  // The number of bytes before actual key values for all encoded keys in this table.
  size_t key_prefix_encoded_len() const {
    return key_prefix_encoded_len_;
  }

  Slice shared_key_prefix() const {
    return Slice(shared_key_prefix_buffer_.data(), shared_key_prefix_len_);
  }

  // Tombstone time cache is valid only for colocated tables.
  std::optional<DocHybridTime> table_tombstone_time() const;

  // Cache a DocDB lookup result.
  // Pass the generation from before the lookup. If truncate bumped the generation while we
  // were looking, stamping with the new generation would make an old no tombstone entry
  // look current. A valid tombstone whose hybrid time is above the current watermark is also
  // rejected: otherwise a concurrent read with watermark <= read_ht < tombstone_ht would stay
  // eligible, hit the entry, and hide every row for a snapshot that predates the truncate.
  void set_table_tombstone_time(
      DocHybridTime table_tombstone_time, uint64_t entry_generation) const;

  // Clear the cached tombstone time (reset to uncached).
  void clear_table_tombstone_time() const;

  // HybridTime below which the tombstone cache must not be consumed or populated.
  // Default kMax = unarmed (cache fully disabled / fail-closed).
  // See AdvanceTombstoneCacheWatermark.
  HybridTime tombstone_cache_watermark() const;

  // Global cache epoch; bumped when Advance actually raises the watermark.
  uint64_t tombstone_cache_generation() const;

  // Monotone watermark advance. kMax is an "unarmed" sentinel (not a numeric max): the first
  // advance replaces it with ht; later advances take max(current, ht). Used to arm at SafeTime
  // and to bump on table-tombstone apply. Bumps tombstone_cache_generation_ so any previously
  // stored cache entry is treated as a miss. Does not clear the cache slot by itself.
  void AdvanceTombstoneCacheWatermark(HybridTime ht) const;

  // Called when a table tombstone is applied to this replica. Always bumps the generation and
  // clears the cache slot; raises the watermark when write_ht is higher. Safe to call more than
  // once for the same write_ht (e.g. pre-write + post-WriteToRocksDB on the xCluster path): the
  // second call still clears and bumps generation so a poison populate in between cannot stick.
  //
  // Residual window (local transactional apply): SafeTime does not wait for intent apply, so a
  // read above commit_ht can run while APPLY (and this notify) is still in flight. Until then a
  // warm pre-truncate "no tombstone" entry stays generation-valid and can resurrect rows. Bounded
  // by apply latency (wider on followers / apply backlog / large txns) - much better than the
  // unbounded staleness this call removes, but a real residual.
  //
  // That window has a second polarity without the store-side watermark check: a miss at
  // read_ht >= T can cache tombstone T while the watermark is still below T; a concurrent read
  // with watermark <= read_ht < T would then hit it and see an empty table.
  // set_table_tombstone_time rejects tombstone_ht > watermark so that cannot land.
  //
  // Cold lookups in the local window stay correct because GetTableTombstoneTime is intent-aware
  // (IntentAwareIterator; txn context even for non-txn YSQL reads) and sees the committed intent.
  // That is why notify-before-WriteToRocksDB is safe locally. It is NOT safe alone for xCluster
  // external intents (keyed by external txn id, invisible to DecodeIntentKey): those need a
  // post-WriteToRocksDB re-notify so a miss that observed "no tombstone" before the memtable
  // publish cannot repopulate under the already-raised watermark.
  //
  // Non-transactional raft apply is tighter: ops apply serially and safe time during apply of H
  // stays below H, so eligible reads cannot interleave past write_ht mid-apply.
  void OnTableTombstoneWritten(HybridTime write_ht) const;

  // True when read_ht is allowed to consume or populate the tombstone cache.
  bool IsTombstoneCacheEligible(HybridTime read_ht) const;

  // Eligibility check + cache consume under one lock. Prefer this over calling
  // IsTombstoneCacheEligible then table_tombstone_time separately: those take and
  // release the mutex independently, so a truncate can land between them.
  std::optional<DocHybridTime> GetCachedTableTombstoneTime(HybridTime read_ht) const;

  Slice upperbound() const {
    return Slice(upperbound_buffer_.data(), upperbound_len_);
  }

  Slice table_key_prefix() const {
    return Slice(shared_key_prefix_buffer_.data(), table_key_prefix_len_);
  }

  // Returns the user key whose bloom filter key is shared by every key a scan bounded by
  // [lower, upper] can return, so data sources may be filtered out using it
  // (BloomFilterMode::kFixed). Returns nullopt when there is no such key, in particular when a
  // bound carries no components a bloom filter key could be derived from (e.g. the encoded empty
  // DocKey used as the lower bound of an unbounded scan) - the bounds then constrain nothing, so
  // the keys the scan returns have many different bloom filter keys. Note that such a bound still
  // has a bloom filter key of its own, it is just not shared by what the scan returns.
  //
  // The result is a user key and not a bloom filter key: the bloom filter key is derived from it by
  // the filter policy of each data source, which differs between filter policy versions.
  Result<std::optional<Slice>> UserKeyForFixedBloomFilter(Slice lower, Slice upper) const;
  size_t NumColumnsUsedByBloomFilterKey() const;

  dockv::VectorValueFormat vector_value_format() const;

  void TEST_SetDefaultTimeToLive(uint64_t ttl_msec) {
    schema_.SetDefaultTimeToLive(ttl_msec);
  }

  // Schema/packing identity only: tombstone-cache fields are deliberately excluded (they are
  // ephemeral per-replica read-path state and must not affect equality of rebuilt contexts).
  static bool TEST_Equals(const DocReadContext& lhs, const DocReadContext& rhs) {
    return Schema::TEST_Equals(lhs.schema_, rhs.schema_) &&
           lhs.schema_packing_storage.TEST_Equals(rhs.schema_packing_storage);
  }

  static DocReadContext TEST_Create(const Schema& schema);

  const Index is_index;

  dockv::SchemaPackingStorage schema_packing_storage;

  std::optional<PgVectorIdxOptionsPB> vector_idx_options;

 private:
  void LogAfterLoad();
  void LogAfterMerge(dockv::OverwriteSchemaPacking overwrite);
  void UpdateKeyPrefix();

  const std::string& LogPrefix() const {
    return log_prefix_;
  }

  Schema schema_;

  // The data about key prefix shared by all entries of this table.
  // shared_key_prefix_* fields store prefix bytes common to all keys in this table.
  // I.e. if table has cotable id or colocation id, then it will be placed in shared_key_prefix_*.
  // Also in case of non empty hash part, it will contain kUInt16Hash byte.
  // When hash part is empty and first byte of encoded range column is the same for all entries
  // in the table, then it will also be present here.
  size_t shared_key_prefix_len_ = 0;
  std::array<uint8_t, 0x20> shared_key_prefix_buffer_;

  // The data about upperbound for this table. I.e. we know that all entries from this table
  // are before the upperbound. And all entries from next table are after this upperbound.
  size_t upperbound_len_ = 0;
  std::array<uint8_t, 0x20> upperbound_buffer_;

  // This field contains number of bytes in encoded key before column values.
  // I.e. it is sum of sizes of cotable id, colocation id, hash code.
  // It is very close to shared_key_prefix_len_ with exception that shared_key_prefix_len_
  // has only one byte for hash code, i.e. shared key entry value type. But not the value of
  // hash code itself.
  // While key_prefix_encoded_len_ will have 3 bytes for it, i.e. full encoded hash code.
  size_t key_prefix_encoded_len_ = 0;

  // Includes cotable_id and colocation_id.
  size_t table_key_prefix_len_ = 0;

  std::string log_prefix_;

  // Serializes tombstone-cache field updates. A hit saves an IntentAwareIterator construction per
  // scan (GetTableTombstoneTime runs once when the doc reader is created), so an uncontended
  // spinlock is cheap next to that. Without it, set_table_tombstone_time's two stores can
  // interleave with OnTableTombstoneWritten and pair a stale value with the current generation.
  mutable simple_spinlock tombstone_cache_mutex_;

  // Cached colocated-table tombstone time (kMax = uncached). Consume/populate gated by
  // tombstone_cache_watermark_ (see IsTombstoneCacheEligible).
  mutable DocHybridTime table_tombstone_time_ = DocHybridTime::kMax;

  // Generation stamped with the cached value. Hits require a match with
  // tombstone_cache_generation_. Kept even under the spinlock: the RocksDB lookup still runs
  // outside the lock, and the generation rejects a populate that raced a truncate during that
  // window (watermark alone cannot reject a "no tombstone" stamp).
  mutable uint64_t tombstone_cache_entry_generation_ = 0;

  // Global epoch bumped when Advance actually raises the watermark, and on every
  // OnTableTombstoneWritten (including same-ht re-notify).
  mutable uint64_t tombstone_cache_generation_ = 0;

  // Default kMax = unarmed: both gates reject, so an unarmed context is cache-off (correct
  // cold-path behavior). Forgotten construction sites therefore fail closed (perf only).
  mutable HybridTime tombstone_cache_watermark_ = HybridTime::kMax;
};

} // namespace yb::docdb
