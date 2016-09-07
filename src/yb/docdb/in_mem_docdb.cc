// Copyright (c) YugaByte, Inc.

#include "yb/docdb/in_mem_docdb.h"
#include "yb/gutil/strings/substitute.h"

using strings::Substitute;

namespace yb {
namespace docdb {

Status InMemDocDB::SetPrimitive(const DocPath& doc_path, const PrimitiveValue& value) {
  const PrimitiveValue encoded_doc_key_as_primitive(doc_path.encoded_doc_key().AsStringRef());
  const bool is_deletion = value.value_type() == ValueType::kTombstone;
  if (doc_path.num_subkeys() == 0) {
    if (is_deletion) {
      root_.DeleteChild(encoded_doc_key_as_primitive);
    } else {
      root_.SetChildPrimitive(encoded_doc_key_as_primitive, value);
    }

    return Status::OK();
  }
  SubDocument* current_subdoc = nullptr;

  if (is_deletion) {
    current_subdoc = root_.GetChild(encoded_doc_key_as_primitive);
    if (current_subdoc == nullptr) {
      // The subdocument we're trying to delete does not exist, nothing to do.
      return Status::OK();
    }
  } else {
    current_subdoc = root_.GetOrAddChild(encoded_doc_key_as_primitive).first;
  }

  const int num_subkeys = doc_path.num_subkeys();
  for (int subkey_index = 0; subkey_index < num_subkeys - 1; ++subkey_index) {
    const PrimitiveValue& subkey = doc_path.subkey(subkey_index);
    if (subkey.value_type() == ValueType::kArrayIndex) {
      return Status::NotSupported("Setting values at a given array index is not supported yet.");
    }

    if (current_subdoc->value_type() != ValueType::kObject) {
      return Status::IllegalState(Substitute(
          "Cannot set or delete values inside a subdocument of type $0",
          ValueTypeToStr(current_subdoc->value_type())));
    }

    if (is_deletion) {
      current_subdoc = current_subdoc->GetChild(subkey);
      if (current_subdoc == nullptr) {
        // Document does not exist, nothing to do.
        return Status::OK();
      }
    } else {
      current_subdoc = current_subdoc->GetOrAddChild(subkey).first;
    }
  }

  if (is_deletion) {
    current_subdoc->DeleteChild(doc_path.last_subkey());
  } else {
    current_subdoc->SetChildPrimitive(doc_path.last_subkey(), value);
  }

  return Status::OK();
}

Status InMemDocDB::DeleteSubDoc(const DocPath &doc_path) {
  return SetPrimitive(doc_path, PrimitiveValue(ValueType::kTombstone));
}

const SubDocument* InMemDocDB::GetDocument(const KeyBytes& encoded_doc_key) const {
  return root_.GetChild(PrimitiveValue(encoded_doc_key.AsStringRef()));
}

}
}
