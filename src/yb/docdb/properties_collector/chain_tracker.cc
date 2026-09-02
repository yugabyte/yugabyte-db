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

#include "yb/docdb/properties_collector/chain_tracker.h"

#include <algorithm>

#include "yb/dockv/doc_key.h"
#include "yb/dockv/value.h"
#include "yb/dockv/value_type.h"

#include "yb/gutil/strings/fastmem.h"

#include "yb/util/status_format.h"

namespace yb::docdb {

size_t AgeBands::BandIndex(int64_t age_micros) {
  size_t band = 0;
  while (band < kEdgesMicros.size() && age_micros >= kEdgesMicros[band]) {
    ++band;
  }
  return band;
}

ChainTracker::ChainTracker(int64_t anchor_micros, bool track_coprefix_subtotals)
    : track_coprefix_subtotals_(track_coprefix_subtotals) {
  stats_.anchor_micros = anchor_micros;
}

// Per entry:
//   1. Classify the value: skip the control-fields prefix, then one byte says tombstone / packed
//      row / other. Values that do not follow the DocDB encoding count in total_entries only.
//   2. Meta records are counted in meta_entries, close the open row and stretch, and are otherwise
//      ignored; the identities are stated over chain_entries for this reason.
//   3. Strip the hybrid time to get the subdoc key; track the encoded-HT extremes.
//   4. Compare with the previous subdoc key by shared-prefix length:
//        identical              -> a shadowed version; its overwriter is the previous entry.
//        shares the row key     -> a new subdoc chain in the same row; reclaimable iff row is dead.
//        otherwise              -> a new row: close the previous one, compute the row key length
//                                  once, decide whether the row is dead (head is a tombstone whose
//                                  subdoc key is exactly the row key).
//   5. A reclaimable entry extends the current stretch and lands in the age band of what makes it
//      droppable; a non-reclaimable entry closes the stretch.
void ChainTracker::Add(Slice key, Slice value) {
  ++stats_.total_entries;
  const size_t entry_bytes = key.size() + value.size();

  // Value kind. A DocDB delete is a Put whose payload is a kTombstone marker, optionally preceded
  // by control fields (merge flags, intent doc hybrid time, TTL, user timestamp).
  bool is_tombstone = false;
  {
    Slice value_slice = value;
    Slice intent_doc_ht;
    const auto control_fields =
        dockv::ValueControlFields::DecodeWithIntentDocHt(&value_slice, &intent_doc_ht);
    if (control_fields.ok()) {
      const auto value_type = dockv::DecodeValueEntryType(value_slice);
      if (value_type == dockv::ValueEntryType::kTombstone) {
        is_tombstone = true;
        ++stats_.tombstone_entries;
      } else if (dockv::IsPackedRow(value_type)) {
        ++stats_.packed_row_entries;
      }
    }
    // Records that do not follow the DocDB value encoding are counted in total_entries only.
  }

  // Meta records (transaction apply state, vector index metadata) are not part of any chain.
  if (key.empty() || dockv::IsRegularDBMetaKeyType(dockv::DecodeKeyEntryType(key)) ||
      dockv::DecodeKeyEntryType(key) == dockv::KeyEntryType::kObsoleteIntentPrefix) {
    OnMetaEntry();
    return;
  }
  if (!stats_.chain_valid) {
    return;
  }

  ++stats_.chain_entries;
  stats_.chain_bytes += entry_bytes;

  // Strip the hybrid time: its size is in the low bits of the last byte, and it is preceded by a
  // kHybridTime marker byte.
  const auto ht_size = DocHybridTime::GetEncodedSize(key);
  if (!ht_size.ok() || key.size() < *ht_size + 2) {
    Invalidate();
    return;
  }
  const size_t subdoc_key_end = key.size() - *ht_size - 1;
  const Slice subdoc_key(key.data(), subdoc_key_end);
  EncodedDocHybridTime encoded_ht(key.Suffix(*ht_size));
  UpdateWriteHtRange(encoded_ht);

  // Chain boundary detection: the length of the prefix shared with the previous subdoc key.
  const size_t compare_len = std::min(subdoc_key_end, prev_key_.size());
  const size_t shared_prefix =
      has_prev_ ? strings::MemoryDifferencePos(key.data(), prev_key_.data(), compare_len) : 0;

  bool reclaimable = false;
  int64_t droppable_by_micros = 0;
  if (has_prev_ && subdoc_key_end == prev_key_.size() && shared_prefix >= subdoc_key_end) {
    // Same subdoc key as the previous (newer) entry: this version is shadowed. It becomes droppable
    // once the entry that shadows it is past the history cutoff.
    ++row_entries_;
    row_bytes_ += entry_bytes;
    reclaimable = true;
    const auto overwriter_micros = PhysicalMicros(prev_entry_ht_);
    if (!overwriter_micros.ok()) {
      Invalidate();
      return;
    }
    droppable_by_micros = *overwriter_micros;
  } else if (has_prev_ && shared_prefix >= row_end_) {
    // Same row, new subdoc key: a live head (of a column or record) in this row.
    ++stats_.num_subdoc_keys;
    ++row_entries_;
    row_bytes_ += entry_bytes;
    if (row_dead_) {
      // Under a row tombstone every entry goes when the tombstone does.
      reclaimable = true;
      droppable_by_micros = row_tombstone_micros_;
    }
  } else {
    // New row.
    CloseRow();
    const auto ends = ComputeRowKeyEnds(subdoc_key);
    if (!ends.ok()) {
      Invalidate();
      return;
    }
    row_end_ = ends->row_end;
    ++stats_.num_rows;
    ++stats_.num_subdoc_keys;
    row_entries_ = 1;
    row_bytes_ = entry_bytes;
    // A dead row: its chain head is a row-level (or table-level) tombstone.
    row_dead_ = subdoc_key_end == row_end_ && is_tombstone;
    if (row_dead_) {
      const auto tombstone_micros = PhysicalMicros(encoded_ht);
      if (!tombstone_micros.ok()) {
        Invalidate();
        return;
      }
      row_tombstone_micros_ = *tombstone_micros;
      reclaimable = true;
      droppable_by_micros = row_tombstone_micros_;
    }
    if (track_coprefix_subtotals_) {
      row_subtotal_ =
          &stats_.coprefix_subtotals[std::string(subdoc_key.cdata(), ends->coprefix_end)];
      ++row_subtotal_->rows;
    }
  }

  if (reclaimable) {
    AddReclaimable(entry_bytes, droppable_by_micros);
  } else {
    CloseStretch();
  }
  if (row_subtotal_ != nullptr) {
    ++row_subtotal_->entries;
    row_subtotal_->tombstone_entries += is_tombstone ? 1 : 0;
    row_subtotal_->reclaimable_entries += reclaimable ? 1 : 0;
  }

  AssignPrevKey(subdoc_key, shared_prefix);
  prev_entry_ht_ = encoded_ht;
  has_prev_ = true;
}

const SstStats& ChainTracker::Finish() {
  if (!finished_) {
    CloseRow();
    CloseStretch();
    if (!min_encoded_ht_.empty()) {
      auto min_ht = min_encoded_ht_.Decode();
      auto max_ht = max_encoded_ht_.Decode();
      if (min_ht.ok() && max_ht.ok()) {
        stats_.min_write_ht = min_ht->hybrid_time();
        stats_.max_write_ht = max_ht->hybrid_time();
      } else {
        Invalidate();
      }
    }
    finished_ = true;
  }
  return stats_;
}

void ChainTracker::OnMetaEntry() {
  ++stats_.meta_entries;
  // A meta record separates chains and stretches but belongs to neither.
  CloseRow();
  CloseStretch();
  has_prev_ = false;
  row_subtotal_ = nullptr;
}

void ChainTracker::CloseRow() {
  // Explicit no-op when no row is open (first entry, consecutive meta records, empty file), so the
  // histograms never acquire phantom rows.
  if (row_entries_ == 0) {
    return;
  }
  stats_.row_chain_hist.Add(row_entries_);
  stats_.row_chain_bytes_hist.Add(row_bytes_);
  stats_.max_row_chain = std::max(stats_.max_row_chain, row_entries_);
  if (row_dead_) {
    ++stats_.dead_rows;
    stats_.dead_row_entries += row_entries_;
  }
  row_entries_ = 0;
  row_bytes_ = 0;
  row_dead_ = false;
}

void ChainTracker::CloseStretch() {
  if (stretch_entries_ == 0) {
    return;
  }
  stats_.stretch_hist.Add(stretch_entries_);
  stats_.stretch_entries_hist.Add(stretch_entries_, stretch_entries_);
  stats_.stretch_bytes_hist.Add(stretch_entries_, stretch_bytes_);
  stats_.max_stretch = std::max(stats_.max_stretch, stretch_entries_);
  stretch_entries_ = 0;
  stretch_bytes_ = 0;
}

void ChainTracker::Invalidate() {
  stats_.chain_valid = false;
  has_prev_ = false;
  row_entries_ = 0;
  row_bytes_ = 0;
  row_dead_ = false;
  stretch_entries_ = 0;
  stretch_bytes_ = 0;
  row_subtotal_ = nullptr;
}

void ChainTracker::AssignPrevKey(Slice subdoc_key, size_t shared_prefix) {
  // Only the suffix past the shared prefix changed; copy just that.
  const size_t keep = std::min(shared_prefix, prev_key_.size());
  prev_key_.resize(subdoc_key.size());
  if (subdoc_key.size() > keep) {
    memcpy(prev_key_.data() + keep, subdoc_key.data() + keep, subdoc_key.size() - keep);
  }
}

void ChainTracker::AddReclaimable(size_t entry_bytes, int64_t droppable_by_micros) {
  ++stats_.reclaimable_entries;
  stats_.reclaimable_bytes += entry_bytes;
  const size_t band = AgeBands::BandIndex(stats_.anchor_micros - droppable_by_micros);
  ++stats_.droppable_age_entries[band];
  stats_.droppable_age_bytes[band] += entry_bytes;
  ++stretch_entries_;
  stretch_bytes_ += entry_bytes;
}

void ChainTracker::UpdateWriteHtRange(const EncodedDocHybridTime& encoded_ht) {
  // EncodedDocHybridTime orders by the time it encodes (the byte order is reversed internally).
  if (min_encoded_ht_.empty() || encoded_ht < min_encoded_ht_) {
    min_encoded_ht_ = encoded_ht;
  }
  if (max_encoded_ht_.empty() || encoded_ht > max_encoded_ht_) {
    max_encoded_ht_ = encoded_ht;
  }
}

Result<int64_t> ChainTracker::PhysicalMicros(const EncodedDocHybridTime& encoded_ht) {
  const auto doc_ht = VERIFY_RESULT(encoded_ht.Decode());
  return static_cast<int64_t>(doc_ht.hybrid_time().GetPhysicalValueMicros());
}

Result<ChainTracker::RowKeyEnds> ChainTracker::ComputeRowKeyEnds(Slice subdoc_key) {
  // Mirrors the first call of SubDocKey::DecodeDocKeyAndSubKeyEnds, including the table-tombstone
  // form (cotable/colocation id followed directly by kGroupEnd: an empty DocKey).
  const auto id_size =
      VERIFY_RESULT(dockv::DocKey::EncodedSize(subdoc_key, dockv::DocKeyPart::kUpToId));
  SCHECK_GT(subdoc_key.size(), id_size, Corruption,
            Format("Cannot have exclusively ID in key $0", subdoc_key.ToDebugHexString()));
  const char first = subdoc_key[0];
  if ((first == dockv::KeyEntryTypeAsChar::kColocationId ||
       first == dockv::KeyEntryTypeAsChar::kTableId) &&
      subdoc_key[id_size] == dockv::KeyEntryTypeAsChar::kGroupEnd) {
    // Table tombstone: the row key is the coprefix plus the group end, so that the tombstone entry
    // itself is the head of its own (dead) row.
    return RowKeyEnds{.coprefix_end = id_size, .row_end = id_size + 1};
  }
  const auto doc_key_size = VERIFY_RESULT(dockv::DocKey::EncodedSize(
      subdoc_key.WithoutPrefix(id_size), dockv::DocKeyPart::kWholeDocKey));
  return RowKeyEnds{.coprefix_end = id_size, .row_end = id_size + doc_key_size};
}

}  // namespace yb::docdb
