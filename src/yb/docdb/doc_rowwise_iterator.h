// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_ROWWISE_ITERATOR_H_
#define YB_DOCDB_ROWWISE_ITERATOR_H_

#include <string>

#include "rocksdb/db.h"

#include "yb/common/encoded_key.h"
#include "yb/common/iterator.h"
#include "yb/common/rowblock.h"
#include "yb/common/scan_spec.h"
#include "yb/common/timestamp.h"
#include "yb/docdb/doc_key.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

// An adapter between SQL-mapped-to-document-DB and Kudu's RowwiseIterator.
class DocRowwiseIterator : public RowwiseIterator {

 public:
  DocRowwiseIterator(const Schema &projection,
                     const Schema &schema,
                     rocksdb::DB *db,
                     Timestamp timestamp = Timestamp::kMax);
  virtual ~DocRowwiseIterator();

  virtual Status Init(ScanSpec *spec) OVERRIDE;

  // This must always be called before NextBlock. The implementation actually finds the first row
  // to scan, and NextBlock expects the RocksDB iterator to already be properly positioned.
  virtual bool HasNext() const OVERRIDE;

  virtual std::string ToString() const OVERRIDE;

  virtual const Schema& schema() const OVERRIDE {
    // Note: this is the schema only for the columns in the projection, not all columns.
    return projection_;
  }

  // This may return one row at a time in the initial implementation, even though Kudu's scanning
  // interface supports returning multiple rows at a time.
  virtual Status NextBlock(RowBlock *dst) OVERRIDE;

  virtual void GetIteratorStats(std::vector<IteratorStats>* stats) const OVERRIDE;

 private:
  DocKey KuduToDocKey(const EncodedKey &encoded_key) {
    return DocKey::FromKuduEncodedKey(encoded_key, schema_);
  }

  const Schema& projection_;

  // The schema for all columns, not just the columns we're scanning.
  const Schema& schema_;

  Timestamp timestamp_;
  rocksdb::DB* const db_;

  // A copy of the exclusive upper bound key of the scan range (if any).
  bool has_upper_bound_key_;
  KeyBytes exclusive_upper_bound_key_;

  std::unique_ptr<rocksdb::Iterator> db_iter_;

  // The mutable fields that follow are modified by HasNext, a const method.

  // Indicates whether we've already finished iterating.
  mutable bool done_;

  // HasNext sets this to the the subdocument key corresponding to the top of the document
  // (document key and a generation timestamp).
  mutable SubDocKey subdoc_key_;

  // Used for keeping track of errors that happen in HasNext. Returned
  mutable Status status_;
};

}
}

#endif
