// Copyright (c) YugaByte, Inc.

#ifndef YB_DOCDB_SUBDOCUMENT_H_
#define YB_DOCDB_SUBDOCUMENT_H_

#include <map>
#include <vector>
#include <ostream>
#include <initializer_list>

#include "yb/docdb/primitive_value.h"

namespace yb {
namespace docdb {

// A subdocument could either be a primitive value, or an arbitrarily nested JSON-like data
// structure. This class is copyable, but care should be taken to avoid expensive implicit copies.
class SubDocument : public PrimitiveValue {
 public:

  explicit SubDocument(ValueType value_type);
  SubDocument() : SubDocument(ValueType::kObject) {
    complex_data_structure_ = nullptr;
  }

  ~SubDocument();

  // Copy constructor. This is potentially very expensive!
  SubDocument(const SubDocument& other);

  // Don't need this for now.
  SubDocument& operator =(const SubDocument& other) = delete;

  // A good way to construct single-level subdocuments. Not very performant, primarily useful
  // for tests.
  template<typename T>
  SubDocument(std::initializer_list<std::initializer_list<T>> elements) {
    type_ = ValueType::kObject;
    complex_data_structure_ = nullptr;
    EnsureContainerAllocated();
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

  // @return The child subdocument of an object at the given key, or nullptr if this subkey does not
  //         exist or this subdocument is not an object.
  SubDocument* GetChild(const PrimitiveValue& key);

  const SubDocument* GetChild(const PrimitiveValue& key) const;

  // Returns the child of this object at the given subkey, or default-constructs one if it does not
  // exist. Fatals if this is not an object. Never returns nullptr.
  // @return A pair of the child at the requested subkey, and a boolean flag indicating whether a
  //         new child subdocument has been added.
  std::pair<SubDocument*, bool> GetOrAddChild(const PrimitiveValue& key);

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

  int object_num_keys() const {
    DCHECK_EQ(ValueType::kObject, type_);
    if (!has_valid_object_container()) {
      return 0;
    }
    assert(object_container().size() <= std::numeric_limits<int>::max());
    return static_cast<int>(object_container().size());
  }

  // Construct a SubDocument from a YQLValuePB.
  static SubDocument FromYQLValuePB(YQLType yql_type, const YQLValuePB& value,
                                    ColumnSchema::SortingType sorting_type);

  // Construct a YQLValuePB from a SubDocument.
  static void ToYQLValuePB(SubDocument doc, YQLType yql_type, YQLValuePB* v);

  // Construct a SubDocument from a YQLExpressionPB.
  static SubDocument FromYQLExpressionPB(YQLType yql_type,
                                         const YQLExpressionPB& yql_expr,
                                         ColumnSchema::SortingType sorting_type);

  // Construct a YQLExpressionPB from a SubDocument.
  static void ToYQLExpressionPB(SubDocument doc, YQLType yql_type, YQLExpressionPB* yql_expr);

 private:

  // Common code used by move constructor and move assignment.
  void MoveFrom(SubDocument* other) {
    if (this == other) {
      return;
    }

    if (IsPrimitiveValueType(other->type_)) {
      new(this) PrimitiveValue(std::move(*other));
    } else {
      // For objects/arrays the internal state is just a type and a pointer.
      type_ = other->type_;
      complex_data_structure_ = other->complex_data_structure_;
      // The internal state of the other subdocument is now owned by this one. Replace it with dummy
      // data without calling the destructor.
#ifndef NDEBUG
      // Another layer of protection against trying to use the old state in debug mode.
      memset(other, 0xab, sizeof(SubDocument));  // Fill with a random value.
#endif
      other->type_ = ValueType::kNull;  // To avoid deallocation of the old object's memory.
    }
  }

  void EnsureContainerAllocated();

  bool container_allocated() const {
    assert(type_ == ValueType::kObject || type_ == ValueType::kArray);
    return complex_data_structure_ != nullptr;
  }

  bool has_valid_container() const {
    return complex_data_structure_ != nullptr;
  }

  bool has_valid_object_container() const {
    return type_ == ValueType::kObject && has_valid_container();
  }

  bool has_valid_array_container() const {
    return type_ == ValueType::kArray && has_valid_container();
  }

  friend void SubDocumentToStreamInternal(ostream& out, const SubDocument& subdoc, int indent);

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
