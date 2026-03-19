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

#pragma once

#include <string>

#include "yb/common/schema.h"
#include "yb/common/pgsql_protocol.messages.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/key_bytes.h"

#include "yb/yql/pggate/pg_table.h"

namespace yb::pggate {

//--------------------------------------------------------------------------------------------------
// PgReadRange is a helper class that represents a scanning range for read operations.
// It can take bounds in various formats, store them internally and apply them to a read request.
// The PgReadRange object is mutable, but it only allows to update the bounds to make them stricter.
// In other words, the lower bound can only be increased, and the upper bound can only be decreased.
// Hence if the object represents an empty range, it remains empty. Empty scan range means no
// execution is needed, and allows execution to take a fast path and skip many execution tasks.
//--------------------------------------------------------------------------------------------------
class PgReadRange {
 public:
  explicit PgReadRange(const PgTable& table) : table_(table) {}
  bool IsEmpty() const { return empty_; }

  // The SetXXX methods update the range bounds. After the change the empty_ flag is updated, so
  // it is not required to compare bounds to check if the range is empty.
  // Set the hash code as the range bound.
  void SetHashCodeBound(uint16_t hash_code, bool is_inclusive, bool is_lower);
  // Set the doc key as the range bound.
  template <class T>
  void SetDocKeyBound(const T& doc_key, bool is_inclusive, bool is_lower);
  // Set the partition's bounds as the range bounds.
  void SetPartitionBounds(size_t partition);

  // Update the bounds on the specified read request. If requests already has bounds, they are
  // updated if the respective new bound is stricter. If the resulting bounds represent an empty
  // range, the function returns false, and the execution can be skipped.
  bool ApplyBounds(LWPgsqlReadRequestPB& req) const;
  // Older (before GHI#28219) DocDB version treated bounds on the requests to the hash distributed
  // relations specially. They expected the bounds to be two-byte hash codes, and did not perform
  // any validations on them. The newer versions expect the bounds to be encoded doc keys, but
  // recognize the old format. Therefore, in "mixed" mode, when cluster nodes of different version
  // work side by side during rolling upgrade, we convert the bounds set with the SetHashCodeBound
  // method to be two-byte hash code, or error out if the bound is not derived from hash code.
  static Status ConvertBoundsToHashCode(LWPgsqlReadRequestPB& req);
 private:
  static Status CheckBoundDerivedFromHashCode(Slice bound, bool is_lower);
  dockv::KeyBytes HashCodeToBound(uint16_t hash_code, bool is_lower) const;
  // After bounds change, check if the range is empty and update the empty flag.
  void ComputeEmpty();
  // Set the new lower bound.
  void SetLowerBound(dockv::KeyBytes&& bound, bool is_inclusive);
  // Set the new upper bound.
  void SetUpperBound(dockv::KeyBytes&& bound, bool is_inclusive);
  // Check if boundaries set on the request define valid (not empty) range.
  static bool CheckScanBounds(const LWPgsqlReadRequestPB& req);
  // Set the lower bound on the request.
  void ApplyLowerBound(LWPgsqlReadRequestPB& req) const;
  // Set the upper bound on the request.
  void ApplyUpperBound(LWPgsqlReadRequestPB& req) const;
  const PgTable& table_;
  dockv::KeyBytes lower_bound_;
  bool lower_bound_is_inclusive_ = false;
  dockv::KeyBytes upper_bound_;
  bool upper_bound_is_inclusive_ = false;
  bool empty_ = false;
};

}  // namespace yb::pggate
