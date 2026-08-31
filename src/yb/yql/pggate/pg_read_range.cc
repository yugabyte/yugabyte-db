//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_read_range.h"

#include "yb/common/schema.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"

#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/util/ybc_guc.h"

namespace yb::pggate {

namespace {

dockv::KeyBytes ToKeyBytes(const Slice& doc_key) {
  return dockv::KeyBytes(doc_key);
}
dockv::KeyBytes ToKeyBytes(const dockv::DocKey& doc_key) {
  return doc_key.Encode();
}

} // namespace

bool PgReadRange::Equals(const PgReadRange& other) const {
  return (empty_ && other.empty_) ||
         (!empty_ && !other.empty_ &&
          lower_bound_.CompareTo(other.lower_bound_) == 0 &&
          upper_bound_.CompareTo(other.upper_bound_) == 0 &&
          lower_bound_is_inclusive_ == other.lower_bound_is_inclusive_ &&
          upper_bound_is_inclusive_ == other.upper_bound_is_inclusive_);
}

bool PgReadRange::Intersects(const PgReadRange& other) const {
  if (empty_ || other.empty_) {
    return false;
  }
  // No intersection if the lower bound is higher than other's upper bound.
  // Special case: empty upper bound is higher than anything.
  if (!other.upper_bound_.empty()) {
    auto cmp = lower_bound_.CompareTo(other.upper_bound_);
    if (cmp > 0 ||
        (cmp == 0 && !(lower_bound_is_inclusive_ && other.upper_bound_is_inclusive_))) {
      return false;
    }
  }
  // Same, other way around.
  if (!upper_bound_.empty()) {
    auto cmp = upper_bound_.CompareTo(other.lower_bound_);
    if (cmp < 0 ||
        (cmp == 0 && !(upper_bound_is_inclusive_ && other.lower_bound_is_inclusive_))) {
      return false;
    }
  }
  return true;
}

std::optional<uint16_t> PgReadRange::DecodeEncodedHashPartitionKeyBound(Slice bound) {
  dockv::DocKeyDecoder decoder(bound);
  uint16_t hash_code;
  auto result = decoder.DecodeHashCode(&hash_code);
  if (!result.ok() || !*result) {
    return std::nullopt;
  }
  // The entry type is kInvalid if the left input is empty.
  const auto next_key_entry_type = dockv::DecodeKeyEntryType(decoder.left_input());
  if (next_key_entry_type == dockv::KeyEntryType::kInvalid ||
      next_key_entry_type == dockv::KeyEntryType::kLowest) {
    return hash_code;
  } else if (hash_code < UINT16_MAX &&
             next_key_entry_type == dockv::KeyEntryType::kHighest) {
    return hash_code + 1;
  }
  return std::nullopt;
}

void PgReadRange::SetHashCodeBound(uint16_t hash_code, bool is_inclusive, bool is_lower) {
  if (!is_inclusive) {
    if (is_lower) {
      DCHECK(hash_code != UINT16_MAX) << Format("Invalid hash code bound: > $0", UINT16_MAX);
      ++hash_code;
    } else {
      DCHECK(hash_code != 0) << "Invalid hash code bound: < 0";
      --hash_code;
    }
  }
  // Edge cases when the respective bound is empty and hence does not change the range.
  if ((is_lower && hash_code == 0) || (!is_lower && hash_code == UINT16_MAX)) {
    return;
  }
  SetBound(HashCodeToBound(hash_code, is_lower), false /* is_inclusive */, is_lower);
}

template <class T>
void PgReadRange::SetDocKeyBound(const T& doc_key, bool is_inclusive, bool is_lower) {
  auto key_bytes = ToKeyBytes(doc_key);
  if (table_->schema().num_hash_key_columns() > 0) {
    auto opt_hash_code = DecodeEncodedHashPartitionKeyBound(key_bytes);
    if (opt_hash_code) {
      // If the document key is found to be a hash partition key, original is_inclusive value
      // does not matter, as valid document key can't be equal to a hash partition key.
      // Conventionally, lower hash code bound is inclusive, and upper hash code bound is exclusive.
      // Specially handle zero upper bound hash code. It may be a legit doc key making an empty
      // range, but SetHashCodeBound would treat it as error.
      if (*opt_hash_code == 0 && !is_lower) {
        empty_ = true;
      } else {
        SetHashCodeBound(*opt_hash_code, /* is_inclusive = */ is_lower, is_lower);
      }
      return;
    }
  }
  SetBound(std::move(key_bytes), is_inclusive, is_lower);
}

void PgReadRange::SetPartitionBounds(size_t partition) {
  const auto& partition_keys = table_->GetPartitionList();
  DCHECK(partition < partition_keys.size()) << "Invalid partition index: " << partition;
  if (table_->schema().num_hash_key_columns() == 0) {
    // Partition key is a document key
    if (partition > 0) {
      auto bound = dockv::KeyBytes(partition_keys[partition]);
      SetLowerBound(std::move(bound), true /* is_inclusive */);
    }
    if (partition < partition_keys.size() - 1) {
      auto bound = dockv::KeyBytes(partition_keys[partition + 1]);
      SetUpperBound(std::move(bound), false /* is_inclusive */);
    }
  } else {
    // Partition key is a hash code
    if (partition > 0) {
      auto hash = dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_keys[partition]);
      auto bound = HashCodeToBound(hash, true /* is_lower */);
      // Hash code bound is not a document key, inclusivity does not matter
      SetLowerBound(std::move(bound), false /* is_inclusive */);
    }
    if (partition < partition_keys.size() - 1) {
      auto hash =
          dockv::PartitionSchema::DecodeMultiColumnHashValue(partition_keys[partition + 1]) - 1;
      auto bound = HashCodeToBound(hash, false /* is_lower */);
      // Hash code bound is not a document key, inclusivity does not matter
      SetUpperBound(std::move(bound), false /* is_inclusive */);
    }
  }
  ComputeEmpty();
}

