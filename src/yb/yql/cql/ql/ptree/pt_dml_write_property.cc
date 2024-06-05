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
#include "yb/yql/cql/ql/ptree/pt_dml_write_property.h"

#include <set>

#include "yb/client/schema.h"
#include "yb/util/string_case.h"
#include "yb/yql/cql/ql/ptree/pt_expr.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

using std::ostream;
using std::string;
using std::vector;

namespace yb {
namespace ql {

using strings::Substitute;

// These property names need to be lowercase, since identifiers are converted to lowercase by the
// scanner phase and as a result if we're doing string matching everything should be lowercase.
const std::map<std::string, PTDmlWriteProperty::KVProperty> PTDmlWriteProperty::kPropertyDataTypes
    = {
    {"options", KVProperty::kOptions}
};

PTDmlWriteProperty::PTDmlWriteProperty(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc,
                                 const MCSharedPtr<MCString>& lhs,
                                 const PTExprPtr& rhs)
    : PTProperty(memctx, loc, lhs, rhs),
      property_type_(DmlWritePropertyType::kDmlWriteProperty) {}

PTDmlWriteProperty::PTDmlWriteProperty(MemoryContext *memctx,
                                 YBLocation::SharedPtr loc)
    : PTProperty(memctx, loc) {
}

PTDmlWriteProperty::~PTDmlWriteProperty() {
}

Status PTDmlWriteProperty::Analyze(SemContext *sem_context) {

  // Verify we have a valid property name in the lhs.
  const auto& update_property_name = lhs_->c_str();
  auto iterator = kPropertyDataTypes.find(update_property_name);
  if (iterator == kPropertyDataTypes.end()) {
    return sem_context->Error(this, Substitute("Unknown property '$0'", lhs_->c_str()).c_str(),
                              ErrorCode::INVALID_UPDATE_PROPERTY);
  }

  if (iterator->second == KVProperty::kOptions) {
    return sem_context->Error(this,
                            Substitute("Invalid value for property '$0'. Value must be a map",
                                        update_property_name).c_str(),
                            ErrorCode::DATATYPE_MISMATCH);
  }

  return Status::OK();
}

std::ostream& operator<<(ostream& os, const DmlWritePropertyType& property_type) {
  switch(property_type) {
    case DmlWritePropertyType::kDmlWriteProperty:
      os << "kDmlWriteProperty";
      break;
    case DmlWritePropertyType::kDmlWritePropertyMap:
      os << "kDmlWritePropertyMap";
      break;
  }
  return os;
}

void PTDmlWriteProperty::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

Status PTDmlWritePropertyListNode::Analyze(SemContext *sem_context) {
  // Set to ensure we don't have duplicate update properties.
  std::set<string> update_properties;
  std::unordered_map<string, PTDmlWriteProperty::SharedPtr> order_tnodes;
  vector<string> order_columns;
  for (PTDmlWriteProperty::SharedPtr tnode : node_list()) {
    if (tnode == nullptr) {
      // This shouldn't happen because AppendList ignores null nodes.
      LOG(ERROR) << "Invalid null property";
      continue;
    }
    switch(tnode->property_type()) {
      case DmlWritePropertyType::kDmlWriteProperty: FALLTHROUGH_INTENDED;
      case DmlWritePropertyType::kDmlWritePropertyMap: {
        string update_property_name = tnode->lhs()->c_str();
        if (update_properties.find(update_property_name) != update_properties.end()) {
          return sem_context->Error(this, ErrorCode::DUPLICATE_UPDATE_PROPERTY);
        }
        RETURN_NOT_OK(tnode->Analyze(sem_context));
        update_properties.insert(update_property_name);
        break;
      }
    }
  }

  return Status::OK();
}

bool PTDmlWritePropertyListNode::ignore_null_jsonb_attributes() {
  for (PTDmlWriteProperty::SharedPtr tnode : node_list()) {
    if (tnode->property_type() == DmlWritePropertyType::kDmlWritePropertyMap) {
      string update_property_name = tnode->lhs()->c_str();
      if (update_property_name == "options") {
        return \
          (std::static_pointer_cast<PTDmlWritePropertyMap>(tnode))->ignore_null_jsonb_attributes();
      }
    }
  }
  return false; // the default
}

const std::map<string, PTDmlWritePropertyMap::PropertyMapType> \
  PTDmlWritePropertyMap::kPropertyDataTypes
    = {
    {"options", PTDmlWritePropertyMap::PropertyMapType::kOptions}
};

PTDmlWritePropertyMap::PTDmlWritePropertyMap(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc)
    : PTDmlWriteProperty(memctx, loc) {
  property_type_ = DmlWritePropertyType::kDmlWritePropertyMap;
  map_elements_ = TreeListNode<PTDmlWriteProperty>::MakeShared(memctx, loc);
}

PTDmlWritePropertyMap::~PTDmlWritePropertyMap() {
}

Status PTDmlWritePropertyMap::Analyze(SemContext *sem_context) {
  // Verify we have a valid property name in the lhs.
  const auto &property_name = lhs_->c_str();
  auto iterator = kPropertyDataTypes.find(property_name);
  if (iterator == kPropertyDataTypes.end()) {
    if (IsValidProperty(property_name)) {
      return sem_context->Error(this, Substitute("Invalid map value for property '$0'",
                                                 property_name).c_str(),
                                ErrorCode::DATATYPE_MISMATCH);
    }
    return sem_context->Error(this, Substitute("Unknown property '$0'", property_name).c_str(),
                              ErrorCode::INVALID_UPDATE_PROPERTY);
  }

  const auto &property_type = iterator->second;

  switch (property_type) {
    case PropertyMapType::kOptions:
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(AnalyzeOptions(sem_context));
      break;
  }
  return Status::OK();
}

void PTDmlWritePropertyMap::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

Status PTDmlWritePropertyMap::AnalyzeOptions(SemContext *sem_context) {
  for (const auto& subproperty : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    auto iter = Options::kSubpropertyDataTypes.find(subproperty_name);
    if (iter == Options::kSubpropertyDataTypes.end()) {
      return STATUS(InvalidArgument, Substitute("Unknown options property $0",
                                                subproperty_name));
    }

    bool bool_val;
    switch(iter->second) {
      case Options::Subproperty::kIgnoreNullJsonbAttributes:
        RETURN_NOT_OK(GetBoolValueFromExpr(subproperty->rhs(), subproperty_name, &bool_val));
        break;
    }
  }
  return Status::OK();
}

bool PTDmlWritePropertyMap::ignore_null_jsonb_attributes() {
  for (const auto& subproperty : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(subproperty->lhs()->c_str(), &subproperty_name);
    auto iter = Options::kSubpropertyDataTypes.find(subproperty_name);
    if (iter != Options::kSubpropertyDataTypes.end() &&
        iter->second == Options::Subproperty::kIgnoreNullJsonbAttributes) {
      return std::dynamic_pointer_cast<PTConstBool>(subproperty->rhs())->Eval();
    }
  }
  return false; // Default if the property isn't specified
}

const std::map<std::string, Options::Subproperty>
Options::kSubpropertyDataTypes = {
    {"ignore_null_jsonb_attributes", Options::Subproperty::kIgnoreNullJsonbAttributes}
};

} // namespace ql
} // namespace yb
