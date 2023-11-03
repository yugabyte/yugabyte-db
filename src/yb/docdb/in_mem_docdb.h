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

#include <map>

#include "yb/rocksdb/db.h"

#include "yb/dockv/subdocument.h"
#include "yb/dockv/doc_path.h"
#include "yb/docdb/key_bounds.h"
#include "yb/dockv/value.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace docdb {

// An in-memory single-versioned single-threaded DocDB representation for testing.
class InMemDocDbState {
 public:
  Status SetPrimitive(const dockv::DocPath& doc_path, const dockv::PrimitiveValue& value);
  Status DeleteSubDoc(const dockv::DocPath& doc_path);

  void SetDocument(const dockv::KeyBytes& encoded_doc_key, dockv::SubDocument&& doc);
  const dockv::SubDocument* GetSubDocument(const dockv::SubDocKey &subdoc_key) const;
  const dockv::SubDocument* GetDocument(const dockv::DocKey& doc_key) const;

  // Capture all documents present in the given RocksDB database at the given hybrid_time and save
  // them into this in-memory DocDB. All current contents of this object are overwritten.
  void CaptureAt(const DocDB& doc_db, HybridTime hybrid_time,
                 rocksdb::QueryId = rocksdb::kDefaultQueryId);

  void SetCaptureHybridTime(HybridTime hybrid_time);

  int num_docs() const { return root_.object_num_keys(); }

  // Compares this state of DocDB to the other one (which it is expected to match), and reports
  // any differences to the log. The reporting of differences to the log can be turned off,
  // but is turned on by default.
  //
  // @param log_diff Whether to log the differences between the two DocDB states.
  // @return true if the two databases have the same state.
  bool EqualsAndLogDiff(const InMemDocDbState &expected, bool log_diff = true);

  std::string ToDebugString() const;

  HybridTime captured_at() const;

  // Check for internal corruption. Used to catch bugs in tests.
  void SanityCheck() const;

 private:
  // We reuse SubDocument for the top-level map, as this is only being used in testing. We wrap
  // encoded DocKeys into PrimitiveValues of type string.
  dockv::SubDocument root_;

  // We use this field when capturing a DocDB state into an InMemDocDbState. This is the latest
  // write hybrid_time at which the capture happened.
  HybridTime captured_at_;
};

}  // namespace docdb
}  // namespace yb
