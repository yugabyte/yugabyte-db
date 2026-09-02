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

#include "yb/docdb/properties_collector/sst_stats_collector.h"

#include "yb/gutil/strings/escaping.h"
#include "yb/gutil/walltime.h"

#include "yb/util/flags.h"
#include "yb/util/status_format.h"
#include "yb/util/stol_utils.h"
#include "yb/util/string_util.h"

DEFINE_NON_RUNTIME_bool(docdb_enable_sst_stats_collector, false,
    "Whether to register the DocDB SST statistics collector on the regular RocksDB of each tablet. "
    "The collector stores garbage and chain statistics in each SST file's properties block. Takes "
    "effect on tablet (re)open.");

DEFINE_RUNTIME_bool(docdb_sst_stats_coprefix_subtotals, false,
    "Whether the DocDB SST statistics collector also records per-table (cotable / colocation id) "
    "subtotals. Only rows carrying a coprefix record subtotals, so plain (non-colocated) tablets "
    "record none. Applies to files built from then on.");

DEFINE_RUNTIME_uint32(sst_tombstone_mark_ratio_percent, 30,
    "Mark an SST file for compaction when at least this percentage of its entries are DocDB "
    "tombstones (in combination with sst_tombstone_mark_min_count). The mark is persisted in the "
    "MANIFEST; nothing consumes it under DocDB's universal compaction until a picker or trigger "
    "explicitly opts in.");

DEFINE_RUNTIME_uint64(sst_tombstone_mark_min_count, 10000,
    "Mark an SST file for compaction only when it contains at least this many DocDB tombstones "
    "(in combination with sst_tombstone_mark_ratio_percent).");

namespace yb::docdb {

namespace {

constexpr size_t kCoprefixSubtotalsMaxBytes = 4096;

// UserCollectedProperties is a std::map<std::string, std::string> without a transparent
// comparator, so lookups need an owning key.
rocksdb::UserCollectedProperties::const_iterator Find(
    const rocksdb::UserCollectedProperties& properties, std::string_view key) {
  return properties.find(std::string(key));
}

void Set(rocksdb::UserCollectedProperties* properties, std::string_view key, std::string value) {
  (*properties)[std::string(key)] = std::move(value);
}

std::string JoinCounts(const AgeBandCounts& counts) {
  std::string result;
  for (size_t i = 0; i < counts.size(); ++i) {
    if (i != 0) {
      result += ',';
    }
    result += std::to_string(counts[i]);
  }
  return result;
}

Result<AgeBandCounts> SplitCounts(const std::string& text) {
  AgeBandCounts result{};
  const auto parts = StringSplit(text, ',');
  SCHECK_EQ(parts.size(), result.size(), Corruption, Format("Expected 8 age bands in '$0'", text));
  for (size_t i = 0; i < parts.size(); ++i) {
    // CheckedStoull rejects signs, empty input and trailing garbage, unlike std::stoull.
    result[i] = VERIFY_RESULT_PREPEND(
        CheckedStoull(Slice(parts[i])), Format("Malformed age band '$0'", parts[i]));
  }
  return result;
}

std::string SerializeCoprefixSubtotals(const std::map<std::string, CoprefixSubtotal>& subtotals) {
  std::string result;
  for (const auto& [coprefix, subtotal] : subtotals) {
    std::string item = Format(
        "$0:$1:$2:$3:$4", strings::b2a_hex(coprefix), subtotal.entries, subtotal.tombstone_entries,
        subtotal.reclaimable_entries, subtotal.rows);
    if (result.size() + item.size() + 1 > kCoprefixSubtotalsMaxBytes) {
      result += ";...";
      break;
    }
    if (!result.empty()) {
      result += ';';
    }
    result += item;
  }
  return result;
}

Result<std::map<std::string, CoprefixSubtotal>> ParseCoprefixSubtotals(const std::string& text) {
  std::map<std::string, CoprefixSubtotal> result;
  if (text.empty()) {
    return result;
  }
  for (const auto& item : StringSplit(text, ';')) {
    if (item == "...") {
      continue;
    }
    const auto fields = StringSplit(item, ':');
    SCHECK_EQ(fields.size(), 5, Corruption, Format("Malformed coprefix subtotal '$0'", item));
    auto& subtotal = result[strings::a2b_hex(fields[0])];
    const auto parse = [&item](const std::string& field) -> Result<uint64_t> {
      return VERIFY_RESULT_PREPEND(
          CheckedStoull(Slice(field)), Format("Malformed coprefix subtotal '$0'", item));
    };
    subtotal.entries = VERIFY_RESULT(parse(fields[1]));
    subtotal.tombstone_entries = VERIFY_RESULT(parse(fields[2]));
    subtotal.reclaimable_entries = VERIFY_RESULT(parse(fields[3]));
    subtotal.rows = VERIFY_RESULT(parse(fields[4]));
  }
  return result;
}

Result<const std::string&> Get(
    const rocksdb::UserCollectedProperties& properties, std::string_view key) {
  const auto it = Find(properties, key);
  SCHECK(it != properties.end(), NotFound, Format("Missing SST property $0", key));
  return it->second;
}

Result<uint64_t> GetUint64(
    const rocksdb::UserCollectedProperties& properties, std::string_view key) {
  const auto& text = VERIFY_RESULT_REF(Get(properties, key));
  return VERIFY_RESULT_PREPEND(
      CheckedStoull(Slice(text)), Format("Malformed SST property $0 = '$1'", key, text));
}

Result<ExponentialHistogram> GetHistogram(
    const rocksdb::UserCollectedProperties& properties, std::string_view key) {
  return ExponentialHistogram::Parse(VERIFY_RESULT_REF(Get(properties, key)));
}

Result<AgeBandCounts> GetAgeBands(
    const rocksdb::UserCollectedProperties& properties, std::string_view key) {
  return SplitCounts(VERIFY_RESULT_REF(Get(properties, key)));
}

class SstStatsCollector : public rocksdb::TablePropertiesCollector {
 public:
  SstStatsCollector()
      : tracker_(GetCurrentTimeMicros(), FLAGS_docdb_sst_stats_coprefix_subtotals) {}

