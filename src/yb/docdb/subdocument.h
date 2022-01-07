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

#ifndef YB_DOCDB_SUBDOCUMENT_H_
#define YB_DOCDB_SUBDOCUMENT_H_

#include <assert.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <limits>
#include <map>
#include <ostream>
#include <string>
#include <vector>

#include "yb/bfql/tserver_opcodes.h"

#include "yb/docdb/primitive_value.h"

#include "yb/gutil/int128.h"
#include "yb/gutil/integral_types.h"

namespace yb {
namespace docdb {

// A subdocument could either be a primitive value, or an arbitrarily nested JSON-like data
// structure. This class is copyable, but care should be taken to avoid expensive implicit copies.
class SubDocument : public PrimitiveValue {
 public:

  explicit SubDocument(ValueType value_type);
  SubDocument();

  ~SubDocument();

  explicit SubDocument(ListExtendOrder extend_order);

  // Copy constructor. This is potentially very expensive!
  SubDocument(const SubDocument& other);

  explicit SubDocument(const std::vector<PrimitiveValue> &elements,
                       ListExtendOrder extend_order = ListExtendOrder::APPEND);

  SubDocument& operator =(const SubDocument& other) {
    this->~SubDocument();
    new(this) SubDocument(other);
    return *this;
  }

  // A good way to construct single-level subdocuments. Not very performant, primarily useful
  // for tests.
  template<typename T>
  SubDocument(std::initializer_list<std::initializer_list<T>> elements) {
    complex_data_structure_ = nullptr;
    EnsureObjectAllocated();
    for (const auto& key_value : elements) {
      CHECK_EQ(2, key_value.size());
      auto iter = key_value.begin();
      const auto& key = *iter;
      ++iter;
      const auto& value = *iter;
      CHECK_EQ(0, object_container().count(PrimitiveValue(key)))
          << "Duplicate key: " << PrimitiveValue(key).ToString();
      object_container().emplace(PrimitiveValue(key), SubDocument(PrimitiveValue(value)));
    }
  }

  // Move assignment and constructor.
  SubDocument& operator =(SubDocument&& other) {
    this->~SubDocument();
    MoveFrom(&other);
    return *this;
  }

  SubDocument(SubDocument&& other) {
    MoveFrom(&other);
  }

  explicit SubDocument(const PrimitiveValue& other) : PrimitiveValue(other) {}
  explicit SubDocument(PrimitiveValue&& other) : PrimitiveValue(std::move(other)) {}

  bool operator==(const SubDocument& other) const;
  bool operator!=(const SubDocument& other) const { return !(*this == other); }

  // "using" did not let us use the alias when instantiating these classes, so we're using typedef.
  typedef std::map<PrimitiveValue, SubDocument> ObjectContainer;
  typedef std::vector<SubDocument> ArrayContainer;

  ObjectContainer& object_container() const {
    assert(has_valid_object_container());
    return *reinterpret_cast<ObjectContainer*>(complex_data_structure_);
  }

  ArrayContainer& array_container() const {
    assert(has_valid_array_container());
    return *reinterpret_cast<ArrayContainer*>(complex_data_structure_);
  }

  // Interpret the SubDocument as a RedisSet.
  // Assume current subdocument is of map type (kObject type)
  CHECKED_STATUS ConvertToRedisSet();

  // Interpret the SubDocument as a RedisTS.
  // Assume current subdocument is of map type (kObject type)
  CHECKED_STATUS ConvertToRedisTS();

  // Interpret the SubDocument as a RedisSortedSet.
  // Assume current subdocument is of map type (kObject type)
  CHECKED_STATUS ConvertToRedisSortedSet();

  // Interpret the SubDocument as a RedisSortedSet.
  // Assume current subdocument is of map type (kObject type)
  CHECKED_STATUS ConvertToRedisList();

  // @return The child subdocument of an object at the given key, or nullptr if this subkey does not
  //         exist or this subdocument is not an object.
  SubDocument* GetChild(const PrimitiveValue& key);

  // Returns the number of children for this subdocument.
  CHECKED_STATUS NumChildren(size_t *num_children);

  const SubDocument* GetChild(const PrimitiveValue& key) const;

  // Returns the child of this object at the given subkey, or default-constructs one if it does not
  // exist. Fatals if this is not an object. Never returns nullptr.
  // @return A pair of the child at the requested subkey, and a boolean flag indicating whether a
  //         new child subdocument has been added.
  std::pair<SubDocument*, bool> GetOrAddChild(const PrimitiveValue& key);

  // Add a list element child of the given value.
  void AddListElement(SubDocument&& value);

  // Set the child subdocument of an object to the given value.
  void SetChild(const PrimitiveValue& key, SubDocument&& value);

  void SetChildPrimitive(const PrimitiveValue& key, PrimitiveValue&& value) {
    SetChild(key, SubDocument(value));
  }

  void SetChildPrimitive(const PrimitiveValue& key, const PrimitiveValue& value) {
    SetChild(key, SubDocument(value));
  }

  // Creates a JSON-like string representation of this subdocument.
  std::string ToString() const;

  // Attempts to delete a child subdocument of an object with the given key. Fatals if this is not
  // an object.
  // @return true if a child object was deleted, false if it did not exist.
  bool DeleteChild(const PrimitiveValue& key);

  int object_num_keys() const;

  // Construct a SubDocument from a QLValuePB.
  static SubDocument FromQLValuePB(const QLValuePB& value,
                                   SortingType sorting_type,
                                   yb::bfql::TSOpcode write_instr = bfql::TSOpcode::kScalarInsert);

  // Construct a QLValuePB from a SubDocument.
  static void ToQLValuePB(const SubDocument& doc,
                          const std::shared_ptr<QLType>& ql_type,
                          QLValuePB* v);

 private:

  CHECKED_STATUS ConvertToCollection(ValueType value_type);

  // Common code used by move constructor and move assignment.
  void MoveFrom(SubDocument* other);

  void EnsureContainerAllocated();
  void EnsureObjectAllocated();

  bool container_allocated() const;

  bool has_valid_container() const {
    return complex_data_structure_ != nullptr;
  }

  bool has_valid_object_container() const;

  bool has_valid_array_container() const;

  friend void SubDocumentToStreamInternal(ostream& out, const SubDocument& subdoc, int indent);
  friend void SubDocCollectionToStreamInternal(ostream& out,
                                        const SubDocument& subdoc,
                                        const int indent,
                                        const string& begin,
                                        const string& end);

  // We use a SubDocument as the top-level map from encoded document keys to documents (also
  // represented as SubDocuments) in InMemDocDbState, and we need access to object_container()
  // from there.
  friend class InMemDocDbState;
};

std::ostream& operator <<(ostream& out, const SubDocument& subdoc);

static_assert(sizeof(SubDocument) == sizeof(PrimitiveValue),
              "It is important that we can cast a PrimitiveValue to a SubDocument.");

}  // namespace docdb
}  // namespace yb

#endif  // YB_DOCDB_SUBDOCUMENT_H_
