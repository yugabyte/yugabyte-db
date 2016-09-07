// Copyright (c) YugaByte, Inc.

#ifndef SRC_YB_DOCDB_IN_MEM_DOCDB_H_
#define SRC_YB_DOCDB_IN_MEM_DOCDB_H_

#include <map>

#include "yb/docdb/doc_key.h"
#include "yb/docdb/subdocument.h"
#include "yb/docdb/doc_path.h"
#include "yb/util/status.h"

namespace yb {
namespace docdb {

// An in-memory single-versioned single-threaded Document DB representation for testing.
class InMemDocDB {
 public:
  Status SetPrimitive(const DocPath& doc_path, const PrimitiveValue& value);
  Status DeleteSubDoc(const DocPath& doc_path);

  const SubDocument* GetDocument(const KeyBytes& encoded_doc_key) const;
 private:
  // We reuse SubDocument for the top-level map, as this is only being used in testing. We wrap
  // encoded DocKeys into PrimitiveValues of type string.
  SubDocument root_;
};

}
}

#endif
