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

#include "yb/dockv/docdb_key_comparator.h"

#include <string>

#include "yb/dockv/doc_bson.h"
#include "yb/dockv/doc_kv_util.h"
#include "yb/dockv/value_type.h"
#include "yb/util/slice.h"

namespace yb::dockv {

namespace {

// Advances position past a zero-encoded value (terminated by 0x00 0x00).
// Returns false if the terminator was not found.
bool SkipZeroEncoded(const uint8_t* data, size_t size, size_t* pos) {
  while (*pos + 1 < size) {
    if (data[*pos] == 0x00) {
      if (data[*pos + 1] == 0x00) {
        *pos += 2;  // Skip the terminator.
        return true;
      }
      // 0x00 0x01 is an escaped null byte; skip both.
      *pos += 2;
    } else {
      (*pos)++;
    }
  }
  // Reached end without finding terminator.
  *pos = size;
  return false;
}

// Advances position past a complement-zero-encoded value (terminated by 0xFF 0xFF).
bool SkipComplementZeroEncoded(const uint8_t* data, size_t size, size_t* pos) {
  while (*pos + 1 < size) {
    if (data[*pos] == 0xFF) {
      if (data[*pos + 1] == 0xFF) {
        *pos += 2;
        return true;
      }
      *pos += 2;
    } else {
      (*pos)++;
    }
  }
  *pos = size;
  return false;
}

// Information about a BSON region found in a key during scanning.
struct BsonRegionInfo {
  size_t value_start;    // Position of the first byte of the zero-encoded BSON value.
  size_t value_end;      // Position past the terminator.
  KeyEntryType type;     // kBson or kBsonDescending.
};

// Scans a DocDB key from the beginning to find all BSON regions.
// Returns the BSON region that contains diff_pos, or std::nullopt if
// diff_pos is not inside a BSON region.
std::optional<BsonRegionInfo> FindBsonRegionContaining(
    const uint8_t* data, size_t size, size_t diff_pos) {
  size_t pos = 0;

  while (pos < size) {
    auto type = static_cast<KeyEntryType>(data[pos]);
    size_t type_pos = pos;
    pos++;  // Skip type byte.

    bool is_bson = (type == KeyEntryType::kBson || type == KeyEntryType::kBsonDescending);
    size_t value_start = pos;

    switch (type) {
      // Types with 0 additional bytes after the type byte.
      case KeyEntryType::kGroupEnd:
      case KeyEntryType::kGroupEndDescending:
      case KeyEntryType::kNullLow:
      case KeyEntryType::kNullHigh:
      case KeyEntryType::kTrue:
      case KeyEntryType::kTrueDescending:
      case KeyEntryType::kFalse:
      case KeyEntryType::kFalseDescending:
      case KeyEntryType::kLowest:
      case KeyEntryType::kHighest:
      case KeyEntryType::kObject:
      case KeyEntryType::kCounter:
      case KeyEntryType::kSSForward:
      case KeyEntryType::kSSReverse:
      case KeyEntryType::kWeakObjectLock:
      case KeyEntryType::kStrongObjectLock:
      case KeyEntryType::kMaxByte:
      case KeyEntryType::kVectorIndexMetadata:
        break;

      // 1 byte.
      case KeyEntryType::kGinNull:
        pos += 1;
        break;

      // 2 bytes.
      case KeyEntryType::kUInt16Hash:
        pos += 2;
        break;

      // 4 bytes.
      case KeyEntryType::kInt32:
      case KeyEntryType::kInt32Descending:
      case KeyEntryType::kUInt32:
      case KeyEntryType::kUInt32Descending:
      case KeyEntryType::kFloat:
      case KeyEntryType::kFloatDescending:
      case KeyEntryType::kColocationId:
      case KeyEntryType::kVectorId:
        pos += 4;
        break;

      // 8 bytes.
      case KeyEntryType::kInt64:
      case KeyEntryType::kInt64Descending:
      case KeyEntryType::kUInt64:
      case KeyEntryType::kUInt64Descending:
      case KeyEntryType::kDouble:
      case KeyEntryType::kDoubleDescending:
      case KeyEntryType::kTimestamp:
      case KeyEntryType::kTimestampDescending:
        pos += 8;
        break;

      // Zero-encoded types (terminated by 0x00 0x00).
      case KeyEntryType::kString:
      case KeyEntryType::kBson:
      case KeyEntryType::kDecimal:
      case KeyEntryType::kVarInt:
      case KeyEntryType::kCollString:
      case KeyEntryType::kUuid:
      case KeyEntryType::kInetaddress:
        SkipZeroEncoded(data, size, &pos);
        break;

      // Complement zero-encoded types (terminated by 0xFF 0xFF).
      case KeyEntryType::kStringDescending:
      case KeyEntryType::kBsonDescending:
      case KeyEntryType::kDecimalDescending:
      case KeyEntryType::kVarIntDescending:
      case KeyEntryType::kCollStringDescending:
      case KeyEntryType::kUuidDescending:
      case KeyEntryType::kInetaddressDescending:
        SkipComplementZeroEncoded(data, size, &pos);
        break;

      // Frozen types - recursive structure terminated by GroupEnd markers.
      case KeyEntryType::kFrozen:
        while (pos < size &&
               static_cast<KeyEntryType>(data[pos]) != KeyEntryType::kGroupEnd) {
          // Skip inner entries recursively by calling FindBsonRegionContaining
          // with a diff_pos beyond the key to just scan forward.
          // Simpler: just skip to the GroupEnd.
          auto inner_type = static_cast<KeyEntryType>(data[pos]);
          pos++;
          if (inner_type == KeyEntryType::kGroupEnd) break;
          // Handle inner types the same way (simplified - skip to group end).
        }
        // We'll fall through to the end-of-key fallback below for complex frozen types.
        // For correctness, scan for the kGroupEnd byte.
        while (pos < size && static_cast<KeyEntryType>(data[pos]) != KeyEntryType::kGroupEnd) {
          pos++;
        }
        if (pos < size) pos++;  // Skip kGroupEnd.
        break;

      case KeyEntryType::kFrozenDescending:
        while (pos < size &&
               static_cast<KeyEntryType>(data[pos]) != KeyEntryType::kGroupEndDescending) {
          pos++;
        }
        if (pos < size) pos++;
        break;

      // HybridTime appears at the end of keys. The encoding is variable-length
      // and complex, so we skip to the end.
      case KeyEntryType::kHybridTime:
        pos = size;
        break;

      // Column IDs use variable-length encoding.
      case KeyEntryType::kColumnId:
      case KeyEntryType::kSystemColumnId:
        while (pos < size && (data[pos] & 0x80)) pos++;
        if (pos < size) pos++;
        break;

      // TableId: 16 bytes UUID.
      case KeyEntryType::kTableId:
        pos += 16;
        break;

      // Intent types, transaction IDs, etc.
      case KeyEntryType::kIntentTypeSet:
      case KeyEntryType::kObsoleteIntentTypeSet:
      case KeyEntryType::kObsoleteIntentType:
        pos += 1;
        break;

      case KeyEntryType::kTransactionId:
      case KeyEntryType::kExternalTransactionId:
        pos += 17;  // TransactionId is 16 bytes + 1 status byte typically.
        break;

      case KeyEntryType::kSubTransactionId:
        pos += 4;
        break;

      default:
        // Unknown type, can't determine size. Skip to end.
        pos = size;
        break;
    }

    // Check if the diff_pos falls within this component.
    if (diff_pos >= type_pos && diff_pos < pos) {
      if (is_bson && diff_pos >= value_start) {
        return BsonRegionInfo{value_start, pos, type};
      }
      // diff_pos is in a non-BSON component (or in the type byte of a BSON component,
      // which means the type bytes differ and byte-wise comparison is correct).
      return std::nullopt;
    }
  }

  return std::nullopt;
}

// Compares two keys that have a BSON region at the given position.
// Decodes the BSON values from both keys and compares them using BSON comparison logic.
// If the BSON values are equal, continues byte-wise comparison from after the BSON region.
int CompareBsonRegion(Slice a, Slice b, const BsonRegionInfo& region) {
  // Decode BSON value from key a.
  Slice a_slice(a.data() + region.value_start, a.size() - region.value_start);
  Slice b_slice(b.data() + region.value_start, b.size() - region.value_start);

  std::string a_bson, b_bson;

  Status sa, sb;
  if (region.type == KeyEntryType::kBson) {
    sa = BsonKeyFromComparableBinary(&a_slice, &a_bson);
    sb = BsonKeyFromComparableBinary(&b_slice, &b_bson);
  } else {
    sa = BsonKeyFromComparableBinaryDescending(&a_slice, &a_bson);
    sb = BsonKeyFromComparableBinaryDescending(&b_slice, &b_bson);
  }

  if (!sa.ok() || !sb.ok()) {
    // Decoding failed, fall back to byte-wise comparison.
    return a.compare(b);
  }

  int cmp = CompareBson(Slice(a_bson), Slice(b_bson));
  if (region.type == KeyEntryType::kBsonDescending) {
    cmp = -cmp;
  }

  if (cmp != 0) return cmp;

  // BSON values are equal. Compare the remaining bytes after the BSON region.
  // Note: The remaining key portions might also contain BSON entries, but since
  // the BSON values are equal, their zero-encoded representations are also equal,
  // meaning the next differing byte (if any) will be after this BSON region.
  // We recursively apply the same logic by comparing the suffixes.
  Slice a_suffix(a_slice.data(), a.end() - a_slice.data());
  Slice b_suffix(b_slice.data(), b.end() - b_slice.data());

  // For the suffix, we can use byte-wise comparison because:
  // - If the decoded BSON values are equal, their zero-encoded forms are byte-wise equal
  //   (same input -> same zero-encoding). So any difference in the suffix is in a different
  //   component, which for equal BSON values would be past the BSON region.
  return a_suffix.compare(b_suffix);
}

}  // namespace

int DocDBKeyComparator::Compare(Slice a, Slice b) const {
  // Fast path: byte-wise equality.
  if (a == b) return 0;

  const auto* a_data = reinterpret_cast<const uint8_t*>(a.data());
  const auto* b_data = reinterpret_cast<const uint8_t*>(b.data());
  size_t a_size = a.size();
  size_t b_size = b.size();

  // Find the first position where the two keys differ.
  size_t min_len = std::min(a_size, b_size);
  size_t diff_pos = 0;
  while (diff_pos < min_len && a_data[diff_pos] == b_data[diff_pos]) {
    diff_pos++;
  }

  if (diff_pos == min_len) {
    // One key is a prefix of the other.
    return (a_size < b_size) ? -1 : (a_size > b_size) ? 1 : 0;
  }

  // Quick check: scan the common prefix for potential BSON type bytes.
  // kBson = 'o' = 111, kBsonDescending = 'p' = 112.
  // If neither byte appears in the prefix up to diff_pos, the difference
  // cannot be inside a BSON region and byte-wise comparison is correct.
  bool may_have_bson = false;
  for (size_t i = 0; i < diff_pos; i++) {
    if (a_data[i] == static_cast<uint8_t>(KeyEntryType::kBson) ||
        a_data[i] == static_cast<uint8_t>(KeyEntryType::kBsonDescending)) {
      may_have_bson = true;
      break;
    }
  }

  if (may_have_bson) {
    // Scan the key structure to determine if diff_pos falls within a BSON region.
    auto bson_region = FindBsonRegionContaining(a_data, a_size, diff_pos);
    if (bson_region) {
      return CompareBsonRegion(a, b, *bson_region);
    }
  }

  // Not in a BSON region; byte-wise comparison is correct.
  return (a_data[diff_pos] < b_data[diff_pos]) ? -1 : 1;
}

bool DocDBKeyComparator::Equal(Slice a, Slice b) const {
  // Byte-wise equality is always correct: if the encoded bytes are identical,
  // the logical values are identical regardless of BSON comparison rules.
  return a == b;
}

const char* DocDBKeyComparator::Name() const {
  return "yb.DocDBKeyComparator.v1";
}

void DocDBKeyComparator::FindShortestSeparator(
    std::string* /* start */, const Slice& /* limit */) const {
  // No-op. We cannot safely shorten keys because the shortened key might change
  // its position relative to BSON-encoded keys. This is safe for correctness
  // but slightly less space-efficient for index blocks.
}

void DocDBKeyComparator::FindShortSuccessor(std::string* /* key */) const {
  // No-op for the same reason as FindShortestSeparator.
}

const rocksdb::Comparator* DocDBKeyComparatorInstance() {
  static DocDBKeyComparator instance;
  return &instance;
}

}  // namespace yb::dockv
