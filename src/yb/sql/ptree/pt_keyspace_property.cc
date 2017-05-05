// Copyright (c) YugaByte, Inc.

#include <set>
#include "yb/client/schema.h"
#include "yb/sql/ptree/pt_keyspace_property.h"
#include "yb/sql/ptree/sem_context.h"
#include "yb/util/string_case.h"

namespace yb {
namespace sql {

using strings::Substitute;
using client::YBColumnSchema;

PTKeyspaceProperty::PTKeyspaceProperty(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc,
                                       const MCString::SharedPtr& lhs,
                                       const PTExpr::SharedPtr& rhs)
    : PTProperty(memctx, loc, lhs, rhs),
      property_type_(KeyspacePropertyType::kKVProperty) {
}

PTKeyspaceProperty::PTKeyspaceProperty(MemoryContext *memctx,
                                       YBLocation::SharedPtr loc)
    : PTProperty(memctx, loc) {
}

PTKeyspaceProperty::~PTKeyspaceProperty() {
}

CHECKED_STATUS PTKeyspaceProperty::Analyze(SemContext *sem_context) {
  return Status::OK();
}

void PTKeyspaceProperty::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

CHECKED_STATUS PTKeyspacePropertyListNode::Analyze(SemContext *sem_context) {
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
    return sem_context->Error(loc(),
                              "Missing mandatory replication strategy class",
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
        return sem_context->Error(loc(),
                                  "Invalid value for property 'replication'. It should be a map",
                                  ErrorCode::INVALID_ARGUMENTS);
      }
      RETURN_SEM_CONTEXT_ERROR_NOT_OK(tnode->Analyze(sem_context));
    } else {
      return sem_context->Error(loc(),
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

CHECKED_STATUS PTKeyspacePropertyMap::Analyze(SemContext *sem_context) {
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
      RETURN_NOT_OK(GetStringValueFromExpr(map_element->rhs(), false, "class", class_name.get()))
    } else if (subproperty_name == "replication_factor") {
      replication_factor.reset(new int64_t);
      RETURN_NOT_OK(GetIntValueFromExpr(map_element->rhs(), "replication_factor",
                                        replication_factor.get()));
    } else {
      other_subproperties.push_back(map_element);
    }
  }

  if (class_name == nullptr) {
    return sem_context->Error(loc(), "Missing mandatory replication strategy class",
                              ErrorCode::INVALID_ARGUMENTS);
  }

  if (*class_name != "SimpleStrategy" && *class_name != "NetworkTopologyStrategy") {
    return sem_context->Error(loc(),
        Substitute("Unable to find replication strategy class 'org.apache.cassandra.locator.$0",
                   *class_name).c_str(),
        ErrorCode::INVALID_ARGUMENTS);
  }
  if (*class_name == "NetworkTopologyStrategy") {
    if (replication_factor != nullptr) {
      return sem_context->Error(loc(),
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
      return sem_context->Error(loc(),
                                Substitute(
                                    "Unrecognized strategy option $0 passed to SimpleStrategy",
                                    other_subproperties.front()->lhs()->c_str()).c_str(),
                                ErrorCode::INVALID_ARGUMENTS);

    }
    if (replication_factor == nullptr) {
      return sem_context->Error(loc(),
                                "SimpleStrategy requires a replication_factor strategy option",
                                ErrorCode::INVALID_ARGUMENTS);
    }
  }

  return Status::OK();
}

void PTKeyspacePropertyMap::PrintSemanticAnalysisResult(SemContext *sem_context) {
  VLOG(3) << "SEMANTIC ANALYSIS RESULT (" << *loc_ << "):\n" << "Not yet avail";
}

} // namespace sql
} // namespace yb
