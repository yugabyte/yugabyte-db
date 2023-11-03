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

#include <set>
#include "yb/client/schema.h"
#include "yb/yql/cql/ql/ptree/pt_keyspace_property.h"
#include "yb/yql/cql/ql/ptree/sem_context.h"
#include "yb/yql/cql/ql/ptree/yb_location.h"

#include "yb/util/logging.h"
#include "yb/util/string_case.h"

using std::string;
using std::vector;

namespace yb {
namespace ql {

using strings::Substitute;

PTKeyspaceProperty::PTKeyspaceProperty(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc,
                                       const MCSharedPtr<MCString>& lhs,
                                       const PTExprPtr& rhs)
    : PTProperty(memctx, loc, lhs, rhs),
      property_type_(KeyspacePropertyType::kKVProperty) {
}

PTKeyspaceProperty::PTKeyspaceProperty(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc)
    : PTProperty(memctx, loc) {
}

PTKeyspaceProperty::~PTKeyspaceProperty() {
}

Status PTKeyspaceProperty::Analyze(SemContext *sem_context) {
  return Status::OK();
}

void PTKeyspaceProperty::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

Status PTKeyspacePropertyListNode::Analyze(SemContext *sem_context) {
  bool has_replication = false;

  // If the statement has properties, 'replication' property must be present. Check this before
  // checking anything else.
  for (PTKeyspaceProperty::SharedPtr tnode : node_list()) {
    const auto property_name = string(tnode->lhs()->c_str());
    if (property_name == "replication") {
      has_replication = true;
      break;
    }
  }
  if (!has_replication) {
    return sem_context->Error(this, "Missing mandatory replication strategy class",
                              ErrorCode::INVALID_ARGUMENTS);
  }

  for (PTKeyspaceProperty::SharedPtr tnode : node_list()) {
    const auto property_name = string(tnode->lhs()->c_str());
    if (property_name == "durable_writes") {
      bool val = false;
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(PTProperty::GetBoolValueFromExpr(tnode->rhs(),
                                                                       "durable_writes", &val));
    } else if (property_name == "replication") {
      if (tnode->property_type() != KeyspacePropertyType::kPropertyMap) {
        return sem_context->Error(this,
                                  "Invalid value for property 'replication'. It should be a map",
                                  ErrorCode::INVALID_ARGUMENTS);
      }
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(tnode->Analyze(sem_context));
    } else {
      return sem_context->Error(this,
                                Substitute("Invalid property $0", property_name).c_str(),
                                ErrorCode::INVALID_ARGUMENTS);
    }
  }
  return Status::OK();
}

PTKeyspacePropertyMap::PTKeyspacePropertyMap(MemoryContext *memctx,
                                             YBLocation::SharedPtr loc)
    : PTKeyspaceProperty(memctx, loc) {
  property_type_ = KeyspacePropertyType::kPropertyMap;
  map_elements_ = TreeListNode<PTKeyspaceProperty>::MakeShared(memctx, loc);
}

PTKeyspacePropertyMap::~PTKeyspacePropertyMap() {
}

Status PTKeyspacePropertyMap::Analyze(SemContext *sem_context) {
  DCHECK_ONLY_NOTNULL(lhs_.get());
  // Verify we have a valid property name in the lhs.
  const auto property_name = string(lhs_->c_str());
  DCHECK_EQ(property_name, "replication");
  // Find 'class' subproperty.
  std::unique_ptr<string> class_name = nullptr;
  std::unique_ptr<int64_t> replication_factor = nullptr;
  vector<PTKeyspaceProperty::SharedPtr> other_subproperties;

  for (const auto &map_element : map_elements_->node_list()) {
    string subproperty_name;
    ToLowerCase(map_element->lhs()->c_str(), &subproperty_name);

    if (subproperty_name == "class") {
      class_name.reset(new string());
      RETURN_NOT_OK(GetStringValueFromExpr(map_element->rhs(), false, "class", class_name.get()));
    } else if (subproperty_name == "replication_factor") {
      replication_factor.reset(new int64_t);
      RETURN_NOT_OK(GetIntValueFromExpr(map_element->rhs(), "replication_factor",
                                        replication_factor.get()));
    } else {
      other_subproperties.push_back(map_element);
    }
  }

  if (class_name == nullptr) {
    return sem_context->Error(this, "Missing mandatory replication strategy class",
                              ErrorCode::INVALID_ARGUMENTS);
  }

  if (*class_name != "SimpleStrategy" && *class_name != "NetworkTopologyStrategy") {
    return sem_context->Error(this,
        Substitute("Unable to find replication strategy class 'org.apache.cassandra.locator.$0",
                   *class_name).c_str(),
        ErrorCode::INVALID_ARGUMENTS);
  }
  if (*class_name == "NetworkTopologyStrategy") {
    if (replication_factor != nullptr) {
      return sem_context->Error(this,
          "replication_factor is an option for SimpleStrategy, not NetworkTopologyStrategy",
          ErrorCode::INVALID_ARGUMENTS);
    }

    // Verify that all subproperties have integer values.
    int64_t val = 0;
    for (const auto& subproperty : other_subproperties) {
      RETURN_NOT_OK(GetIntValueFromExpr(subproperty->rhs(), subproperty->lhs()->c_str(), &val));
    }
  } else {
    if (!other_subproperties.empty()) {
      return sem_context->Error(this,
                                Substitute(
                                    "Unrecognized strategy option $0 passed to SimpleStrategy",
                                    other_subproperties.front()->lhs()->c_str()).c_str(),
                                ErrorCode::INVALID_ARGUMENTS);

    }
    if (replication_factor == nullptr) {
      return sem_context->Error(this,
                                "SimpleStrategy requires a replication_factor strategy option",
                                ErrorCode::INVALID_ARGUMENTS);
    }
  }

  return Status::OK();
}

void PTKeyspacePropertyMap::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

} // namespace ql
} // namespace yb
