// Copyright (c) YugaByte, Inc.
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

#include "yb/docdb/doc_key.h"
#include "yb/docdb/value.h"
#include "yb/docdb/docdb_fwd.h"

#include "yb/util/slice.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

class ScanChoices {
 public:
  explicit ScanChoices(bool is_forward_scan) : is_forward_scan_(is_forward_scan) {}
  virtual ~ScanChoices() {}

  bool CurrentTargetMatchesKey(const Slice& curr);

  // Returns false if there are still target keys we need to scan, and true if we are done.
  virtual bool FinishedWithScanChoices() const { return finished_; }

  virtual bool IsInitialPositionKnown() const { return false; }

  // Go to the next scan target if any.
  virtual Status DoneWithCurrentTarget() = 0;

  // Go (directly) to the new target (or the one after if new_target does not
  // exist in the desired list/range). If the new_target is larger than all scan target options it
  // means we are done.
  virtual Status SkipTargetsUpTo(const Slice& new_target) = 0;

  // If the given doc_key isn't already at the desired target, seek appropriately to go to the
  // current target.
  virtual Status SeekToCurrentTarget(IntentAwareIteratorIf* db_iter) = 0;

  static void AppendToKey(const std::vector<KeyEntryValue>& values, KeyBytes* key_bytes);

  static Result<std::vector<KeyEntryValue>> DecodeKeyEntryValue(
      DocKeyDecoder* decoder, size_t num_cols);

  static ScanChoicesPtr Create(
      const Schema& schema, const DocQLScanSpec& doc_spec,
      const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key);

  static ScanChoicesPtr Create(
      const Schema& schema, const DocPgsqlScanSpec& doc_spec,
      const KeyBytes& lower_doc_key, const KeyBytes& upper_doc_key);

 protected:
  const bool is_forward_scan_;
  KeyBytes current_scan_target_;
  bool finished_ = false;

 private:
  DISALLOW_COPY_AND_ASSIGN(ScanChoices);
};

}  // namespace docdb
}  // namespace yb
