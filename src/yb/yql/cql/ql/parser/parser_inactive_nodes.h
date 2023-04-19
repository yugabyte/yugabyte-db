//--------------------------------------------------------------------------------------------------
// The following only applies to changes made to this file as part of YugaByte development.
//
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
// Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
// Portions Copyright (c) 1994, Regents of the University of California
//
// parser_inactive_nodes.h
//   Definitions for parse tree nodes that are not used. A lof of PostgreQL features are not
//   supported by Yugabyte, so there are unused node types.
//
// Many of the node types used in parsetrees include a "location" field. This is a byte (not
// character) offset in the original source text, to be used for positioning an error cursor when
// there is an error related to the node.  Access to the original source text is needed to make use
// of the location.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <limits.h>

namespace yb {
namespace ql {

class UndefTreeNode : public TreeNode {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types.
  typedef MCSharedPtr<UndefTreeNode> SharedPtr;
  typedef MCSharedPtr<const UndefTreeNode> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Public functions.
  UndefTreeNode() {}
  ~UndefTreeNode() {}

  // Node type.
  virtual TreeNodeOpcode opcode() const override {
    return TreeNodeOpcode::kNoOp;
  }
};

//------------------------------------------------------------------------------------------------
// NOTE: All entities below this line in this modules are copies of PostgreQL's code. We made
// some minor changes to avoid lint errors such as using '{' for if blocks, change the comment
// style from '/**/' to '//', and post-fix data members with "_".
//
// The following PostgreSQL C-sytle structures are kept here for compilation purpose. They are used
// for SQL features that are unsupported by QL. When these features are supported, we'll
// redefined these types with C++ structures.
//--------------------------------------------------------------------------------------------------

// JoinType -
//    enums for types of relation joins
//
// JoinType determines the exact semantics of joining two relations using
// a matching qualification.  For example, it tells what to do with a tuple
// that has no match in the other relation.
//
// This is needed in both parsenodes.h and plannodes.h, so put it here...
typedef enum JoinType : int {
  // The canonical kinds of joins according to the SQL JOIN syntax. Only
  // these codes can appear in parser output (e.g., JoinExpr nodes).
  JOIN_INNER,         /* matching tuple pairs only */
  JOIN_LEFT,          /* pairs + unmatched LHS tuples */
  JOIN_FULL,          /* pairs + unmatched LHS + unmatched RHS */
  JOIN_RIGHT,         /* pairs + unmatched RHS tuples */

  // Semijoins and anti-semijoins (as defined in relational theory) do not
  // appear in the SQL JOIN syntax, but there are standard idioms for
  // representing them (e.g., using EXISTS).  The planner recognizes these
  // cases and converts them to joins.  So the planner and executor must
  // support these codes.  NOTE: in JOIN_SEMI output, it is unspecified
  // which matching RHS row is joined to.  In JOIN_ANTI output, the row is
  // guaranteed to be null-extended.
  JOIN_SEMI,          /* 1 copy of each LHS row that has match(es) */
  JOIN_ANTI,          /* 1 copy of each LHS row that has no match */

  // These codes are used internally in the planner, but are not supported
  // by the executor (nor, indeed, by most of the planner).
  JOIN_UNIQUE_OUTER,      /* LHS path must be made unique */
  JOIN_UNIQUE_INNER     /* RHS path must be made unique */

  // We might need additional join types someday.
} JoinType;

typedef enum FunctionParameterMode : int {
  // the assigned enum values appear in pg_proc, don't change 'em!.
  FUNC_PARAM_IN = 'i',    /* input only */
  FUNC_PARAM_OUT = 'o',   /* output only */
  FUNC_PARAM_INOUT = 'b',   /* both */
  FUNC_PARAM_VARIADIC = 'v',  /* variadic (always input) */
  FUNC_PARAM_TABLE = 't'    /* table function output column */
} FunctionParameterMode;

// What to do at commit time for temporary relations.
typedef enum OnCommitAction : int {
  ONCOMMIT_NOOP,        /* No ON COMMIT clause (do nothing) */
  ONCOMMIT_PRESERVE_ROWS,   /* ON COMMIT PRESERVE ROWS (do nothing) */
  ONCOMMIT_DELETE_ROWS,   /* ON COMMIT DELETE ROWS */
  ONCOMMIT_DROP       /* ON COMMIT DROP */
} OnCommitAction;

// TableLikeClause - CREATE TABLE ( ... LIKE ... ) clause
typedef enum TableLikeOption : int {
  CREATE_TABLE_LIKE_DEFAULTS = 1 << 0,
  CREATE_TABLE_LIKE_CONSTRAINTS = 1 << 1,
  CREATE_TABLE_LIKE_INDEXES = 1 << 2,
  CREATE_TABLE_LIKE_STORAGE = 1 << 3,
  CREATE_TABLE_LIKE_COMMENTS = 1 << 4,
  CREATE_TABLE_LIKE_ALL = INT_MAX
} TableLikeOption;

// Foreign key action codes.
#define FKCONSTR_ACTION_NOACTION    'a'
#define FKCONSTR_ACTION_RESTRICT    'r'
#define FKCONSTR_ACTION_CASCADE     'c'
#define FKCONSTR_ACTION_SETNULL     'n'
#define FKCONSTR_ACTION_SETDEFAULT  'd'

// Foreign key matchtype codes.
#define FKCONSTR_MATCH_FULL         'f'
#define FKCONSTR_MATCH_PARTIAL      'p'
#define FKCONSTR_MATCH_SIMPLE       's'

typedef enum GrantObjectType : int {
  ACL_OBJECT_COLUMN,      /* column */
  ACL_OBJECT_RELATION,    /* table, view */
  ACL_OBJECT_SEQUENCE,    /* sequence */
  ACL_OBJECT_DATABASE,    /* database */
  ACL_OBJECT_DOMAIN,      /* domain */
  ACL_OBJECT_FDW,       /* foreign-data wrapper */
  ACL_OBJECT_FOREIGN_SERVER,  /* foreign server */
  ACL_OBJECT_FUNCTION,    /* function */
  ACL_OBJECT_LANGUAGE,    /* procedural language */
  ACL_OBJECT_LARGEOBJECT,   /* largeobject */
  ACL_OBJECT_NAMESPACE,   /* namespace */
  ACL_OBJECT_TABLESPACE,    /* tablespace */
  ACL_OBJECT_TYPE       /* type */
} GrantObjectType;

/* Sort ordering options for ORDER BY and CREATE INDEX */
typedef enum SortByDir : int {
  SORTBY_DEFAULT,
  SORTBY_ASC,
  SORTBY_DESC,
  SORTBY_USING        /* not allowed in CREATE INDEX ... */
} SortByDir;

typedef enum SortByNulls : int {
  SORTBY_NULLS_DEFAULT,
  SORTBY_NULLS_FIRST,
  SORTBY_NULLS_LAST
} SortByNulls;

// Reindex options.
#define REINDEXOPT_VERBOSE 1 << 0   /* print progress info */

typedef enum ReindexObjectType : int {
  REINDEX_OBJECT_INDEX,   /* index */
  REINDEX_OBJECT_TABLE,   /* table or materialized view */
  REINDEX_OBJECT_SCHEMA,    /* schema */
  REINDEX_OBJECT_SYSTEM,    /* system catalogs */
  REINDEX_OBJECT_DATABASE   /* database */
} ReindexObjectType;

// CmdType -
//    enums for type of operation represented by a Query or PlannedStmt
//
// This is needed in both parsenodes.h and plannodes.h, so put it here...
typedef enum CmdType : int {
  CMD_UNKNOWN,
  CMD_SELECT,         /* select stmt */
  CMD_UPDATE,         /* update stmt */
  CMD_INSERT,         /* insert stmt */
  CMD_DELETE,
  CMD_UTILITY,        /* cmds like create, destroy, copy, vacuum, etc. */
  CMD_NOTHING         /* dummy command for instead nothing rules with qual */
} CmdType;

//    Create View Statement
typedef enum ViewCheckOption : int {
  NO_CHECK_OPTION,
  LOCAL_CHECK_OPTION,
  CASCADED_CHECK_OPTION
} ViewCheckOption;

//    Vacuum and Analyze Statements
//
// Even though these are nominally two statements, it's convenient to use
// just one node type for both.  Note that at least one of VACOPT_VACUUM
// and VACOPT_ANALYZE must be set in options.
typedef enum VacuumOption : int {
  VACOPT_VACUUM = 1 << 0,   /* do VACUUM */
  VACOPT_ANALYZE = 1 << 1,  /* do ANALYZE */
  VACOPT_VERBOSE = 1 << 2,  /* print progress info */
  VACOPT_FREEZE = 1 << 3,   /* FREEZE option */
  VACOPT_FULL = 1 << 4,   /* FULL (non-concurrent) vacuum */
  VACOPT_NOWAIT = 1 << 5,   /* don't wait to get lock (autovacuum only) */
  VACOPT_SKIPTOAST = 1 << 6 /* don't process the TOAST table, if any */
} VacuumOption;

// Declare Cursor Statement
//
// Note: the "query" field of DeclareCursorStmt is only used in the raw grammar
// output.  After parse analysis it's set to null, and the Query points to the
// DeclareCursorStmt, not vice versa.
#define CURSOR_OPT_BINARY   0x0001  /* BINARY */
#define CURSOR_OPT_SCROLL   0x0002  /* SCROLL explicitly given */
#define CURSOR_OPT_NO_SCROLL  0x0004  /* NO SCROLL explicitly given */
#define CURSOR_OPT_INSENSITIVE  0x0008  /* INSENSITIVE */
#define CURSOR_OPT_HOLD     0x0010  /* WITH HOLD */
/* these planner-control flags do not correspond to any SQL grammar: */
#define CURSOR_OPT_FAST_PLAN  0x0020  /* prefer fast-start plan */
#define CURSOR_OPT_GENERIC_PLAN 0x0040  /* force use of generic plan */
#define CURSOR_OPT_CUSTOM_PLAN  0x0080  /* force use of custom plan */

// Import Foreign Schema Statement
typedef enum ImportForeignSchemaType : int {
  FDW_IMPORT_SCHEMA_ALL,    /* all relations wanted */
  FDW_IMPORT_SCHEMA_LIMIT_TO, /* include only listed tables in import */
  FDW_IMPORT_SCHEMA_EXCEPT  /* exclude listed tables from import */
} ImportForeignSchemaType;

}  // namespace ql
}  // namespace yb