  Status AddUserKey(
      const Slice& key, const Slice& value, rocksdb::EntryType type, rocksdb::SequenceNumber seq,
      uint64_t file_size) override {
    // A properties collector must never fail the table build; the tracker records parse failures
    // in chain_valid instead of returning them.
    tracker_.Add(key, value);
    return Status::OK();
  }

  Status Finish(rocksdb::UserCollectedProperties* properties) override {
    SstStatsToProperties(tracker_.Finish(), properties);
    return Status::OK();
  }

  rocksdb::UserCollectedProperties GetReadableProperties() const override {
    rocksdb::UserCollectedProperties properties;
    // GetReadableProperties may be called before Finish; report what has been seen so far without
    // closing the open row.
    SstStatsToProperties(tracker_.stats(), &properties);
    return properties;
  }

  const char* Name() const override { return "DocDbSstStatsCollector"; }

  // Sets FileMetaData::marked_for_compaction on the just-built file, which the MANIFEST persists.
  // Nothing consumes the mark under DocDB's universal compaction; the raw counts are stored
  // regardless, so thresholds can be revisited without losing information.
  bool NeedCompact() const override {
    const auto& stats = tracker_.stats();
    return stats.tombstone_entries >= FLAGS_sst_tombstone_mark_min_count &&
           stats.tombstone_entries * 100 >=
               stats.total_entries * FLAGS_sst_tombstone_mark_ratio_percent;
  }

 private:
  ChainTracker tracker_;
};

class SstStatsCollectorFactory : public rocksdb::TablePropertiesCollectorFactory {
 public:
  rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector(
      rocksdb::TablePropertiesCollectorFactory::Context context) override {
    return new SstStatsCollector();
  }

