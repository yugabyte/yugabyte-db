// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_IN_MEM_DOCDB_H_
#define YB_DOCDB_IN_MEM_DOCDB_H_

#include <map>

#include "rocksdb/db.h"

#include "yb/docdb/doc_key.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/doc_path.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

// An in-memory single-versioned single-threaded DocDB representation for testing.
class InMemDocDbState {
 public:
  Status SetPrimitive(const DocPath& doc_path, const PrimitiveValue& value);
  Status DeleteSubDoc(const DocPath& doc_path);

  void SetDocument(const KeyBytes& encoded_doc_key, SubDocument&& doc);
  const SubDocument* GetDocument(const KeyBytes& encoded_doc_key) const;

  // Capture all documents present in the given RocksDB database at the given timestamp and save
  // them into this in-memory DocDB. All current contents of this object are overwritten.
  void CaptureAt(rocksdb::DB* rocksdb, Timestamp timestamp);

  void SetCaptureTimestamp(Timestamp timestamp);

  int num_docs() const { return root_.object_num_keys(); }

  // Compares this state of DocDB to the other one (which it is expected to match), and reports
  // any differences to the log. The reporting of differences to the log can be turned off,
  // but is turned on by default.
  //
  // @param log_diff Whether to log the differences between the two DocDB states.
  // @return true if the two databases have the same state.
  bool EqualsAndLogDiff(const InMemDocDbState &expected, bool log_diff = true);

  std::string ToDebugString() const;

  Timestamp captured_at() const;

  // Check for internal corruption. Used to catch bugs in tests.
  void SanityCheck() const;

 private:
  // We reuse SubDocument for the top-level map, as this is only being used in testing. We wrap
  // encoded DocKeys into PrimitiveValues of type string.
  SubDocument root_;

  // We use this field when capturing a DocDB state into an InMemDocDbState. This is the latest
  // write timestamp at which the capture happened.
  Timestamp captured_at_;
};

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_IN_MEM_DOCDB_H_
