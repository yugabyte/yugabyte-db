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

#include "yb/dockv/subdocument.h"

#include <map>
#include <vector>

#include "yb/common/constants.h"
#include "yb/common/ql_type.h"
#include "yb/common/ql_value.h"

#include "yb/dockv/value_type.h"

#include "yb/gutil/casts.h"

#include "yb/util/status.h"

using std::make_pair;
using std::ostringstream;
using std::shared_ptr;
using std::string;
using std::vector;
using std::ostream;

using yb::bfql::TSOpcode;

namespace yb::dockv {

SubDocument::SubDocument(ValueEntryType value_type) : PrimitiveValue(value_type) {
  if (IsCollectionType(value_type)) {
    EnsureContainerAllocated();
  }
}

SubDocument::SubDocument() : SubDocument(ValueEntryType::kObject) {}

SubDocument::SubDocument(ListExtendOrder extend_order) : SubDocument(ValueEntryType::kArray) {
  extend_order_ = extend_order;
}

SubDocument::SubDocument(
    const std::vector<PrimitiveValue> &elements, ListExtendOrder extend_order) {
  type_ = ValueEntryType::kArray;
  extend_order_ = extend_order;
  complex_data_structure_ = new ArrayContainer();
  array_container().reserve(elements.size());
  for (auto& elt : elements) {
    array_container().emplace_back(elt);
  }
}


SubDocument::~SubDocument() {
  switch (type_) {
    case ValueEntryType::kObject: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisList: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisSortedSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kRedisTS:
      if (has_valid_container()) {
        delete &object_container();
      }
      break;
    case ValueEntryType::kArray:
      if (has_valid_container()) {
        delete &array_container();
      }
      break;
    default:
      break;
  }
}

SubDocument::SubDocument(const SubDocument& other) {
  if (IsPrimitiveValueType(other.type_) ||
      other.type_ == ValueEntryType::kInvalid ||
      other.type_ == ValueEntryType::kTombstone) {
    new(this) PrimitiveValue(other);
  } else {
    type_ = other.type_;
    ttl_seconds_ = other.ttl_seconds_;
    write_time_ = other.write_time_;
    complex_data_structure_ = nullptr;
    switch (type_) {
      case ValueEntryType::kObject:
        if (other.has_valid_object_container()) {
          complex_data_structure_ = new ObjectContainer(other.object_container());
        }
        break;
      case ValueEntryType::kArray:
        if (other.has_valid_array_container()) {
          complex_data_structure_ = new ArrayContainer(other.array_container());
        }
        break;
      default:
        LOG(FATAL) << "Trying to copy an invalid/unsupported SubDocument type: "
                   << AsString(type_);
    }
  }
}

bool SubDocument::operator ==(const SubDocument& other) const {
  if (type_ != other.type_) {
    return false;
  }
  if (IsPrimitiveValueType(type_)) {
    return this->PrimitiveValue::operator==(other);
  }
  switch (type_) {
    case ValueEntryType::kObject:
      if (has_valid_container() != other.has_valid_container()) {
        return has_valid_container() ? object_container().empty()
                                     : other.object_container().empty();
      }
      if (has_valid_container()) {
        return object_container() == other.object_container();
      } else {
        return true;  // Both container pointers are nullptr.
      }
    case ValueEntryType::kArray:
      if (has_valid_container() != other.has_valid_container()) {
        return has_valid_container() ? array_container().empty()
                                     : other.array_container().empty();
      }
      if (has_valid_container()) {
        return array_container() == other.array_container();
      } else {
        return true;
      }
    default:
      LOG(FATAL) << "Trying to compare SubDocuments of invalid type: " << AsString(type_);
  }
  // We'll get here if both container pointers are null.
  return true;
}

Status SubDocument::ConvertToCollection(ValueEntryType value_type) {
  if (!has_valid_object_container()) {
    return STATUS(InvalidArgument, "Subdocument doesn't have valid object container");
  }
  type_ = value_type;
  return Status::OK();
}

void SubDocument::MoveFrom(SubDocument* other) {
  if (this == other) {
    return;
  }

  if (IsPrimitiveValueType(other->type_)) {
    new(this) PrimitiveValue(std::move(*other));
  } else {
    // For objects/arrays the internal state is just a type and a pointer.
    extend_order_ = other->extend_order_;
    type_ = other->type_;
    ttl_seconds_ = other->ttl_seconds_;
    write_time_ = other->write_time_;
    complex_data_structure_ = other->complex_data_structure_;
    // The internal state of the other subdocument is now owned by this one.
#ifndef NDEBUG
    // Another layer of protection against trying to use the old state in debug mode.
    memset(static_cast<void*>(other), 0xab, sizeof(SubDocument));  // Fill with a random value.
#endif
    other->type_ = ValueEntryType::kNullLow;  // To avoid deallocation of the old object's memory.
  }
}

Status SubDocument::ConvertToRedisTS() {
  return ConvertToCollection(ValueEntryType::kRedisTS);
}

Status SubDocument::ConvertToRedisSet() {
  return ConvertToCollection(ValueEntryType::kRedisSet);
}

Status SubDocument::ConvertToRedisSortedSet() {
  return ConvertToCollection(ValueEntryType::kRedisSortedSet);
}

Status SubDocument::ConvertToRedisList() {
  return ConvertToCollection(ValueEntryType::kRedisList);
}

Status SubDocument::NumChildren(size_t *num_children) {
  if (!has_valid_object_container()) {
    return STATUS(IllegalState, "Not a valid object container");
  }
  *num_children = object_container().size();
  return Status::OK();
}

SubDocument* SubDocument::GetChild(const KeyEntryValue& key) {
  if (!has_valid_object_container()) {
    return nullptr;
  }
  auto& obj_container = object_container();
  auto iter = obj_container.find(key);
  if (iter == obj_container.end()) {
    return nullptr;
  } else {
    return &iter->second;
  }
}

const SubDocument* SubDocument::GetChild(const KeyEntryValue& key) const {
  if (!has_valid_object_container()) {
    return nullptr;
  }
  const auto& obj_container = object_container();
  auto iter = obj_container.find(key);
  if (iter == obj_container.end()) {
    return nullptr;
  } else {
    return &iter->second;
  }
}

std::pair<SubDocument*, bool> SubDocument::GetOrAddChild(const KeyEntryValue& key) {
  DCHECK(IsObjectType(type_));
  EnsureContainerAllocated();
  auto& obj_container = object_container();
  auto iter = obj_container.find(key);
  if (iter == obj_container.end()) {
    auto ret = obj_container.insert(make_pair(key, SubDocument()));
    CHECK(ret.second);
    return make_pair(&ret.first->second, true);  // New subdocument created.
  } else {
    return make_pair(&iter->second, false);  // No new subdocument created.
  }
}

void SubDocument::AddListElement(SubDocument&& value) {
  DCHECK_EQ(ValueEntryType::kArray, type_);
  EnsureContainerAllocated();
  array_container().emplace_back(std::move(value));
}

void SubDocument::SetChild(const KeyEntryValue& key, SubDocument&& value) {
  EnsureObjectAllocated();
  auto& obj_container = object_container();
  auto existing_element = obj_container.find(key);
  if (existing_element == obj_container.end()) {
    const bool inserted_value = obj_container.emplace(key, std::move(value)).second;
    CHECK(inserted_value);
  } else {
    existing_element->second = std::move(value);
  }
}

SubDocument& SubDocument::AllocateChild(const KeyEntryValue& key) {
  EnsureObjectAllocated();
  return object_container().emplace(
      std::piecewise_construct,
      std::tuple(std::cref(key)),
      std::tuple(ValueEntryType::kInvalid)).first->second;
}

bool SubDocument::DeleteChild(const KeyEntryValue& key) {
  CHECK_EQ(ValueEntryType::kObject, type_);
  if (!has_valid_object_container())
    return false;
  return object_container().erase(key) > 0;
}

string SubDocument::ToString(bool render_options) const {
  ostringstream ss;
  SubDocumentToStreamInternal(ss, *this, 0, render_options);
  return ss.str();
}

ostream& operator <<(ostream& out, const SubDocument& subdoc) {
  SubDocumentToStreamInternal(out, subdoc, 0, false);
  return out;
}

void SubDocumentToStreamInternal(ostream& out,
                                 const SubDocument& subdoc,
                                 const int indent,
                                 bool render_options) {
  if (subdoc.IsPrimitive() ||
      subdoc.value_type() == ValueEntryType::kInvalid ||
      subdoc.value_type() == ValueEntryType::kTombstone) {
    out << static_cast<const PrimitiveValue*>(&subdoc)->ToString(render_options);
    return;
  }
  switch (subdoc.value_type()) {
    case ValueEntryType::kRedisSortedSet: FALLTHROUGH_INTENDED;
    case ValueEntryType::kObject: {
      out << "{";
      if (subdoc.container_allocated()) {
        bool first_pair = true;
        for (const auto& key_value : subdoc.object_container()) {
          if (!first_pair) {
            out << ",";
          }
          first_pair = false;
          out << "\n" << string(indent + 2, ' ') << key_value.first.ToString() << ": ";
          SubDocumentToStreamInternal(out, key_value.second, indent + 2, render_options);
        }
        if (!first_pair) {
          out << "\n" << string(indent, ' ');
        }
      }
      out << "}";
      break;
    }
    case ValueEntryType::kArray: {
      out << "[";
      if (subdoc.container_allocated()) {
        out << (subdoc.GetExtendOrder() == ListExtendOrder::APPEND ? "APPEND" : "PREPEND") << "\n";
        const auto& list = subdoc.array_container();
        size_t i = 0;
        for (; i < list.size(); i++) {
          if (i != 0) {
            out << ",";
          }
          out << "\n" << string(indent + 2, ' ') << i << ": ";
          SubDocumentToStreamInternal(out, list[i], indent + 2, render_options);
        }
        if (i > 0) {
          out << "\n" << string(indent, ' ');
        }
      }
      out << "]";
      break;
    }
    case ValueEntryType::kRedisSet: {
      SubDocCollectionToStreamInternal(out, subdoc, indent, "(", ")");
      break;
    }
    case ValueEntryType::kRedisList: {
      SubDocCollectionToStreamInternal(out, subdoc, indent, "[", "]");
      break;
    }
    case ValueEntryType::kRedisTS: {
      SubDocCollectionToStreamInternal(out, subdoc, indent, "<", ">");
      break;
    }
    default:
      LOG(FATAL) << "Invalid subdocument type: " << ToString(subdoc.value_type());
  }
}

void SubDocCollectionToStreamInternal(ostream& out,
                                      const SubDocument& subdoc,
                                      const int indent,
                                      const string& begin_delim,
                                      const string& end_delim) {
  out << begin_delim;
  if (subdoc.container_allocated()) {
    const auto& keys = subdoc.object_container();
    for (auto iter = keys.begin(); iter != keys.end(); iter++) {
      if (iter != keys.begin()) {
        out << ",";
      }
      out << "\n" << string(indent + 2, ' ') << (*iter).first.ToString();
    }
    if (!keys.empty()) {
      out << "\n" << string(indent, ' ');
    }
  }
  out << end_delim;
}

void SubDocument::EnsureObjectAllocated() {
  type_ = ValueEntryType::kObject;
  EnsureContainerAllocated();
}

void SubDocument::EnsureContainerAllocated() {
  if (complex_data_structure_ == nullptr) {
    if (IsObjectType(type_)) {
      complex_data_structure_ = new ObjectContainer();
    } else if (type_ == ValueEntryType::kArray) {
      complex_data_structure_ = new ArrayContainer();
    }
  }
}

SubDocument SubDocument::FromQLValuePB(const QLValuePB& value,
                                       SortingType sorting_type,
                                       TSOpcode write_instr) {
  switch (value.value_case()) {
    case QLValuePB::kMapValue: {
      QLMapValuePB map = value.map_value();
      // this equality should be ensured by checks before getting here
      DCHECK_EQ(map.keys_size(), map.values_size());

      SubDocument map_doc;
      for (int i = 0; i < map.keys_size(); i++) {
        auto pv_key = KeyEntryValue::FromQLValuePB(map.keys(i), sorting_type);
        SubDocument pv_val = SubDocument::FromQLValuePB(map.values(i), sorting_type, write_instr);
        map_doc.SetChild(pv_key, std::move(pv_val));
      }
      // ensure container allocated even if map is empty
      map_doc.EnsureContainerAllocated();
      return map_doc;
    }
    case QLValuePB::kSetValue: {
      QLSeqValuePB set = value.set_value();
      SubDocument set_doc;
      for (auto& elem : set.elems()) {
        auto pv_key = KeyEntryValue::FromQLValuePB(elem, sorting_type);
        if (write_instr == TSOpcode::kSetRemove || write_instr == TSOpcode::kMapRemove) {
          // representing sets elems as keys pointing to tombstones to remove those entries
          set_doc.SetChildPrimitive(pv_key, PrimitiveValue::kTombstone);
        }  else {
          // representing sets elems as keys pointing to empty (null) values
          set_doc.SetChildPrimitive(pv_key, PrimitiveValue());
        }
      }
      // ensure container allocated even if set is empty
      set_doc.EnsureContainerAllocated();
      return set_doc;
    }
    case QLValuePB::kListValue: {
      QLSeqValuePB list = value.list_value();
      SubDocument list_doc(ValueEntryType::kArray);
      // ensure container allocated even if list is empty
      list_doc.EnsureContainerAllocated();
      for (int i = 0; i < list.elems_size(); i++) {
        SubDocument pv_val = SubDocument::FromQLValuePB(list.elems(i), sorting_type, write_instr);
        list_doc.AddListElement(std::move(pv_val));
      }
      return list_doc;
    }

    default:
      return SubDocument(PrimitiveValue::FromQLValuePB(value));
  }
}

void SubDocument::ToQLValuePB(const shared_ptr<QLType>& ql_type, QLValuePB* ql_value) const {
  // interpreting empty collections as null values following Cassandra semantics
  if (ql_type->HasComplexValues() && (!has_valid_object_container() || object_num_keys() == 0)) {
    SetNull(ql_value);
    return;
  }

  switch (ql_type->main()) {
    case DataType::MAP: {
      const shared_ptr<QLType>& keys_type = ql_type->params()[0];
      const shared_ptr<QLType>& values_type = ql_type->params()[1];
      QLMapValuePB *value_pb = ql_value->mutable_map_value();
      value_pb->clear_keys();
      value_pb->clear_values();
      for (auto &pair : object_container()) {
        pair.first.ToQLValuePB(keys_type, value_pb->add_keys());
        pair.second.ToQLValuePB(values_type, value_pb->add_values());
      }
      return;
    }
    case DataType::SET: {
      const shared_ptr<QLType>& elems_type = ql_type->params()[0];
      QLSeqValuePB *value_pb = ql_value->mutable_set_value();
      value_pb->clear_elems();
      for (auto &pair : object_container()) {
        pair.first.ToQLValuePB(elems_type, value_pb->add_elems());
        // set elems are represented as subdocument keys so we ignore the (empty) values
      }
      return;
    }
    case DataType::LIST: {
      const shared_ptr<QLType>& elems_type = ql_type->params()[0];
      QLSeqValuePB *value_pb = ql_value->mutable_list_value();
      value_pb->clear_elems();
      for (auto &pair : object_container()) {
        // list elems are represented as subdocument values with keys only used for ordering
        pair.second.ToQLValuePB(elems_type, value_pb->add_elems());
      }
      return;
    }
    case DataType::USER_DEFINED_TYPE: {
      const shared_ptr<QLType>& keys_type = QLType::Create(DataType::INT16);
      QLMapValuePB *value_pb = ql_value->mutable_map_value();
      value_pb->clear_keys();
      value_pb->clear_values();
      for (auto &pair : object_container()) {
        QLValuePB *key = value_pb->add_keys();
        pair.first.ToQLValuePB(keys_type, key);
        pair.second.ToQLValuePB(ql_type->param_type(key->int16_value()), value_pb->add_values());
      }
      return;
    }
    case DataType::TUPLE:
      break;

    default: {
      return PrimitiveValue::ToQLValuePB(ql_type, ql_value);
    }
  }
  LOG(FATAL) << "Unsupported datatype in SubDocument: " << ql_type->ToString();
}

int SubDocument::object_num_keys() const {
  DCHECK(IsObjectType(type_));
  if (!has_valid_object_container()) {
    return 0;
  }
  return narrow_cast<int>(object_container().size());
}

bool SubDocument::container_allocated() const {
  CHECK(IsCollectionType(type_));
  return complex_data_structure_ != nullptr;
}

bool SubDocument::has_valid_object_container() const {
  return (IsObjectType(type_)) && has_valid_container();
}

bool SubDocument::has_valid_array_container() const {
  return type_ == ValueEntryType::kArray && has_valid_container();
}

}  // namespace yb::dockv
