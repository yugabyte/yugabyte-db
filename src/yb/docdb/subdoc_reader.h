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

#ifndef YB_DOCDB_SUBDOC_READER_H_
#define YB_DOCDB_SUBDOC_READER_H_

#include <string>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/intent_aware_iterator.h"
#include "yb/gutil/macros.h"
#include "yb/rocksdb/cache.h"

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_types.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/intent.h"
#include "yb/docdb/primitive_value.h"
#include "yb/docdb/value.h"
#include "yb/docdb/subdocument.h"

#include "yb/util/status.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {

// This class orchestrates the creation of a SubDocument stored in RocksDB with key
// target_subdocument_key, respecting the expiration and high write time passed to it on
// construction.
class SubDocumentReader {
 public:
  SubDocumentReader(
      const KeyBytes& target_subdocument_key,
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      DocHybridTime highest_ancestor_write_time,
      Expiration inherited_expiration);

  // Populate the provided SubDocument* with the data for the provided target_subdocument_key. This
  // method assumes the provided IntentAwareIterator is pointing to the beginning of the range which
  // represents target_subdocument_key. If no such data is found at the current position, no data
  // will be populated on the provided SubDocument*.
  CHECKED_STATUS Get(SubDocument* result);

 private:
  const KeyBytes& target_subdocument_key_;
  IntentAwareIterator* const iter_;
  DeadlineInfo* const deadline_info_;
  const DocHybridTime highest_ancestor_write_time_;
  const Expiration inherited_expiration_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_SUBDOC_READER_H_
