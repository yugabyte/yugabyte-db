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

#include "yb/common/doc_hybrid_time.h"
#include "yb/common/read_hybrid_time.h"
#include "yb/common/transaction.h"

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/expiration.h"
#include "yb/docdb/value.h"

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"
#include "yb/util/strongly_typed_bool.h"

namespace yb {
namespace docdb {


// This class is responsible for housing state used to determine whether a row is still valid based
// on it's write time. It can be constructed in one of two ways:
// (1) Using just a write_time_watermark -- this is useful if some parent row was written at a given
//     time and all children written before that time should be considered overwritten.
// (2) Using additionally a read_time and expiration -- this is useful if the data we're reading has
//     TTL enabled, e.g. CQL data.
class ObsolescenceTracker {
 public:
  ObsolescenceTracker() = default;
  explicit ObsolescenceTracker(DocHybridTime write_time_watermark);
  ObsolescenceTracker(
      const ReadHybridTime& read_time, DocHybridTime write_time_watermark, Expiration expiration);

  bool IsObsolete(const DocHybridTime& write_time) const;

  // Create an instance of ObsolescenceTracker derived from this instance. This "child" instance
  // will incorporate the parents data with the new data, but notably the child's TTL will be
  // ignored if the parent did not have a TTL. The assumption here is that an ObsolescenceTracker
  // constructed without an expiration is constructed in the context of a database in which TTL's
  // are not a valid concept (e.g. in SQL), so the same will be true of any subsequent children.
  ObsolescenceTracker Child(const DocHybridTime& write_time, const MonoDelta& ttl) const;

  // A version of the above method which only updates the tracked high write_time_watermark_.
  ObsolescenceTracker Child(const DocHybridTime& write_time) const;

  const DocHybridTime& GetHighWriteTime();

  // Performs the computation required to set the TTL on a row before constructing a SubDocument
  // from it. Caller should provide the write time of the row whose TTL is tracked by this instance.
  // Returns boost::none if this instance is not tracking TTL.
  boost::optional<uint64_t> GetTtlRemainingSeconds(const HybridTime& ttl_write_time) const;

 protected:
  DocHybridTime write_time_watermark_ = DocHybridTime::kMin;
  boost::optional<ReadHybridTime> read_time_;
  boost::optional<Expiration> expiration_;
};


// This class orchestrates the creation of a SubDocument stored in RocksDB with key
// target_subdocument_key, respecting the expiration and high write time passed to it on
// construction.
class SubDocumentReader {
 public:
  SubDocumentReader(
      const KeyBytes& target_subdocument_key,
      IntentAwareIterator* iter,
      DeadlineInfo* deadline_info,
      const ObsolescenceTracker& ancestor_obsolescence_tracker);

  // Populate the provided SubDocument* with the data for the provided target_subdocument_key. This
  // method assumes the provided IntentAwareIterator is pointing to the beginning of the range which
  // represents target_subdocument_key. If no such data is found at the current position, no data
  // will be populated on the provided SubDocument*.
  CHECKED_STATUS Get(SubDocument* result);

 private:
  const KeyBytes& target_subdocument_key_;
  IntentAwareIterator* const iter_;
  DeadlineInfo* const deadline_info_;
  // Tracks the combined obsolescence info of not only this SubDocument's direct parent but all
  // ancestors of the SubDocument.
  ObsolescenceTracker ancestor_obsolescence_tracker_;
};

// This class is responsible for initializing TTL and overwrite metadata based on parent rows, and
// then initializing and returning SubDocumentReader's which will produce SubDocument instances.
class SubDocumentReaderBuilder {
 public:
  SubDocumentReaderBuilder(IntentAwareIterator* iter, DeadlineInfo* deadline_info);

  // Updates expiration/overwrite data by scanning all parents of this Builder's
  // target_subdocument_key.
  CHECKED_STATUS InitObsolescenceInfo(
      const ObsolescenceTracker& table_obsolescence_tracker,
      const Slice& root_doc_key, const Slice& target_subdocument_key);

  // Does NOT seek. This method assumes the caller has seeked iter to the key corresponding to
  // projection. It will return a SubDocumentReader which will read the key currently pointed to,
  // assuming the iterator is seeked to the key corresponding to sub_doc_key. Use of this method
  // without explicit seeking to sub_doc_key by the caller is not supported.
  Result<std::unique_ptr<SubDocumentReader>> Build(const KeyBytes& sub_doc_key);

 private:
  CHECKED_STATUS UpdateWithParentWriteInfo(const Slice& parent_key_without_ht);

  IntentAwareIterator* iter_;
  DeadlineInfo* deadline_info_;
  ObsolescenceTracker parent_obsolescence_tracker_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_SUBDOC_READER_H_
