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

#include "yb/client/yb_op.h"
#include "yb/common/schema.h"
#include "yb/dockv/doc_key.h"
#include "yb/dockv/partition.h"

#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tabledesc.h"
#include "yb/yql/pggate/util/pg_tuple.h"
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
  if (is_lower) {
    SetLowerBound(HashCodeToBound(hash_code, true /* is_lower */), false /* is_inclusive */);
  } else {
    SetUpperBound(HashCodeToBound(hash_code, false /* is_lower */), false /* is_inclusive */);
  }
  ComputeEmpty();
}

template <class T>
void PgReadRange::SetDocKeyBound(const T& doc_key, bool is_inclusive, bool is_lower) {
  auto bound = ToKeyBytes(doc_key);
  if (is_lower) {
    SetLowerBound(std::move(bound), is_inclusive);
  } else {
    SetUpperBound(std::move(bound), is_inclusive);
  }
  ComputeEmpty();
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

bool PgReadRange::ApplyBounds(LWPgsqlReadRequestPB& req) const {
  if (empty_) {
    return false;
  }
  ApplyLowerBound(req);
  ApplyUpperBound(req);
  return CheckScanBounds(req);
}

Status PgReadRange::ConvertBoundsToHashCode(LWPgsqlReadRequestPB& req) {
  DCHECK(!yb_allow_dockey_bounds);

  // If the bounds are empty, there is nothing to do.
  if (!req.has_lower_bound() && !req.has_upper_bound()) {
    return Status::OK();
  }

  // If the bounds are hash code already, there is nothing to do.
  if (client::AreBoundsHashCode(req)) {
    return Status::OK();
  }

  if (req.has_lower_bound()) {
    RETURN_NOT_OK(CheckBoundDerivedFromHashCode(req.lower_bound().key(), /* is_lower = */ true));
    const auto hash_code = VERIFY_RESULT(dockv::DocKey::DecodeHash(req.lower_bound().key()));
    const auto& bound = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
    req.mutable_lower_bound()->dup_key(bound);
    req.mutable_lower_bound()->set_is_inclusive(true);
  }

  if (req.has_upper_bound()) {
    RETURN_NOT_OK(CheckBoundDerivedFromHashCode(req.upper_bound().key(), /* is_lower = */ false));
    const auto hash_code = VERIFY_RESULT(dockv::DocKey::DecodeHash(req.upper_bound().key()));
    const auto& bound = dockv::PartitionSchema::EncodeMultiColumnHashValue(hash_code);
    req.mutable_upper_bound()->dup_key(bound);
    req.mutable_upper_bound()->set_is_inclusive(true);
  }
  return Status::OK();
}

// Make sure the bound matches the format produced by the HashCodeToBound function below,
// and therefore represents a bare hash code bound.
Status PgReadRange::CheckBoundDerivedFromHashCode(Slice bound, bool is_lower) {
  dockv::DocKey dockey;
  RETURN_NOT_OK(
      dockey.DecodeFrom(bound, dockv::DocKeyPart::kWholeDocKey, dockv::AllowSpecial::kTrue));
  const auto& hashed_components = dockey.hashed_group();
  const auto& range_components = dockey.range_group();

  const auto expected_type =
      is_lower ? dockv::KeyEntryType::kLowest : dockv::KeyEntryType::kHighest;

  if (hashed_components.size() != 1 || hashed_components[0].type() != expected_type ||
      range_components.size() != 1 || range_components[0].type() != expected_type) {
    return STATUS(
        RuntimeError,
        "This feature is not supported because the AutoFlag 'yb_allow_dockey_bounds' is false. "
        "This typically happends during an upgrade to the version that introduced this flag. "
        "Please re-try after the upgrade is complete and the AutoFlag is set to true.");
  }
  return Status::OK();
}

// The format produced by this function must be recognized by the CheckBoundDerivedFromHashCode
// function above. If modified, the CheckBoundDerivedFromHashCode function must be updated
// accordingly.
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

    if (diff == 0 && !lower_bound_is_inclusive_) {
      // The bound may be already exclusive, but assignment won't hurt.
      req.mutable_lower_bound()->set_is_inclusive(false);
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

    if (diff == 0 && !upper_bound_is_inclusive_) {
      // The bound may be already exclusive, but assignment won't hurt.
      req.mutable_upper_bound()->set_is_inclusive(false);
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
