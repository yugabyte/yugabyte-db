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

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/hybrid_time.h"

#include "yb/docdb/properties_collector/exponential_histogram.h"

#include "yb/util/slice.h"

namespace yb::docdb {

// Fixed age bands for "how old is the entry that made this garbage droppable". A consumer sums the
// bands that are wholly older than the history cutoff it is applying. The 15 m edge brackets the
// default history retention (timestamp_history_retention_interval_sec = 900).
struct AgeBands {
  static constexpr size_t kNumBands = 8;
  // Upper edges of bands 0..6, in microseconds: 5 m, 15 m, 1 h, 6 h, 24 h, 7 d, 30 d.
  // Band 7 is everything older.
  static constexpr std::array<int64_t, kNumBands - 1> kEdgesMicros = {
      5LL * 60 * 1000000,
      15LL * 60 * 1000000,
      60LL * 60 * 1000000,
      6LL * 60 * 60 * 1000000,
      24LL * 60 * 60 * 1000000,
      7LL * 24 * 60 * 60 * 1000000,
      30LL * 24 * 60 * 60 * 1000000,
  };

  // Band for an age; negative ages (clock skew) land in band 0.
  static size_t BandIndex(int64_t age_micros);
};

using AgeBandCounts = std::array<uint64_t, AgeBands::kNumBands>;

// Per-coprefix (cotable / colocation id) subtotals for colocated tablets.
struct CoprefixSubtotal {
  uint64_t entries = 0;
  uint64_t tombstone_entries = 0;
  uint64_t reclaimable_entries = 0;
  uint64_t rows = 0;
};

// Everything the collector records for one SST file. Definitions in
// properties_collector/README.md, "Vocabulary".
struct SstStats {
  // Every entry handed to the collector, including meta records and entries that failed to parse.
  uint64_t total_entries = 0;
  uint64_t tombstone_entries = 0;
  uint64_t packed_row_entries = 0;
  // Regular-DB meta records (transaction apply state, vector index metadata). Not chain-tracked.
  uint64_t meta_entries = 0;

  // The chain-tracked population. The identities below hold over these, not over total_entries.
  uint64_t chain_entries = 0;     // Ec
  uint64_t chain_bytes = 0;       // raw key + value bytes of chain-tracked entries
  uint64_t num_subdoc_keys = 0;   // K: distinct subdocument keys = number of subdoc chains
  uint64_t num_rows = 0;          // R: distinct rows = number of row chains

  // Rows whose chain head is a row-level (or table-level) tombstone, and all their entries.
  uint64_t dead_rows = 0;
  uint64_t dead_row_entries = 0;

  // All the garbage: shadowed entries of live rows + every entry of dead rows, counted once at scan
  // time (the two sets are disjoint by construction, so there is no double counting).
  uint64_t reclaimable_entries = 0;
  uint64_t reclaimable_bytes = 0;

  uint64_t max_row_chain = 0;
  uint64_t max_stretch = 0;

  // False once a key failed to parse; the chain statistics are then partial and must not be used
  // for the identities. The v1 counters (total/tombstone) stay valid.
  bool chain_valid = true;

  // Exact write-time range of chain-tracked entries (invalid when there are none).
  HybridTime min_write_ht = HybridTime::kInvalid;
  HybridTime max_write_ht = HybridTime::kInvalid;
  // Wall-clock reference the age bands are measured against (taken at collector construction).
  int64_t anchor_micros = 0;

  // Row-chain length distribution, by row count and by bytes.
  ExponentialHistogram row_chain_hist;
  ExponentialHistogram row_chain_bytes_hist;
  // Stretch distribution: maximal runs of consecutive reclaimable entries in file order, ignoring
  // key boundaries. By stretch count, by entries contained, by bytes contained.
  ExponentialHistogram stretch_hist;
  ExponentialHistogram stretch_entries_hist;
  ExponentialHistogram stretch_bytes_hist;

  // Reclaimable entries and bytes by the age of what makes them droppable: for a shadowed entry the
  // entry that shadows it, for a dead-row entry the row tombstone.
  AgeBandCounts droppable_age_entries{};
  AgeBandCounts droppable_age_bytes{};

  // Only populated when subtotals are enabled AND the tablet is colocated; key = the raw
  // cotable / colocation prefix bytes. Rows without a coprefix (plain tables) record none.
  std::map<std::string, CoprefixSubtotal> coprefix_subtotals;

  // Derived identities (meaningful only while chain_valid).
  uint64_t shadowed_entries() const { return chain_entries - num_subdoc_keys; }
  // Heads beyond one per row: a repack-opportunity measure, not a garbage class. In a dead row
  // these heads are also garbage and counted by reclaimable, so the two overlap there. Never a
  // trigger input either way.
  uint64_t repackable_entries() const { return num_subdoc_keys - num_rows; }
  uint64_t collapsible_entries() const { return chain_entries - num_rows; }
};

// Walks the entries of one SST in build order and derives SstStats in a single pass. Because the
// file is sorted and versions of a key are adjacent (newest first), the tracker never looks
// anything up: it compares each key with the previous one. No allocation in steady state.
//
// Usage: Add() once per entry, then Finish(). Add() never fails; a key that cannot be parsed
// invalidates the chain statistics (chain_valid = false) and the tracker keeps counting the v1
// scalars.
class ChainTracker {
 public:
  ChainTracker(int64_t anchor_micros, bool track_coprefix_subtotals);

  void Add(Slice key, Slice value);

  // Closes the open row and stretch and returns the statistics. Idempotent.
  const SstStats& Finish();

  const SstStats& stats() const { return stats_; }

 private:
  void OnMetaEntry();
  void CloseRow();
  void CloseStretch();
  void Invalidate();
  void AssignPrevKey(Slice subdoc_key, size_t shared_prefix);
  void AddReclaimable(size_t entry_bytes, int64_t droppable_by_micros);
  void UpdateWriteHtRange(const EncodedDocHybridTime& encoded_ht);
  Result<int64_t> PhysicalMicros(const EncodedDocHybridTime& encoded_ht);

  // Length of the row key prefix of a subdocument key (hybrid time already stripped), and of its
  // coprefix (cotable / colocation id, possibly empty).
  struct RowKeyEnds {
    size_t coprefix_end;
    size_t row_end;
  };
  static Result<RowKeyEnds> ComputeRowKeyEnds(Slice subdoc_key);

  SstStats stats_;
  const bool track_coprefix_subtotals_;
  bool finished_ = false;

  // Previous chain-tracked entry.
  bool has_prev_ = false;
  std::vector<char> prev_key_;              // subdoc key without hybrid time
  EncodedDocHybridTime prev_entry_ht_;
  EncodedDocHybridTime min_encoded_ht_;     // lexicographic extremes of the encoded form
  EncodedDocHybridTime max_encoded_ht_;

  // Current row.
  size_t row_end_ = 0;
  uint64_t row_entries_ = 0;
  uint64_t row_bytes_ = 0;
  bool row_dead_ = false;
  int64_t row_tombstone_micros_ = 0;        // valid when row_dead_
  CoprefixSubtotal* row_subtotal_ = nullptr;

  // Current stretch of consecutive reclaimable entries.
  uint64_t stretch_entries_ = 0;
  uint64_t stretch_bytes_ = 0;
};

}  // namespace yb::docdb
