//--------------------------------------------------------------------------------------------------
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
//
// SQL Option Declaration.
// This module defines the enums to represent various optional clause in SQL.
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/ptree/pt_option.h"

#include <ostream>

namespace yb {
namespace ql {

const char* ObjectTypeName(ObjectType object_type) {
  switch (object_type) {
    case ObjectType::AGGREGATE: return "aggregate";
    case ObjectType::AMOP: return "access method operator";
    case ObjectType::AMPROC: return "access method procedure";
    case ObjectType::ATTRIBUTE: return "attribute";
    case ObjectType::CAST: return "cast";
    case ObjectType::COLUMN: return "column";
    case ObjectType::COLLATION: return "collation";
    case ObjectType::CONVERSION: return "conversion";
    case ObjectType::DATABASE: return "database";
    case ObjectType::DEFAULT: return "default";
    case ObjectType::DEFACL: return "defacl";
    case ObjectType::DOMAIN: return "domain";
    case ObjectType::DOMCONSTRAINT: return "domain constraint";
    case ObjectType::EVENT_TRIGGER: return "event trigger";
    case ObjectType::EXTENSION: return "extension";
    case ObjectType::FDW: return "foreign data wrapper";
    case ObjectType::FOREIGN_SERVER: return "foreign server";
    case ObjectType::FOREIGN_TABLE: return "foreign table";
    case ObjectType::FUNCTION: return "function";
    case ObjectType::INDEX: return "index";
    case ObjectType::LANGUAGE: return "language";
    case ObjectType::LARGEOBJECT: return "large object";
    case ObjectType::MATVIEW: return "materialized view";
    case ObjectType::OPCLASS: return "operator class";
    case ObjectType::OPERATOR: return "operator";
    case ObjectType::OPFAMILY: return "operator family";
    case ObjectType::POLICY: return "policy";
    case ObjectType::ROLE: return "role";
    case ObjectType::RULE: return "rule";
    case ObjectType::SCHEMA: return "schema";
    case ObjectType::SEQUENCE: return "sequence";
    case ObjectType::TABCONSTRAINT: return "table constraint";
    case ObjectType::TABLE: return "table";
    case ObjectType::TABLESPACE: return "tablespace";
    case ObjectType::TRANSFORM: return "transform";
    case ObjectType::TRIGGER: return "trigger";
    case ObjectType::TSCONFIGURATION: return "text search configuration";
    case ObjectType::TSDICTIONARY: return "text search dictionary";
    case ObjectType::TSPARSER: return "text search parser";
    case ObjectType::TSTEMPLATE: return "text search template";
    case ObjectType::TYPE: return "type";
    case ObjectType::USER_MAPPING: return "user mapping";
    case ObjectType::VIEW: return "view";
  }
  return "unknown object type";
}

std::ostream& operator<<(std::ostream& out, ObjectType object_type) {
  return out << ObjectTypeName(object_type);
}

}  // namespace ql
}  // namespace yb
