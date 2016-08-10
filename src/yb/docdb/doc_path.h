// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_PATH_H_
#define YB_DOCDB_DOC_PATH_H_

#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// Identifies a particular subdocument inside the logical representation of the document database.
// By "logical representation" we mean that we are not concerned with the exact keys used in the
// underlying key-value store, and we do not keep track of any "generation timestamps" of various
// parent subdocuments of the subdocument we are pointing to.

class DocPath {
 public:
  template<class... T>
  DocPath(const KeyBytes& encoded_doc_key, T... subkeys) {
    encoded_doc_key_ = encoded_doc_key;
    AppendPrimitiveValues(&subkeys_, subkeys...);
  }

  const KeyBytes& encoded_doc_key() const { return encoded_doc_key_; }
  int num_subkeys() const { return subkeys_.size(); }
  const PrimitiveValue& subkey(int i) const {
    assert(0 <= i && i < num_subkeys());
    return subkeys_[i];
  }

 private:

  // Encoded key identifying the document. This key can itself contain multiple components
  // (hash bucket, hashed components, range components).
	KeyBytes encoded_doc_key_;

  std::vector<PrimitiveValue> subkeys_;
};

}
}

#endif
