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

#include "yb/yql/pgsql/ptree/pg_option.h"

namespace yb {
namespace pgsql {

const char* ObjectTypeName(ObjectType object_type) {
  switch (object_type) {
    case OBJECT_AGGREGATE: return "aggregate";
    case OBJECT_AMOP: return "access method operator";
    case OBJECT_AMPROC: return "access method procedure";
    case OBJECT_ATTRIBUTE: return "attribute";
    case OBJECT_CAST: return "cast";
    case OBJECT_COLUMN: return "column";
    case OBJECT_COLLATION: return "collation";
    case OBJECT_CONVERSION: return "conversion";
    case OBJECT_DATABASE: return "database";
    case OBJECT_DEFAULT: return "default";
    case OBJECT_DEFACL: return "defacl";
    case OBJECT_DOMAIN: return "domain";
    case OBJECT_DOMCONSTRAINT: return "domain constraint";
    case OBJECT_EVENT_TRIGGER: return "event trigger";
    case OBJECT_EXTENSION: return "extension";
    case OBJECT_FDW: return "foreign data wrapper";
    case OBJECT_FOREIGN_SERVER: return "foreign server";
    case OBJECT_FOREIGN_TABLE: return "foreign table";
    case OBJECT_FUNCTION: return "function";
    case OBJECT_INDEX: return "index";
    case OBJECT_LANGUAGE: return "language";
    case OBJECT_LARGEOBJECT: return "large object";
    case OBJECT_MATVIEW: return "materialized view";
    case OBJECT_OPCLASS: return "operator class";
    case OBJECT_OPERATOR: return "operator";
    case OBJECT_OPFAMILY: return "operator family";
    case OBJECT_POLICY: return "policy";
    case OBJECT_ROLE: return "role";
    case OBJECT_RULE: return "rule";
    case OBJECT_SCHEMA: return "schema";
    case OBJECT_SEQUENCE: return "sequence";
    case OBJECT_TABCONSTRAINT: return "table constraint";
    case OBJECT_TABLE: return "table";
    case OBJECT_TABLESPACE: return "tablespace";
    case OBJECT_TRANSFORM: return "transform";
    case OBJECT_TRIGGER: return "trigger";
    case OBJECT_TSCONFIGURATION: return "text search configuration";
    case OBJECT_TSDICTIONARY: return "text search dictionary";
    case OBJECT_TSPARSER: return "text search parser";
    case OBJECT_TSTEMPLATE: return "text search template";
    case OBJECT_TYPE: return "type";
    case OBJECT_USER_MAPPING: return "user mapping";
    case OBJECT_VIEW: return "view";
  }
  return "unknown object type";
}

}  // namespace pgsql
}  // namespace yb
