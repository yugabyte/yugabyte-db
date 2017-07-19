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

#include "yb/docdb/subdocument.h"

#include <map>
#include <sstream>
#include <vector>

#include "yb/common/ql_bfunc.h"

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
    case ValueType::kObject: FALLTHROUGH_INTENDED;
    case ValueType::kRedisSet:
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
                   << docdb::ToString(type_);
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
      LOG(FATAL) << "Trying to compare SubDocuments of invalid type: " << docdb::ToString(type_);
  }
  // We'll get here if both container pointers are null.
  return true;
}

Status SubDocument::ConvertToRedisSet() {
  if (type_ != ValueType::kObject) {
    return STATUS_FORMAT(
        InvalidArgument, "Expected kObject Subdocument, found $0", type_);
  }
  if (!has_valid_object_container()) {
    return STATUS(InvalidArgument, "Subdocument doesn't have valid object container");
  }
  type_ = ValueType::kRedisSet;
  return Status::OK();
}

Status SubDocument::ConvertToArray() {
  if (type_ != ValueType::kObject) {
    return STATUS_FORMAT(
        InvalidArgument, "Expected kObject Subdocument, found $0", type_);
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
    case ValueType::kRedisSet: {
      out << "(";
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
      out << ")";
      break;
    }
    default:
      LOG(FATAL) << "Invalid subdocument type: " << ToString(subdoc.value_type());
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

SubDocument SubDocument::FromQLValuePB(const QLValuePB& value,
                                       ColumnSchema::SortingType sorting_type,
                                       WriteAction write_action) {
  switch (value.value_case()) {
    case QLValuePB::kMapValue: {
      QLMapValuePB map = QLValue::map_value(value);
      // this equality should be ensured by checks before getting here
      DCHECK_EQ(map.keys_size(), map.values_size());

      SubDocument map_doc;
      for (int i = 0; i < map.keys_size(); i++) {
        PrimitiveValue pv_key = PrimitiveValue::FromQLValuePB(map.keys(i), sorting_type);
        SubDocument pv_val = SubDocument::FromQLValuePB(map.values(i), sorting_type, write_action);
        map_doc.SetChild(pv_key, std::move(pv_val));
      }
      // ensure container allocated even if map is empty
      map_doc.EnsureContainerAllocated();
      return map_doc;
    }
    case QLValuePB::kSetValue: {
      QLSeqValuePB set = QLValue::set_value(value);
      SubDocument set_doc;
      for (auto& elem : set.elems()) {
        PrimitiveValue pv_key = PrimitiveValue::FromQLValuePB(elem, sorting_type);
        if (write_action == WriteAction::REMOVE_KEYS) {
          // representing sets elems as keys pointing to tombstones to remove those entries
          set_doc.SetChildPrimitive(pv_key, PrimitiveValue(ValueType::kTombstone));
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
      QLSeqValuePB list = QLValue::list_value(value);
      SubDocument list_doc(ValueType::kArray);
      // ensure container allocated even if list is empty
      list_doc.EnsureContainerAllocated();
      for (int i = 0; i < list.elems_size(); i++) {
        SubDocument pv_val = SubDocument::FromQLValuePB(list.elems(i), sorting_type, write_action);
        list_doc.AddListElement(std::move(pv_val));
      }
      return list_doc;
    }

    default:
      return SubDocument(PrimitiveValue::FromQLValuePB(value, sorting_type));
  }
}

void SubDocument::ToQLValuePB(SubDocument doc,
                              const shared_ptr<QLType>& ql_type,
                              QLValuePB* ql_value) {
  // interpreting empty collections as null values following Cassandra semantics
  if (ql_type->HasComplexValues() && (!doc.has_valid_object_container() ||
                                       doc.object_num_keys() == 0)) {
    QLValue::SetNull(ql_value);
    return;
  }

  switch (ql_type->main()) {
    case MAP: {
      const shared_ptr<QLType>& keys_type = ql_type->params()[0];
      const shared_ptr<QLType>& values_type = ql_type->params()[1];
      QLValue::set_map_value(ql_value);
      for (auto &pair : doc.object_container()) {
        QLValuePB *key = QLValue::add_map_key(ql_value);
        PrimitiveValue::ToQLValuePB(pair.first, keys_type, key);
        QLValuePB *value = QLValue::add_map_value(ql_value);
        SubDocument::ToQLValuePB(pair.second, values_type, value);
      }
      return;
    }
    case SET: {
      const shared_ptr<QLType>& elems_type = ql_type->params()[0];
      QLValue::set_set_value(ql_value);
      for (auto &pair : doc.object_container()) {
        QLValuePB *elem = QLValue::add_set_elem(ql_value);
        PrimitiveValue::ToQLValuePB(pair.first, elems_type, elem);
        // set elems are represented as subdocument keys so we ignore the (empty) values
      }
      return;
    }
    case LIST: {
      const shared_ptr<QLType>& elems_type = ql_type->params()[0];
      QLValue::set_list_value(ql_value);
      for (auto &pair : doc.object_container()) {
        // list elems are represented as subdocument values with keys only used for ordering
        QLValuePB *elem = QLValue::add_list_elem(ql_value);
        SubDocument::ToQLValuePB(pair.second, elems_type, elem);
      }
      return;
    }
    case USER_DEFINED_TYPE: {
      const shared_ptr<QLType>& keys_type = QLType::Create(INT16);
      QLValue::set_map_value(ql_value);
      for (auto &pair : doc.object_container()) {
        QLValuePB *key = QLValue::add_map_key(ql_value);
        PrimitiveValue::ToQLValuePB(pair.first, keys_type, key);
        QLValuePB *value = QLValue::add_map_value(ql_value);
        SubDocument::ToQLValuePB(pair.second, ql_type->param_type(key->int16_value()), value);
      }
      return;
    }
    case TUPLE:
      break;

    default: {
      return PrimitiveValue::ToQLValuePB(doc, ql_type, ql_value);
    }
  }
  LOG(FATAL) << "Unsupported datatype in SubDocument: " << ql_type->ToString();
}

void SubDocument::ToQLExpressionPB(SubDocument doc,
                                   const shared_ptr<QLType>& ql_type,
                                   QLExpressionPB* ql_expr) {
  ToQLValuePB(doc, ql_type, ql_expr->mutable_value());
}


CHECKED_STATUS SubDocument::FromQLExpressionPB(const QLExpressionPB& ql_expr,
                                               const ColumnSchema& column_schema,
                                               const QLTableRow& table_row,
                                               SubDocument* sub_doc,
                                               WriteAction* write_action) {
  switch (ql_expr.expr_case()) {
    case QLExpressionPB::ExprCase::kValue:  // Scenarios: SET column = constant.
      *sub_doc = FromQLValuePB(ql_expr.value(), column_schema.sorting_type(), *write_action);
      return Status::OK();

    case QLExpressionPB::ExprCase::kColumnId:  // Scenarios: SET column = column.
      FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kSubscriptedCol:  // Scenarios: SET column = column[key].
      FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBfcall: {  // Scenarios: SET column = func().
      QLValueWithPB result;
      RETURN_NOT_OK(YQLExpression::Evaluate(ql_expr, table_row, &result, write_action));
      // Type of the result could be changed for some write actions (e.g. REMOVE_KEYS from map)
      *sub_doc = FromQLValuePB(result.value(), column_schema.sorting_type(), *write_action);
      return Status::OK();
    }

    case QLExpressionPB::ExprCase::kTscall: {  // Scenarios: SELECT TTL(col).
      QLValueWithPB result;
      RETURN_NOT_OK(EvalQLBFCallTServerPB(ql_expr.tscall(), table_row, &result));
      *sub_doc = FromQLValuePB(result.value(), column_schema.sorting_type(), *write_action);
      return Status::OK();
    }

    case QLExpressionPB::ExprCase::kCondition:
      LOG(FATAL) << "Internal error: Conditional expression is not allowed in this context";
      break;

    case QLExpressionPB::ExprCase::kBocall: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::kBindId: FALLTHROUGH_INTENDED;
    case QLExpressionPB::ExprCase::EXPR_NOT_SET:
      break;
  }
  LOG(FATAL) << "Internal error: invalid column or value expression: " << ql_expr.expr_case();
}

// TODO(neil)
// - Need to work with Mihnea on collection function and merge this work with DocExprExecutor().
// - When memory pool is implemented in DocDB, we should run some perf tool to optimize the
// expression evaluating process. The intermediate / temporary QLValue should be allocated
// in the pool. Currently, the argument structures are on stack, but their contents are in the
// heap memory.
CHECKED_STATUS SubDocument::EvalQLBFCallTServerPB(const QLBCallPB& tscall,
                                                  const QLTableRow& table_row,
                                                  QLValueWithPB *result) {
  bfql::TSOpcode tsopcode = static_cast<bfql::TSOpcode>(tscall.opcode());
  switch (tsopcode) {
    case bfql::TSOpcode::kNoOp:
      break;

    case bfql::TSOpcode::kWriteTime: {
      // TODO(neil) Call Docdb for the value and fill in the result.
      // "args" should be just one "kColumnId". The variable "table_row" is not enough for this
      // operator, so we must pass other information together with value.
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      const QLExpressionPB& operand = tscall.operands().Get(0);
      DCHECK(operand.has_column_id()) << "WriteTime operator expects a column ID";
      LOG(INFO) << "Seek WRITETIME for column " << operand.column_id();

      LOG(FATAL) << "Failed to execute WRITETIME";
    }

    case bfql::TSOpcode::kTtl: {
      // TODO(neil) Call Docdb for the value and fill in the result.
      // "args" should be just one "kColumnId". The variable "table_row" is not enough for this
      // operator, so we must pass other information together with value.
      DCHECK_EQ(tscall.operands().size(), 1) << "WriteTime takes only one argument, a column";
      const QLExpressionPB& operand = tscall.operands().Get(0);
      DCHECK(operand.has_column_id()) << "WriteTime operator expects a column ID";
      LOG(INFO) << "Seek WRITETIME for column " << operand.column_id();

      LOG(FATAL) << "Failed to execute TTL";
    }

    case bfql::TSOpcode::kCount: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kSum: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kAvg: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kMin: FALLTHROUGH_INTENDED;
    case bfql::TSOpcode::kMax:
      // TODO(neil) Call DocDB to execute aggregate functions.
      // These functions operate across many rows, so some state variables must be kept beyond the
      // scope of one row. The variable "table_row" is not enough for these operators.
      LOG(FATAL) << "Failed to execute aggregate function";
  }

  return Status::OK();
}

}  // namespace docdb
}  // namespace yb
