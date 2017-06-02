// Copyright (c) YugaByte, Inc.

#include "yb/docdb/subdocument.h"

#include <map>
#include <sstream>
#include <vector>

#include "yb/common/yql_bfunc.h"

using std::endl;
using std::make_pair;
using std::map;
using std::ostringstream;
using std::shared_ptr;
using std::string;
using std::vector;

namespace yb {
namespace docdb {

SubDocument::SubDocument(ValueType value_type) : PrimitiveValue(value_type) {
  complex_data_structure_ = nullptr;
}

SubDocument::~SubDocument() {
  switch (type_) {
    case ValueType::kObject:
      if (has_valid_container()) {
        delete &object_container();
      }
      break;
    case ValueType::kArray:
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
      other.type_ == ValueType::kInvalidValueType ||
      other.type_ == ValueType::kTombstone) {
    new(this) PrimitiveValue(other);
  } else {
    type_ = other.type_;
    complex_data_structure_ = nullptr;
    switch (type_) {
      case ValueType::kObject:
        if (other.has_valid_object_container()) {
          complex_data_structure_ = new ObjectContainer(other.object_container());
        }
        break;
      case ValueType::kArray:
        if (other.has_valid_array_container()) {
          complex_data_structure_ = new ArrayContainer(other.array_container());
        }
        break;
      default:
        LOG(FATAL) << "Trying to copy an invalid/unsupported SubDocument type: "
                   << ValueTypeToStr(type_);
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
    case ValueType::kObject:
      if (has_valid_container() != other.has_valid_container()) {
        return has_valid_container() ? object_container().empty()
                                     : other.object_container().empty();
      }
      if (has_valid_container()) {
        return object_container() == other.object_container();
      } else {
        return true;  // Both container pointers are nullptr.
      }
    case ValueType::kArray:
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
      LOG(FATAL) << "Trying to compare SubDocuments of invalid type: " << ValueTypeToStr(type_);
  }
  // We'll get here if both container pointers are null.
  return true;
}

Status SubDocument::ConvertToArray() {
  if (type_ != ValueType::kObject) {
    return STATUS_SUBSTITUTE(
        InvalidArgument, "Expected kObject Subdocument, found $0", ValueTypeToStr(type_));
  }
  if (!has_valid_object_container()) {
    return STATUS(InvalidArgument, "Subdocument doesn't have valid object container");
  }
  const ObjectContainer& map = object_container();
  ArrayContainer* list = new ArrayContainer();
  list->reserve(map.size());
  // Elements in std::map are ordered by operator< on the key.
  // So iteration goes through sorted key order.
  for (auto ent : map) {
    list->emplace_back(std::move(ent.second));
  }
  type_ = ValueType::kArray;
  delete &object_container();
  complex_data_structure_ = list;
  return Status::OK();
}

SubDocument* SubDocument::GetChild(const PrimitiveValue& key) {
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

const SubDocument* SubDocument::GetChild(const PrimitiveValue& key) const {
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

pair<SubDocument*, bool> SubDocument::GetOrAddChild(const PrimitiveValue& key) {
  DCHECK_EQ(ValueType::kObject, type_);
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
  DCHECK_EQ(ValueType::kArray, type_);
  EnsureContainerAllocated();
  array_container().emplace_back(std::move(value));
}

void SubDocument::SetChild(const PrimitiveValue& key, SubDocument&& value) {
  type_ = ValueType::kObject;
  EnsureContainerAllocated();
  auto& obj_container = object_container();
  auto existing_element = obj_container.find(key);
  if (existing_element == obj_container.end()) {
    const bool inserted_value = obj_container.emplace(key, std::move(value)).second;
    CHECK(inserted_value);
  } else {
    existing_element->second = std::move(value);
  }
}

bool SubDocument::DeleteChild(const PrimitiveValue& key) {
  CHECK_EQ(ValueType::kObject, type_);
  if (!has_valid_object_container())
    return false;
  return object_container().erase(key) > 0;
}

string SubDocument::ToString() const {
  ostringstream ss;
  ss << *this;
  return ss.str();
}

ostream& operator <<(ostream& out, const SubDocument& subdoc) {
  SubDocumentToStreamInternal(out, subdoc, 0);
  return out;
}

void SubDocumentToStreamInternal(ostream& out,
                                 const SubDocument& subdoc,
                                 const int indent) {
  if (subdoc.IsPrimitive() ||
      subdoc.value_type() == ValueType::kInvalidValueType ||
      subdoc.value_type() == ValueType::kTombstone) {
    out << static_cast<const PrimitiveValue*>(&subdoc)->ToString();
    return;
  }
  switch (subdoc.value_type()) {
    case ValueType::kObject: {
      out << "{";
      if (subdoc.container_allocated()) {
        bool first_pair = true;
        for (const auto& key_value : subdoc.object_container()) {
          if (!first_pair) {
            out << ",";
          }
          first_pair = false;
          out << "\n" << string(indent + 2, ' ') << key_value.first.ToString() << ": ";
          SubDocumentToStreamInternal(out, key_value.second, indent + 2);
        }
        if (!first_pair) {
          out << "\n" << string(indent, ' ');
        }
      }
      out << "}";
      break;
    }
    case ValueType::kArray: {
      out << "[";
      if (subdoc.container_allocated()) {
        const auto& list = subdoc.array_container();
        int i = 0;
        for (; i < list.size(); i++) {
          if (i != 0) {
            out << ",";
          }
          out << "\n" << string(indent + 2, ' ') << i << ": ";
          SubDocumentToStreamInternal(out, list[i], indent + 2);
        }
        if (i > 0) {
          out << "\n" << string(indent, ' ');
        }
      }
      out << "]";
      break;
    }
    default:
      LOG(FATAL) << "Invalid subdocument type: " << ValueTypeToStr(subdoc.value_type());
  }
}

void SubDocument::EnsureContainerAllocated() {
  if (complex_data_structure_ == nullptr) {
    if (type_ == ValueType::kObject) {
      complex_data_structure_ = new ObjectContainer();
    } else if (type_ == ValueType::kArray) {
      complex_data_structure_ = new ArrayContainer();
    }
  }
}

SubDocument SubDocument::FromYQLValuePB(const shared_ptr<YQLType>& yql_type,
                                        const YQLValuePB& value,
                                        ColumnSchema::SortingType sorting_type) {
  if (YQLValue::IsNull(value)) {
    return SubDocument(ValueType::kTombstone);
  }

  switch (yql_type->main()) {
    case MAP: {
      YQLMapValuePB map = YQLValue::map_value(value);
      // this equality should be ensured by checks before getting here
      DCHECK_EQ(map.keys_size(), map.values_size());

      const shared_ptr<YQLType>& keys_type = yql_type->params()[0];
      const shared_ptr<YQLType>& values_type = yql_type->params()[1];
      SubDocument map_doc;
      for (int i = 0; i < map.keys_size(); i++) {
        PrimitiveValue pv_key = PrimitiveValue::FromYQLValuePB(keys_type, map.keys(i),
            sorting_type);
        SubDocument pv_value = SubDocument::FromYQLValuePB(values_type, map.values(i),
            sorting_type);
        map_doc.SetChild(pv_key, std::move(pv_value));
      }
      // ensure container allocated even if map is empty
      map_doc.EnsureContainerAllocated();
      return map_doc;
    }
    case SET: {
      YQLSeqValuePB set = YQLValue::set_value(value);
      const shared_ptr<YQLType>& elems_type = yql_type->params()[0];
      SubDocument set_doc;
      for (auto& elem : set.elems()) {
        // representing sets elems as keys pointing to empty (null) values
        PrimitiveValue pv_key = PrimitiveValue::FromYQLValuePB(elems_type, elem, sorting_type);
        set_doc.SetChildPrimitive(pv_key, PrimitiveValue());
      }
      // ensure container allocated even if set is empty
      set_doc.EnsureContainerAllocated();
      return set_doc;
    }
    case LIST: {
      YQLSeqValuePB list = YQLValue::list_value(value);
      const shared_ptr<YQLType>& elems_type = yql_type->params()[0];
      SubDocument list_doc(ValueType::kArray);
      // ensure container allocated even if list is empty
      list_doc.EnsureContainerAllocated();
      for (int i = 0; i < list.elems_size(); i++) {
        SubDocument pv_value = SubDocument::FromYQLValuePB(elems_type, list.elems(i),
            sorting_type);
        list_doc.AddListElement(std::move(pv_value));
      }
      return list_doc;
    }
    case TUPLE:
      break;

    default:
      return SubDocument(PrimitiveValue::FromYQLValuePB(yql_type, value, sorting_type));
  }
  LOG(FATAL) << "Unsupported datatype in SubDocument: " << yql_type->ToString();
}

void SubDocument::ToYQLValuePB(SubDocument doc,
                               const shared_ptr<YQLType>& yql_type,
                               YQLValuePB* yql_value) {
  // interpreting empty collections as null values following Cassandra semantics
  if (yql_type->IsParametric() && (!doc.has_valid_object_container() ||
                                  doc.object_num_keys() == 0)) {
    YQLValue::SetNull(yql_value);
    return;
  }

  switch (yql_type->main()) {
    case MAP: {
      const shared_ptr<YQLType>& keys_type = yql_type->params()[0];
      const shared_ptr<YQLType>& values_type = yql_type->params()[1];
      YQLValue::set_map_value(yql_value);
      for (auto &pair : doc.object_container()) {
        YQLValuePB *key = YQLValue::add_map_key(yql_value);
        PrimitiveValue::ToYQLValuePB(pair.first, keys_type, key);
        YQLValuePB *value = YQLValue::add_map_value(yql_value);
        SubDocument::ToYQLValuePB(pair.second, values_type, value);
      }
      return;
    }
    case SET: {
      const shared_ptr<YQLType>& elems_type = yql_type->params()[0];
      YQLValue::set_set_value(yql_value);
      for (auto &pair : doc.object_container()) {
        YQLValuePB *elem = YQLValue::add_set_elem(yql_value);
        PrimitiveValue::ToYQLValuePB(pair.first, elems_type, elem);
        // set elems are represented as subdocument keys so we ignore the (empty) values
      }
      return;
    }
    case LIST: {
      const shared_ptr<YQLType>& elems_type = yql_type->params()[0];
      YQLValue::set_list_value(yql_value);
      for (auto &pair : doc.object_container()) {
        // list elems are represented as subdocument values with keys only used for ordering
        YQLValuePB *elem = YQLValue::add_list_elem(yql_value);
        SubDocument::ToYQLValuePB(pair.second, elems_type, elem);
      }
      return;
    }
    case TUPLE:
      break;

    default: {
      return PrimitiveValue::ToYQLValuePB(doc, yql_type, yql_value);
    }
  }
  LOG(FATAL) << "Unsupported datatype in SubDocument: " << yql_type->ToString();
}

void SubDocument::ToYQLExpressionPB(SubDocument doc,
                                    const shared_ptr<YQLType>& yql_type,
                                    YQLExpressionPB* yql_expr) {
  ToYQLValuePB(doc, yql_type, yql_expr->mutable_value());
}

SubDocument SubDocument::FromYQLExpressionPB(const shared_ptr<YQLType>& yql_type,
                                             const YQLExpressionPB& yql_expr,
                                             ColumnSchema::SortingType sorting_type,
                                             const YQLValueMap& value_map) {
  switch (yql_expr.expr_case()) {
    case YQLExpressionPB::ExprCase::kValue:  // Scenarios: SET column = constant.
      return FromYQLValuePB(yql_type, yql_expr.value(), sorting_type);

    case YQLExpressionPB::ExprCase::kColumnId:  // Scenarios: SET column = column.
      FALLTHROUGH_INTENDED;

    case YQLExpressionPB::ExprCase::kBfcall: {  // Scenarios: SET column = func().
      YQLValueWithPB result;
      EvalYQLExpressionPB(yql_expr, value_map, &result);
      return FromYQLValuePB(yql_type, result.value(), sorting_type);
    }

    case YQLExpressionPB::ExprCase::kCondition:
      LOG(FATAL) << "Internal error: Conditional expression is not allowed in this context";
      break;

    case YQLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << yql_expr.expr_case();
}

// TODO(neil) When memory pool is implemented in DocDB, we should run some perf tool to optimize
// the expression evaluating process. The intermediate / temporary YQLValue should be allocated
// in the pool. Currently, the argument structures are on stack, but their contents are in the
// heap memory.
void SubDocument::EvalYQLExpressionPB(const YQLExpressionPB& yql_expr,
                                      const YQLValueMap& value_map,
                                      YQLValueWithPB *result) {
  switch (yql_expr.expr_case()) {
    case YQLExpressionPB::ExprCase::kValue:
      result->Assign(yql_expr.value());
      break;

    case YQLExpressionPB::ExprCase::kBfcall: {
      // First evaluate the arguments.
      const YQLBFCallPB& bfcall = yql_expr.bfcall();
      vector<YQLValueWithPB> args(bfcall.operands().size());
      int arg_index = 0;
      for (auto operand : bfcall.operands()) {
        EvalYQLExpressionPB(operand, value_map, &args[arg_index]);
        arg_index++;
      }

      // Execute the builtin call associated with the given opcode.
      YQLBfunc::Exec(static_cast<bfyql::BFOpcode>(bfcall.bfopcode()), &args, result);
      break;
    }

    case YQLExpressionPB::ExprCase::kColumnId: {
      auto iter = value_map.find(ColumnId(yql_expr.column_id()));
      if (iter != value_map.end()) {
        result->Assign(iter->second);
      } else {
        result->SetNull();
      }
      break;
    }

    case YQLExpressionPB::ExprCase::kCondition:
      LOG(FATAL) << "Internal error: Conditional expression is not allowed in this context";
      break;

    case YQLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
  }
}

}  // namespace docdb
}  // namespace yb
