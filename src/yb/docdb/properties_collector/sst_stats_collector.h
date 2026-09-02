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

#include <memory>
#include <string_view>

#include "yb/docdb/properties_collector/chain_tracker.h"

#include "yb/rocksdb/table_properties.h"

namespace yb::docdb {

// DocDB-aware SST properties collector: measures garbage (tombstones, shadowed versions, dead rows)
// and chain / stretch distributions while an SST file is built, and stores them in the file's
// user_collected_properties block. Design and vocabulary: properties_collector/README.md.

// Keys of the properties this collector stores, all under "yb.docdb.". Values are decimal strings
// unless noted. Groups:
//   counts       total/tombstone/packed-row/meta entries; chain entries (Ec) and bytes; subdoc keys
//                (K); rows (R); dead rows and their entries; reclaimable entries and bytes; max row
//                chain; max stretch. Shadowed (Ec-K), repackable (K-R), collapsible (Ec-R) are
//                identities, not stored.
//   validity     collector_version, chain_valid.
//   write range  exact min/max write hybrid time of chain-tracked entries; the age-band anchor.
//   histograms   row-chain length (by rows, by bytes); stretch length (by stretches, by entries,
//                by bytes). ExponentialHistogram::Serialize() form.
//   age bands    reclaimable entries and bytes by age of what makes them droppable (8 bands).
//   colocation   per-coprefix subtotals, only when enabled by flag.
// A typical file adds well under 1 KB (histograms are sparse); the fully dense worst case is about
// 1.7 KB per histogram.
struct SstStatsPropertyKeys {
  static constexpr std::string_view kCollectorVersion = "yb.docdb.collector_version";
  static constexpr std::string_view kTotalEntries = "yb.docdb.total_entries";
  static constexpr std::string_view kTombstoneEntries = "yb.docdb.tombstone_entries";
  static constexpr std::string_view kPackedRowEntries = "yb.docdb.packed_row_entries";
  static constexpr std::string_view kMetaEntries = "yb.docdb.meta_entries";
  static constexpr std::string_view kChainEntries = "yb.docdb.chain_entries";
  static constexpr std::string_view kChainBytes = "yb.docdb.chain_bytes";
  static constexpr std::string_view kNumSubdocKeys = "yb.docdb.num_subdoc_keys";
  static constexpr std::string_view kNumRows = "yb.docdb.num_rows";
  static constexpr std::string_view kDeadRows = "yb.docdb.dead_rows";
  static constexpr std::string_view kDeadRowEntries = "yb.docdb.dead_row_entries";
  static constexpr std::string_view kReclaimableEntries = "yb.docdb.reclaimable_entries";
  static constexpr std::string_view kReclaimableBytes = "yb.docdb.reclaimable_bytes";
  static constexpr std::string_view kMaxRowChain = "yb.docdb.max_row_chain";
  static constexpr std::string_view kMaxStretch = "yb.docdb.max_stretch";
  static constexpr std::string_view kChainValid = "yb.docdb.chain_valid";  // "1" or "0"
  // HybridTime::ToUint64().
  static constexpr std::string_view kMinWriteHt = "yb.docdb.min_write_ht";
  static constexpr std::string_view kMaxWriteHt = "yb.docdb.max_write_ht";
  static constexpr std::string_view kAnchorMicros = "yb.docdb.stats_anchor_micros";
  // ExponentialHistogram::Serialize().
  static constexpr std::string_view kRowChainHist = "yb.docdb.row_chain_hist";
  static constexpr std::string_view kRowChainBytesHist = "yb.docdb.row_chain_bytes_hist";
  static constexpr std::string_view kStretchHist = "yb.docdb.stretch_hist";
  static constexpr std::string_view kStretchEntriesHist = "yb.docdb.stretch_entries_hist";
  static constexpr std::string_view kStretchBytesHist = "yb.docdb.stretch_bytes_hist";
  // 8 comma-separated counts.
  static constexpr std::string_view kDroppableAgeEntries = "yb.docdb.droppable_age_entries";
  static constexpr std::string_view kDroppableAgeBytes = "yb.docdb.droppable_age_bytes";
  // "<hex coprefix>:<entries>:<tombstones>:<reclaimable>:<rows>" items joined by ';'.
  static constexpr std::string_view kCoprefixSubtotals = "yb.docdb.coprefix_subtotals";
};

// Value of kCollectorVersion written by this code.
inline constexpr std::string_view kSstStatsCollectorVersion = "2";

// Serializes statistics into a properties map (what Finish() writes) and reads them back.
void SstStatsToProperties(const SstStats& stats, rocksdb::UserCollectedProperties* properties);
Result<SstStats> SstStatsFromProperties(const rocksdb::UserCollectedProperties& properties);

// Creates the collector factory. Registered on each tablet's regular RocksDB when
// --docdb_enable_sst_stats_collector is set.
std::shared_ptr<rocksdb::TablePropertiesCollectorFactory> MakeSstStatsCollectorFactory();

}  // namespace yb::docdb