  const char* Name() const override { return "DocDbSstStatsCollectorFactory"; }
};

}  // namespace

void SstStatsToProperties(const SstStats& stats, rocksdb::UserCollectedProperties* properties) {
  using K = SstStatsPropertyKeys;
  Set(properties, K::kCollectorVersion, std::string(kSstStatsCollectorVersion));
  Set(properties, K::kTotalEntries, std::to_string(stats.total_entries));
  Set(properties, K::kTombstoneEntries, std::to_string(stats.tombstone_entries));
  Set(properties, K::kPackedRowEntries, std::to_string(stats.packed_row_entries));
  Set(properties, K::kMetaEntries, std::to_string(stats.meta_entries));
  Set(properties, K::kChainEntries, std::to_string(stats.chain_entries));
  Set(properties, K::kChainBytes, std::to_string(stats.chain_bytes));
  Set(properties, K::kNumSubdocKeys, std::to_string(stats.num_subdoc_keys));
  Set(properties, K::kNumRows, std::to_string(stats.num_rows));
  Set(properties, K::kDeadRows, std::to_string(stats.dead_rows));
  Set(properties, K::kDeadRowEntries, std::to_string(stats.dead_row_entries));
  Set(properties, K::kReclaimableEntries, std::to_string(stats.reclaimable_entries));
  Set(properties, K::kReclaimableBytes, std::to_string(stats.reclaimable_bytes));
  Set(properties, K::kMaxRowChain, std::to_string(stats.max_row_chain));
  Set(properties, K::kMaxStretch, std::to_string(stats.max_stretch));
  Set(properties, K::kChainValid, stats.chain_valid ? "1" : "0");
  Set(properties, K::kMinWriteHt, std::to_string(stats.min_write_ht.ToUint64()));
  Set(properties, K::kMaxWriteHt, std::to_string(stats.max_write_ht.ToUint64()));
  Set(properties, K::kAnchorMicros, std::to_string(stats.anchor_micros));
  Set(properties, K::kRowChainHist, stats.row_chain_hist.Serialize());
  Set(properties, K::kRowChainBytesHist, stats.row_chain_bytes_hist.Serialize());
  Set(properties, K::kStretchHist, stats.stretch_hist.Serialize());
  Set(properties, K::kStretchEntriesHist, stats.stretch_entries_hist.Serialize());
  Set(properties, K::kStretchBytesHist, stats.stretch_bytes_hist.Serialize());
  Set(properties, K::kDroppableAgeEntries, JoinCounts(stats.droppable_age_entries));
  Set(properties, K::kDroppableAgeBytes, JoinCounts(stats.droppable_age_bytes));
  if (!stats.coprefix_subtotals.empty()) {
    Set(properties, K::kCoprefixSubtotals, SerializeCoprefixSubtotals(stats.coprefix_subtotals));
  }
}

Result<SstStats> SstStatsFromProperties(const rocksdb::UserCollectedProperties& properties) {
  using K = SstStatsPropertyKeys;
  const auto version = Find(properties, K::kCollectorVersion);
  SCHECK(version != properties.end(), NotFound, "No DocDB SST statistics in properties");
  SCHECK_EQ(version->second, kSstStatsCollectorVersion, NotSupported,
            Format("Unsupported DocDB SST statistics version $0", version->second));
  SstStats stats;
  stats.total_entries = VERIFY_RESULT(GetUint64(properties, K::kTotalEntries));
  stats.tombstone_entries = VERIFY_RESULT(GetUint64(properties, K::kTombstoneEntries));
  stats.packed_row_entries = VERIFY_RESULT(GetUint64(properties, K::kPackedRowEntries));
  stats.meta_entries = VERIFY_RESULT(GetUint64(properties, K::kMetaEntries));
  stats.chain_entries = VERIFY_RESULT(GetUint64(properties, K::kChainEntries));
  stats.chain_bytes = VERIFY_RESULT(GetUint64(properties, K::kChainBytes));
  stats.num_subdoc_keys = VERIFY_RESULT(GetUint64(properties, K::kNumSubdocKeys));
  stats.num_rows = VERIFY_RESULT(GetUint64(properties, K::kNumRows));
  stats.dead_rows = VERIFY_RESULT(GetUint64(properties, K::kDeadRows));
  stats.dead_row_entries = VERIFY_RESULT(GetUint64(properties, K::kDeadRowEntries));
  stats.reclaimable_entries = VERIFY_RESULT(GetUint64(properties, K::kReclaimableEntries));
  stats.reclaimable_bytes = VERIFY_RESULT(GetUint64(properties, K::kReclaimableBytes));
  stats.max_row_chain = VERIFY_RESULT(GetUint64(properties, K::kMaxRowChain));
  stats.max_stretch = VERIFY_RESULT(GetUint64(properties, K::kMaxStretch));
  stats.chain_valid = VERIFY_RESULT(GetUint64(properties, K::kChainValid)) != 0;
  stats.min_write_ht = HybridTime(VERIFY_RESULT(GetUint64(properties, K::kMinWriteHt)));
  stats.max_write_ht = HybridTime(VERIFY_RESULT(GetUint64(properties, K::kMaxWriteHt)));
  stats.anchor_micros =
      static_cast<int64_t>(VERIFY_RESULT(GetUint64(properties, K::kAnchorMicros)));
  stats.row_chain_hist = VERIFY_RESULT(GetHistogram(properties, K::kRowChainHist));
  stats.row_chain_bytes_hist = VERIFY_RESULT(GetHistogram(properties, K::kRowChainBytesHist));
  stats.stretch_hist = VERIFY_RESULT(GetHistogram(properties, K::kStretchHist));
  stats.stretch_entries_hist = VERIFY_RESULT(GetHistogram(properties, K::kStretchEntriesHist));
  stats.stretch_bytes_hist = VERIFY_RESULT(GetHistogram(properties, K::kStretchBytesHist));
  stats.droppable_age_entries = VERIFY_RESULT(GetAgeBands(properties, K::kDroppableAgeEntries));
  stats.droppable_age_bytes = VERIFY_RESULT(GetAgeBands(properties, K::kDroppableAgeBytes));
  const auto subtotals = Find(properties, K::kCoprefixSubtotals);
  if (subtotals != properties.end()) {
    stats.coprefix_subtotals = VERIFY_RESULT(ParseCoprefixSubtotals(subtotals->second));
  }
  return stats;
}

std::shared_ptr<rocksdb::TablePropertiesCollectorFactory> MakeSstStatsCollectorFactory() {
  return std::make_shared<SstStatsCollectorFactory>();
}

}  // namespace yb::docdb
