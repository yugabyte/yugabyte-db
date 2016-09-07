// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_DOC_PATH_H_
#define YB_DOCDB_DOC_PATH_H_

#include "yb/docdb/primitive_value.h"
#include "yb/gutil/strings/substitute.h"
#include "rocksdb/util/string_util.h"

#include <ostream>

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

  DocPath(const KeyBytes& encoded_doc_key, const vector<PrimitiveValue>& subkeys)
      : encoded_doc_key_(encoded_doc_key),
        subkeys_(subkeys) {
  }

  const KeyBytes& encoded_doc_key() const { return encoded_doc_key_; }
  int num_subkeys() const { return subkeys_.size(); }
  const PrimitiveValue& subkey(int i) const {
    assert(0 <= i && i < num_subkeys());
    return subkeys_[i];
  }

  std::string ToString() const {
    return strings::Substitute("DocPath($0, $1)",
        BestEffortKeyBytesToStr(encoded_doc_key_), rocksdb::VectorToString(subkeys_));
  }

  void AddSubKey(const PrimitiveValue& subkey) {
    subkeys_.emplace_back(subkey);
  }

  void AddSubKey(PrimitiveValue&& subkey) {
    subkeys_.emplace_back(std::move(subkey));
  }

  const PrimitiveValue& last_subkey() const {
    assert(!subkeys_.empty());
    return subkeys_.back();
  }

 private:

  // Encoded key identifying the document. This key can itself contain multiple components
  // (hash bucket, hashed components, range components).
  // TODO: should this really be encoded?
  KeyBytes encoded_doc_key_;

  std::vector<PrimitiveValue> subkeys_;
};

inline std::ostream& operator << (std::ostream& out, const DocPath& doc_path) {
  return out << doc_path.ToString();
}

}
}

#endif
