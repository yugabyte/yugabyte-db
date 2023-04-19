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

#pragma once

#include <cmath> // Include cmath to get DOMAIN definition and undef it here
#include <iosfwd>

namespace yb {
namespace ql {

// Drop option.
typedef enum DropBehavior : int {
  DROP_RESTRICT,        /* drop fails if any dependent objects */
  DROP_CASCADE        /* remove dependent objects too */
} DropBehavior;

// When a command can act on several kinds of objects with only one
// parse structure required, use these constants to designate the
// object type.  Note that commands typically don't support all the types.
#undef DOMAIN
enum class ObjectType : int {
  AGGREGATE,
  AMOP,
  AMPROC,
  ATTRIBUTE,     /* type's attribute, when distinct from column */
  CAST,
  COLUMN,
  COLLATION,
  CONVERSION,
  DATABASE,
  DEFAULT,
  DEFACL,
  DOMAIN,
  DOMCONSTRAINT,
  EVENT_TRIGGER,
  EXTENSION,
  FDW,
  FOREIGN_SERVER,
  FOREIGN_TABLE,
  FUNCTION,
  INDEX,
  LANGUAGE,
  LARGEOBJECT,
  MATVIEW,
  OPCLASS,
  OPERATOR,
  OPFAMILY,
  POLICY,
  ROLE,
  RULE,
  SCHEMA,
  SEQUENCE,
  TABCONSTRAINT,
  TABLE,
  TABLESPACE,
  TRANSFORM,
  TRIGGER,
  TSCONFIGURATION,
  TSDICTIONARY,
  TSPARSER,
  TSTEMPLATE,
  TYPE,
  USER_MAPPING,
  VIEW
};

const char* ObjectTypeName(ObjectType object_type);
std::ostream& operator<<(std::ostream& out, ObjectType object_type);

}  // namespace ql
}  // namespace yb