void PgReadRange::SetRequestBounds(const LWPgsqlReadRequestPB& req) {
  if (req.has_lower_bound()) {
    auto bound = ToKeyBytes(req.lower_bound().key());
    SetLowerBound(std::move(bound), req.lower_bound().is_inclusive());
  }
  if (req.has_upper_bound()) {
    auto bound = ToKeyBytes(req.upper_bound().key());
    SetUpperBound(std::move(bound), req.upper_bound().is_inclusive());
  }
  ComputeEmpty();
}

bool PgReadRange::ApplyBounds(LWPgsqlReadRequestPB& req) const {
  if (empty_) {
    return false;
  }
  ApplyLowerBound(req);
  ApplyUpperBound(req);
  return CheckScanBounds(req);
}

Status PgReadRange::ConvertBoundsToHashCode(LWPgsqlReadRequestPB& req) {
  constexpr auto kErrorMessage =
      "The request's $0 bound ($1) does not appear to be a correctly encoded hash code. "
      "The auto flag 'yb_allow_dockey_bounds' is likely false. "
      "This typically happens during an upgrade to the version that introduced this flag. "
      "Please re-try after the upgrade is complete and the AutoFlag is set to true.";
  DCHECK(!yb_allow_dockey_bounds);

  // If the bounds are empty, there is nothing to do.
  if (!req.has_lower_bound() && !req.has_upper_bound()) {
    return Status::OK();
  }

  // Skip if the bound is a hash code bound already.
  if (req.has_lower_bound() &&
      !dockv::PartitionSchema::IsValidHashPartitionKeyBound(req.lower_bound().key())) {
    auto hash_code = DecodeEncodedHashPartitionKeyBound(req.lower_bound().key());
    if (!hash_code) {
      return STATUS_FORMAT(
          RuntimeError, kErrorMessage, "lower", req.lower_bound().key().ToDebugString());
    }
    const auto bound = dockv::PartitionSchema::EncodeMultiColumnHashValue(*hash_code);
    req.mutable_lower_bound()->dup_key(bound);
    req.mutable_lower_bound()->set_is_inclusive(true);
  }

  // Skip if the bound is a hash code bound already.
  if (req.has_upper_bound() &&
      !dockv::PartitionSchema::IsValidHashPartitionKeyBound(req.upper_bound().key())) {
    auto hash_code = DecodeEncodedHashPartitionKeyBound(req.upper_bound().key());
    if (!hash_code || *hash_code == 0) {
      return STATUS_FORMAT(
          RuntimeError, kErrorMessage, "upper", req.upper_bound().key().ToDebugString());
    }
    // Upper bound partition key is exclusive, but request's hash code upper bound is inclusive.
    const auto bound = dockv::PartitionSchema::EncodeMultiColumnHashValue(*hash_code - 1);
    req.mutable_upper_bound()->dup_key(bound);
    req.mutable_upper_bound()->set_is_inclusive(true);
  }
  return Status::OK();
}

