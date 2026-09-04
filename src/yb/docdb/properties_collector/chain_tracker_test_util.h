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

// Shared helpers for the properties_collector tests: encoded-entry builders and the worked
// "anatomy strip" example from README.md.

#include <string>
#include <vector>

#include "yb/common/column_id.h"

#include "yb/docdb/properties_collector/chain_tracker.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value_type.h"

#include "yb/util/test_macros.h"

namespace yb::docdb {

// A fixed wall-clock reference for the age bands: 2023-11-14T22:13:20Z.
inline constexpr int64_t kAnchorMicros = 1700000000LL * 1000000;
inline constexpr int64_t kMinute = 60LL * 1000000;
inline constexpr int64_t kHour = 60 * kMinute;
inline constexpr int64_t kDay = 24 * kHour;

// An entry as the collector sees it: the RocksDB user key (subdoc key + hybrid time) and value.
struct Entry {
  std::string key;
  std::string value;
};

inline HybridTime AgeToHt(int64_t age_micros) {
  return HybridTime::FromMicros(kAnchorMicros - age_micros);
}

inline dockv::DocKey Row(int32_t id) {
  return dockv::DocKey(
      /* hash = */ 1000 + id, dockv::MakeKeyEntryValues("h"), dockv::MakeKeyEntryValues(id));
}

inline dockv::DocKey ColocatedRow(ColocationId colocation_id, int32_t id) {
  return dockv::DocKey(
      colocation_id, /* hash = */ 1000 + id, dockv::MakeKeyEntryValues("h"),
      dockv::MakeKeyEntryValues(id));
}

// Row-level key (packed row or row tombstone): the DocKey itself, no subkeys.
inline std::string RowKey(const dockv::DocKey& row, int64_t age_micros) {
  return dockv::SubDocKey(row, AgeToHt(age_micros)).Encode().ToStringBuffer();
}

inline std::string ColumnKey(const dockv::DocKey& row, int column, int64_t age_micros) {
  return dockv::SubDocKey(
      row, AgeToHt(age_micros),
      dockv::KeyEntryValues{dockv::KeyEntryValue::MakeColumnId(ColumnId(column))})
      .Encode().ToStringBuffer();
}

inline std::string Tombstone() {
  return std::string(1, dockv::ValueEntryTypeAsChar::kTombstone);
}

inline std::string PackedRow(const std::string& payload) {
  return std::string(1, dockv::ValueEntryTypeAsChar::kPackedRowV2) + payload;
}

inline std::string Str(const std::string& s) {
  return std::string(1, dockv::ValueEntryTypeAsChar::kString) + s;
}

inline std::string MetaKey() {
  return std::string(1, dockv::KeyEntryTypeAsChar::kTransactionApplyState) + "txn-id-bytes";
}

class TrackerFixture {
 public:
  explicit TrackerFixture(bool subtotals = false) : tracker_(kAnchorMicros, subtotals) {}

  TrackerFixture& Add(const Entry& e) {
    tracker_.Add(e.key, e.value);
    return *this;
  }

  TrackerFixture& Add(const std::vector<Entry>& entries) {
    for (const auto& e : entries) {
      Add(e);
    }
    return *this;
  }

  const SstStats& Finish() { return tracker_.Finish(); }

 private:
  ChainTracker tracker_;
};

// The anatomy strip from the design (README "Vocabulary" example).
//
// Live row r1, 6 entries, newest first within each key:
//   [packed v3][packed v2][packed v1][col a v2][col a v1][col b v1]
// Dead row r2, 3 entries:
//   [row tombstone][col a v1][col b v1]
inline std::vector<Entry> AnatomyStrip() {
  const auto r1 = Row(1);
  const auto r2 = Row(2);
  return {
      {RowKey(r1, 1 * kMinute), PackedRow("v3")},
      {RowKey(r1, 20 * kMinute), PackedRow("v2")},
      {RowKey(r1, 2 * kHour), PackedRow("v1")},
      {ColumnKey(r1, 1, 10 * kMinute), Str("a2")},
      {ColumnKey(r1, 1, 3 * kDay), Str("a1")},
      {ColumnKey(r1, 2, 1 * kMinute), Str("b1")},
      {RowKey(r2, 2 * kDay), Tombstone()},
      {ColumnKey(r2, 1, 5 * kDay), Str("a1")},
      {ColumnKey(r2, 2, 6 * kDay), Str("b1")},
  };
}

inline void ExpectIdentities(const SstStats& s) {
  ASSERT_TRUE(s.chain_valid);
  ASSERT_EQ(s.chain_entries, s.shadowed_entries() + s.num_subdoc_keys);
  ASSERT_EQ(s.num_subdoc_keys, s.repackable_entries() + s.num_rows);
  ASSERT_EQ(s.collapsible_entries(), s.shadowed_entries() + s.repackable_entries());
  ASSERT_EQ(s.total_entries, s.chain_entries + s.meta_entries);
  ASSERT_EQ(s.row_chain_hist.TotalWeight(), s.num_rows);
  ASSERT_EQ(s.stretch_entries_hist.TotalWeight(), s.reclaimable_entries);
  ASSERT_EQ(s.stretch_bytes_hist.TotalWeight(), s.reclaimable_bytes);
  uint64_t banded_entries = 0, banded_bytes = 0;
  for (size_t i = 0; i < AgeBands::kNumBands; ++i) {
    banded_entries += s.droppable_age_entries[i];
    banded_bytes += s.droppable_age_bytes[i];
  }
  ASSERT_EQ(banded_entries, s.reclaimable_entries);
  ASSERT_EQ(banded_bytes, s.reclaimable_bytes);
}

}  // namespace yb::docdb
