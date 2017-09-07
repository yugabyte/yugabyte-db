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

#ifndef YB_SQL_PTREE_PT_OPTION_H_
#define YB_SQL_PTREE_PT_OPTION_H_

namespace yb {
namespace sql {

// Drop option.
typedef enum DropBehavior : int {
  DROP_RESTRICT,        /* drop fails if any dependent objects */
  DROP_CASCADE        /* remove dependent objects too */
} DropBehavior;

// When a command can act on several kinds of objects with only one
// parse structure required, use these constants to designate the
// object type.  Note that commands typically don't support all the types.
typedef enum ObjectType : int {
  OBJECT_AGGREGATE,
  OBJECT_AMOP,
  OBJECT_AMPROC,
  OBJECT_ATTRIBUTE,     /* type's attribute, when distinct from column */
  OBJECT_CAST,
  OBJECT_COLUMN,
  OBJECT_COLLATION,
  OBJECT_CONVERSION,
  OBJECT_DATABASE,
  OBJECT_DEFAULT,
  OBJECT_DEFACL,
  OBJECT_DOMAIN,
  OBJECT_DOMCONSTRAINT,
  OBJECT_EVENT_TRIGGER,
  OBJECT_EXTENSION,
  OBJECT_FDW,
  OBJECT_FOREIGN_SERVER,
  OBJECT_FOREIGN_TABLE,
  OBJECT_FUNCTION,
  OBJECT_INDEX,
  OBJECT_LANGUAGE,
  OBJECT_LARGEOBJECT,
  OBJECT_MATVIEW,
  OBJECT_OPCLASS,
  OBJECT_OPERATOR,
  OBJECT_OPFAMILY,
  OBJECT_POLICY,
  OBJECT_ROLE,
  OBJECT_RULE,
  OBJECT_SCHEMA,
  OBJECT_SEQUENCE,
  OBJECT_TABCONSTRAINT,
  OBJECT_TABLE,
  OBJECT_TABLESPACE,
  OBJECT_TRANSFORM,
  OBJECT_TRIGGER,
  OBJECT_TSCONFIGURATION,
  OBJECT_TSDICTIONARY,
  OBJECT_TSPARSER,
  OBJECT_TSTEMPLATE,
  OBJECT_TYPE,
  OBJECT_USER_MAPPING,
  OBJECT_VIEW
} ObjectType;

}  // namespace sql
}  // namespace yb

#endif // YB_SQL_PTREE_PT_OPTION_H_
