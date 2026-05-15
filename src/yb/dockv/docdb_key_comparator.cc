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

      // TableId, TransactionId, ExternalTransactionId, and TransactionApplyState
      // are encoded via KeyBytes::AppendString -> ZeroEncodeAndAppendStrToKey,
      // so they live in the zero-encoded group.
      case KeyEntryType::kTableId:
      case KeyEntryType::kTransactionId:
      case KeyEntryType::kExternalTransactionId:
        SkipZeroEncoded(data, size, &pos);
        break;

      // Frozen types contain a sequence of zero-encoded values terminated by a
      // kGroupEnd marker, where any inner string-like component can itself
      // contain 0x00 0x01-escaped null bytes. Parsing them robustly without
      // duplicating the full DocDB key decoder is non-trivial, and DocumentDB
      // tablets never have frozen primary keys. Bail to byte-wise comparison.
      case KeyEntryType::kFrozen:
      case KeyEntryType::kFrozenDescending:
      // Intent records, sub-transaction ids, and other internal markers don't
      // appear in DocumentDB collection PKs either; rather than guess their
      // encoded width, bail out so the caller falls back to byte-wise compare.
      case KeyEntryType::kIntentTypeSet:
      case KeyEntryType::kObsoleteIntentTypeSet:
      case KeyEntryType::kObsoleteIntentType:
      case KeyEntryType::kSubTransactionId:
        return std::nullopt;

      default:
        // Unknown type, can't determine size. Bail to byte-wise comparison.
        return std::nullopt;
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

// Forward declaration so CompareBsonRegion can recurse on the suffix when the
// BSON region compares equal.
int CompareKeyBsonAware(Slice a, Slice b);

// Compares two keys that have a BSON region at the given position.
// Decodes the BSON values from both keys and compares them using BSON comparison logic.
// If the BSON values are equal, recurses on the suffix using CompareKeyBsonAware
// so that any subsequent BSON columns are also compared with canonical ordering
// (a plain byte-wise compare on the suffix would miss type-equal-but-binary-
// different cases like int32 vs int64).
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

  if (cmp != 0) {
    return cmp;
  }

  // BSON values are equal. The suffix after the BSON region may itself contain
  // more BSON columns or other typed values; recurse so any further BSON
  // regions are compared with canonical ordering rather than byte-wise.
  Slice a_suffix(a_slice.data(), a.end() - a_slice.data());
  Slice b_suffix(b_slice.data(), b.end() - b_slice.data());
  return CompareKeyBsonAware(a_suffix, b_suffix);
}

int CompareKeyBsonAware(Slice a, Slice b) {
  // Fast path: byte-wise equality.
  if (a == b) {
    return 0;
  }

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
  // This is faster than the structural FindBsonRegionContaining walk for
  // tablets where the diff lands inside a fixed-width column ahead of the
  // BSON region (e.g. shard_key_value bigint preceding object_id bson).
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

}  // namespace

int DocDBKeyComparator::Compare(Slice a, Slice b) const {
  return CompareKeyBsonAware(a, b);
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