dockv::KeyBytes PgReadRange::HashCodeToBound(uint16_t hash_code, bool is_lower) const {
  static const dockv::KeyEntryValues kLowest{dockv::KeyEntryValue{dockv::KeyEntryType::kLowest}};
  static const dockv::KeyEntryValues kHighest{dockv::KeyEntryValue{dockv::KeyEntryType::kHighest}};

  const auto& hash_range_components = is_lower ? kLowest : kHighest;
  return dockv::DocKey(
      table_->schema(), hash_code, hash_range_components, hash_range_components).Encode();
}

void PgReadRange::ComputeEmpty() {
  // The range may already be empty, or guaranteed to be non-empty because of an open bound.
  if (empty_ || lower_bound_.empty() || upper_bound_.empty()) {
    return;
  }
  auto diff = lower_bound_.CompareTo(upper_bound_);
  empty_ = (diff > 0 || (diff == 0 && !(lower_bound_is_inclusive_ && upper_bound_is_inclusive_)));
}

void PgReadRange::SetLowerBound(dockv::KeyBytes&& bound, bool is_inclusive) {
  auto diff = lower_bound_.CompareTo(bound);
  if (diff < 0) {
    lower_bound_ = std::move(bound);
    lower_bound_is_inclusive_ = is_inclusive;
  } else if (diff == 0) {
    lower_bound_is_inclusive_ &= is_inclusive;
  }
}

void PgReadRange::SetUpperBound(dockv::KeyBytes&& bound, bool is_inclusive) {
  auto diff = upper_bound_.empty() ? 1 : upper_bound_.CompareTo(bound);
  if (diff > 0) {
    upper_bound_ = std::move(bound);
    upper_bound_is_inclusive_ = is_inclusive;
  } else if (diff == 0) {
    upper_bound_is_inclusive_ &= is_inclusive;
  }
}

bool PgReadRange::CheckScanBounds(const LWPgsqlReadRequestPB& req) {
  const auto key_diff = req.has_lower_bound() && req.has_upper_bound()
      ? req.lower_bound().key().compare(req.upper_bound().key()) : -1;
  return key_diff < 0 ||
        (key_diff == 0 && req.lower_bound().is_inclusive() && req.upper_bound().is_inclusive());
}

void PgReadRange::ApplyLowerBound(LWPgsqlReadRequestPB& req) const {
  if (lower_bound_.empty()) {
    return;
  }

  // Update existing lower bound if the new one is more restrictive.
  if (req.has_lower_bound()) {
    const auto key = req.lower_bound().key();
    // With GHI#28219, bounds are expected to be dockeys.
    DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(key));
    const auto diff = key.compare(lower_bound_);
    if (diff > 0) {
      return;
    }

    if (diff == 0) {
      // Keys are equal, update only inclusivity if necessary.
      if (!lower_bound_is_inclusive_) {
        req.mutable_lower_bound()->set_is_inclusive(false);
      }
      return;
    }
  }
  req.mutable_lower_bound()->dup_key(lower_bound_);
  req.mutable_lower_bound()->set_is_inclusive(lower_bound_is_inclusive_);
}

void PgReadRange::ApplyUpperBound(LWPgsqlReadRequestPB& req) const {
  if (upper_bound_.empty()) {
    return;
  }

  // Update existing upper bound if the new one is more restrictive.
  if (req.has_upper_bound()) {
    const auto key = req.upper_bound().key();
    // With GHI#28219, bounds are expected to be dockeys.
    DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(key));
    const auto diff = key.compare(upper_bound_);

    if (diff < 0) {
      return;
    }

    if (diff == 0) {
      // Keys are equal, update only inclusivity if necessary.
      if (!upper_bound_is_inclusive_) {
        req.mutable_upper_bound()->set_is_inclusive(false);
      }
      return;
    }
  }
  req.mutable_upper_bound()->dup_key(upper_bound_);
  req.mutable_upper_bound()->set_is_inclusive(upper_bound_is_inclusive_);
}

// Explicit template instantiations
template void PgReadRange::SetDocKeyBound<dockv::DocKey>(const dockv::DocKey&, bool, bool);
template void PgReadRange::SetDocKeyBound<Slice>(const Slice&, bool, bool);

}  // namespace yb::pggate
