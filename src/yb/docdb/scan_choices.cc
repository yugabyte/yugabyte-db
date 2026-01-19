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

#include "yb/docdb/scan_choices.h"

#include "yb/common/schema.h"

#include "yb/docdb/doc_ql_scanspec.h"
#include "yb/docdb/doc_pgsql_scanspec.h"
#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/hybrid_scan_choices.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/value_type.h"

#include "yb/qlexpr/doc_scanspec_util.h"
#include "yb/qlexpr/ql_scanspec.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {

int TEST_scan_trivial_expectation = -1;

namespace docdb {

using dockv::DocKey;
using dockv::DocKeyDecoder;
using dockv::KeyBytes;
using dockv::KeyEntryType;
using dockv::KeyEntryValue;
using dockv::KeyEntryTypeAsChar;

class EmptyScanChoices : public ScanChoices {
 public:
  explicit EmptyScanChoices(const docdb::BloomFilterOptions& bloom_filter_options)
      : bloom_filter_options_(bloom_filter_options) {
  }

  bool Finished() const override {
    return false;
  }

  Result<bool> InterestedInRow(dockv::KeyBytes* row_key, IntentAwareIterator& iter) override {
    return true;
  }

  Result<bool> AdvanceToNextRow(dockv::KeyBytes* row_key,
                                IntentAwareIterator& iter,
                                bool current_fetched_row_skipped) override {
    return false;
  }

  Result<bool> PrepareIterator(IntentAwareIterator& iter, Slice table_key_prefix) override {
    return false;
  }

  docdb::BloomFilterOptions BloomFilterOptions() override {
    return bloom_filter_options_;
  }

 private:
  docdb::BloomFilterOptions bloom_filter_options_;
};

Result<ScanChoicesPtr> ScanChoices::Create(
    const DocReadContext& doc_read_context, const qlexpr::YQLScanSpec& doc_spec,
    const qlexpr::ScanBounds& bounds, Slice table_key_prefix,
    AllowVariableBloomFilter allow_variable_bloom_filter) {
  const auto& schema = doc_read_context.schema();
  auto prefixlen = doc_spec.prefix_length();
  auto num_hash_cols = schema.num_hash_key_columns();
  auto num_key_cols = schema.num_key_columns();
  // Do not count system columns.
  if (num_key_cols > 0 && schema.column(num_key_cols - 1).order() < 0) {
    num_key_cols--;
  }
  auto valid_prefixlen =
    prefixlen > 0 && prefixlen >= num_hash_cols && prefixlen <= num_key_cols;
  // Prefix must span at least all the hash columns since the first column is a hash code of all
  // hash columns in a hash partitioned table. And the hash code column cannot be skip'ed without
  // skip'ing all hash columns as well.
  if (prefixlen != 0 && !valid_prefixlen) {
    LOG(DFATAL)
      << "Prefix length: " << prefixlen << " is invalid for schema: "
      << "num_hash_cols: " << num_hash_cols << ", num_key_cols: " << num_key_cols;
  }

  auto test_scan_trivial_expectation = ANNOTATE_UNPROTECTED_READ(TEST_scan_trivial_expectation);
  if (test_scan_trivial_expectation >= 0 &&
      schema.num_key_columns() == 3 && schema.num_columns() == 3) {
    CHECK_EQ(bounds.trivial, test_scan_trivial_expectation != 0);
  }

  // bounds.trivial means that we just need lower and upper bounds for the scan.
  // So could use empty scan choices in case of the trivial range scan.
  if (doc_spec.options() || (doc_spec.range_bounds() && !bounds.trivial) || valid_prefixlen) {
    auto result = std::make_unique<HybridScanChoices>(
        schema, doc_spec, bounds.lower, bounds.upper, table_key_prefix);
    RETURN_NOT_OK(result->Init(doc_read_context));
    return result;
  }

  return std::make_unique<EmptyScanChoices>(VERIFY_RESULT(BloomFilterOptions::Make(
      doc_read_context, bounds.lower, bounds.upper, allow_variable_bloom_filter)));
}

ScanChoicesPtr ScanChoices::CreateEmpty() {
  return std::make_unique<EmptyScanChoices>(BloomFilterOptions::Inactive());
}

}  // namespace docdb
}  // namespace yb
