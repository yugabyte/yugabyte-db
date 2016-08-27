%{
//--------------------------------------------------------------------------------------------------
// Portions Copyright (c) YugaByte, Inc.
// Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
// Portions Copyright (c) 1994, Regents of the University of California
//
// POSTGRESQL BISON rules/actions
//
// NOTES
//    CAPITALS are used to represent terminal symbols.
//    non-capitals are used to represent non-terminals.
//
//    In general, nothing in this file should initiate database accesses
//    nor depend on changeable state (such as SET variables).  If you do
//    database accesses, your code will fail when we have aborted the
//    current transaction and are just parsing commands to find the next
//    ROLLBACK or COMMIT.  If you make use of SET variables, then you
//    will do the wrong thing in multi-query strings like this:
//      SET SQL_inheritance TO off; SELECT * FROM foo;
//    because the entire string is parsed by gram.y before the SET gets
//    executed.  Anything that depends on the database or changeable state
//    should be handled during parse analysis so that it happens at the
//    right time not the wrong time.  The handling of SQL_inheritance is
//    a good example.
//--------------------------------------------------------------------------------------------------
%}

//--------------------------------------------------------------------------------------------------
// NOTE: All parsing rules are copies of PostgreSql's code. Currently, we left all processing code
// out. After parse tree and nodes are defined, they should be created while processing the rules.
//--------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------
// BISON options.
// The parsing context.
%skeleton "lalr1.cc" /* -*- C++ -*- */
%require "3.0.2"

// Debugging options. These should be deleted after coding is completed.
%debug
%define parse.error verbose
%define parse.assert

// BISON options.
%defines                                                                  // Generate header files.

// Define class for YACC.
%define api.namespace {yb::sql}
%define parser_class_name {GramProcessor}

// First parameter for yylex and yyparse.
%parse-param { Parser *parser_ }

// Define token.
%define api.value.type variant
%define api.token.constructor
%define api.token.prefix {TOK_}

//--------------------------------------------------------------------------------------------------
// The following code block goes into YACC generated *.hh header file.
%code requires {
#include <stdbool.h>

#include <list>

#include "yb/sql/parser/parse_tree.h"
#include "yb/sql/parser/parser_inactive_nodes.h"
#include "yb/gutil/macros.h"

namespace yb {
namespace sql {

class Parser;

// Parsing types.
// Use shared_ptr here because YACC does not know how to use move() instead of "=" for pointers.
typedef TreeNode                      *NodeType;
typedef int64_t                        Int64Type;
typedef bool                           BoolType;
typedef char                           CharType;
typedef const char                    *KeywordType;
typedef char                          *StringType;

// Inactive parsing node types.
typedef UndefTreeNode                 *UndefType;
typedef void                          *list;
typedef JoinType                       jtype;
typedef DropBehavior                   dbehavior;
typedef ObjectType                     objtype;
typedef FunctionParameterMode          fun_param_mode;
typedef OnCommitAction                 oncommit;

} // namespace sql.
} // namespace yb.

}

//--------------------------------------------------------------------------------------------------
// The code in the rest of this file go into YACC generated *.cc source file.
%code {
#include <stdio.h>
#include <string.h>

#include "yb/sql/parser/parse_tree.h"
#include "yb/sql/parser/parser.h"
#include "yb/sql/parser/scanner_util.h"

using namespace std;
using namespace yb::sql;

#undef yylex
#define yylex parser_->Scan

#define parser_strdup(x) parser_->PTreeMem()->Strdup(x)
}

//--------------------------------------------------------------------------------------------------
// Active tree node declarations (%type).
// The rule names are currently used by YbSql.
%type <NodeType>          stmtblock stmtmulti stmt CreateStmt schema_stmt

%type <KeywordType>       unreserved_keyword type_func_name_keyword
%type <KeywordType>       col_name_keyword reserved_keyword

//--------------------------------------------------------------------------------------------------
// Inactive tree node declarations (%type).
// The rule names are currently not used.

%type <KeywordType>       iso_level opt_encoding opt_boolean_or_string row_security_cmd
                          RowSecurityDefaultForCmd explain_option_name all_Op MathOp extract_arg

%type <CharType>          enable_trigger

%type <StringType>        createdb_opt_name RoleId opt_type foreign_server_version
                          opt_foreign_server_version opt_in_database OptSchemaName copy_file_name
                          database_name access_method_clause access_method attr_name name
                          cursor_name file_name index_name opt_index_name
                          cluster_index_specification generic_option_name character opt_charset
                          Sconst comment_text notify_payload ColId ColLabel var_name
                          type_function_name param_name NonReservedWord NonReservedWord_or_Sconst
                          ExistingIndex OptTableSpace OptConsTableSpace opt_provider
                          security_label opt_existing_window_name

%type <BoolType>          opt_if_not_exists xml_whitespace_option constraints_set_mode opt_varying
                          opt_timezone opt_no_inherit opt_ordinality opt_instead opt_unique
                          opt_concurrently opt_verbose opt_full opt_freeze opt_default opt_recheck
                          copy_from opt_program all_or_distinct opt_trusted opt_restart_seqs
                          opt_or_replace opt_grant_grant_option opt_grant_admin_option opt_nowait
                          opt_if_exists opt_with_data TriggerForSpec TriggerForType

%type <Int64Type>         TableLikeOptionList TableLikeOption key_actions key_delete key_match
                          key_update key_action ConstraintAttributeSpec ConstraintAttributeElem
                          opt_check_option document_or_content reindex_target_type
                          reindex_target_multitable reindex_option_list reindex_option_elem Iconst
                          SignedIconst sub_type opt_column event cursor_options opt_hold
                          opt_set_data row_or_rows first_or_next OptTemp OptNoLog
                          for_locking_strength defacl_privilege_target import_qualification_type
                          opt_lock lock_type cast_context vacuum_option_list vacuum_option_elem
                          opt_nowait_or_skip TriggerActionTime add_drop opt_asc_desc opt_nulls_order

%type <jtype>             join_type
%type <dbehavior>         opt_drop_behavior
%type <objtype>           drop_type comment_type security_label_type
%type <fun_param_mode>    arg_class
%type <oncommit>          OnCommitOption

%type <UndefType>         inactive_stmt inactive_schema_stmt
                          AlterEventTrigStmt AlterDatabaseStmt AlterDatabaseSetStmt AlterDomainStmt
                          AlterEnumStmt AlterFdwStmt AlterForeignServerStmt AlterObjectSchemaStmt
                          AlterOwnerStmt AlterSeqStmt AlterSystemStmt AlterTableStmt
                          AlterTblSpcStmt AlterExtensionStmt AlterExtensionContentsStmt
                          AlterForeignTableStmt AlterCompositeTypeStmt AlterUserStmt
                          AlterUserMappingStmt AlterUserSetStmt AlterRoleStmt AlterRoleSetStmt
                          AlterPolicyStmt AlterDefaultPrivilegesStmt
                          DefACLAction AnalyzeStmt ClosePortalStmt ClusterStmt CommentStmt
                          ConstraintsSetStmt CopyStmt CreateAsStmt CreateCastStmt
                          CreateDomainStmt CreateExtensionStmt CreateOpClassStmt
                          CreateOpFamilyStmt AlterOpFamilyStmt CreatePLangStmt
                          CreateSchemaStmt CreateSeqStmt CreateTableSpaceStmt
                          CreateFdwStmt CreateForeignServerStmt CreateForeignTableStmt
                          CreateAssertStmt CreateTransformStmt CreateTrigStmt CreateEventTrigStmt
                          CreateUserStmt CreateUserMappingStmt CreateRoleStmt CreatePolicyStmt
                          CreatedbStmt DeclareCursorStmt DefineStmt DeleteStmt DiscardStmt DoStmt
                          DropOpClassStmt DropOpFamilyStmt DropPLangStmt DropStmt
                          DropAssertStmt DropTrigStmt DropRuleStmt DropCastStmt DropRoleStmt
                          DropPolicyStmt DropUserStmt DropdbStmt DropTableSpaceStmt DropFdwStmt
                          DropTransformStmt
                          DropForeignServerStmt DropUserMappingStmt ExplainStmt FetchStmt
                          GrantStmt GrantRoleStmt ImportForeignSchemaStmt IndexStmt InsertStmt
                          ListenStmt LoadStmt LockStmt NotifyStmt ExplainableStmt PreparableStmt
                          CreateFunctionStmt AlterFunctionStmt ReindexStmt RemoveAggrStmt
                          RemoveFuncStmt RemoveOperStmt RenameStmt RevokeStmt RevokeRoleStmt
                          RuleActionStmt RuleActionStmtOrEmpty RuleStmt
                          SecLabelStmt SelectStmt TransactionStmt TruncateStmt
                          UnlistenStmt UpdateStmt VacuumStmt
                          VariableResetStmt VariableSetStmt VariableShowStmt
                          ViewStmt CheckPointStmt CreateConversionStmt
                          DeallocateStmt PrepareStmt ExecuteStmt
                          DropOwnedStmt ReassignOwnedStmt
                          AlterTSConfigurationStmt AlterTSDictionaryStmt
                          CreateMatViewStmt RefreshMatViewStmt
                          select_no_parens select_with_parens select_clause simple_select
                          values_clause
                          alter_column_default opclass_item opclass_drop alter_using
                          alter_table_cmd alter_type_cmd opt_collate_clause replica_identity
                          createdb_opt_item copy_opt_item transaction_mode_item
                          create_extension_opt_item alter_extension_opt_item
                          CreateOptRoleElem AlterOptRoleElem
                          TriggerFuncArg
                          TriggerWhen
                          event_trigger_when_item
                          qualified_name insert_target OptConstrFromTable
                          RowSecurityOptionalWithCheck RowSecurityOptionalExpr
                          grantee
                          privilege
                          privilege_target
                          function_with_argtypes
                          DefACLOption
                          import_qualification
                          group_by_item empty_grouping_set rollup_clause cube_clause
                          grouping_sets_clause
                          fdw_option
                          OptTempTableName
                          into_clause create_as_target create_mv_target
                          createfunc_opt_item common_func_opt_item dostmt_opt_item
                          func_arg func_arg_with_default table_func_column aggr_arg
                          func_return func_type
                          for_locking_item
                          join_outer join_qual
                          overlay_placing substr_from substr_for
                          opt_binary opt_oids copy_delimiter
                          fetch_args limit_clause select_limit_value
                          offset_clause select_offset_value
                          select_offset_value2 opt_select_fetch_first_value
                          SeqOptElem
                          insert_rest
                          opt_conf_expr
                          opt_on_conflict
                          generic_set set_rest set_rest_more generic_reset reset_rest
                          SetResetClause FunctionSetResetClause
                          TableElement TypedTableElement ConstraintElem TableFuncElement
                          columnDef columnOptions
                          def_elem reloption_elem old_aggr_elem
                          def_arg columnElem where_clause where_or_current_clause
                          a_expr b_expr c_expr AexprConst indirection_el
                          columnref in_expr having_clause func_table array_expr
                          ExclusionWhereClause
                          func_arg_expr
                          case_expr case_arg when_clause case_default
                          ctext_expr
                          NumericOnly
                          alias_clause opt_alias_clause
                          sortby
                          index_elem
                          table_ref
                          joined_table
                          relation_expr
                          relation_expr_opt_alias
                          tablesample_clause opt_repeatable_clause
                          target_el single_set_clause set_target insert_column_item
                          generic_option_arg
                          generic_option_elem alter_generic_option_elem
                          explain_option_arg
                          explain_option_elem
                          copy_generic_opt_arg copy_generic_opt_arg_list_item
                          copy_generic_opt_elem
                          Typename SimpleTypename ConstTypename
                          GenericType Numeric opt_float
                          Character ConstCharacter
                          CharacterWithLength CharacterWithoutLength
                          ConstDatetime ConstInterval
                          Bit ConstBit BitWithLength BitWithoutLength
                          var_value zone_value
                          auth_ident RoleSpec opt_granted_by
                          TableConstraint TableLikeClause
                          ColConstraint ColConstraintElem ConstraintAttr
                          OptTableSpaceOwner
                          xml_attribute_el
                          xml_root_version opt_xml_root_standalone
                          xmlexists_argument
                          func_application func_expr_common_subexpr
                          func_expr func_expr_windowless
                          common_table_expr
                          with_clause opt_with_clause
                          filter_clause
                          window_definition over_clause window_specification
                          opt_frame_clause frame_extent frame_bound

%type <list>              alter_table_cmds alter_type_cmds createdb_opt_list createdb_opt_items
                          copy_opt_list transaction_mode_list create_extension_opt_list
                          alter_extension_opt_list OptRoleList AlterOptRoleList OptSchemaEltList
                          TriggerEvents TriggerOneEvent event_trigger_when_list
                          event_trigger_value_list func_name handler_name qual_Op qual_all_Op
                          subquery_Op opt_class opt_inline_handler opt_validator validator_clause
                          opt_collate RowSecurityDefaultToRole RowSecurityOptionalToRole
                          grantee_list privileges privilege_list function_with_argtypes_list
                          OptTableElementList TableElementList OptInherit definition
                          OptTypedTableElementList TypedTableElementList reloptions opt_reloptions
                          OptWith distinct_clause opt_all_clause opt_definition func_args
                          func_args_list func_args_with_defaults func_args_with_defaults_list
                          aggr_args aggr_args_list func_as createfunc_opt_list alterfunc_opt_list
                          old_aggr_definition old_aggr_list oper_argtypes RuleActionList
                          RuleActionMulti opt_column_list columnList opt_name_list sort_clause
                          opt_sort_clause sortby_list index_params name_list role_list from_clause
                          from_list opt_array_bounds qualified_name_list any_name any_name_list
                          type_name_list any_operator expr_list attrs target_list opt_target_list
                          insert_column_list set_target_list set_clause_list set_clause
                          multiple_set_clause ctext_expr_list ctext_row def_list indirection
                          opt_indirection reloption_list group_clause TriggerFuncArgs select_limit
                          opt_select_limit opclass_item_list opclass_drop_list opclass_purpose
                          opt_opfamily transaction_mode_list_or_empty OptTableFuncElementList
                          TableFuncElementList opt_type_modifiers prep_type_clause
                          execute_param_clause using_clause returning_clause opt_enum_val_list
                          enum_val_list table_func_column_list create_generic_options
                          alter_generic_options relation_expr_list dostmt_opt_list
                          transform_element_list transform_type_list group_by_list opt_fdw_options
                          fdw_options for_locking_clause opt_for_locking_clause for_locking_items
                          locked_rels_list extract_list overlay_list position_list substr_list
                          trim_list opt_interval interval_second OptSeqOptList SeqOptList
                          rowsfrom_item rowsfrom_list opt_col_def_list ExclusionConstraintList
                          ExclusionConstraintElem func_arg_list row explicit_row implicit_row
                          type_list array_expr_list when_clause_list NumericOnly_list
                          func_alias_clause generic_option_list alter_generic_option_list
                          explain_option_list copy_generic_opt_list copy_generic_opt_arg_list
                          copy_options ColQualList constraints_set_list xml_attribute_list
                          xml_attributes cte_list within_group_clause window_clause
                          window_definition_list opt_partition_clause DefACLOptionList var_list

//--------------------------------------------------------------------------------------------------
// Token definitions (%token).
// Keyword: If you want to make any keyword changes, update the keyword table in
//   src/include/parser/kwlist.h
// and add new keywords to the appropriate one of the reserved-or-not-so-reserved keyword lists.
// Search for "Keyword category lists".

// Declarations for ordinary keywords in alphabetical order.
%token <KeywordType>      ABORT_P ABSOLUTE_P ACCESS ACTION ADD_P ADMIN AFTER  AGGREGATE ALL
                          ALSO ALTER ALWAYS ANALYSE ANALYZE AND ANY ARRAY AS ASC ASSERTION
                          ASSIGNMENT ASYMMETRIC AT ATTRIBUTE AUTHORIZATION

                          BACKWARD BEFORE BEGIN_P BETWEEN BIGINT BINARY BIT BOOLEAN_P BOTH BY

                          CACHE CALLED CASCADE CASCADED CASE CAST CATALOG_P CHAIN CHAR_P
                          CHARACTER CHARACTERISTICS CHECK CHECKPOINT CLASS CLOSE CLUSTER
                          COALESCE COLLATE COLLATION COLUMN COMMENT COMMENTS COMMIT COMMITTED
                          CONCURRENTLY CONFIGURATION CONFLICT CONNECTION CONSTRAINT CONSTRAINTS
                          CONTENT_P CONTINUE_P CONVERSION_P COPY COST CREATE CROSS CSV CUBE
                          CURRENT_P CURRENT_CATALOG CURRENT_DATE CURRENT_ROLE CURRENT_SCHEMA
                          CURRENT_TIME CURRENT_TIMESTAMP CURRENT_USER CURSOR CYCLE

                          DATA_P DATABASE DAY_P DEALLOCATE DEC DECIMAL_P DECLARE DEFAULT
                          DEFAULTS DEFERRABLE DEFERRED DEFINER DELETE_P DELIMITER DELIMITERS
                          DESC DICTIONARY DISABLE_P DISCARD DISTINCT DO DOCUMENT_P DOMAIN_P
                          DOUBLE_P DROP

                          EACH ELSE ENABLE_P ENCODING ENCRYPTED END_P ENUM_P ESCAPE EVENT
                          EXCEPT EXCLUDE EXCLUDING EXCLUSIVE EXECUTE EXISTS EXPLAIN EXTENSION
                          EXTERNAL EXTRACT

                          FALSE_P FAMILY FETCH FILTER FIRST_P FLOAT_P FOLLOWING FOR FORCE
                          FOREIGN FORWARD FREEZE FROM FULL FUNCTION FUNCTIONS

                          GLOBAL GRANT GRANTED GREATEST GROUP_P GROUPING

                          HANDLER HAVING HEADER_P HOLD HOUR_P

                          IDENTITY_P IF_P ILIKE IMMEDIATE IMMUTABLE IMPLICIT_P IMPORT_P IN_P
                          INCLUDING INCREMENT INDEX INDEXES INHERIT INHERITS INITIALLY INLINE_P
                          INNER_P INOUT INPUT_P INSENSITIVE INSERT INSTEAD INT_P INTEGER
                          INTERSECT INTERVAL INTO INVOKER IS ISNULL ISOLATION

                          JOIN

                          KEY

                          LABEL LANGUAGE LARGE_P LAST_P LATERAL_P LEADING LEAKPROOF LEAST LEFT
                          LEVEL LIKE LIMIT LISTEN LOAD LOCAL LOCALTIME LOCALTIMESTAMP LOCATION
                          LOCK_P LOCKED LOGGED

                          MAPPING MATCH MATERIALIZED MAXVALUE MINUTE_P MINVALUE MODE MONTH_P
                          MOVE

                          NAME_P NAMES NATIONAL NATURAL NCHAR NEXT NO NONE NOT NOTHING NOTIFY
                          NOTNULL NOWAIT NULL_P NULLIF NULLS_P NUMERIC

                          OBJECT_P OF OFF OFFSET OIDS ON ONLY OPERATOR OPTION OPTIONS OR ORDER
                          ORDINALITY OUT_P OUTER_P OVER OVERLAPS OVERLAY OWNED OWNER

                          PARSER PARTIAL PARTITION PASSING PASSWORD PLACING PLANS POLICY
                          POSITION PRECEDING PRECISION PRESERVE PREPARE PREPARED PRIMARY PRIOR
                          PRIVILEGES PROCEDURAL PROCEDURE PROGRAM

                          QUOTE

                          RANGE READ REAL REASSIGN RECHECK RECURSIVE REF REFERENCES REFRESH
                          REINDEX RELATIVE_P RELEASE RENAME REPEATABLE REPLACE REPLICA RESET
                          RESTART RESTRICT RETURNING RETURNS REVOKE RIGHT ROLE ROLLBACK ROLLUP
                          ROW ROWS RULE

                          SAVEPOINT SCHEMA SCROLL SEARCH SECOND_P SECURITY SELECT SEQUENCE
                          SEQUENCES SERIALIZABLE SERVER SESSION SESSION_USER SET SETS SETOF
                          SHARE SHOW SIMILAR SIMPLE SKIP SMALLINT SNAPSHOT SOME SQL_P STABLE
                          STANDALONE_P START STATEMENT STATISTICS STDIN STDOUT STORAGE
                          STRICT_P STRIP_P SUBSTRING SYMMETRIC SYSID SYSTEM_P

                          TABLE TABLES TABLESAMPLE TABLESPACE TEMP TEMPLATE TEMPORARY TEXT_P
                          THEN TIME TIMESTAMP TO TRAILING TRANSACTION TRANSFORM TREAT TRIGGER
                          TRIM TRUE_P TRUNCATE TRUSTED TYPE_P TYPES_P

                          UNBOUNDED UNCOMMITTED UNENCRYPTED UNION UNIQUE UNKNOWN UNLISTEN
                          UNLOGGED UNTIL UPDATE USER USING

                          VACUUM VALID VALIDATE VALIDATOR VALUE_P VALUES VARCHAR VARIADIC
                          VARYING VERBOSE VERSION_P VIEW VIEWS VOLATILE

                          WHEN WHERE WHITESPACE_P WINDOW WITH WITHIN WITHOUT WORK WRAPPER WRITE

                          XML_P XMLATTRIBUTES XMLCONCAT XMLELEMENT XMLEXISTS XMLFOREST
                          XMLPARSE XMLPI XMLROOT XMLSERIALIZE

                          YEAR_P YES_P

                          ZONE

// Identifer.
%token <StringType>       IDENT

// Bind parameters: "$1", "$2", "$3", etc.
%token <Int64Type>        PARAM

// Float constants.
%token <StringType>       FCONST SCONST BCONST XCONST Op

// Integer constants.
%token <Int64Type>        ICONST

// Integer constants.
%token <CharType>         CCONST

// Multichar operators: "::", "..", ":=", "=>", "<=", ">=", "<>", "!=".
%token                    TYPECAST DOT_DOT COLON_EQUALS EQUALS_GREATER LESS_EQUALS
                          GREATER_EQUALS NOT_EQUALS

// Special ops. The grammar treat them as keywords, but they are not in the kwlist.h list and so can
// never be entered directly.  The filter in parser.c creates these tokens when required (based on
// looking one token ahead). NOT_LA exists so that productions such as NOT LIKE can be given the
// same precedence as LIKE; otherwise they'd effectively have the same precedence as NOT, at least
// with respect to their left-hand subexpression. NULLS_LA and WITH_LA are needed to make the
// grammar LALR(1).
%token                    NOT_LA NULLS_LA WITH_LA

%token END                0 "end of file";

//--------------------------------------------------------------------------------------------------
// Precedence: lowest to highest.
%nonassoc   SET                                                      // see relation_expr_opt_alias.
%left       UNION EXCEPT
%left       INTERSECT
%left       OR
%left       AND
%right      NOT
%nonassoc   IS ISNULL NOTNULL                                // IS sets precedence for IS NULL, etc.
%nonassoc   '<' '>' '=' LESS_EQUALS GREATER_EQUALS NOT_EQUALS
%nonassoc   BETWEEN IN_P LIKE ILIKE SIMILAR NOT_LA
%nonassoc   ESCAPE                                  // ESCAPE must be just above LIKE/ILIKE/SIMILAR.
%left       POSTFIXOP                                                 // dummy for postfix Op rules.

// To support target_el without AS, we must give IDENT an explicit priority
// between POSTFIXOP and Op.  We can safely assign the same priority to
// various unreserved keywords as needed to resolve ambiguities (this can't
// have any bad effects since obviously the keywords will still behave the
// same as if they weren't keywords).  We need to do this for PARTITION,
// RANGE, ROWS to support opt_existing_window_name; and for RANGE, ROWS
// so that they can follow a_expr without creating postfix-operator problems;
// and for NULL so that it can follow b_expr in ColQualList without creating
// postfix-operator problems.
//
// To support CUBE and ROLLUP in GROUP BY without reserving them, we give them
// an explicit priority lower than '(', so that a rule with CUBE '(' will shift
// rather than reducing a conflicting rule that takes CUBE as a function name.
// Using the same precedence as IDENT seems right for the reasons given above.
//
// The frame_bound productions UNBOUNDED PRECEDING and UNBOUNDED FOLLOWING
// are even messier: since UNBOUNDED is an unreserved keyword (per spec!),
// there is no principled way to distinguish these from the productions
// a_expr PRECEDING/FOLLOWING.  We hack this up by giving UNBOUNDED slightly
// lower precedence than PRECEDING and FOLLOWING.  At present this doesn't
// appear to cause UNBOUNDED to be treated differently from other unreserved
// keywords anywhere else in the grammar, but it's definitely risky.  We can
// blame any funny behavior of UNBOUNDED on the SQL standard, though.
%nonassoc UNBOUNDED                                  // ideally should have same precedence as IDENT
%nonassoc IDENT NULL_P PARTITION RANGE ROWS PRECEDING FOLLOWING CUBE ROLLUP
%left     Op OPERATOR                             // multi-character ops and user-defined operators
%left     '+' '-'
%left     '*' '/' '%'
%left     '^'

// Unary Operators.
%left     AT                                                     // sets precedence for AT TIME ZONE
%left     COLLATE
%right    UMINUS
%left     '[' ']'
%left     '(' ')'
%left     TYPECAST
%left     '.'

// These might seem to be low-precedence, but actually they are not part
// of the arithmetic hierarchy at all in their use as JOIN operators.
// We make them high-precedence to support their use as function names.
// They wouldn't be given a precedence at all, were it not that we need
// left-associativity among the JOIN rules themselves.
%left     JOIN CROSS LEFT FULL RIGHT INNER_P NATURAL

// kluge to keep xml_whitespace_option from causing shift/reduce conflicts.
%right    PRESERVE STRIP_P

//--------------------------------------------------------------------------------------------------
// Logging.
%printer { yyoutput << $$; } <*>;

//--------------------------------------------------------------------------------------------------
// Scanning cursor location: Keep tracking of current text location.
%locations

%%
%start stmtblock;

//--------------------------------------------------------------------------------------------------
// ACTIVE PARSING RULES.
// YbSql currently uses these PostgreSql rules.
//--------------------------------------------------------------------------------------------------
// The target production for the whole parse.
stmtblock:
  stmtmulti {
    $$ = move($1);
  }
;

// The thrashing around here is to discard "empty" statements...
stmtmulti:
  stmtmulti ';' stmt {
    $$ = $3;
  }
  | stmt {
    $$ = nullptr;
  }
;

stmt:
  CreateStmt {
    $$ = $1;
  }
  | inactive_stmt {
    // Report error that the syntax is not yet supported.
    $$ = $1;
  }
;

schema_stmt:
  CreateStmt {
    $$ = $1;
  }
  | inactive_schema_stmt {
    // Report error that the syntax is not yet supported.
    $$ = $1;
  }
;

CreateStmt:
  CREATE OptTemp TABLE qualified_name '(' OptTableElementList ')'
  OptInherit OptWith OnCommitOption OptTableSpace {
    $$ = NULL;
  }
  | CREATE OptTemp TABLE IF_P NOT EXISTS qualified_name '(' OptTableElementList ')'
  OptInherit OptWith OnCommitOption OptTableSpace {
    $$ = NULL;
  }
  | CREATE OptTemp TABLE qualified_name OF any_name
  OptTypedTableElementList OptWith OnCommitOption OptTableSpace {
    $$ = NULL;
  }
  | CREATE OptTemp TABLE IF_P NOT EXISTS qualified_name OF any_name
  OptTypedTableElementList OptWith OnCommitOption OptTableSpace {
    $$ = NULL;
  }
;

//--------------------------------------------------------------------------------------------------
// Keyword category lists.
// Every keyword present in the Postgres grammar should appear in exactly one of these lists.
// - Put a new keyword into the first list that it can go into without causing shift or reduce
//   conflicts.  The earlier lists define "less reserved" categories of keywords.
// - Make sure that each keyword's category in kwlist.h matches where it is listed here.  (Someday
//   we may be able to generate these lists and kwlist.h's table from a common master list.)
//
// NOTE:
// BISON (YACC) should apply the default action "$$ = $1;" when left and right values of a syntax
// rule are of the same datatype. Unfortunately, BISON 3.0.4 didn't do this for the case where left
// variable is a non-terminal and right hand value is a terminal. As a result, we won't rely on
// BISON default behavior.
//--------------------------------------------------------------------------------------------------

// "Unreserved" keywords --- available for use as any kind of name.
unreserved_keyword:
  ABORT_P { $$ = $1; }
  | ABSOLUTE_P { $$ = $1; }
  | ACCESS { $$ = $1; }
  | ACTION { $$ = $1; }
  | ADD_P { $$ = $1; }
  | ADMIN { $$ = $1; }
  | AFTER { $$ = $1; }
  | AGGREGATE { $$ = $1; }
  | ALSO { $$ = $1; }
  | ALTER { $$ = $1; }
  | ALWAYS { $$ = $1; }
  | ASSERTION { $$ = $1; }
  | ASSIGNMENT { $$ = $1; }
  | AT { $$ = $1; }
  | ATTRIBUTE { $$ = $1; }
  | BACKWARD { $$ = $1; }
  | BEFORE { $$ = $1; }
  | BEGIN_P { $$ = $1; }
  | BY { $$ = $1; }
  | CACHE { $$ = $1; }
  | CALLED { $$ = $1; }
  | CASCADE { $$ = $1; }
  | CASCADED { $$ = $1; }
  | CATALOG_P { $$ = $1; }
  | CHAIN { $$ = $1; }
  | CHARACTERISTICS { $$ = $1; }
  | CHECKPOINT { $$ = $1; }
  | CLASS { $$ = $1; }
  | CLOSE { $$ = $1; }
  | CLUSTER { $$ = $1; }
  | COMMENT { $$ = $1; }
  | COMMENTS { $$ = $1; }
  | COMMIT { $$ = $1; }
  | COMMITTED { $$ = $1; }
  | CONFIGURATION { $$ = $1; }
  | CONFLICT { $$ = $1; }
  | CONNECTION { $$ = $1; }
  | CONSTRAINTS { $$ = $1; }
  | CONTENT_P { $$ = $1; }
  | CONTINUE_P { $$ = $1; }
  | CONVERSION_P { $$ = $1; }
  | COPY { $$ = $1; }
  | COST { $$ = $1; }
  | CSV { $$ = $1; }
  | CUBE { $$ = $1; }
  | CURRENT_P { $$ = $1; }
  | CURSOR { $$ = $1; }
  | CYCLE { $$ = $1; }
  | DATA_P { $$ = $1; }
  | DATABASE { $$ = $1; }
  | DAY_P { $$ = $1; }
  | DEALLOCATE { $$ = $1; }
  | DECLARE { $$ = $1; }
  | DEFAULTS { $$ = $1; }
  | DEFERRED { $$ = $1; }
  | DEFINER { $$ = $1; }
  | DELETE_P { $$ = $1; }
  | DELIMITER { $$ = $1; }
  | DELIMITERS { $$ = $1; }
  | DICTIONARY { $$ = $1; }
  | DISABLE_P { $$ = $1; }
  | DISCARD { $$ = $1; }
  | DOCUMENT_P { $$ = $1; }
  | DOMAIN_P { $$ = $1; }
  | DOUBLE_P { $$ = $1; }
  | DROP { $$ = $1; }
  | EACH { $$ = $1; }
  | ENABLE_P { $$ = $1; }
  | ENCODING { $$ = $1; }
  | ENCRYPTED { $$ = $1; }
  | ENUM_P { $$ = $1; }
  | ESCAPE { $$ = $1; }
  | EVENT { $$ = $1; }
  | EXCLUDE { $$ = $1; }
  | EXCLUDING { $$ = $1; }
  | EXCLUSIVE { $$ = $1; }
  | EXECUTE { $$ = $1; }
  | EXPLAIN { $$ = $1; }
  | EXTENSION { $$ = $1; }
  | EXTERNAL { $$ = $1; }
  | FAMILY { $$ = $1; }
  | FILTER { $$ = $1; }
  | FIRST_P { $$ = $1; }
  | FOLLOWING { $$ = $1; }
  | FORCE { $$ = $1; }
  | FORWARD { $$ = $1; }
  | FUNCTION { $$ = $1; }
  | FUNCTIONS { $$ = $1; }
  | GLOBAL { $$ = $1; }
  | GRANTED { $$ = $1; }
  | HANDLER { $$ = $1; }
  | HEADER_P { $$ = $1; }
  | HOLD { $$ = $1; }
  | HOUR_P { $$ = $1; }
  | IDENTITY_P { $$ = $1; }
  | IF_P { $$ = $1; }
  | IMMEDIATE { $$ = $1; }
  | IMMUTABLE { $$ = $1; }
  | IMPLICIT_P { $$ = $1; }
  | IMPORT_P { $$ = $1; }
  | INCLUDING { $$ = $1; }
  | INCREMENT { $$ = $1; }
  | INDEX { $$ = $1; }
  | INDEXES { $$ = $1; }
  | INHERIT { $$ = $1; }
  | INHERITS { $$ = $1; }
  | INLINE_P { $$ = $1; }
  | INPUT_P { $$ = $1; }
  | INSENSITIVE { $$ = $1; }
  | INSERT { $$ = $1; }
  | INSTEAD { $$ = $1; }
  | INVOKER { $$ = $1; }
  | ISOLATION { $$ = $1; }
  | KEY { $$ = $1; }
  | LABEL { $$ = $1; }
  | LANGUAGE { $$ = $1; }
  | LARGE_P { $$ = $1; }
  | LAST_P { $$ = $1; }
  | LEAKPROOF { $$ = $1; }
  | LEVEL { $$ = $1; }
  | LISTEN { $$ = $1; }
  | LOAD { $$ = $1; }
  | LOCAL { $$ = $1; }
  | LOCATION { $$ = $1; }
  | LOCK_P { $$ = $1; }
  | LOCKED { $$ = $1; }
  | LOGGED { $$ = $1; }
  | MAPPING { $$ = $1; }
  | MATCH { $$ = $1; }
  | MATERIALIZED { $$ = $1; }
  | MAXVALUE { $$ = $1; }
  | MINUTE_P { $$ = $1; }
  | MINVALUE { $$ = $1; }
  | MODE { $$ = $1; }
  | MONTH_P { $$ = $1; }
  | MOVE { $$ = $1; }
  | NAME_P { $$ = $1; }
  | NAMES { $$ = $1; }
  | NEXT { $$ = $1; }
  | NO { $$ = $1; }
  | NOTHING { $$ = $1; }
  | NOTIFY { $$ = $1; }
  | NOWAIT { $$ = $1; }
  | NULLS_P { $$ = $1; }
  | OBJECT_P { $$ = $1; }
  | OF { $$ = $1; }
  | OFF { $$ = $1; }
  | OIDS { $$ = $1; }
  | OPERATOR { $$ = $1; }
  | OPTION { $$ = $1; }
  | OPTIONS { $$ = $1; }
  | ORDINALITY { $$ = $1; }
  | OVER { $$ = $1; }
  | OWNED { $$ = $1; }
  | OWNER { $$ = $1; }
  | PARSER { $$ = $1; }
  | PARTIAL { $$ = $1; }
  | PARTITION { $$ = $1; }
  | PASSING { $$ = $1; }
  | PASSWORD { $$ = $1; }
  | PLANS { $$ = $1; }
  | POLICY { $$ = $1; }
  | PRECEDING { $$ = $1; }
  | PREPARE { $$ = $1; }
  | PREPARED { $$ = $1; }
  | PRESERVE { $$ = $1; }
  | PRIOR { $$ = $1; }
  | PRIVILEGES { $$ = $1; }
  | PROCEDURAL { $$ = $1; }
  | PROCEDURE { $$ = $1; }
  | PROGRAM { $$ = $1; }
  | QUOTE { $$ = $1; }
  | RANGE { $$ = $1; }
  | READ { $$ = $1; }
  | REASSIGN { $$ = $1; }
  | RECHECK { $$ = $1; }
  | RECURSIVE { $$ = $1; }
  | REF { $$ = $1; }
  | REFRESH { $$ = $1; }
  | REINDEX { $$ = $1; }
  | RELATIVE_P { $$ = $1; }
  | RELEASE { $$ = $1; }
  | RENAME { $$ = $1; }
  | REPEATABLE { $$ = $1; }
  | REPLACE { $$ = $1; }
  | REPLICA { $$ = $1; }
  | RESET { $$ = $1; }
  | RESTART { $$ = $1; }
  | RESTRICT { $$ = $1; }
  | RETURNS { $$ = $1; }
  | REVOKE { $$ = $1; }
  | ROLE { $$ = $1; }
  | ROLLBACK { $$ = $1; }
  | ROLLUP { $$ = $1; }
  | ROWS { $$ = $1; }
  | RULE { $$ = $1; }
  | SAVEPOINT { $$ = $1; }
  | SCHEMA { $$ = $1; }
  | SCROLL { $$ = $1; }
  | SEARCH { $$ = $1; }
  | SECOND_P { $$ = $1; }
  | SECURITY { $$ = $1; }
  | SEQUENCE { $$ = $1; }
  | SEQUENCES { $$ = $1; }
  | SERIALIZABLE { $$ = $1; }
  | SERVER { $$ = $1; }
  | SESSION { $$ = $1; }
  | SET { $$ = $1; }
  | SETS { $$ = $1; }
  | SHARE { $$ = $1; }
  | SHOW { $$ = $1; }
  | SIMPLE { $$ = $1; }
  | SKIP { $$ = $1; }
  | SNAPSHOT { $$ = $1; }
  | SQL_P { $$ = $1; }
  | STABLE { $$ = $1; }
  | STANDALONE_P { $$ = $1; }
  | START { $$ = $1; }
  | STATEMENT { $$ = $1; }
  | STATISTICS { $$ = $1; }
  | STDIN { $$ = $1; }
  | STDOUT { $$ = $1; }
  | STORAGE { $$ = $1; }
  | STRICT_P { $$ = $1; }
  | STRIP_P { $$ = $1; }
  | SYSID { $$ = $1; }
  | SYSTEM_P { $$ = $1; }
  | TABLES { $$ = $1; }
  | TABLESPACE { $$ = $1; }
  | TEMP { $$ = $1; }
  | TEMPLATE { $$ = $1; }
  | TEMPORARY { $$ = $1; }
  | TEXT_P { $$ = $1; }
  | TRANSACTION { $$ = $1; }
  | TRANSFORM { $$ = $1; }
  | TRIGGER { $$ = $1; }
  | TRUNCATE { $$ = $1; }
  | TRUSTED { $$ = $1; }
  | TYPE_P { $$ = $1; }
  | TYPES_P { $$ = $1; }
  | UNBOUNDED { $$ = $1; }
  | UNCOMMITTED { $$ = $1; }
  | UNENCRYPTED { $$ = $1; }
  | UNKNOWN { $$ = $1; }
  | UNLISTEN { $$ = $1; }
  | UNLOGGED { $$ = $1; }
  | UNTIL { $$ = $1; }
  | UPDATE { $$ = $1; }
  | VACUUM { $$ = $1; }
  | VALID { $$ = $1; }
  | VALIDATE { $$ = $1; }
  | VALIDATOR { $$ = $1; }
  | VALUE_P { $$ = $1; }
  | VARYING { $$ = $1; }
  | VERSION_P { $$ = $1; }
  | VIEW { $$ = $1; }
  | VIEWS { $$ = $1; }
  | VOLATILE { $$ = $1; }
  | WHITESPACE_P { $$ = $1; }
  | WITHIN { $$ = $1; }
  | WITHOUT { $$ = $1; }
  | WORK { $$ = $1; }
  | WRAPPER { $$ = $1; }
  | WRITE { $$ = $1; }
  | XML_P { $$ = $1; }
  | YEAR_P { $$ = $1; }
  | YES_P { $$ = $1; }
  | ZONE { $$ = $1; }
  ;

// Column identifier --- keywords that can be column, table, etc names.
//
// Many of these keywords will in fact be recognized as type or function
// names too; but they have special productions for the purpose, and so
// can't be treated as "generic" type or function names.
//
// The type names appearing here are not usable as function names
// because they can be followed by '(' in typename productions, which
// looks too much like a function call for an LR(1) parser.
col_name_keyword:
  BETWEEN { $$ = $1; }
  | BIGINT { $$ = $1; }
  | BIT { $$ = $1; }
  | BOOLEAN_P { $$ = $1; }
  | CHAR_P { $$ = $1; }
  | CHARACTER { $$ = $1; }
  | COALESCE { $$ = $1; }
  | DEC { $$ = $1; }
  | DECIMAL_P { $$ = $1; }
  | EXISTS { $$ = $1; }
  | EXTRACT { $$ = $1; }
  | FLOAT_P { $$ = $1; }
  | GREATEST { $$ = $1; }
  | GROUPING { $$ = $1; }
  | INOUT { $$ = $1; }
  | INT_P { $$ = $1; }
  | INTEGER { $$ = $1; }
  | INTERVAL { $$ = $1; }
  | LEAST { $$ = $1; }
  | NATIONAL { $$ = $1; }
  | NCHAR { $$ = $1; }
  | NONE { $$ = $1; }
  | NULLIF { $$ = $1; }
  | NUMERIC { $$ = $1; }
  | OUT_P { $$ = $1; }
  | OVERLAY { $$ = $1; }
  | POSITION { $$ = $1; }
  | PRECISION { $$ = $1; }
  | REAL { $$ = $1; }
  | ROW { $$ = $1; }
  | SETOF { $$ = $1; }
  | SMALLINT { $$ = $1; }
  | SUBSTRING { $$ = $1; }
  | TIME { $$ = $1; }
  | TIMESTAMP { $$ = $1; }
  | TREAT { $$ = $1; }
  | TRIM { $$ = $1; }
  | VALUES { $$ = $1; }
  | VARCHAR { $$ = $1; }
  | XMLATTRIBUTES { $$ = $1; }
  | XMLCONCAT { $$ = $1; }
  | XMLELEMENT { $$ = $1; }
  | XMLEXISTS { $$ = $1; }
  | XMLFOREST { $$ = $1; }
  | XMLPARSE { $$ = $1; }
  | XMLPI { $$ = $1; }
  | XMLROOT { $$ = $1; }
  | XMLSERIALIZE { $$ = $1; }
;

// Type/function identifier --- keywords that can be type or function names.
//
// Most of these are keywords that are used as operators in expressions;
// in general such keywords can't be column names because they would be
// ambiguous with variables, but they are unambiguous as function identifiers.
//
// Do not include POSITION, SUBSTRING, etc here since they have explicit
// productions in a_expr to support the goofy SQL9x argument syntax.
// - thomas 2000-11-28
type_func_name_keyword:
  AUTHORIZATION { $$ = $1; }
  | BINARY { $$ = $1; }
  | COLLATION { $$ = $1; }
  | CONCURRENTLY { $$ = $1; }
  | CROSS { $$ = $1; }
  | CURRENT_SCHEMA { $$ = $1; }
  | FREEZE { $$ = $1; }
  | FULL { $$ = $1; }
  | ILIKE { $$ = $1; }
  | INNER_P { $$ = $1; }
  | IS { $$ = $1; }
  | ISNULL { $$ = $1; }
  | JOIN { $$ = $1; }
  | LEFT { $$ = $1; }
  | LIKE { $$ = $1; }
  | NATURAL { $$ = $1; }
  | NOTNULL { $$ = $1; }
  | OUTER_P { $$ = $1; }
  | OVERLAPS { $$ = $1; }
  | RIGHT { $$ = $1; }
  | SIMILAR { $$ = $1; }
  | TABLESAMPLE { $$ = $1; }
  | VERBOSE { $$ = $1; }
;

// Reserved keyword --- these keywords are usable only as a ColLabel.
//
// Keywords appear here if they could not be distinguished from variable,
// type, or function names in some contexts.  Don't put things here unless
// forced to.
reserved_keyword:
  ALL { $$ = $1; }
  | ANALYSE { $$ = $1; }
  | ANALYZE { $$ = $1; }
  | AND { $$ = $1; }
  | ANY { $$ = $1; }
  | ARRAY { $$ = $1; }
  | AS { $$ = $1; }
  | ASC { $$ = $1; }
  | ASYMMETRIC { $$ = $1; }
  | BOTH { $$ = $1; }
  | CASE { $$ = $1; }
  | CAST { $$ = $1; }
  | CHECK { $$ = $1; }
  | COLLATE { $$ = $1; }
  | COLUMN { $$ = $1; }
  | CONSTRAINT { $$ = $1; }
  | CREATE { $$ = $1; }
  | CURRENT_CATALOG { $$ = $1; }
  | CURRENT_DATE { $$ = $1; }
  | CURRENT_ROLE { $$ = $1; }
  | CURRENT_TIME { $$ = $1; }
  | CURRENT_TIMESTAMP { $$ = $1; }
  | CURRENT_USER { $$ = $1; }
  | DEFAULT { $$ = $1; }
  | DEFERRABLE { $$ = $1; }
  | DESC { $$ = $1; }
  | DISTINCT { $$ = $1; }
  | DO { $$ = $1; }
  | ELSE { $$ = $1; }
  | END_P { $$ = $1; }
  | EXCEPT { $$ = $1; }
  | FALSE_P { $$ = $1; }
  | FETCH { $$ = $1; }
  | FOR { $$ = $1; }
  | FOREIGN { $$ = $1; }
  | FROM { $$ = $1; }
  | GRANT { $$ = $1; }
  | GROUP_P { $$ = $1; }
  | HAVING { $$ = $1; }
  | IN_P { $$ = $1; }
  | INITIALLY { $$ = $1; }
  | INTERSECT { $$ = $1; }
  | INTO { $$ = $1; }
  | LATERAL_P { $$ = $1; }
  | LEADING { $$ = $1; }
  | LIMIT { $$ = $1; }
  | LOCALTIME { $$ = $1; }
  | LOCALTIMESTAMP { $$ = $1; }
  | NOT { $$ = $1; }
  | NULL_P { $$ = $1; }
  | OFFSET { $$ = $1; }
  | ON { $$ = $1; }
  | ONLY { $$ = $1; }
  | OR { $$ = $1; }
  | ORDER { $$ = $1; }
  | PLACING { $$ = $1; }
  | PRIMARY { $$ = $1; }
  | REFERENCES { $$ = $1; }
  | RETURNING { $$ = $1; }
  | SELECT { $$ = $1; }
  | SESSION_USER { $$ = $1; }
  | SOME { $$ = $1; }
  | SYMMETRIC { $$ = $1; }
  | TABLE { $$ = $1; }
  | THEN { $$ = $1; }
  | TO { $$ = $1; }
  | TRAILING { $$ = $1; }
  | TRUE_P { $$ = $1; }
  | UNION { $$ = $1; }
  | UNIQUE { $$ = $1; }
  | USER { $$ = $1; }
  | USING { $$ = $1; }
  | VARIADIC { $$ = $1; }
  | WHEN { $$ = $1; }
  | WHERE { $$ = $1; }
  | WINDOW { $$ = $1; }
  | WITH { $$ = $1; }
;


//--------------------------------------------------------------------------------------------------
// INACTIVE PARSING RULES. INACTIVE PARSING RULES. INACTIVE PARSING RULES. INACTIVE PARSING RULES.
//
// YbSql currently does not use these PostgreSql rules.
//--------------------------------------------------------------------------------------------------

inactive_stmt : AlterEventTrigStmt
  | AlterDatabaseStmt
  | AlterDatabaseSetStmt
  | AlterDefaultPrivilegesStmt
  | AlterDomainStmt
  | AlterEnumStmt
  | AlterExtensionStmt
  | AlterExtensionContentsStmt
  | AlterFdwStmt
  | AlterForeignServerStmt
  | AlterForeignTableStmt
  | AlterFunctionStmt
  | AlterObjectSchemaStmt
  | AlterOwnerStmt
  | AlterPolicyStmt
  | AlterSeqStmt
  | AlterSystemStmt
  | AlterTableStmt
  | AlterTblSpcStmt
  | AlterCompositeTypeStmt
  | AlterRoleSetStmt
  | AlterRoleStmt
  | AlterTSConfigurationStmt
  | AlterTSDictionaryStmt
  | AlterUserMappingStmt
  | AlterUserSetStmt
  | AlterUserStmt
  | AnalyzeStmt
  | CheckPointStmt
  | ClosePortalStmt
  | ClusterStmt
  | CommentStmt
  | ConstraintsSetStmt
  | CopyStmt
  | CreateAsStmt
  | CreateAssertStmt
  | CreateCastStmt
  | CreateConversionStmt
  | CreateDomainStmt
  | CreateExtensionStmt
  | CreateFdwStmt
  | CreateForeignServerStmt
  | CreateForeignTableStmt
  | CreateFunctionStmt
  | CreateMatViewStmt
  | CreateOpClassStmt
  | CreateOpFamilyStmt
  | AlterOpFamilyStmt
  | CreatePolicyStmt
  | CreatePLangStmt
  | CreateSchemaStmt
  | CreateSeqStmt
  | CreateTableSpaceStmt
  | CreateTransformStmt
  | CreateTrigStmt
  | CreateEventTrigStmt
  | CreateRoleStmt
  | CreateUserStmt
  | CreateUserMappingStmt
  | CreatedbStmt
  | DeallocateStmt
  | DeclareCursorStmt
  | DefineStmt
  | DeleteStmt
  | DiscardStmt
  | DoStmt
  | DropAssertStmt
  | DropCastStmt
  | DropFdwStmt
  | DropForeignServerStmt
  | DropOpClassStmt
  | DropOpFamilyStmt
  | DropOwnedStmt
  | DropPolicyStmt
  | DropPLangStmt
  | DropRuleStmt
  | DropStmt
  | DropTableSpaceStmt
  | DropTransformStmt
  | DropTrigStmt
  | DropRoleStmt
  | DropUserStmt
  | DropUserMappingStmt
  | DropdbStmt
  | ExecuteStmt
  | ExplainStmt
  | FetchStmt
  | GrantStmt
  | GrantRoleStmt
  | ImportForeignSchemaStmt
  | IndexStmt
  | InsertStmt
  | ListenStmt
  | RefreshMatViewStmt
  | LoadStmt
  | LockStmt
  | NotifyStmt
  | PrepareStmt
  | ReassignOwnedStmt
  | ReindexStmt
  | RemoveAggrStmt
  | RemoveFuncStmt
  | RemoveOperStmt
  | RenameStmt
  | RevokeStmt
  | RevokeRoleStmt
  | RuleStmt
  | SecLabelStmt
  | SelectStmt
  | TransactionStmt
  | TruncateStmt
  | UnlistenStmt
  | UpdateStmt
  | VacuumStmt
  | VariableResetStmt
  | VariableSetStmt
  | VariableShowStmt
  | ViewStmt
  | /*EMPTY*/
  { $$ = NULL; }
;

/*****************************************************************************
 *
 * Create a new Postgres DBMS role
 *
 *****************************************************************************/

CreateRoleStmt: CREATE ROLE RoleId opt_with OptRoleList {
    $$ = nullptr;
  }
;


opt_with: WITH {}
  | WITH_LA {}
  | /*EMPTY*/ {}
;

/*
 * Options for CREATE ROLE and ALTER ROLE (also used by CREATE/ALTER USER
 * for backwards compatibility).  Note: the only option required by SQL99
 * is "WITH ADMIN name".
 */
OptRoleList: OptRoleList CreateOptRoleElem
  { $$ = NULL; }
  | /* EMPTY */ { $$ = NULL; }
;

AlterOptRoleList:
  AlterOptRoleList AlterOptRoleElem {
    $$ = NULL;
  }
  | /* EMPTY */ {
  }
;

AlterOptRoleElem:
  PASSWORD Sconst {
    $$ = NULL;
  }
  | PASSWORD NULL_P {
    $$ = NULL;
  }
  | ENCRYPTED PASSWORD Sconst {
    $$ = NULL;
  }
  | UNENCRYPTED PASSWORD Sconst {
    $$ = NULL;
  }
  | INHERIT {
    $$ = NULL;
  }
  | CONNECTION LIMIT SignedIconst {
    $$ = NULL;
  }
  | VALID UNTIL Sconst {
    $$ = NULL;
  }
  | USER role_list {
    $$ = NULL;
  }
  | IDENT {
    $$ = NULL;
  }
;

CreateOptRoleElem:
  AlterOptRoleElem {
    $$ = $1;
  }
  | SYSID Iconst {
  }
  | ADMIN role_list {
  }
  | ROLE role_list {
  }
  | IN_P ROLE role_list {
  }
  | IN_P GROUP_P role_list {
  }
;


/*****************************************************************************
 *
 * Create a new Postgres DBMS user (role with implied login ability)
 *
 *****************************************************************************/

CreateUserStmt:
  CREATE USER RoleId opt_with OptRoleList {
  }
;


/*****************************************************************************
 *
 * Alter a postgresql DBMS role
 *
 *****************************************************************************/

AlterRoleStmt:
  ALTER ROLE RoleSpec opt_with AlterOptRoleList {
  }
;

opt_in_database:
  /* EMPTY */ { }
  | IN_P DATABASE database_name { $$ = $3; }
;

AlterRoleSetStmt:
  ALTER ROLE RoleSpec opt_in_database SetResetClause {
  }
  | ALTER ROLE ALL opt_in_database SetResetClause {
  }
;


/*****************************************************************************
 *
 * Alter a postgresql DBMS user
 *
 *****************************************************************************/

AlterUserStmt: ALTER USER RoleSpec opt_with AlterOptRoleList {
  }
;


AlterUserSetStmt: ALTER USER RoleSpec SetResetClause {
  }
;


/*****************************************************************************
 *
 * Drop a postgresql DBMS role
 *
 * XXX Ideally this would have CASCADE/RESTRICT options, but since a role
 * might own objects in multiple databases, there is presently no way to
 * implement either cascading or restricting.  Caveat DBA.
 *****************************************************************************/

DropRoleStmt:
  DROP ROLE role_list {
  }
  | DROP ROLE IF_P EXISTS role_list {
  }
;

/*****************************************************************************
 *
 * Drop a postgresql DBMS user
 *
 * XXX Ideally this would have CASCADE/RESTRICT options, but since a user
 * might own objects in multiple databases, there is presently no way to
 * implement either cascading or restricting.  Caveat DBA.
 *****************************************************************************/

DropUserStmt:
  DROP USER role_list {
  }
  | DROP USER IF_P EXISTS role_list {
  }
;

/*****************************************************************************
 *
 * Manipulate a schema
 *
 *****************************************************************************/

CreateSchemaStmt:
  CREATE SCHEMA OptSchemaName AUTHORIZATION RoleSpec OptSchemaEltList {
  }
  | CREATE SCHEMA ColId OptSchemaEltList {
  }
  | CREATE SCHEMA IF_P NOT EXISTS OptSchemaName AUTHORIZATION RoleSpec OptSchemaEltList {
  }
  | CREATE SCHEMA IF_P NOT EXISTS ColId OptSchemaEltList {
  }
;

OptSchemaName:
  ColId                     { $$ = $1; }
  | /* EMPTY */             { }
;

OptSchemaEltList:
  OptSchemaEltList schema_stmt {
  }
  | /* EMPTY */ {
  }
;

/*
 *  schema_stmt are the ones that can show up inside a CREATE SCHEMA
 *  statement (in addition to by themselves).
 */
inactive_schema_stmt:
  IndexStmt
  | CreateSeqStmt
  | CreateTrigStmt
  | GrantStmt
  | ViewStmt
;


/*****************************************************************************
 *
 * Set PG internal variable
 *    SET name TO 'var_value'
 * Include SQL syntax (thomas 1997-10-22):
 *    SET TIME ZONE 'var_value'
 *
 *****************************************************************************/

VariableSetStmt:
  SET set_rest {
  }
  | SET LOCAL set_rest {
  }
  | SET SESSION set_rest {
  }
;

set_rest:
  TRANSACTION transaction_mode_list {
  }
  | SESSION CHARACTERISTICS AS TRANSACTION transaction_mode_list {
  }
  | set_rest_more
  ;

generic_set:
  var_name TO var_list {
  }
  | var_name '=' var_list {
  }
  | var_name TO DEFAULT {
  }
  | var_name '=' DEFAULT {
  }
;

set_rest_more:  /* Generic SET syntaxes: */
  generic_set {$$ = $1;}
  | var_name FROM CURRENT_P {
  }
  /* Special syntaxes mandated by SQL standard: */
  | TIME ZONE zone_value {
  }
  | CATALOG_P Sconst {
  }
  | SCHEMA Sconst {
  }
  | NAMES opt_encoding {
  }
  | ROLE NonReservedWord_or_Sconst {
  }
  | SESSION AUTHORIZATION NonReservedWord_or_Sconst {
  }
  | SESSION AUTHORIZATION DEFAULT {
  }
  | XML_P OPTION document_or_content {
  }
  /* Special syntaxes invented by PostgreSQL: */
  | TRANSACTION SNAPSHOT Sconst {
  }
;

var_name:
  ColId { $$ = $1; }
  | var_name '.' ColId {
  }
;

var_list:
  var_value { }
  | var_list ',' var_value { }
;

var_value:
  opt_boolean_or_string {}
  | NumericOnly {}
;

iso_level:
  READ UNCOMMITTED            { $$ = "read uncommitted"; }
  | READ COMMITTED            { $$ = "read committed"; }
  | REPEATABLE READ           { $$ = "repeatable read"; }
  | SERIALIZABLE              { $$ = "serializable"; }
;

opt_boolean_or_string:
  TRUE_P                      { $$ = "true"; }
  | FALSE_P                   { $$ = "false"; }
  | ON                        { $$ = "on"; }
  /*
   * OFF is also accepted as a boolean value, but is handled by
   * the NonReservedWord rule.  The action for booleans and strings
   * is the same, so we don't need to distinguish them here.
   */
  | NonReservedWord_or_Sconst { $$ = $1; }
;

/* Timezone values can be:
 * - a string such as 'pst8pdt'
 * - an identifier such as "pst8pdt"
 * - an integer or floating point number
 * - a time interval per SQL99
 * ColId gives reduce/reduce errors against ConstInterval and LOCAL,
 * so use IDENT (meaning we reject anything that is a key word).
 */
zone_value:
  Sconst {
  }
  | IDENT {
  }
  | ConstInterval Sconst opt_interval {
  }
  | ConstInterval '(' Iconst ')' Sconst {
  }
  | NumericOnly {}
  | DEFAULT {}
  | LOCAL {}
;

opt_encoding:
  Sconst {}
  | DEFAULT {}
  | /*EMPTY*/ {}
;

NonReservedWord_or_Sconst:
  NonReservedWord {}
  | Sconst {}
;

VariableResetStmt:
  RESET reset_rest {}
;

reset_rest:
  generic_reset {}
  | TIME ZONE {
  }
  | TRANSACTION ISOLATION LEVEL {
  }
  | SESSION AUTHORIZATION {
  }
;

generic_reset:
  var_name {
  }
  | ALL {
  }
;

/* SetResetClause allows SET or RESET without LOCAL */
SetResetClause:
  SET set_rest {}
  | VariableResetStmt {}
;

/* SetResetClause allows SET or RESET without LOCAL */
FunctionSetResetClause:
  SET set_rest_more {}
  | VariableResetStmt {}
;

VariableShowStmt:
  SHOW var_name {
  }
  | SHOW TIME ZONE {
  }
  | SHOW TRANSACTION ISOLATION LEVEL {
  }
  | SHOW SESSION AUTHORIZATION {
  }
  | SHOW ALL {
  }
;


ConstraintsSetStmt:
  SET CONSTRAINTS constraints_set_list constraints_set_mode {
  }
;

constraints_set_list:
  ALL {}
  | qualified_name_list {}
;

constraints_set_mode:
  DEFERRED { $$ = true; }
  | IMMEDIATE { $$ = false; }
;


/*
 * Checkpoint statement
 */
CheckPointStmt:
  CHECKPOINT {
  }
;


/*****************************************************************************
 *
 * DISCARD { ALL | TEMP | PLANS | SEQUENCES }
 *
 *****************************************************************************/

DiscardStmt:
  DISCARD ALL {
  }
  | DISCARD TEMP {
  }
  | DISCARD TEMPORARY {
  }
  | DISCARD PLANS {
  }
  | DISCARD SEQUENCES {
  }
;


/*****************************************************************************
 *
 *  ALTER [ TABLE | INDEX | SEQUENCE | VIEW | MATERIALIZED VIEW ] variations
 *
 * Note: we accept all subcommands for each of the five variants, and sort
 * out what's really legal at execution time.
 *****************************************************************************/

AlterTableStmt:
  ALTER TABLE relation_expr alter_table_cmds {
  }
  | ALTER TABLE IF_P EXISTS relation_expr alter_table_cmds {
  }
  | ALTER TABLE ALL IN_P TABLESPACE name SET TABLESPACE name opt_nowait {
  }
  | ALTER TABLE ALL IN_P TABLESPACE name OWNED BY role_list SET TABLESPACE name opt_nowait {
  }
  | ALTER INDEX qualified_name alter_table_cmds {
  }
  | ALTER INDEX IF_P EXISTS qualified_name alter_table_cmds {
  }
  | ALTER INDEX ALL IN_P TABLESPACE name SET TABLESPACE name opt_nowait {
  }
  | ALTER INDEX ALL IN_P TABLESPACE name OWNED BY role_list SET TABLESPACE name opt_nowait {
  }
  | ALTER SEQUENCE qualified_name alter_table_cmds {
  }
  | ALTER SEQUENCE IF_P EXISTS qualified_name alter_table_cmds {
  }
  | ALTER VIEW qualified_name alter_table_cmds {
  }
  | ALTER VIEW IF_P EXISTS qualified_name alter_table_cmds {
  }
  | ALTER MATERIALIZED VIEW qualified_name alter_table_cmds {
  }
  | ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name alter_table_cmds {
  }
  | ALTER MATERIALIZED VIEW ALL IN_P TABLESPACE name SET TABLESPACE name opt_nowait {
  }
  | ALTER MATERIALIZED VIEW ALL IN_P TABLESPACE name OWNED BY role_list
    SET TABLESPACE name opt_nowait {
  }
;

alter_table_cmds:
  alter_table_cmd {}
  | alter_table_cmds ',' alter_table_cmd {}
;

alter_table_cmd:
  /* ALTER TABLE <name> ADD <coldef> */
  ADD_P columnDef {
  }
  /* ALTER TABLE <name> ADD COLUMN <coldef> */
  | ADD_P COLUMN columnDef {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> {SET DEFAULT <expr>|DROP DEFAULT} */
  | ALTER opt_column ColId alter_column_default {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> DROP NOT NULL */
  | ALTER opt_column ColId DROP NOT NULL_P {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> SET NOT NULL */
  | ALTER opt_column ColId SET NOT NULL_P {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> SET STATISTICS <SignedIconst> */
  | ALTER opt_column ColId SET STATISTICS SignedIconst {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> SET ( column_parameter = value [, ... ] ) */
  | ALTER opt_column ColId SET reloptions {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> SET ( column_parameter = value [, ... ] ) */
  | ALTER opt_column ColId RESET reloptions {
  }
  /* ALTER TABLE <name> ALTER [COLUMN] <colname> SET STORAGE <storagemode> */
  | ALTER opt_column ColId SET STORAGE ColId {
  }
  /* ALTER TABLE <name> DROP [COLUMN] IF EXISTS <colname> [RESTRICT|CASCADE] */
  | DROP opt_column IF_P EXISTS ColId opt_drop_behavior {
  }
  /* ALTER TABLE <name> DROP [COLUMN] <colname> [RESTRICT|CASCADE] */
  | DROP opt_column ColId opt_drop_behavior {
  }
  /*
   * ALTER TABLE <name> ALTER [COLUMN] <colname> [SET DATA] TYPE <typename>
   *    [ USING <expression> ]
   */
  | ALTER opt_column ColId opt_set_data TYPE_P Typename opt_collate_clause alter_using {
  }
  /* ALTER FOREIGN TABLE <name> ALTER [COLUMN] <colname> OPTIONS */
  | ALTER opt_column ColId alter_generic_options {
  }
  /* ALTER TABLE <name> ADD CONSTRAINT ... */
  | ADD_P TableConstraint {
  }
  /* ALTER TABLE <name> ALTER CONSTRAINT ... */
  | ALTER CONSTRAINT name ConstraintAttributeSpec {
  }
  /* ALTER TABLE <name> VALIDATE CONSTRAINT ... */
  | VALIDATE CONSTRAINT name {
  }
  /* ALTER TABLE <name> DROP CONSTRAINT IF EXISTS <name> [RESTRICT|CASCADE] */
  | DROP CONSTRAINT IF_P EXISTS name opt_drop_behavior {
  }
  /* ALTER TABLE <name> DROP CONSTRAINT <name> [RESTRICT|CASCADE] */
  | DROP CONSTRAINT name opt_drop_behavior {
  }
  /* ALTER TABLE <name> SET WITH OIDS  */
  | SET WITH OIDS {
  }
  /* ALTER TABLE <name> SET WITHOUT OIDS  */
  | SET WITHOUT OIDS {
  }
  /* ALTER TABLE <name> CLUSTER ON <indexname> */
  | CLUSTER ON name {
  }
  /* ALTER TABLE <name> SET WITHOUT CLUSTER */
  | SET WITHOUT CLUSTER {
  }
  /* ALTER TABLE <name> SET LOGGED  */
  | SET LOGGED {
  }
  /* ALTER TABLE <name> SET UNLOGGED  */
  | SET UNLOGGED {
  }
  /* ALTER TABLE <name> ENABLE TRIGGER <trig> */
  | ENABLE_P TRIGGER name {
  }
  /* ALTER TABLE <name> ENABLE ALWAYS TRIGGER <trig> */
  | ENABLE_P ALWAYS TRIGGER name {
  }
  /* ALTER TABLE <name> ENABLE REPLICA TRIGGER <trig> */
  | ENABLE_P REPLICA TRIGGER name {
  }
  /* ALTER TABLE <name> ENABLE TRIGGER ALL */
  | ENABLE_P TRIGGER ALL {
  }
  /* ALTER TABLE <name> ENABLE TRIGGER USER */
  | ENABLE_P TRIGGER USER {
  }
  /* ALTER TABLE <name> DISABLE TRIGGER <trig> */
  | DISABLE_P TRIGGER name {
  }
  /* ALTER TABLE <name> DISABLE TRIGGER ALL */
  | DISABLE_P TRIGGER ALL {
  }
  /* ALTER TABLE <name> DISABLE TRIGGER USER */
  | DISABLE_P TRIGGER USER {
  }
  /* ALTER TABLE <name> ENABLE RULE <rule> */
  | ENABLE_P RULE name {
  }
  /* ALTER TABLE <name> ENABLE ALWAYS RULE <rule> */
  | ENABLE_P ALWAYS RULE name {
  }
  /* ALTER TABLE <name> ENABLE REPLICA RULE <rule> */
  | ENABLE_P REPLICA RULE name {
  }
  /* ALTER TABLE <name> DISABLE RULE <rule> */
  | DISABLE_P RULE name {
  }
  /* ALTER TABLE <name> INHERIT <parent> */
  | INHERIT qualified_name {
  }
  /* ALTER TABLE <name> NO INHERIT <parent> */
  | NO INHERIT qualified_name {
  }
  /* ALTER TABLE <name> OF <type_name> */
  | OF any_name {
  }
  /* ALTER TABLE <name> NOT OF */
  | NOT OF {
  }
  /* ALTER TABLE <name> OWNER TO RoleSpec */
  | OWNER TO RoleSpec {
  }
  /* ALTER TABLE <name> SET TABLESPACE <tablespacename> */
  | SET TABLESPACE name {
  }
  /* ALTER TABLE <name> SET (...) */
  | SET reloptions {
  }
  /* ALTER TABLE <name> RESET (...) */
  | RESET reloptions {
  }
  /* ALTER TABLE <name> REPLICA IDENTITY  */
  | REPLICA IDENTITY_P replica_identity {
  }
  /* ALTER TABLE <name> ENABLE ROW LEVEL SECURITY */
  | ENABLE_P ROW LEVEL SECURITY {
  }
  /* ALTER TABLE <name> DISABLE ROW LEVEL SECURITY */
  | DISABLE_P ROW LEVEL SECURITY {
  }
  /* ALTER TABLE <name> FORCE ROW LEVEL SECURITY */
  | FORCE ROW LEVEL SECURITY {
  }
  /* ALTER TABLE <name> NO FORCE ROW LEVEL SECURITY */
  | NO FORCE ROW LEVEL SECURITY {
  }
  | alter_generic_options {
  }
;

alter_column_default:
  SET DEFAULT a_expr { $$ = $3; }
  | DROP DEFAULT     { $$ = NULL; }
;

opt_drop_behavior:
  CASCADE             { $$ = DROP_CASCADE; }
  | RESTRICT          { $$ = DROP_RESTRICT; }
  | /* EMPTY */       { $$ = DROP_RESTRICT; /* default */ }
    ;

opt_collate_clause:
  COLLATE any_name {
  }
  | /* EMPTY */       { $$ = NULL; }
;

alter_using:
  USING a_expr        { $$ = $2; }
  | /* EMPTY */       { $$ = NULL; }
;

replica_identity:
  NOTHING {
  }
  | FULL {
  }
  | DEFAULT {
  }
  | USING INDEX name {
  }
;

reloptions:
  '(' reloption_list ')'          { $$ = $2; }
;

opt_reloptions:
  WITH reloptions                 { $$ = $2; }
  |    /* EMPTY */                { $$ = NULL; }
;

reloption_list:
  reloption_elem                  { }
  | reloption_list ',' reloption_elem   { }
;

/* This should match def_elem and also allow qualified names */
reloption_elem:
  ColLabel '=' def_arg {
  }
  | ColLabel {
  }
  | ColLabel '.' ColLabel '=' def_arg {
  }
  | ColLabel '.' ColLabel {
  }
;


/*****************************************************************************
 *
 *  ALTER TYPE
 *
 * really variants of the ALTER TABLE subcommands with different spellings
 *****************************************************************************/

AlterCompositeTypeStmt:
  ALTER TYPE_P any_name alter_type_cmds {
  }
;

alter_type_cmds:
  alter_type_cmd                        { }
  | alter_type_cmds ',' alter_type_cmd  { }
;

alter_type_cmd:
  /* ALTER TYPE <name> ADD ATTRIBUTE <coldef> [RESTRICT|CASCADE] */
  ADD_P ATTRIBUTE TableFuncElement opt_drop_behavior {
  }
  /* ALTER TYPE <name> DROP ATTRIBUTE IF EXISTS <attname> [RESTRICT|CASCADE] */
  | DROP ATTRIBUTE IF_P EXISTS ColId opt_drop_behavior {
  }
  /* ALTER TYPE <name> DROP ATTRIBUTE <attname> [RESTRICT|CASCADE] */
  | DROP ATTRIBUTE ColId opt_drop_behavior {
  }
  /* ALTER TYPE <name> ALTER ATTRIBUTE <attname> [SET DATA] TYPE <typename> [RESTRICT|CASCADE] */
  | ALTER ATTRIBUTE ColId opt_set_data TYPE_P Typename opt_collate_clause opt_drop_behavior {
  }
;


/*****************************************************************************
 *
 *    QUERY :
 *        close <portalname>
 *
 *****************************************************************************/

ClosePortalStmt:
  CLOSE cursor_name {}
  | CLOSE ALL {}
;


/*****************************************************************************
 *
 *    QUERY :
 *        COPY relname [(columnList)] FROM/TO file [WITH] [(options)]
 *        COPY ( SELECT ... ) TO file [WITH] [(options)]
 *
 *        where 'file' can be one of:
 *        { PROGRAM 'command' | STDIN | STDOUT | 'filename' }
 *
 *        In the preferred syntax the options are comma-separated
 *        and use generic identifiers instead of keywords.  The pre-9.0
 *        syntax had a hard-wired, space-separated set of options.
 *
 *        Really old syntax, from versions 7.2 and prior:
 *        COPY [ BINARY ] table [ WITH OIDS ] FROM/TO file
 *          [ [ USING ] DELIMITERS 'delimiter' ] ]
 *          [ WITH NULL AS 'null string' ]
 *        This option placement is not supported with COPY (SELECT...).
 *
 *****************************************************************************/

CopyStmt:
  COPY opt_binary qualified_name opt_column_list opt_oids
  copy_from opt_program copy_file_name copy_delimiter opt_with copy_options {}
  | COPY select_with_parens TO opt_program copy_file_name opt_with copy_options {}
;

copy_from:
  FROM                  { $$ = true; }
  | TO                  { $$ = false; }
;

opt_program:
  PROGRAM               { $$ = true; }
  | /* EMPTY */         { $$ = false; }
;

/*
 * copy_file_name NULL indicates stdio is used. Whether stdin or stdout is
 * used depends on the direction. (It really doesn't make sense to copy from
 * stdout. We silently correct the "typo".)    - AY 9/94
 */
copy_file_name:
  Sconst                  { $$ = $1; }
  | STDIN                 { $$ = NULL; }
  | STDOUT                { $$ = NULL; }
;

copy_options:
  copy_opt_list                   { $$ = $1; }
  | '(' copy_generic_opt_list ')' { $$ = $2; }
    ;

/* old COPY option syntax */
copy_opt_list:
  copy_opt_list copy_opt_item { }
  | /* EMPTY */               { $$ = NULL; }
;

copy_opt_item:
  BINARY {}
  | OIDS {}
  | FREEZE {}
  | DELIMITER opt_as Sconst {}
  | NULL_P opt_as Sconst {}
  | CSV {}
  | HEADER_P {}
  | QUOTE opt_as Sconst {}
  | ESCAPE opt_as Sconst {}
  | FORCE QUOTE columnList {}
  | FORCE QUOTE '*' {}
  | FORCE NOT NULL_P columnList {}
  | FORCE NULL_P columnList {}
  | ENCODING Sconst {}
;

/* The following exist for backward compatibility with very old versions */

opt_binary:
  BINARY {}
  | /*EMPTY*/ { $$ = NULL; }
;

opt_oids:
  WITH OIDS {}
  | /*EMPTY*/  { $$ = NULL; }
    ;

copy_delimiter:
  opt_using DELIMITERS Sconst { }
  | /*EMPTY*/                 { $$ = NULL; }
    ;

opt_using:
  USING                   {}
  | /*EMPTY*/             {}
;

/* new COPY option syntax */
copy_generic_opt_list:
  copy_generic_opt_elem {
  }
  | copy_generic_opt_list ',' copy_generic_opt_elem {
  }
;

copy_generic_opt_elem:
  ColLabel copy_generic_opt_arg {
  }
;

copy_generic_opt_arg:
  opt_boolean_or_string                 { }
  | NumericOnly                         { }
  | '*'                                 { }
  | '(' copy_generic_opt_arg_list ')'   { }
  | /* EMPTY */                         { }
;

copy_generic_opt_arg_list:
  copy_generic_opt_arg_list_item {
  }
  | copy_generic_opt_arg_list ',' copy_generic_opt_arg_list_item {
  }
;

/* beware of emitting non-string list elements here; see commands/define.c */
copy_generic_opt_arg_list_item:
  opt_boolean_or_string {}
;


/*****************************************************************************
 *
 *    QUERY :
 *        CREATE TABLE relname
 *
 *****************************************************************************/

/*
CreateStmt:
  CREATE OptTemp TABLE qualified_name '(' OptTableElementList ')'
  OptInherit OptWith OnCommitOption OptTableSpace {
  }
  | CREATE OptTemp TABLE IF_P NOT EXISTS qualified_name '(' OptTableElementList ')'
  OptInherit OptWith OnCommitOption OptTableSpace {
  }
  | CREATE OptTemp TABLE qualified_name OF any_name
  OptTypedTableElementList OptWith OnCommitOption OptTableSpace {
  }
  | CREATE OptTemp TABLE IF_P NOT EXISTS qualified_name OF any_name
  OptTypedTableElementList OptWith OnCommitOption OptTableSpace {
  }
;
*/

/*
 * Redundancy here is needed to avoid shift/reduce conflicts,
 * since TEMP is not a reserved word.  See also OptTempTableName.
 *
 * NOTE: we accept both GLOBAL and LOCAL options.  They currently do nothing,
 * but future versions might consider GLOBAL to request SQL-spec-compliant
 * temp table behavior, so warn about that.  Since we have no modules the
 * LOCAL keyword is really meaningless; furthermore, some other products
 * implement LOCAL as meaning the same as our default temp table behavior,
 * so we'll probably continue to treat LOCAL as a noise word.
 */
OptTemp:
  TEMPORARY             { }
  | TEMP                { }
  | LOCAL TEMPORARY     { }
  | LOCAL TEMP          { }
  | GLOBAL TEMPORARY    { }
  | GLOBAL TEMP         { }
  | UNLOGGED            { }
  | /*EMPTY*/           { }
;

OptTableElementList:
  TableElementList      { $$ = $1; }
  | /*EMPTY*/ {
  }
;

OptTypedTableElementList:
  '(' TypedTableElementList ')'   { $$ = $2; }
  | /*EMPTY*/ {
  }
;

TableElementList:
  TableElement {
  }
  | TableElementList ',' TableElement {
  }
;

TypedTableElementList:
  TypedTableElement {
  }
  | TypedTableElementList ',' TypedTableElement {
  }
;

TableElement:
  columnDef             { $$ = $1; }
  | TableLikeClause     { $$ = $1; }
  | TableConstraint     { $$ = $1; }
;

TypedTableElement:
  columnOptions         { $$ = $1; }
  | TableConstraint     { $$ = $1; }
;

columnDef:
  ColId Typename create_generic_options ColQualList {
  }
;

columnOptions:
  ColId WITH OPTIONS ColQualList {
  }
;

ColQualList:
  ColQualList ColConstraint {
  }
  | /*EMPTY*/ {
  }
;

ColConstraint:
  CONSTRAINT name ColConstraintElem {
  }
  | ColConstraintElem           { $$ = $1; }
  | ConstraintAttr            { $$ = $1; }
  | COLLATE any_name {
  }
;

/* DEFAULT NULL is already the default for Postgres.
 * But define it here and carry it forward into the system
 * to make it explicit.
 * - thomas 1998-09-13
 *
 * WITH NULL and NULL are not SQL-standard syntax elements,
 * so leave them out. Use DEFAULT NULL to explicitly indicate
 * that a column may have that value. WITH NULL leads to
 * shift/reduce conflicts with WITH TIME ZONE anyway.
 * - thomas 1999-01-08
 *
 * DEFAULT expression must be b_expr not a_expr to prevent shift/reduce
 * conflict on NOT (since NOT might start a subsequent NOT NULL constraint,
 * or be part of a_expr NOT LIKE or similar constructs).
 */
ColConstraintElem:
  NOT NULL_P {
  }
  | NULL_P {
  }
  | UNIQUE opt_definition OptConsTableSpace {
  }
  | PRIMARY KEY opt_definition OptConsTableSpace {
  }
  | CHECK '(' a_expr ')' opt_no_inherit {
  }
  | DEFAULT b_expr {
  }
  | REFERENCES qualified_name opt_column_list key_match key_actions {
  }
;

/*
 * ConstraintAttr represents constraint attributes, which we parse as if
 * they were independent constraint clauses, in order to avoid shift/reduce
 * conflicts (since NOT might start either an independent NOT NULL clause
 * or an attribute).  parse_utilcmd.c is responsible for attaching the
 * attribute information to the preceding "real" constraint node, and for
 * complaining if attribute clauses appear in the wrong place or wrong
 * combinations.
 *
 * See also ConstraintAttributeSpec, which can be used in places where
 * there is no parsing conflict.  (Note: currently, NOT VALID and NO INHERIT
 * are allowed clauses in ConstraintAttributeSpec, but not here.  Someday we
 * might need to allow them here too, but for the moment it doesn't seem
 * useful in the statements that use ConstraintAttr.)
 */
ConstraintAttr:
  DEFERRABLE {
  }
  | NOT DEFERRABLE {
  }
  | INITIALLY DEFERRED {
  }
  | INITIALLY IMMEDIATE {
  }
;


TableLikeClause:
  LIKE qualified_name TableLikeOptionList {
  }
;

TableLikeOptionList:
  TableLikeOptionList INCLUDING TableLikeOption   { $$ = $1 | $3; }
  | TableLikeOptionList EXCLUDING TableLikeOption { $$ = $1 & ~$3; }
  | /* EMPTY */                                   { $$ = 0; }
    ;

TableLikeOption:
  DEFAULTS        { $$ = CREATE_TABLE_LIKE_DEFAULTS; }
  | CONSTRAINTS   { $$ = CREATE_TABLE_LIKE_CONSTRAINTS; }
  | INDEXES       { $$ = CREATE_TABLE_LIKE_INDEXES; }
  | STORAGE       { $$ = CREATE_TABLE_LIKE_STORAGE; }
  | COMMENTS      { $$ = CREATE_TABLE_LIKE_COMMENTS; }
  | ALL           { $$ = CREATE_TABLE_LIKE_ALL; }
;


/* ConstraintElem specifies constraint syntax which is not embedded into
 *  a column definition. ColConstraintElem specifies the embedded form.
 * - thomas 1997-12-03
 */
TableConstraint:
  CONSTRAINT name ConstraintElem {
  }
  | ConstraintElem {
    $$ = $1;
  }
;

ConstraintElem:
  CHECK '(' a_expr ')' ConstraintAttributeSpec {
  }
  | UNIQUE '(' columnList ')' opt_definition OptConsTableSpace ConstraintAttributeSpec {
  }
  | UNIQUE ExistingIndex ConstraintAttributeSpec {
  }
  | PRIMARY KEY '(' columnList ')' opt_definition OptConsTableSpace ConstraintAttributeSpec {
  }
  | PRIMARY KEY ExistingIndex ConstraintAttributeSpec {
  }
  | EXCLUDE access_method_clause '(' ExclusionConstraintList ')'
  opt_definition OptConsTableSpace ExclusionWhereClause ConstraintAttributeSpec {
  }
  | FOREIGN KEY '(' columnList ')' REFERENCES qualified_name
  opt_column_list key_match key_actions ConstraintAttributeSpec {
  }
;

opt_no_inherit:
  NO INHERIT                    {  $$ = true; }
  | /* EMPTY */                 {  $$ = false; }
    ;

opt_column_list:
  '(' columnList ')'            { $$ = $2; }
  | /*EMPTY*/ {
  }
;

columnList:
  columnElem {
  }
  | columnList ',' columnElem {
  }
;

columnElem:
  ColId {
  }
;

key_match:
  MATCH FULL {
    $$ = FKCONSTR_MATCH_FULL;
  }
  | MATCH PARTIAL {
    $$ = FKCONSTR_MATCH_PARTIAL;
  }
  | MATCH SIMPLE {
    $$ = FKCONSTR_MATCH_SIMPLE;
  }
  | /*EMPTY*/ {
    $$ = FKCONSTR_MATCH_SIMPLE;
  }
;

ExclusionConstraintList:
  ExclusionConstraintElem {
    // $$ = list_make1($1);
  }
  | ExclusionConstraintList ',' ExclusionConstraintElem {
    // $$ = lappend($1, $3);
  }
;

ExclusionConstraintElem:
  index_elem WITH any_operator {
    // $$ = list_make2($1, $3);
  }
  /* allow OPERATOR() decoration for the benefit of ruleutils.c */
  | index_elem WITH OPERATOR '(' any_operator ')' {
  }
;

ExclusionWhereClause:
  WHERE '(' a_expr ')'          { $$ = $3; }
  | /*EMPTY*/                   { $$ = NULL; }
;

/*
 * We combine the update and delete actions into one value temporarily
 * for simplicity of parsing, and then break them down again in the
 * calling production.  update is in the left 8 bits, delete in the right.
 * Note that NOACTION is the default.
 */
key_actions:
  key_update {
    $$ = ($1 << 8) | (FKCONSTR_ACTION_NOACTION & 0xFF);
  }
  | key_delete {
    $$ = (FKCONSTR_ACTION_NOACTION << 8) | ($1 & 0xFF);
  }
  | key_update key_delete {
    $$ = ($1 << 8) | ($2 & 0xFF);
  }
  | key_delete key_update {
    $$ = ($2 << 8) | ($1 & 0xFF);
  }
  | /*EMPTY*/ {
    $$ = (FKCONSTR_ACTION_NOACTION << 8) | (FKCONSTR_ACTION_NOACTION & 0xFF);
  }
;

key_update:
  ON UPDATE key_action {
    $$ = $3;
  }
;

key_delete:
  ON DELETE_P key_action {
    $$ = $3;
  }
;

key_action:
  NO ACTION         { $$ = FKCONSTR_ACTION_NOACTION; }
  | RESTRICT        { $$ = FKCONSTR_ACTION_RESTRICT; }
  | CASCADE         { $$ = FKCONSTR_ACTION_CASCADE; }
  | SET NULL_P      { $$ = FKCONSTR_ACTION_SETNULL; }
  | SET DEFAULT     { $$ = FKCONSTR_ACTION_SETDEFAULT; }
;

OptInherit:
  INHERITS '(' qualified_name_list ')'  { $$ = $3; }
  | /*EMPTY*/ {
  }
;

/* WITH (options) is preferred, WITH OIDS and WITHOUT OIDS are legacy forms */
OptWith:
  WITH reloptions       { $$ = $2; }
  | WITH OIDS {
  }
  | WITHOUT OIDS {
  }
  | /*EMPTY*/ {
  }
;

OnCommitOption:
  ON COMMIT DROP              { $$ = ONCOMMIT_DROP; }
  | ON COMMIT DELETE_P ROWS   { $$ = ONCOMMIT_DELETE_ROWS; }
  | ON COMMIT PRESERVE ROWS   { $$ = ONCOMMIT_PRESERVE_ROWS; }
  | /*EMPTY*/                 { $$ = ONCOMMIT_NOOP; }
;

OptTableSpace:
  TABLESPACE name             { $$ = $2; }
  | /*EMPTY*/                 { $$ = NULL; }
;

OptConsTableSpace:
  USING INDEX TABLESPACE name { $$ = $4; }
  | /*EMPTY*/                 { $$ = NULL; }
;

ExistingIndex:
  USING INDEX index_name      { $$ = $3; }
;


/*****************************************************************************
 *
 *    QUERY :
 *        CREATE TABLE relname AS SelectStmt [ WITH [NO] DATA ]
 *
 *
 * Note: SELECT ... INTO is a now-deprecated alternative for this.
 *
 *****************************************************************************/

CreateAsStmt:
  CREATE OptTemp TABLE create_as_target AS SelectStmt opt_with_data {
  }
  | CREATE OptTemp TABLE IF_P NOT EXISTS create_as_target AS SelectStmt opt_with_data {
  }
;

create_as_target:
  qualified_name opt_column_list OptWith OnCommitOption OptTableSpace {
  }
;

opt_with_data:
  WITH DATA_P               { $$ = true; }
  | WITH NO DATA_P          { $$ = false; }
  | /*EMPTY*/               { $$ = true; }
;

/*****************************************************************************
 *
 *    QUERY :
 *        CREATE MATERIALIZED VIEW relname AS SelectStmt
 *
 *****************************************************************************/

CreateMatViewStmt:
  CREATE OptNoLog MATERIALIZED VIEW create_mv_target AS SelectStmt opt_with_data {
  }
  | CREATE OptNoLog MATERIALIZED VIEW IF_P NOT EXISTS create_mv_target AS SelectStmt opt_with_data {
  }
;

create_mv_target:
  qualified_name opt_column_list opt_reloptions OptTableSpace {
  }
;

OptNoLog:
  UNLOGGED            { }
  | /*EMPTY*/         { }
;


/*****************************************************************************
 *
 *    QUERY :
 *        REFRESH MATERIALIZED VIEW qualified_name
 *
 *****************************************************************************/

RefreshMatViewStmt:
  REFRESH MATERIALIZED VIEW opt_concurrently qualified_name opt_with_data {
  }
;


/*****************************************************************************
 *
 *    QUERY :
 *        CREATE SEQUENCE seqname
 *        ALTER SEQUENCE seqname
 *
 *****************************************************************************/

CreateSeqStmt:
  CREATE OptTemp SEQUENCE qualified_name OptSeqOptList {
  }
  | CREATE OptTemp SEQUENCE IF_P NOT EXISTS qualified_name OptSeqOptList {
  }
;

AlterSeqStmt:
  ALTER SEQUENCE qualified_name SeqOptList {
  }
  | ALTER SEQUENCE IF_P EXISTS qualified_name SeqOptList {
  }
;

OptSeqOptList:
  SeqOptList                { $$ = $1; }
  | /*EMPTY*/ {
  }
;

SeqOptList:
  SeqOptElem {
  }
  | SeqOptList SeqOptElem {
  }
;

SeqOptElem:
  CACHE NumericOnly {
  }
  | CYCLE {
  }
  | NO CYCLE {
  }
  | INCREMENT opt_by NumericOnly {
  }
  | MAXVALUE NumericOnly {
  }
  | MINVALUE NumericOnly {
  }
  | NO MAXVALUE {
  }
  | NO MINVALUE {
  }
  | OWNED BY any_name {
  }
  | START opt_with NumericOnly {
  }
  | RESTART {
  }
  | RESTART opt_with NumericOnly {
  }
;

opt_by:
  BY            {}
  | /* empty */ {}
;

NumericOnly:
  FCONST {
    // $$ = makeFloat($1);
  }
  | '-' FCONST {
    // $$ = makeFloat($2);
    // doNegateFloat($$);
  }
  | SignedIconst {
    // $$ = makeInteger($1);
  }
;

NumericOnly_list:
  NumericOnly {
    // $$ = list_make1($1);
  }
  | NumericOnly_list ',' NumericOnly {
    // $$ = lappend($1, $3);
  }
;

/*****************************************************************************
 *
 *    QUERIES :
 *        CREATE [OR REPLACE] [TRUSTED] [PROCEDURAL] LANGUAGE ...
 *        DROP [PROCEDURAL] LANGUAGE ...
 *
 *****************************************************************************/

CreatePLangStmt:
  CREATE opt_or_replace opt_trusted opt_procedural LANGUAGE NonReservedWord_or_Sconst {
  }
  | CREATE opt_or_replace opt_trusted opt_procedural LANGUAGE NonReservedWord_or_Sconst
    HANDLER handler_name opt_inline_handler opt_validator {
  }
;

opt_trusted:
  TRUSTED                 { $$ = true; }
  | /*EMPTY*/             { $$ = false; }
;

/* This ought to be just func_name, but that causes reduce/reduce conflicts
 * (CREATE LANGUAGE is the only place where func_name isn't followed by '(').
 * Work around by using simple names, instead.
 */
handler_name:
  name {
    // $$ = list_make1(makeString($1));
  }
  | name attrs {
    // $$ = lcons(makeString($1), $2);
  }
;

opt_inline_handler:
  INLINE_P handler_name         { $$ = $2; }
  | /*EMPTY*/ {
  }
;

validator_clause:
  VALIDATOR handler_name        { $$ = $2; }
  | NO VALIDATOR {
  }
;

opt_validator:
  validator_clause              { $$ = $1; }
  | /*EMPTY*/ {
  }
;

DropPLangStmt:
  DROP opt_procedural LANGUAGE NonReservedWord_or_Sconst opt_drop_behavior {
  }
  | DROP opt_procedural LANGUAGE IF_P EXISTS NonReservedWord_or_Sconst opt_drop_behavior {
  }
;

opt_procedural:
  PROCEDURAL                {}
  | /*EMPTY*/               {}
;

/*****************************************************************************
 *
 *    QUERY:
 *             CREATE TABLESPACE tablespace LOCATION '/path/to/tablespace/'
 *
 *****************************************************************************/

CreateTableSpaceStmt:
  CREATE TABLESPACE name OptTableSpaceOwner LOCATION Sconst opt_reloptions {
  }
;

OptTableSpaceOwner:
  OWNER RoleSpec      { $$ = $2; }
  | /*EMPTY */        { $$ = NULL; }
;

/*****************************************************************************
 *
 *    QUERY :
 *        DROP TABLESPACE <tablespace>
 *
 *    No need for drop behaviour as we cannot implement dependencies for
 *    objects in other databases; we can only support RESTRICT.
 *
 ****************************************************************************/

DropTableSpaceStmt:
  DROP TABLESPACE name {
  }
  |  DROP TABLESPACE IF_P EXISTS name {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *             CREATE EXTENSION extension
 *             [ WITH ] [ SCHEMA schema ] [ VERSION version ] [ FROM oldversion ]
 *
 *****************************************************************************/

CreateExtensionStmt:
  CREATE EXTENSION name opt_with create_extension_opt_list {
  }
  | CREATE EXTENSION IF_P NOT EXISTS name opt_with create_extension_opt_list {
  }
;

create_extension_opt_list:
  create_extension_opt_list create_extension_opt_item {
    // $$ = lappend($1, $2);
  }
  | /* EMPTY */ {
  }
;

create_extension_opt_item:
  SCHEMA name {
    // $$ = makeDefElem("schema", (Node *)makeString($2));
  }
  | VERSION_P NonReservedWord_or_Sconst {
    // $$ = makeDefElem("new_version", (Node *)makeString($2));
  }
  | FROM NonReservedWord_or_Sconst {
    // $$ = makeDefElem("old_version", (Node *)makeString($2));
  }
;

/*****************************************************************************
 *
 * ALTER EXTENSION name UPDATE [ TO version ]
 *
 *****************************************************************************/

AlterExtensionStmt:
  ALTER EXTENSION name UPDATE alter_extension_opt_list {
  }
;

alter_extension_opt_list:
  alter_extension_opt_list alter_extension_opt_item {
    // $$ = lappend($1, $2);
  }
  | /* EMPTY */ {
  }
;

alter_extension_opt_item:
  TO NonReservedWord_or_Sconst {
  }
;

/*****************************************************************************
 *
 * ALTER EXTENSION name ADD/DROP object-identifier
 *
 *****************************************************************************/

AlterExtensionContentsStmt:
  ALTER EXTENSION name add_drop AGGREGATE func_name aggr_args {
  }
  | ALTER EXTENSION name add_drop CAST '(' Typename AS Typename ')' {
  }
  | ALTER EXTENSION name add_drop COLLATION any_name {
  }
  | ALTER EXTENSION name add_drop CONVERSION_P any_name {
  }
  | ALTER EXTENSION name add_drop DOMAIN_P Typename {
  }
  | ALTER EXTENSION name add_drop FUNCTION function_with_argtypes {
  }
  | ALTER EXTENSION name add_drop opt_procedural LANGUAGE name {
  }
  | ALTER EXTENSION name add_drop OPERATOR any_operator oper_argtypes {
  }
  | ALTER EXTENSION name add_drop OPERATOR CLASS any_name USING access_method {
  }
  | ALTER EXTENSION name add_drop OPERATOR FAMILY any_name USING access_method {
  }
  | ALTER EXTENSION name add_drop SCHEMA name {
  }
  | ALTER EXTENSION name add_drop EVENT TRIGGER name {
  }
  | ALTER EXTENSION name add_drop TABLE any_name {
  }
  | ALTER EXTENSION name add_drop TEXT_P SEARCH PARSER any_name {
  }
  | ALTER EXTENSION name add_drop TEXT_P SEARCH DICTIONARY any_name {
  }
  | ALTER EXTENSION name add_drop TEXT_P SEARCH TEMPLATE any_name {
  }
  | ALTER EXTENSION name add_drop TEXT_P SEARCH CONFIGURATION any_name {
  }
  | ALTER EXTENSION name add_drop SEQUENCE any_name {
  }
  | ALTER EXTENSION name add_drop VIEW any_name {
  }
  | ALTER EXTENSION name add_drop MATERIALIZED VIEW any_name {
  }
  | ALTER EXTENSION name add_drop FOREIGN TABLE any_name {
  }
  | ALTER EXTENSION name add_drop FOREIGN DATA_P WRAPPER name {
  }
  | ALTER EXTENSION name add_drop SERVER name {
  }
  | ALTER EXTENSION name add_drop TRANSFORM FOR Typename LANGUAGE name {
  }
  | ALTER EXTENSION name add_drop TYPE_P Typename {
  }
;

add_drop:
  ADD_P                   { $$ = +1; }
  | DROP                  { $$ = -1; }
;

/*****************************************************************************
 *
 *    QUERY:
 *             CREATE FOREIGN DATA WRAPPER name options
 *
 *****************************************************************************/

CreateFdwStmt:
  CREATE FOREIGN DATA_P WRAPPER name opt_fdw_options create_generic_options {
  }
;

fdw_option:
  HANDLER handler_name        { }
  | NO HANDLER                { }
  | VALIDATOR handler_name    { }
  | NO VALIDATOR              { }
;

fdw_options:
  fdw_option                  { }
  | fdw_options fdw_option    { }
;

opt_fdw_options:
  fdw_options                 { $$ = $1; }
  | /*EMPTY*/ {
  }
;

/*****************************************************************************
 *
 *    QUERY :
 *        DROP FOREIGN DATA WRAPPER name
 *
 ****************************************************************************/

DropFdwStmt:
  DROP FOREIGN DATA_P WRAPPER name opt_drop_behavior {
  }
  | DROP FOREIGN DATA_P WRAPPER IF_P EXISTS name opt_drop_behavior {
  }
;

/*****************************************************************************
 *
 *    QUERY :
 *        ALTER FOREIGN DATA WRAPPER name options
 *
 ****************************************************************************/

AlterFdwStmt:
  ALTER FOREIGN DATA_P WRAPPER name opt_fdw_options alter_generic_options {
  }
  | ALTER FOREIGN DATA_P WRAPPER name fdw_options {
  }
;

/* Options definition for CREATE FDW, SERVER and USER MAPPING */
create_generic_options:
  OPTIONS '(' generic_option_list ')'     { $$ = $3; }
  | /*EMPTY*/ {
  }
;

generic_option_list:
  generic_option_elem {
    // $$ = list_make1($1);
  }
  | generic_option_list ',' generic_option_elem {
    // $$ = lappend($1, $3);
  }
;

/* Options definition for ALTER FDW, SERVER and USER MAPPING */
alter_generic_options:
  OPTIONS '(' alter_generic_option_list ')' {
    $$ = $3;
  }
;

alter_generic_option_list:
  alter_generic_option_elem {
    // $$ = list_make1($1);
  }
  | alter_generic_option_list ',' alter_generic_option_elem {
    // $$ = lappend($1, $3);
  }
;

alter_generic_option_elem:
  generic_option_elem {
  }
  | SET generic_option_elem {
  }
  | ADD_P generic_option_elem {
  }
  | DROP generic_option_name {
  }
;

generic_option_elem:
  generic_option_name generic_option_arg {
  }
;

generic_option_name:
  ColLabel { $$ = $1; }
;

/* We could use def_arg here, but the spec only requires string literals */
generic_option_arg:
  Sconst {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *             CREATE SERVER name [TYPE] [VERSION] [OPTIONS]
 *
 *****************************************************************************/

CreateForeignServerStmt:
  CREATE SERVER name opt_type opt_foreign_server_version
  FOREIGN DATA_P WRAPPER name create_generic_options {
  }
;

opt_type:
  TYPE_P Sconst           { $$ = $2; }
  | /*EMPTY*/             { $$ = NULL; }
;

foreign_server_version:
  VERSION_P Sconst        { $$ = $2; }
  | VERSION_P NULL_P      { $$ = NULL; }
;

opt_foreign_server_version:
  foreign_server_version  { $$ = $1; }
  | /*EMPTY*/             { $$ = NULL; }
;

/*****************************************************************************
 *
 *    QUERY :
 *        DROP SERVER name
 *
 ****************************************************************************/

DropForeignServerStmt:
  DROP SERVER name opt_drop_behavior {
  }
  |  DROP SERVER IF_P EXISTS name opt_drop_behavior {
  }
;

/*****************************************************************************
 *
 *    QUERY :
 *        ALTER SERVER name [VERSION] [OPTIONS]
 *
 ****************************************************************************/

AlterForeignServerStmt:
  ALTER SERVER name foreign_server_version alter_generic_options {
  }
  | ALTER SERVER name foreign_server_version {
  }
  | ALTER SERVER name alter_generic_options {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *             CREATE FOREIGN TABLE relname (...) SERVER name (...)
 *
 *****************************************************************************/

CreateForeignTableStmt:
  CREATE FOREIGN TABLE qualified_name '(' OptTableElementList ')'
  OptInherit SERVER name create_generic_options {
  }
  | CREATE FOREIGN TABLE IF_P NOT EXISTS qualified_name '(' OptTableElementList ')'
  OptInherit SERVER name create_generic_options {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *             ALTER FOREIGN TABLE relname [...]
 *
 *****************************************************************************/

AlterForeignTableStmt:
  ALTER FOREIGN TABLE relation_expr alter_table_cmds {
  }
  | ALTER FOREIGN TABLE IF_P EXISTS relation_expr alter_table_cmds {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *        IMPORT FOREIGN SCHEMA remote_schema
 *        [ { LIMIT TO | EXCEPT } ( table_list ) ]
 *        FROM SERVER server_name INTO local_schema [ OPTIONS (...) ]
 *
 ****************************************************************************/

ImportForeignSchemaStmt:
  IMPORT_P FOREIGN SCHEMA name import_qualification
  FROM SERVER name INTO name create_generic_options {
  }
;

import_qualification_type:
  LIMIT TO        { $$ = FDW_IMPORT_SCHEMA_LIMIT_TO; }
  | EXCEPT        { $$ = FDW_IMPORT_SCHEMA_EXCEPT; }
;

import_qualification:
  import_qualification_type '(' relation_expr_list ')' {
  }
  | /*EMPTY*/ {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *             CREATE USER MAPPING FOR auth_ident SERVER name [OPTIONS]
 *
 *****************************************************************************/

CreateUserMappingStmt:
  CREATE USER MAPPING FOR auth_ident SERVER name create_generic_options {
  }
;

/* User mapping authorization identifier */
auth_ident:
  RoleSpec {
    // $$ = $1;
  }
  | USER {
    // $$ = makeRoleSpec(ROLESPEC_CURRENT_USER, @1);
  }
;

/*****************************************************************************
 *
 *    QUERY :
 *        DROP USER MAPPING FOR auth_ident SERVER name
 *
 ****************************************************************************/

DropUserMappingStmt:
  DROP USER MAPPING FOR auth_ident SERVER name {
  }
  |  DROP USER MAPPING IF_P EXISTS FOR auth_ident SERVER name {
  }
;

/*****************************************************************************
 *
 *    QUERY :
 *        ALTER USER MAPPING FOR auth_ident SERVER name OPTIONS
 *
 ****************************************************************************/

AlterUserMappingStmt:
  ALTER USER MAPPING FOR auth_ident SERVER name alter_generic_options {
  }
;

/*****************************************************************************
 *
 *    QUERIES:
 *        CREATE POLICY name ON table [FOR cmd] [TO role, ...]
 *          [USING (qual)] [WITH CHECK (with_check)]
 *        ALTER POLICY name ON table [TO role, ...]
 *          [USING (qual)] [WITH CHECK (with_check)]
 *        DROP POLICY name ON table
 *
 *****************************************************************************/

CreatePolicyStmt:
  CREATE POLICY name ON qualified_name RowSecurityDefaultForCmd RowSecurityDefaultToRole
  RowSecurityOptionalExpr RowSecurityOptionalWithCheck {
  }
;

AlterPolicyStmt:
  ALTER POLICY name ON qualified_name RowSecurityOptionalToRole
  RowSecurityOptionalExpr RowSecurityOptionalWithCheck {
  }
;

DropPolicyStmt:
  DROP POLICY name ON any_name opt_drop_behavior {
  }
  | DROP POLICY IF_P EXISTS name ON any_name opt_drop_behavior {
  }
;

RowSecurityOptionalExpr:
  USING '(' a_expr ')'        { $$ = $3; }
  | /* EMPTY */               { $$ = NULL; }
;

RowSecurityOptionalWithCheck:
  WITH CHECK '(' a_expr ')'   { $$ = $4; }
  | /* EMPTY */               { $$ = NULL; }
;

RowSecurityDefaultToRole:
  TO role_list                { $$ = $2; }
  | /* EMPTY */               { }
;

RowSecurityOptionalToRole:
  TO role_list                { $$ = $2; }
  | /* EMPTY */               { $$ = NULL; }
;

RowSecurityDefaultForCmd:
  FOR row_security_cmd        { $$ = $2; }
  | /* EMPTY */               { $$ = "all"; }
    ;

row_security_cmd:
  ALL                         { $$ = "all"; }
  | SELECT                    { $$ = "select"; }
  | INSERT                    { $$ = "insert"; }
  | UPDATE                    { $$ = "update"; }
  | DELETE_P                  { $$ = "delete"; }
;

/*****************************************************************************
 *
 *    QUERIES :
 *        CREATE TRIGGER ...
 *        DROP TRIGGER ...
 *
 *****************************************************************************/

CreateTrigStmt:
  CREATE TRIGGER name TriggerActionTime TriggerEvents ON
  qualified_name TriggerForSpec TriggerWhen EXECUTE PROCEDURE func_name '(' TriggerFuncArgs ')' {
  }
  | CREATE CONSTRAINT TRIGGER name AFTER TriggerEvents ON
  qualified_name OptConstrFromTable ConstraintAttributeSpec
  FOR EACH ROW TriggerWhen EXECUTE PROCEDURE func_name '(' TriggerFuncArgs ')' {
  }
;

TriggerActionTime:
  BEFORE                  { }
  | AFTER                 { }
  | INSTEAD OF            { }
;

TriggerEvents:
  TriggerOneEvent {
    $$ = $1;
  }
  | TriggerEvents OR TriggerOneEvent {
  }
;

TriggerOneEvent:
  INSERT {
  }
  | DELETE_P {
  }
  | UPDATE {
  }
  | UPDATE OF columnList {
  }
  | TRUNCATE {
  }
;

TriggerForSpec:
  FOR TriggerForOptEach TriggerForType {
    $$ = $3;
  }
  | /* EMPTY */ {
  }
;

TriggerForOptEach:
  EACH                  {}
  | /*EMPTY*/           {}
;

TriggerForType:
  ROW                   { $$ = true; }
  | STATEMENT           { $$ = false; }
;

TriggerWhen:
  WHEN '(' a_expr ')'   { $$ = $3; }
  | /*EMPTY*/           { $$ = NULL; }
;

TriggerFuncArgs:
  TriggerFuncArg {
  }
  | TriggerFuncArgs ',' TriggerFuncArg  {
  }
  | /*EMPTY*/ {
  }
;

TriggerFuncArg:
  Iconst {
    // $$ = makeString(psprintf("%d", $1));
  }
  | FCONST {
    // $$ = makeString($1);
  }
  | Sconst {
    // $$ = makeString($1);
  }
  | ColLabel {
    // $$ = makeString($1);
  }
;

OptConstrFromTable:
  FROM qualified_name {
    $$ = $2;
  }
  | /*EMPTY*/ {
    $$ = NULL;
  }
;

ConstraintAttributeSpec:
  /*EMPTY*/ {
    $$ = 0;
  }
  | ConstraintAttributeSpec ConstraintAttributeElem {
  }
;

ConstraintAttributeElem:
  NOT DEFERRABLE            { }
  | DEFERRABLE              { }
  | INITIALLY IMMEDIATE     { }
  | INITIALLY DEFERRED      { }
  | NOT VALID               { }
  | NO INHERIT              { }
;

DropTrigStmt:
  DROP TRIGGER name ON any_name opt_drop_behavior {
  }
  | DROP TRIGGER IF_P EXISTS name ON any_name opt_drop_behavior {
  }
;

/*****************************************************************************
 *
 *    QUERIES :
 *        CREATE EVENT TRIGGER ...
 *        ALTER EVENT TRIGGER ...
 *
 *****************************************************************************/

CreateEventTrigStmt:
  CREATE EVENT TRIGGER name ON ColLabel EXECUTE PROCEDURE func_name '(' ')' {
  }
  | CREATE EVENT TRIGGER name ON ColLabel WHEN event_trigger_when_list
  EXECUTE PROCEDURE func_name '(' ')' {
  }
;

event_trigger_when_list:
  event_trigger_when_item {
  }
  | event_trigger_when_list AND event_trigger_when_item {
  }
;

event_trigger_when_item:
  ColId IN_P '(' event_trigger_value_list ')' {
  }
;

event_trigger_value_list:
  SCONST {
  }
  | event_trigger_value_list ',' SCONST {
  }
;

AlterEventTrigStmt:
  ALTER EVENT TRIGGER name enable_trigger {
  }
;

enable_trigger:
  ENABLE_P                { }
  | ENABLE_P REPLICA      { }
  | ENABLE_P ALWAYS       { }
  | DISABLE_P             { }
;

/*****************************************************************************
 *
 *    QUERIES :
 *        CREATE ASSERTION ...
 *        DROP ASSERTION ...
 *
 *****************************************************************************/

CreateAssertStmt:
  CREATE ASSERTION name CHECK '(' a_expr ')' ConstraintAttributeSpec {
  }
;

DropAssertStmt:
  DROP ASSERTION name opt_drop_behavior {
  }
;


/*****************************************************************************
 *
 *    QUERY :
 *        define (aggregate,operator,type)
 *
 *****************************************************************************/

DefineStmt:
  CREATE AGGREGATE func_name aggr_args definition {
  }
  | CREATE AGGREGATE func_name old_aggr_definition {
  }
  | CREATE OPERATOR any_operator definition {
  }
  | CREATE TYPE_P any_name definition {
  }
  | CREATE TYPE_P any_name {
  }
  | CREATE TYPE_P any_name AS '(' OptTableFuncElementList ')' {
  }
  | CREATE TYPE_P any_name AS ENUM_P '(' opt_enum_val_list ')' {
  }
  | CREATE TYPE_P any_name AS RANGE definition {
  }
  | CREATE TEXT_P SEARCH PARSER any_name definition {
  }
  | CREATE TEXT_P SEARCH DICTIONARY any_name definition {
  }
  | CREATE TEXT_P SEARCH TEMPLATE any_name definition {
  }
  | CREATE TEXT_P SEARCH CONFIGURATION any_name definition {
  }
  | CREATE COLLATION any_name definition {
  }
  | CREATE COLLATION any_name FROM any_name {
  }
;

definition:
  '(' def_list ')' {
    $$ = $2;
  }
;

def_list:
  def_elem {
  }
  | def_list ',' def_elem {
  }
;

def_elem:
  ColLabel '=' def_arg {
  }
  | ColLabel {
  }
;

/* Note: any simple identifier will be returned as a type name! */
def_arg:
  func_type             {}
  | reserved_keyword    {}
  | qual_all_Op         {}
  | NumericOnly         {}
  | Sconst              {}
;

old_aggr_definition:
  '(' old_aggr_list ')' {
    $$ = $2;
  }
;

old_aggr_list:
  old_aggr_elem {
  }
  | old_aggr_list ',' old_aggr_elem  {
  }
;

/*
 * Must use IDENT here to avoid reduce/reduce conflicts; fortunately none of
 * the item names needed in old aggregate definitions are likely to become
 * SQL keywords.
 */
old_aggr_elem:
  IDENT '=' def_arg {
  }
;

opt_enum_val_list:
  enum_val_list {
    $$ = $1;
  }
  | /*EMPTY*/ {
  }
;

enum_val_list:
  Sconst {
  }
  | enum_val_list ',' Sconst {
  }
;

/*****************************************************************************
 *
 *  ALTER TYPE enumtype ADD ...
 *
 *****************************************************************************/

AlterEnumStmt:
  ALTER TYPE_P any_name ADD_P VALUE_P opt_if_not_exists Sconst {
  }
  | ALTER TYPE_P any_name ADD_P VALUE_P opt_if_not_exists Sconst BEFORE Sconst {
  }
  | ALTER TYPE_P any_name ADD_P VALUE_P opt_if_not_exists Sconst AFTER Sconst {
  }
;

opt_if_not_exists:
  IF_P NOT EXISTS            { $$ = true; }
  | /* empty */              { $$ = false; }
;


/*****************************************************************************
 *
 *    QUERIES :
 *        CREATE OPERATOR CLASS ...
 *        CREATE OPERATOR FAMILY ...
 *        ALTER OPERATOR FAMILY ...
 *        DROP OPERATOR CLASS ...
 *        DROP OPERATOR FAMILY ...
 *
 *****************************************************************************/

CreateOpClassStmt:
  CREATE OPERATOR CLASS any_name opt_default FOR TYPE_P Typename
  USING access_method opt_opfamily AS opclass_item_list {
  }
;

opclass_item_list:
  opclass_item {
  }
  | opclass_item_list ',' opclass_item {
  }
;

opclass_item:
  OPERATOR Iconst any_operator opclass_purpose opt_recheck {
  }
  | OPERATOR Iconst any_operator oper_argtypes opclass_purpose opt_recheck {
  }
  | FUNCTION Iconst func_name func_args {
  }
  | FUNCTION Iconst '(' type_list ')' func_name func_args {
  }
  | STORAGE Typename {
  }
;

opt_default:
  DEFAULT                 { $$ = true; }
  | /*EMPTY*/             { $$ = false; }
;

opt_opfamily:
  FAMILY any_name         { $$ = $2; }
  | /*EMPTY*/ {
  }
;

opclass_purpose:
  FOR SEARCH {
  }
  | FOR ORDER BY any_name { $$ = $4; }
  | /*EMPTY*/ {
  }
;

opt_recheck:
  RECHECK {
  }
  | /*EMPTY*/             { $$ = false; }
;


CreateOpFamilyStmt:
  CREATE OPERATOR FAMILY any_name USING access_method {
  }
;

AlterOpFamilyStmt:
  ALTER OPERATOR FAMILY any_name USING access_method ADD_P opclass_item_list {
  }
  | ALTER OPERATOR FAMILY any_name USING access_method DROP opclass_drop_list {
  }
;

opclass_drop_list:
  opclass_drop {
  }
  | opclass_drop_list ',' opclass_drop {
  }
;

opclass_drop:
  OPERATOR Iconst '(' type_list ')' {
  }
  | FUNCTION Iconst '(' type_list ')' {
  }
;

DropOpClassStmt:
  DROP OPERATOR CLASS any_name USING access_method opt_drop_behavior {
  }
  | DROP OPERATOR CLASS IF_P EXISTS any_name USING access_method opt_drop_behavior {
  }
;

DropOpFamilyStmt:
  DROP OPERATOR FAMILY any_name USING access_method opt_drop_behavior {
  }
  | DROP OPERATOR FAMILY IF_P EXISTS any_name USING access_method opt_drop_behavior {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *
 *    DROP OWNED BY username [, username ...] [ RESTRICT | CASCADE ]
 *    REASSIGN OWNED BY username [, username ...] TO username
 *
 *****************************************************************************/
DropOwnedStmt:
  DROP OWNED BY role_list opt_drop_behavior {
  }
;

ReassignOwnedStmt:
  REASSIGN OWNED BY role_list TO RoleSpec {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *
 *    DROP itemtype [ IF EXISTS ] itemname [, itemname ...]
 *           [ RESTRICT | CASCADE ]
 *
 *****************************************************************************/

DropStmt:
  DROP drop_type IF_P EXISTS any_name_list opt_drop_behavior {
  }
  | DROP drop_type any_name_list opt_drop_behavior {
  }
  | DROP TYPE_P type_name_list opt_drop_behavior {
  }
  | DROP TYPE_P IF_P EXISTS type_name_list opt_drop_behavior {
  }
  | DROP DOMAIN_P type_name_list opt_drop_behavior {
  }
  | DROP DOMAIN_P IF_P EXISTS type_name_list opt_drop_behavior {
  }
  | DROP INDEX CONCURRENTLY any_name_list opt_drop_behavior {
  }
  | DROP INDEX CONCURRENTLY IF_P EXISTS any_name_list opt_drop_behavior {
  }
;

drop_type:
  TABLE                           { $$ = OBJECT_TABLE; }
  | SEQUENCE                      { $$ = OBJECT_SEQUENCE; }
  | VIEW                          { $$ = OBJECT_VIEW; }
  | MATERIALIZED VIEW             { $$ = OBJECT_MATVIEW; }
  | INDEX                         { $$ = OBJECT_INDEX; }
  | FOREIGN TABLE                 { $$ = OBJECT_FOREIGN_TABLE; }
  | EVENT TRIGGER                 { $$ = OBJECT_EVENT_TRIGGER; }
  | COLLATION                     { $$ = OBJECT_COLLATION; }
  | CONVERSION_P                  { $$ = OBJECT_CONVERSION; }
  | SCHEMA                        { $$ = OBJECT_SCHEMA; }
  | EXTENSION                     { $$ = OBJECT_EXTENSION; }
  | TEXT_P SEARCH PARSER          { $$ = OBJECT_TSPARSER; }
  | TEXT_P SEARCH DICTIONARY      { $$ = OBJECT_TSDICTIONARY; }
  | TEXT_P SEARCH TEMPLATE        { $$ = OBJECT_TSTEMPLATE; }
  | TEXT_P SEARCH CONFIGURATION   { $$ = OBJECT_TSCONFIGURATION; }
;

any_name_list:
  any_name {
  }
  | any_name_list ',' any_name {
  }
;

any_name:
  ColId {
  }
  | ColId attrs {
  }
;

attrs:
  '.' attr_name {
  }
  | attrs '.' attr_name {
  }
;

type_name_list:
  Typename {
  }
  | type_name_list ',' Typename {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *        truncate table relname1, relname2, ...
 *
 *****************************************************************************/

TruncateStmt:
  TRUNCATE opt_table relation_expr_list opt_restart_seqs opt_drop_behavior {
  }
;

opt_restart_seqs:
  CONTINUE_P IDENTITY_P   { $$ = false; }
  | RESTART IDENTITY_P    { $$ = true; }
  | /* EMPTY */           { $$ = false; }
;

/*****************************************************************************
 *
 *  The COMMENT ON statement can take different forms based upon the type of
 *  the object associated with the comment. The form of the statement is:
 *
 *  COMMENT ON [ [ CONVERSION | COLLATION | DATABASE | DOMAIN |
 *                 EXTENSION | EVENT TRIGGER | FOREIGN DATA WRAPPER |
 *                 FOREIGN TABLE | INDEX | [PROCEDURAL] LANGUAGE |
 *                 MATERIALIZED VIEW | POLICY | ROLE | SCHEMA | SEQUENCE |
 *                 SERVER | TABLE | TABLESPACE |
 *                 TEXT SEARCH CONFIGURATION | TEXT SEARCH DICTIONARY |
 *                 TEXT SEARCH PARSER | TEXT SEARCH TEMPLATE | TYPE |
 *                 VIEW] <objname> |
 *         AGGREGATE <aggname> (arg1, ...) |
 *         CAST (<src type> AS <dst type>) |
 *         COLUMN <relname>.<colname> |
 *         CONSTRAINT <constraintname> ON <relname> |
 *         CONSTRAINT <constraintname> ON DOMAIN <domainname> |
 *         FUNCTION <funcname> (arg1, arg2, ...) |
 *         LARGE OBJECT <oid> |
 *         OPERATOR <op> (leftoperand_typ, rightoperand_typ) |
 *         OPERATOR CLASS <name> USING <access-method> |
 *         OPERATOR FAMILY <name> USING <access-method> |
 *         RULE <rulename> ON <relname> |
 *         TRIGGER <triggername> ON <relname> ]
 *         IS 'text'
 *
 *****************************************************************************/

CommentStmt:
  COMMENT ON comment_type any_name IS comment_text {
  }
  | COMMENT ON TYPE_P Typename IS comment_text {
  }
  | COMMENT ON DOMAIN_P Typename IS comment_text {
  }
  | COMMENT ON AGGREGATE func_name aggr_args IS comment_text {
  }
  | COMMENT ON FUNCTION func_name func_args IS comment_text {
  }
  | COMMENT ON OPERATOR any_operator oper_argtypes IS comment_text {
  }
  | COMMENT ON CONSTRAINT name ON any_name IS comment_text {
  }
  | COMMENT ON CONSTRAINT name ON DOMAIN_P any_name IS comment_text {
  }
  | COMMENT ON POLICY name ON any_name IS comment_text {
  }
  | COMMENT ON RULE name ON any_name IS comment_text {
  }
  | COMMENT ON RULE name IS comment_text {
  }
  | COMMENT ON TRANSFORM FOR Typename LANGUAGE name IS comment_text {
  }
  | COMMENT ON TRIGGER name ON any_name IS comment_text {
  }
  | COMMENT ON OPERATOR CLASS any_name USING access_method IS comment_text {
  }
  | COMMENT ON OPERATOR FAMILY any_name USING access_method IS comment_text {
  }
  | COMMENT ON LARGE_P OBJECT_P NumericOnly IS comment_text {
  }
  | COMMENT ON CAST '(' Typename AS Typename ')' IS comment_text {
  }
  | COMMENT ON opt_procedural LANGUAGE any_name IS comment_text {
  }
;

comment_type:
  COLUMN                          { $$ = OBJECT_COLUMN; }
  | DATABASE                      { $$ = OBJECT_DATABASE; }
  | SCHEMA                        { $$ = OBJECT_SCHEMA; }
  | INDEX                         { $$ = OBJECT_INDEX; }
  | SEQUENCE                      { $$ = OBJECT_SEQUENCE; }
  | TABLE                         { $$ = OBJECT_TABLE; }
  | VIEW                          { $$ = OBJECT_VIEW; }
  | MATERIALIZED VIEW             { $$ = OBJECT_MATVIEW; }
  | COLLATION                     { $$ = OBJECT_COLLATION; }
  | CONVERSION_P                  { $$ = OBJECT_CONVERSION; }
  | TABLESPACE                    { $$ = OBJECT_TABLESPACE; }
  | EXTENSION                     { $$ = OBJECT_EXTENSION; }
  | ROLE                          { $$ = OBJECT_ROLE; }
  | FOREIGN TABLE                 { $$ = OBJECT_FOREIGN_TABLE; }
  | SERVER                        { $$ = OBJECT_FOREIGN_SERVER; }
  | FOREIGN DATA_P WRAPPER        { $$ = OBJECT_FDW; }
  | EVENT TRIGGER                 { $$ = OBJECT_EVENT_TRIGGER; }
  | TEXT_P SEARCH CONFIGURATION   { $$ = OBJECT_TSCONFIGURATION; }
  | TEXT_P SEARCH DICTIONARY      { $$ = OBJECT_TSDICTIONARY; }
  | TEXT_P SEARCH PARSER          { $$ = OBJECT_TSPARSER; }
  | TEXT_P SEARCH TEMPLATE        { $$ = OBJECT_TSTEMPLATE; }
;

comment_text:
  Sconst                { $$ = $1; }
  | NULL_P              { $$ = NULL; }
;


/*****************************************************************************
 *
 *  SECURITY LABEL [FOR <provider>] ON <object> IS <label>
 *
 *  As with COMMENT ON, <object> can refer to various types of database
 *  objects (e.g. TABLE, COLUMN, etc.).
 *
 *****************************************************************************/

SecLabelStmt:
  SECURITY LABEL opt_provider ON security_label_type any_name IS security_label {
  }
  | SECURITY LABEL opt_provider ON TYPE_P Typename IS security_label {
  }
  | SECURITY LABEL opt_provider ON DOMAIN_P Typename IS security_label {
  }
  | SECURITY LABEL opt_provider ON AGGREGATE func_name aggr_args IS security_label {
  }
  | SECURITY LABEL opt_provider ON FUNCTION func_name func_args IS security_label {
  }
  | SECURITY LABEL opt_provider ON LARGE_P OBJECT_P NumericOnly IS security_label {
  }
  | SECURITY LABEL opt_provider ON opt_procedural LANGUAGE any_name IS security_label {
  }
;

opt_provider:
  FOR NonReservedWord_or_Sconst { $$ = $2; }
  | /* empty */                 { $$ = NULL; }
;

security_label_type:
  COLUMN                        { $$ = OBJECT_COLUMN; }
  | DATABASE                    { $$ = OBJECT_DATABASE; }
  | EVENT TRIGGER               { $$ = OBJECT_EVENT_TRIGGER; }
  | FOREIGN TABLE               { $$ = OBJECT_FOREIGN_TABLE; }
  | SCHEMA                      { $$ = OBJECT_SCHEMA; }
  | SEQUENCE                    { $$ = OBJECT_SEQUENCE; }
  | TABLE                       { $$ = OBJECT_TABLE; }
  | ROLE                        { $$ = OBJECT_ROLE; }
  | TABLESPACE                  { $$ = OBJECT_TABLESPACE; }
  | VIEW                        { $$ = OBJECT_VIEW; }
  | MATERIALIZED VIEW           { $$ = OBJECT_MATVIEW; }
;

security_label:
  Sconst        { $$ = $1; }
  | NULL_P      { $$ = NULL; }
;

/*****************************************************************************
 *
 *    QUERY:
 *      fetch/move
 *
 *****************************************************************************/

FetchStmt:
  FETCH fetch_args {
  }
  | MOVE fetch_args {
  }
;

fetch_args:
  cursor_name {
  }
  | from_in cursor_name {
  }
  | NEXT opt_from_in cursor_name {
  }
  | PRIOR opt_from_in cursor_name {
  }
  | FIRST_P opt_from_in cursor_name {
  }
  | LAST_P opt_from_in cursor_name {
  }
  | ABSOLUTE_P SignedIconst opt_from_in cursor_name {
  }
  | RELATIVE_P SignedIconst opt_from_in cursor_name {
  }
  | SignedIconst opt_from_in cursor_name {
  }
  | ALL opt_from_in cursor_name {
  }
  | FORWARD opt_from_in cursor_name {
  }
  | FORWARD SignedIconst opt_from_in cursor_name {
  }
  | FORWARD ALL opt_from_in cursor_name {
  }
  | BACKWARD opt_from_in cursor_name {
  }
  | BACKWARD SignedIconst opt_from_in cursor_name {
  }
  | BACKWARD ALL opt_from_in cursor_name {
  }
;

from_in:
  FROM                  {}
  | IN_P                {}
;

opt_from_in:
  from_in               {}
  | /* EMPTY */         {}
;


/*****************************************************************************
 *
 * GRANT and REVOKE statements
 *
 *****************************************************************************/

GrantStmt:
  GRANT privileges ON privilege_target TO grantee_list opt_grant_grant_option {
  }
;

RevokeStmt:
  REVOKE privileges ON privilege_target FROM grantee_list opt_drop_behavior {
  }
  | REVOKE GRANT OPTION FOR privileges ON privilege_target FROM grantee_list opt_drop_behavior {
  }
;

/*
 * Privilege names are represented as strings; the validity of the privilege
 * names gets checked at execution.  This is a bit annoying but we have little
 * choice because of the syntactic conflict with lists of role names in
 * GRANT/REVOKE.  What's more, we have to call out in the "privilege"
 * production any reserved keywords that need to be usable as privilege names.
 */

/* either ALL [PRIVILEGES] or a list of individual privileges */
privileges:
  privilege_list {
  }
  | ALL {
  }
  | ALL PRIVILEGES {
  }
  | ALL '(' columnList ')' {
  }
  | ALL PRIVILEGES '(' columnList ')' {
  }
;

privilege_list:
  privilege {
  }
  | privilege_list ',' privilege {
  }
;

privilege:
  SELECT opt_column_list {
  }
  | REFERENCES opt_column_list {
  }
  | CREATE opt_column_list {
  }
  | ColId opt_column_list {
  }
;

/* Don't bother trying to fold the first two rules into one using
 * opt_table.  You're going to get conflicts.
 */
privilege_target:
  qualified_name_list {
  }
  | TABLE qualified_name_list {
  }
  | SEQUENCE qualified_name_list {
  }
  | FOREIGN DATA_P WRAPPER name_list {
  }
  | FOREIGN SERVER name_list {
  }
  | FUNCTION function_with_argtypes_list {
  }
  | DATABASE name_list {
  }
  | DOMAIN_P any_name_list {
  }
  | LANGUAGE name_list {
  }
  | LARGE_P OBJECT_P NumericOnly_list {
  }
  | SCHEMA name_list {
  }
  | TABLESPACE name_list {
  }
  | TYPE_P any_name_list {
  }
  | ALL TABLES IN_P SCHEMA name_list {
  }
  | ALL SEQUENCES IN_P SCHEMA name_list {
  }
  | ALL FUNCTIONS IN_P SCHEMA name_list {
  }
;

grantee_list:
  grantee {
  }
  | grantee_list ',' grantee {
  }
;

grantee:
  RoleSpec {
  }
  | GROUP_P RoleSpec {
  }
;


opt_grant_grant_option:
  WITH GRANT OPTION        { $$ = true; }
  | /*EMPTY*/              { $$ = false; }
;

function_with_argtypes_list:
  function_with_argtypes {
  }
  | function_with_argtypes_list ',' function_with_argtypes {
  }
;

function_with_argtypes:
  func_name func_args {
  }
;

/*****************************************************************************
 *
 * GRANT and REVOKE ROLE statements
 *
 *****************************************************************************/

GrantRoleStmt:
  GRANT privilege_list TO role_list opt_grant_admin_option opt_granted_by {
  }
;

RevokeRoleStmt:
  REVOKE privilege_list FROM role_list opt_granted_by opt_drop_behavior {
  }
  | REVOKE ADMIN OPTION FOR privilege_list FROM role_list opt_granted_by opt_drop_behavior {
  }
;

opt_grant_admin_option:
  WITH ADMIN OPTION {
    $$ = true;
  }
  | /*EMPTY*/ {
    $$ = false;
  }
;

opt_granted_by:
  GRANTED BY RoleSpec {
    $$ = $3;
  }
  | /*EMPTY*/ { $$ = NULL; }
;

/*****************************************************************************
 *
 * ALTER DEFAULT PRIVILEGES statement
 *
 *****************************************************************************/

AlterDefaultPrivilegesStmt:
  ALTER DEFAULT PRIVILEGES DefACLOptionList DefACLAction {
  }
;

DefACLOptionList:
  DefACLOptionList DefACLOption {
  }
  | /* EMPTY */ {
  }
;

DefACLOption:
  IN_P SCHEMA name_list {
  }
  | FOR ROLE role_list {
  }
  | FOR USER role_list {
  }
;

/*
 * This should match GRANT/REVOKE, except that individual target objects
 * are not mentioned and we only allow a subset of object types.
 */
DefACLAction:
  GRANT privileges ON defacl_privilege_target TO grantee_list opt_grant_grant_option {
  }
  | REVOKE privileges ON defacl_privilege_target FROM grantee_list opt_drop_behavior {
  }
  | REVOKE GRANT OPTION FOR privileges ON defacl_privilege_target
  FROM grantee_list opt_drop_behavior {
  }
;

defacl_privilege_target:
  TABLES        { $$ = ACL_OBJECT_RELATION; }
  | FUNCTIONS   { $$ = ACL_OBJECT_FUNCTION; }
  | SEQUENCES   { $$ = ACL_OBJECT_SEQUENCE; }
  | TYPES_P     { $$ = ACL_OBJECT_TYPE; }
;


/*****************************************************************************
 *
 *    QUERY: CREATE INDEX
 *
 * Note: we cannot put TABLESPACE clause after WHERE clause unless we are
 * willing to make TABLESPACE a fully reserved word.
 *****************************************************************************/

IndexStmt:
  CREATE opt_unique INDEX opt_concurrently opt_index_name ON qualified_name
  access_method_clause '(' index_params ')' opt_reloptions OptTableSpace where_clause {
  }
  | CREATE opt_unique INDEX opt_concurrently IF_P NOT EXISTS index_name ON qualified_name
  access_method_clause '(' index_params ')' opt_reloptions OptTableSpace where_clause {
  }
;

opt_unique:
  UNIQUE                            { $$ = true; }
  | /*EMPTY*/                       { $$ = false; }
;

opt_concurrently:
  CONCURRENTLY                      { $$ = true; }
  | /*EMPTY*/                       { $$ = false; }
;

opt_index_name:
  index_name                        { $$ = $1; }
  | /*EMPTY*/                       { $$ = NULL; }
;

access_method_clause:
  USING access_method               { $$ = $2; }
  | /*EMPTY*/                       { }
;

index_params:
  index_elem                        { }
  | index_params ',' index_elem     { }
;

/*
 * Index attributes can be either simple column references, or arbitrary
 * expressions in parens.  For backwards-compatibility reasons, we allow
 * an expression that's just a function call to be written without parens.
 */
index_elem:
  ColId opt_collate opt_class opt_asc_desc opt_nulls_order {
  }
  | func_expr_windowless opt_collate opt_class opt_asc_desc opt_nulls_order {
  }
  | '(' a_expr ')' opt_collate opt_class opt_asc_desc opt_nulls_order {
  }
;

opt_collate:
  COLLATE any_name {
    $$ = $2;
  }
  | /*EMPTY*/ {
  }
;

opt_class:
  any_name {
    $$ = $1;
  }
  | USING any_name {
    $$ = $2;
  }
  | /*EMPTY*/ {
  }
;

opt_asc_desc:
  ASC             { $$ = SORTBY_ASC; }
  | DESC          { $$ = SORTBY_DESC; }
  | /*EMPTY*/     { $$ = SORTBY_DEFAULT; }
;

opt_nulls_order:
  NULLS_LA FIRST_P     { $$ = SORTBY_NULLS_FIRST; }
  | NULLS_LA LAST_P    { $$ = SORTBY_NULLS_LAST; }
  | /*EMPTY*/          { $$ = SORTBY_NULLS_DEFAULT; }
;


/*****************************************************************************
 *
 *    QUERY:
 *        create [or replace] function <fname>
 *            [(<type-1> { , <type-n>})]
 *            returns <type-r>
 *            as <filename or code in language as appropriate>
 *            language <lang> [with parameters]
 *
 *****************************************************************************/

CreateFunctionStmt:
  CREATE opt_or_replace FUNCTION func_name func_args_with_defaults
  RETURNS func_return createfunc_opt_list opt_definition {
  }
  | CREATE opt_or_replace FUNCTION func_name func_args_with_defaults
  RETURNS TABLE '(' table_func_column_list ')' createfunc_opt_list opt_definition {
  }
  | CREATE opt_or_replace FUNCTION func_name func_args_with_defaults
  createfunc_opt_list opt_definition {
  }
;

opt_or_replace:
  OR REPLACE                { $$ = true; }
  | /*EMPTY*/               { $$ = false; }
;

func_args:
  '(' func_args_list ')'    { $$ = $2; }
  | '(' ')' {
  }
;

func_args_list:
  func_arg {
  }
  | func_args_list ',' func_arg {
  }
;

/*
 * func_args_with_defaults is separate because we only want to accept
 * defaults in CREATE FUNCTION, not in ALTER etc.
 */
func_args_with_defaults:
  '(' func_args_with_defaults_list ')' {
    $$ = $2;
  }
  | '(' ')' {
  }
;

func_args_with_defaults_list:
  func_arg_with_default {
  }
  | func_args_with_defaults_list ',' func_arg_with_default {
  }
;

/*
 * The style with arg_class first is SQL99 standard, but Oracle puts
 * param_name first; accept both since it's likely people will try both
 * anyway.  Don't bother trying to save productions by letting arg_class
 * have an empty alternative ... you'll get shift/reduce conflicts.
 *
 * We can catch over-specified arguments here if we want to,
 * but for now better to silently swallow typmod, etc.
 * - thomas 2000-03-22
 */
func_arg:
  arg_class param_name func_type {
  }
  | param_name arg_class func_type {
  }
  | param_name func_type {
  }
  | arg_class func_type {
  }
  | func_type {
  }
;

/* INOUT is SQL99 standard, IN OUT is for Oracle compatibility */
arg_class:
  IN_P                { $$ = FUNC_PARAM_IN; }
  | OUT_P             { $$ = FUNC_PARAM_OUT; }
  | INOUT             { $$ = FUNC_PARAM_INOUT; }
  | IN_P OUT_P        { $$ = FUNC_PARAM_INOUT; }
  | VARIADIC          { $$ = FUNC_PARAM_VARIADIC; }
;

/*
 * Ideally param_name should be ColId, but that causes too many conflicts.
 */
param_name:
  type_function_name
;

func_return:
 func_type {}
;

/*
 * We would like to make the %TYPE productions here be ColId attrs etc,
 * but that causes reduce/reduce conflicts.  type_function_name
 * is next best choice.
 */
func_type:
  Typename {
    $$ = $1;
  }
  | type_function_name attrs '%' TYPE_P {
  }
  | SETOF type_function_name attrs '%' TYPE_P {
  }
;

func_arg_with_default:
  func_arg {
  }
  | func_arg DEFAULT a_expr {
  }
  | func_arg '=' a_expr {
  }
;

/* Aggregate args can be most things that function args can be */
aggr_arg:
  func_arg {
  }
;

/*
 * The SQL standard offers no guidance on how to declare aggregate argument
 * lists, since it doesn't have CREATE AGGREGATE etc.  We accept these cases:
 *
 * (*)                  - normal agg with no args
 * (aggr_arg,...)           - normal agg with args
 * (ORDER BY aggr_arg,...)        - ordered-set agg with no direct args
 * (aggr_arg,... ORDER BY aggr_arg,...) - ordered-set agg with direct args
 *
 * The zero-argument case is spelled with '*' for consistency with COUNT(*).
 *
 * An additional restriction is that if the direct-args list ends in a
 * VARIADIC item, the ordered-args list must contain exactly one item that
 * is also VARIADIC with the same type.  This allows us to collapse the two
 * VARIADIC items into one, which is necessary to represent the aggregate in
 * pg_proc.  We check this at the grammar stage so that we can return a list
 * in which the second VARIADIC item is already discarded, avoiding extra work
 * in cases such as DROP AGGREGATE.
 *
 * The return value of this production is a two-element list, in which the
 * first item is a sublist of FunctionParameter nodes (with any duplicate
 * VARIADIC item already dropped, as per above) and the second is an integer
 * Value node, containing -1 if there was no ORDER BY and otherwise the number
 * of argument declarations before the ORDER BY.  (If this number is equal
 * to the first sublist's length, then we dropped a duplicate VARIADIC item.)
 * This representation is passed as-is to CREATE AGGREGATE; for operations
 * on existing aggregates, we can just apply extractArgTypes to the first
 * sublist.
 */
aggr_args:
  '(' '*' ')' {
  }
  | '(' aggr_args_list ')' {
  }
  | '(' ORDER BY aggr_args_list ')' {
  }
  | '(' aggr_args_list ORDER BY aggr_args_list ')' {
  }
;

aggr_args_list:
  aggr_arg {
  }
  | aggr_args_list ',' aggr_arg {
  }
;

createfunc_opt_list:
  /* Must be at least one to prevent conflict */
  createfunc_opt_item {
  }
  | createfunc_opt_list createfunc_opt_item {
  }
;

/*
 * Options common to both CREATE FUNCTION and ALTER FUNCTION
 */
common_func_opt_item:
  CALLED ON NULL_P INPUT_P {
  }
  | RETURNS NULL_P ON NULL_P INPUT_P {
  }
  | STRICT_P {
  }
  | IMMUTABLE {
  }
  | STABLE {
  }
  | VOLATILE {
  }
  | EXTERNAL SECURITY DEFINER {
  }
  | EXTERNAL SECURITY INVOKER {
  }
  | SECURITY DEFINER {
  }
  | SECURITY INVOKER {
  }
  | LEAKPROOF {
  }
  | NOT LEAKPROOF {
  }
  | COST NumericOnly {
  }
  | ROWS NumericOnly {
  }
  | FunctionSetResetClause {
  }
;

createfunc_opt_item:
  AS func_as {
  }
  | LANGUAGE NonReservedWord_or_Sconst {
  }
  | TRANSFORM transform_type_list {
  }
  | WINDOW {
  }
  | common_func_opt_item {
  }
;

func_as:
  Sconst {
  }
  | Sconst ',' Sconst {
  }
;

transform_type_list:
  FOR TYPE_P Typename {
  }
  | transform_type_list ',' FOR TYPE_P Typename {
  }
;

opt_definition:
  WITH definition           { $$ = $2; }
  | /*EMPTY*/ {
  }
;

table_func_column:
  param_name func_type {
  }
;

table_func_column_list:
  table_func_column {
  }
  | table_func_column_list ',' table_func_column {
  }
;

/*****************************************************************************
 * ALTER FUNCTION
 *
 * RENAME and OWNER subcommands are already provided by the generic
 * ALTER infrastructure, here we just specify alterations that can
 * only be applied to functions.
 *
 *****************************************************************************/
AlterFunctionStmt:
  ALTER FUNCTION function_with_argtypes alterfunc_opt_list opt_restrict {
  }
;

alterfunc_opt_list:
  /* At least one option must be specified */
  common_func_opt_item {
  }
  | alterfunc_opt_list common_func_opt_item {
  }
;

/* Ignored, merely for SQL compliance */
opt_restrict:
  RESTRICT
  | /* EMPTY */
;


/*****************************************************************************
 *
 *    QUERY:
 *
 *    DROP FUNCTION funcname (arg1, arg2, ...) [ RESTRICT | CASCADE ]
 *    DROP AGGREGATE aggname (arg1, ...) [ RESTRICT | CASCADE ]
 *    DROP OPERATOR opname (leftoperand_typ, rightoperand_typ) [ RESTRICT | CASCADE ]
 *
 *****************************************************************************/

RemoveFuncStmt:
  DROP FUNCTION func_name func_args opt_drop_behavior {
  }
  | DROP FUNCTION IF_P EXISTS func_name func_args opt_drop_behavior {
  }
;

RemoveAggrStmt:
  DROP AGGREGATE func_name aggr_args opt_drop_behavior {
  }
  | DROP AGGREGATE IF_P EXISTS func_name aggr_args opt_drop_behavior {
  }
;

RemoveOperStmt:
  DROP OPERATOR any_operator oper_argtypes opt_drop_behavior {
  }
  | DROP OPERATOR IF_P EXISTS any_operator oper_argtypes opt_drop_behavior {
  }
;

oper_argtypes:
  '(' Typename ')' {
  }
  | '(' Typename ',' Typename ')' {
  }
  | '(' NONE ',' Typename ')' {                                                    /* left unary */
  }
  | '(' Typename ',' NONE ')' {                                                   /* right unary */
  }
;

any_operator:
  all_Op {
  }
  | ColId '.' any_operator {
  }
;

/*****************************************************************************
 *
 *    DO <anonymous code block> [ LANGUAGE language ]
 *
 * We use a DefElem list for future extensibility, and to allow flexibility
 * in the clause order.
 *
 *****************************************************************************/

DoStmt:
  DO dostmt_opt_list {
  }
;

dostmt_opt_list:
  dostmt_opt_item {
  }
  | dostmt_opt_list dostmt_opt_item {
  }
;

dostmt_opt_item:
  Sconst {
  }
  | LANGUAGE NonReservedWord_or_Sconst {
  }
;

/*****************************************************************************
 *
 *    CREATE CAST / DROP CAST
 *
 *****************************************************************************/

CreateCastStmt:
  CREATE CAST '(' Typename AS Typename ')'
  WITH FUNCTION function_with_argtypes cast_context {
  }
  | CREATE CAST '(' Typename AS Typename ')'
  WITHOUT FUNCTION cast_context {
  }
  | CREATE CAST '(' Typename AS Typename ')'
  WITH INOUT cast_context {
  }
;

cast_context:
  AS IMPLICIT_P             { }
  | AS ASSIGNMENT           { }
  | /*EMPTY*/               { }
;

DropCastStmt:
  DROP CAST opt_if_exists '(' Typename AS Typename ')' opt_drop_behavior {
  }
;

opt_if_exists:
  IF_P EXISTS               { $$ = true; }
  | /*EMPTY*/               { $$ = false; }
;


/*****************************************************************************
 *
 *    CREATE TRANSFORM / DROP TRANSFORM
 *
 *****************************************************************************/

CreateTransformStmt:
  CREATE opt_or_replace TRANSFORM FOR Typename LANGUAGE name '(' transform_element_list ')' {
  }
;

transform_element_list:
  FROM SQL_P WITH FUNCTION function_with_argtypes ','
  TO SQL_P WITH FUNCTION function_with_argtypes {
  }
  | TO SQL_P WITH FUNCTION function_with_argtypes ','
  FROM SQL_P WITH FUNCTION function_with_argtypes {
  }
  | FROM SQL_P WITH FUNCTION function_with_argtypes {
  }
  | TO SQL_P WITH FUNCTION function_with_argtypes {
  }
;

DropTransformStmt:
  DROP TRANSFORM opt_if_exists FOR Typename LANGUAGE name opt_drop_behavior {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *
 *    REINDEX [ (options) ] type <name>
 *****************************************************************************/

ReindexStmt:
  REINDEX reindex_target_type qualified_name {
  }
  | REINDEX reindex_target_multitable name {
  }
  | REINDEX '(' reindex_option_list ')' reindex_target_type qualified_name {
  }
  | REINDEX '(' reindex_option_list ')' reindex_target_multitable name {
  }
;

reindex_target_type:
  INDEX             { $$ = REINDEX_OBJECT_INDEX; }
  | TABLE           { $$ = REINDEX_OBJECT_TABLE; }
;
reindex_target_multitable:
  SCHEMA            { $$ = REINDEX_OBJECT_SCHEMA; }
  | SYSTEM_P        { $$ = REINDEX_OBJECT_SYSTEM; }
  | DATABASE        { $$ = REINDEX_OBJECT_DATABASE; }
;
reindex_option_list:
  reindex_option_elem {
  }
  | reindex_option_list ',' reindex_option_elem {
  }
;
reindex_option_elem:
  VERBOSE           { $$ = REINDEXOPT_VERBOSE; }
;

/*****************************************************************************
 *
 * ALTER TABLESPACE
 *
 *****************************************************************************/

AlterTblSpcStmt:
  ALTER TABLESPACE name SET reloptions {
  }
  | ALTER TABLESPACE name RESET reloptions {
  }
;

/*****************************************************************************
 *
 * ALTER THING name RENAME TO newname
 *
 *****************************************************************************/

RenameStmt:
  ALTER AGGREGATE func_name aggr_args RENAME TO name {
  }
  | ALTER COLLATION any_name RENAME TO name {
  }
  | ALTER CONVERSION_P any_name RENAME TO name {
  }
  | ALTER DATABASE database_name RENAME TO database_name {
  }
  | ALTER DOMAIN_P any_name RENAME TO name {
  }
  | ALTER DOMAIN_P any_name RENAME CONSTRAINT name TO name {
  }
  | ALTER FOREIGN DATA_P WRAPPER name RENAME TO name {
  }
  | ALTER FUNCTION function_with_argtypes RENAME TO name {
  }
  | ALTER GROUP_P RoleId RENAME TO RoleId {
  }
  | ALTER opt_procedural LANGUAGE name RENAME TO name {
  }
  | ALTER OPERATOR CLASS any_name USING access_method RENAME TO name {
  }
  | ALTER OPERATOR FAMILY any_name USING access_method RENAME TO name {
  }
  | ALTER POLICY name ON qualified_name RENAME TO name {
  }
  | ALTER POLICY IF_P EXISTS name ON qualified_name RENAME TO name {
  }
  | ALTER SCHEMA name RENAME TO name {
  }
  | ALTER SERVER name RENAME TO name {
  }
  | ALTER TABLE relation_expr RENAME TO name {
  }
  | ALTER TABLE IF_P EXISTS relation_expr RENAME TO name {
  }
  | ALTER SEQUENCE qualified_name RENAME TO name {
  }
  | ALTER SEQUENCE IF_P EXISTS qualified_name RENAME TO name {
  }
  | ALTER VIEW qualified_name RENAME TO name {
  }
  | ALTER VIEW IF_P EXISTS qualified_name RENAME TO name {
  }
  | ALTER MATERIALIZED VIEW qualified_name RENAME TO name {
  }
  | ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name RENAME TO name {
  }
  | ALTER INDEX qualified_name RENAME TO name {
  }
  | ALTER INDEX IF_P EXISTS qualified_name RENAME TO name {
  }
  | ALTER FOREIGN TABLE relation_expr RENAME TO name {
  }
  | ALTER FOREIGN TABLE IF_P EXISTS relation_expr RENAME TO name {
  }
  | ALTER TABLE relation_expr RENAME opt_column name TO name {
  }
  | ALTER TABLE IF_P EXISTS relation_expr RENAME opt_column name TO name {
  }
  | ALTER MATERIALIZED VIEW qualified_name RENAME opt_column name TO name {
  }
  | ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name RENAME opt_column name TO name {
  }
  | ALTER TABLE relation_expr RENAME CONSTRAINT name TO name {
  }
  | ALTER TABLE IF_P EXISTS relation_expr RENAME CONSTRAINT name TO name {
  }
  | ALTER FOREIGN TABLE relation_expr RENAME opt_column name TO name {
  }
  | ALTER FOREIGN TABLE IF_P EXISTS relation_expr RENAME opt_column name TO name {
  }
  | ALTER RULE name ON qualified_name RENAME TO name {
  }
  | ALTER TRIGGER name ON qualified_name RENAME TO name {
  }
  | ALTER EVENT TRIGGER name RENAME TO name {
  }
  | ALTER ROLE RoleId RENAME TO RoleId {
  }
  | ALTER USER RoleId RENAME TO RoleId {
  }
  | ALTER TABLESPACE name RENAME TO name {
  }
  | ALTER TEXT_P SEARCH PARSER any_name RENAME TO name {
  }
  | ALTER TEXT_P SEARCH DICTIONARY any_name RENAME TO name {
  }
  | ALTER TEXT_P SEARCH TEMPLATE any_name RENAME TO name {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name RENAME TO name {
  }
  | ALTER TYPE_P any_name RENAME TO name {
  }
  | ALTER TYPE_P any_name RENAME ATTRIBUTE name TO name opt_drop_behavior {
  }
;

opt_column:
  COLUMN                    { $$ = 1; }
  | /*EMPTY*/               { $$ = 0; }
;

opt_set_data:
  SET DATA_P                { $$ = 1; }
  | /*EMPTY*/               { $$ = 0; }
;

/*****************************************************************************
 *
 * ALTER THING name SET SCHEMA name
 *
 *****************************************************************************/

AlterObjectSchemaStmt:
  ALTER AGGREGATE func_name aggr_args SET SCHEMA name {
  }
  | ALTER COLLATION any_name SET SCHEMA name {
  }
  | ALTER CONVERSION_P any_name SET SCHEMA name {
  }
  | ALTER DOMAIN_P any_name SET SCHEMA name {
  }
  | ALTER EXTENSION any_name SET SCHEMA name {
  }
  | ALTER FUNCTION function_with_argtypes SET SCHEMA name {
  }
  | ALTER OPERATOR any_operator oper_argtypes SET SCHEMA name {
  }
  | ALTER OPERATOR CLASS any_name USING access_method SET SCHEMA name {
  }
  | ALTER OPERATOR FAMILY any_name USING access_method SET SCHEMA name {
  }
  | ALTER TABLE relation_expr SET SCHEMA name {
  }
  | ALTER TABLE IF_P EXISTS relation_expr SET SCHEMA name {
  }
  | ALTER TEXT_P SEARCH PARSER any_name SET SCHEMA name {
  }
  | ALTER TEXT_P SEARCH DICTIONARY any_name SET SCHEMA name {
  }
  | ALTER TEXT_P SEARCH TEMPLATE any_name SET SCHEMA name {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name SET SCHEMA name {
  }
  | ALTER SEQUENCE qualified_name SET SCHEMA name {
  }
  | ALTER SEQUENCE IF_P EXISTS qualified_name SET SCHEMA name {
  }
  | ALTER VIEW qualified_name SET SCHEMA name {
  }
  | ALTER VIEW IF_P EXISTS qualified_name SET SCHEMA name {
  }
  | ALTER MATERIALIZED VIEW qualified_name SET SCHEMA name {
  }
  | ALTER MATERIALIZED VIEW IF_P EXISTS qualified_name SET SCHEMA name {
  }
  | ALTER FOREIGN TABLE relation_expr SET SCHEMA name {
  }
  | ALTER FOREIGN TABLE IF_P EXISTS relation_expr SET SCHEMA name {
  }
  | ALTER TYPE_P any_name SET SCHEMA name {
  }
;

/*****************************************************************************
 *
 * ALTER THING name OWNER TO newname
 *
 *****************************************************************************/

AlterOwnerStmt:
  ALTER AGGREGATE func_name aggr_args OWNER TO RoleSpec {
  }
  | ALTER COLLATION any_name OWNER TO RoleSpec {
  }
  | ALTER CONVERSION_P any_name OWNER TO RoleSpec {
  }
  | ALTER DATABASE database_name OWNER TO RoleSpec {
  }
  | ALTER DOMAIN_P any_name OWNER TO RoleSpec {
  }
  | ALTER FUNCTION function_with_argtypes OWNER TO RoleSpec {
  }
  | ALTER opt_procedural LANGUAGE name OWNER TO RoleSpec {
  }
  | ALTER LARGE_P OBJECT_P NumericOnly OWNER TO RoleSpec {
  }
  | ALTER OPERATOR any_operator oper_argtypes OWNER TO RoleSpec {
  }
  | ALTER OPERATOR CLASS any_name USING access_method OWNER TO RoleSpec {
  }
  | ALTER OPERATOR FAMILY any_name USING access_method OWNER TO RoleSpec {
  }
  | ALTER SCHEMA name OWNER TO RoleSpec {
  }
  | ALTER TYPE_P any_name OWNER TO RoleSpec {
  }
  | ALTER TABLESPACE name OWNER TO RoleSpec {
  }
  | ALTER TEXT_P SEARCH DICTIONARY any_name OWNER TO RoleSpec {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name OWNER TO RoleSpec {
  }
  | ALTER FOREIGN DATA_P WRAPPER name OWNER TO RoleSpec {
  }
  | ALTER SERVER name OWNER TO RoleSpec {
  }
  | ALTER EVENT TRIGGER name OWNER TO RoleSpec {
  }
;


/*****************************************************************************
 *
 *    QUERY:  Define Rewrite Rule
 *
 *****************************************************************************/

RuleStmt:
  CREATE opt_or_replace RULE name AS ON event TO qualified_name where_clause
  DO opt_instead RuleActionList {
  }
;

RuleActionList:
  NOTHING {
  }
  | RuleActionStmt {
  }
  | '(' RuleActionMulti ')' {
  }
;

/* the thrashing around here is to discard "empty" statements... */
RuleActionMulti:
  RuleActionMulti ';' RuleActionStmtOrEmpty {
  }
  | RuleActionStmtOrEmpty {
  }
;

RuleActionStmt:
  SelectStmt
  | InsertStmt
  | UpdateStmt
  | DeleteStmt
  | NotifyStmt
;

RuleActionStmtOrEmpty:
  RuleActionStmt              { $$ = $1; }
  | /*EMPTY*/                 { $$ = NULL; }
;

event:
  SELECT                  { $$ = CMD_SELECT; }
  | UPDATE                { $$ = CMD_UPDATE; }
  | DELETE_P              { $$ = CMD_DELETE; }
  | INSERT                { $$ = CMD_INSERT; }
;

opt_instead:
  INSTEAD                 { $$ = true; }
  | ALSO                  { $$ = false; }
  | /*EMPTY*/             { $$ = false; }
;

DropRuleStmt:
  DROP RULE name ON any_name opt_drop_behavior {
  }
  | DROP RULE IF_P EXISTS name ON any_name opt_drop_behavior {
  }
;


/*****************************************************************************
 *
 *    QUERY:
 *        NOTIFY <identifier> can appear both in rule bodies and
 *        as a query-level command
 *
 *****************************************************************************/

NotifyStmt:
  NOTIFY ColId notify_payload {
  }
;

notify_payload:
  ',' Sconst              { $$ = $2; }
  | /*EMPTY*/             { $$ = NULL; }
;

ListenStmt:
  LISTEN ColId {
  }
;

UnlistenStmt:
  UNLISTEN ColId {
  }
  | UNLISTEN '*' {
  }
;


/*****************************************************************************
 *
 *    Transactions:
 *
 *    BEGIN / COMMIT / ROLLBACK
 *    (also older versions END / ABORT)
 *
 *****************************************************************************/

TransactionStmt:
  ABORT_P opt_transaction {
  }
  | BEGIN_P opt_transaction transaction_mode_list_or_empty {
  }
  | START TRANSACTION transaction_mode_list_or_empty {
  }
  | COMMIT opt_transaction {
  }
  | END_P opt_transaction {
  }
  | ROLLBACK opt_transaction {
  }
  | SAVEPOINT ColId {
  }
  | RELEASE SAVEPOINT ColId {
  }
  | RELEASE ColId {
  }
  | ROLLBACK opt_transaction TO SAVEPOINT ColId {
  }
  | ROLLBACK opt_transaction TO ColId {
  }
  | PREPARE TRANSACTION Sconst {
  }
  | COMMIT PREPARED Sconst {
  }
  | ROLLBACK PREPARED Sconst {
  }
;

opt_transaction:
  WORK                      {}
  | TRANSACTION             {}
  | /*EMPTY*/               {}
;

transaction_mode_item:
  ISOLATION LEVEL iso_level {
  }
  | READ ONLY {
  }
  | READ WRITE {
  }
  | DEFERRABLE {
  }
  | NOT DEFERRABLE {
  }
;

/* Syntax with commas is SQL-spec, without commas is Postgres historical */
transaction_mode_list:
  transaction_mode_item {
  }
  | transaction_mode_list ',' transaction_mode_item {
  }
  | transaction_mode_list transaction_mode_item {
  }
;

transaction_mode_list_or_empty:
  transaction_mode_list   { $$ = $1; }
  | /* EMPTY */ {
  }
;


/*****************************************************************************
 *
 *  QUERY:
 *    CREATE [ OR REPLACE ] [ TEMP ] VIEW <viewname> '('target-list ')'
 *      AS <query> [ WITH [ CASCADED | LOCAL ] CHECK OPTION ]
 *
 *****************************************************************************/

ViewStmt:
  CREATE OptTemp VIEW qualified_name opt_column_list opt_reloptions
  AS SelectStmt opt_check_option {
  }
  | CREATE OR REPLACE OptTemp VIEW qualified_name opt_column_list opt_reloptions
  AS SelectStmt opt_check_option {
  }
  | CREATE OptTemp RECURSIVE VIEW qualified_name '(' columnList ')' opt_reloptions
  AS SelectStmt opt_check_option {
  }
  | CREATE OR REPLACE OptTemp RECURSIVE VIEW qualified_name '(' columnList ')' opt_reloptions
  AS SelectStmt opt_check_option {
  }
;

opt_check_option:
  WITH CHECK OPTION             { $$ = CASCADED_CHECK_OPTION; }
  | WITH CASCADED CHECK OPTION  { $$ = CASCADED_CHECK_OPTION; }
  | WITH LOCAL CHECK OPTION     { $$ = LOCAL_CHECK_OPTION; }
  | /* EMPTY */                 { $$ = NO_CHECK_OPTION; }
;

/*****************************************************************************
 *
 *    QUERY:
 *        LOAD "filename"
 *
 *****************************************************************************/

LoadStmt:
  LOAD file_name {
  }
;

/*****************************************************************************
 *
 *    CREATE DATABASE
 *
 *****************************************************************************/

CreatedbStmt:
  CREATE DATABASE database_name opt_with createdb_opt_list {
  }
;

createdb_opt_list:
  createdb_opt_items        { $$ = $1; }
  | /* EMPTY */ {
  }
;

createdb_opt_items:
  createdb_opt_item                       { }
  | createdb_opt_items createdb_opt_item  { }
;

createdb_opt_item:
  createdb_opt_name opt_equal SignedIconst {
  }
  | createdb_opt_name opt_equal opt_boolean_or_string {
  }
  | createdb_opt_name opt_equal DEFAULT {
  }
;

/*
 * Ideally we'd use ColId here, but that causes shift/reduce conflicts against
 * the ALTER DATABASE SET/RESET syntaxes.  Instead call out specific keywords
 * we need, and allow IDENT so that database option names don't have to be
 * parser keywords unless they are already keywords for other reasons.
 *
 * XXX this coding technique is fragile since if someone makes a formerly
 * non-keyword option name into a keyword and forgets to add it here, the
 * option will silently break.  Best defense is to provide a regression test
 * exercising every such option, at least at the syntax level.
 */
createdb_opt_name:
  IDENT                 { $$ = parser_strdup($1); }
  | CONNECTION LIMIT    { $$ = parser_strdup("connection_limit"); }
  | ENCODING            { $$ = parser_strdup($1); }
  | LOCATION            { $$ = parser_strdup($1); }
  | OWNER               { $$ = parser_strdup($1); }
  | TABLESPACE          { $$ = parser_strdup($1); }
  | TEMPLATE            { $$ = parser_strdup($1); }
;

/*
 *  Though the equals sign doesn't match other WITH options, pg_dump uses
 *  equals for backward compatibility, and it doesn't seem worth removing it.
 */
opt_equal:
  '='                   {}
  | /*EMPTY*/           {}
;


/*****************************************************************************
 *
 *    ALTER DATABASE
 *
 *****************************************************************************/

AlterDatabaseStmt:
  ALTER DATABASE database_name WITH createdb_opt_list {
  }
  | ALTER DATABASE database_name createdb_opt_list {
  }
  | ALTER DATABASE database_name SET TABLESPACE name {
  }
;

AlterDatabaseSetStmt:
  ALTER DATABASE database_name SetResetClause {
  }
;


/*****************************************************************************
 *
 *    DROP DATABASE [ IF EXISTS ]
 *
 * This is implicitly CASCADE, no need for drop behavior
 *****************************************************************************/

DropdbStmt:
  DROP DATABASE database_name {
  }
  | DROP DATABASE IF_P EXISTS database_name {
  }
;


/*****************************************************************************
 *
 *    ALTER SYSTEM
 *
 * This is used to change configuration parameters persistently.
 *****************************************************************************/

AlterSystemStmt:
  ALTER SYSTEM_P SET generic_set {
  }
  | ALTER SYSTEM_P RESET generic_reset{
  }
;


/*****************************************************************************
 *
 * Manipulate a domain
 *
 *****************************************************************************/

CreateDomainStmt:
  CREATE DOMAIN_P any_name opt_as Typename ColQualList {
  }
;

AlterDomainStmt:
  /* ALTER DOMAIN <domain> {SET DEFAULT <expr>|DROP DEFAULT} */
  ALTER DOMAIN_P any_name alter_column_default {
  }
  /* ALTER DOMAIN <domain> DROP NOT NULL */
  | ALTER DOMAIN_P any_name DROP NOT NULL_P {
  }
  /* ALTER DOMAIN <domain> SET NOT NULL */
  | ALTER DOMAIN_P any_name SET NOT NULL_P {
  }
  /* ALTER DOMAIN <domain> ADD CONSTRAINT ... */
  | ALTER DOMAIN_P any_name ADD_P TableConstraint {
  }
  /* ALTER DOMAIN <domain> DROP CONSTRAINT <name> [RESTRICT|CASCADE] */
  | ALTER DOMAIN_P any_name DROP CONSTRAINT name opt_drop_behavior {
  }
  /* ALTER DOMAIN <domain> DROP CONSTRAINT IF EXISTS <name> [RESTRICT|CASCADE] */
  | ALTER DOMAIN_P any_name DROP CONSTRAINT IF_P EXISTS name opt_drop_behavior {
  }
  /* ALTER DOMAIN <domain> VALIDATE CONSTRAINT <name> */
  | ALTER DOMAIN_P any_name VALIDATE CONSTRAINT name {
  }
;

opt_as:
  AS                        {}
  | /* EMPTY */             {}
;


/*****************************************************************************
 *
 * Manipulate a text search dictionary or configuration
 *
 *****************************************************************************/

AlterTSDictionaryStmt:
  ALTER TEXT_P SEARCH DICTIONARY any_name definition {
  }
;

AlterTSConfigurationStmt:
  ALTER TEXT_P SEARCH CONFIGURATION any_name ADD_P MAPPING FOR name_list any_with any_name_list {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name ALTER MAPPING FOR name_list any_with any_name_list {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name ALTER MAPPING REPLACE any_name any_with any_name {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name ALTER MAPPING FOR name_list
    REPLACE any_name any_with any_name {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name DROP MAPPING FOR name_list {
  }
  | ALTER TEXT_P SEARCH CONFIGURATION any_name DROP MAPPING IF_P EXISTS FOR name_list {
  }
;

/* Use this if TIME or ORDINALITY after WITH should be taken as an identifier */
any_with:
  WITH                    {}
  | WITH_LA               {}
;


/*****************************************************************************
 *
 * Manipulate a conversion
 *
 *    CREATE [DEFAULT] CONVERSION <conversion_name>
 *    FOR <encoding_name> TO <encoding_name> FROM <func_name>
 *
 *****************************************************************************/

CreateConversionStmt:
  CREATE opt_default CONVERSION_P any_name FOR Sconst TO Sconst FROM any_name {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *        CLUSTER [VERBOSE] <qualified_name> [ USING <index_name> ]
 *        CLUSTER [VERBOSE]
 *        CLUSTER [VERBOSE] <index_name> ON <qualified_name> (for pre-8.3)
 *
 *****************************************************************************/

ClusterStmt:
  CLUSTER opt_verbose qualified_name cluster_index_specification {
  }
  | CLUSTER opt_verbose {
  }
  /* kept for pre-8.3 compatibility */
  | CLUSTER opt_verbose index_name ON qualified_name {
  }
;

cluster_index_specification:
  USING index_name    { $$ = $2; }
  | /*EMPTY*/         { $$ = NULL; }
;


/*****************************************************************************
 *
 *    QUERY:
 *        VACUUM
 *        ANALYZE
 *
 *****************************************************************************/

VacuumStmt:
  VACUUM opt_full opt_freeze opt_verbose {
  }
  | VACUUM opt_full opt_freeze opt_verbose qualified_name {
  }
  | VACUUM opt_full opt_freeze opt_verbose AnalyzeStmt {
  }
  | VACUUM '(' vacuum_option_list ')' {
  }
  | VACUUM '(' vacuum_option_list ')' qualified_name opt_name_list {
  }
;

vacuum_option_list:
  vacuum_option_elem {
  }
  | vacuum_option_list ',' vacuum_option_elem {
  }
;

vacuum_option_elem:
  analyze_keyword   { $$ = VACOPT_ANALYZE; }
  | VERBOSE     { $$ = VACOPT_VERBOSE; }
  | FREEZE      { $$ = VACOPT_FREEZE; }
  | FULL        { $$ = VACOPT_FULL; }
;

AnalyzeStmt:
  analyze_keyword opt_verbose {
  }
  | analyze_keyword opt_verbose qualified_name opt_name_list {
  }
;

analyze_keyword:
  ANALYZE                         {}
  | ANALYSE /* British */         {}
    ;

opt_verbose:
  VERBOSE                   { $$ = true; }
  | /*EMPTY*/               { $$ = false; }
;

opt_full:
  FULL                      { $$ = true; }
  | /*EMPTY*/               { $$ = false; }
    ;

opt_freeze:
  FREEZE                    { $$ = true; }
  | /*EMPTY*/               { $$ = false; }
;

opt_name_list:
  '(' name_list ')'         { $$ = $2; }
  | /*EMPTY*/ {
  }
;


/*****************************************************************************
 *
 *    QUERY:
 *        EXPLAIN [ANALYZE] [VERBOSE] query
 *        EXPLAIN ( options ) query
 *
 *****************************************************************************/

ExplainStmt:
  EXPLAIN ExplainableStmt {
  }
  | EXPLAIN analyze_keyword opt_verbose ExplainableStmt {
  }
  | EXPLAIN VERBOSE ExplainableStmt {
  }
  | EXPLAIN '(' explain_option_list ')' ExplainableStmt {
  }
;

ExplainableStmt:
  SelectStmt
  | InsertStmt
  | UpdateStmt
  | DeleteStmt
  | DeclareCursorStmt
  | CreateAsStmt
  | CreateMatViewStmt
  | RefreshMatViewStmt
  | ExecuteStmt         /* by default all are $$=$1 */
;

explain_option_list:
  explain_option_elem {
  }
  | explain_option_list ',' explain_option_elem {
  }
;

explain_option_elem:
  explain_option_name explain_option_arg {
  }
;

explain_option_name:
  NonReservedWord     { $$ = $1; }
  | analyze_keyword   { $$ = "analyze"; }
;

explain_option_arg:
  opt_boolean_or_string {
  }
  | NumericOnly {
  }
  | /* EMPTY */ {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *        PREPARE <plan_name> [(args, ...)] AS <query>
 *
 *****************************************************************************/

PrepareStmt:
  PREPARE name prep_type_clause AS PreparableStmt {
  }
;

prep_type_clause:
  '(' type_list ')' {
  }
  | /* EMPTY */ {
  }
;

PreparableStmt:
  SelectStmt
  | InsertStmt
  | UpdateStmt
  | DeleteStmt          /* by default all are $$=$1 */
;

/*****************************************************************************
 *
 * EXECUTE <plan_name> [(params, ...)]
 * CREATE TABLE <name> AS EXECUTE <plan_name> [(params, ...)]
 *
 *****************************************************************************/

ExecuteStmt:
  EXECUTE name execute_param_clause {
  }
  | CREATE OptTemp TABLE create_as_target AS
    EXECUTE name execute_param_clause opt_with_data {
  }
;

execute_param_clause:
  '(' expr_list ')' {
  }
  | /* EMPTY */ {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *        DEALLOCATE [PREPARE] <plan_name>
 *
 *****************************************************************************/

DeallocateStmt:
  DEALLOCATE name {
  }
  | DEALLOCATE PREPARE name {
  }
  | DEALLOCATE ALL {
  }
  | DEALLOCATE PREPARE ALL {
  }
;

/*****************************************************************************
 *
 *    QUERY:
 *        INSERT STATEMENTS
 *
 *****************************************************************************/

InsertStmt:
  opt_with_clause INSERT INTO insert_target insert_rest opt_on_conflict returning_clause {
  }
;

/*
 * Can't easily make AS optional here, because VALUES in insert_rest would
 * have a shift/reduce conflict with VALUES as an optional alias.  We could
 * easily allow unreserved_keywords as optional aliases, but that'd be an odd
 * divergence from other places.  So just require AS for now.
 */
insert_target:
  qualified_name {
  }
  | qualified_name AS ColId {
  }
;

insert_rest:
  SelectStmt {
  }
  | '(' insert_column_list ')' SelectStmt {
  }
  | DEFAULT VALUES {
  }
;

insert_column_list:
  insert_column_item {
  }
  | insert_column_list ',' insert_column_item {
  }
;

insert_column_item:
  ColId opt_indirection {
  }
;

opt_on_conflict:
  ON CONFLICT opt_conf_expr DO UPDATE SET set_clause_list where_clause {
  }
  | ON CONFLICT opt_conf_expr DO NOTHING {
  }
  | /*EMPTY*/ {
  }
;

opt_conf_expr:
  '(' index_params ')' where_clause {
  }
  | ON CONSTRAINT name {
  }
  | /*EMPTY*/ {
  }
;

returning_clause:
  RETURNING target_list   { $$ = $2; }
  | /* EMPTY */ {
  }
;


/*****************************************************************************
 *
 *    QUERY:
 *        DELETE STATEMENTS
 *
 *****************************************************************************/

DeleteStmt:
  opt_with_clause DELETE_P FROM relation_expr_opt_alias
  using_clause where_or_current_clause returning_clause {
}
;

using_clause:
  USING from_list           { $$ = $2; }
  | /*EMPTY*/ {
  }
;


/*****************************************************************************
 *
 *    QUERY:
 *        LOCK TABLE
 *
 *****************************************************************************/

LockStmt:
  LOCK_P opt_table relation_expr_list opt_lock opt_nowait {
  }
;

opt_lock:
  IN_P lock_type MODE {
  }
  | /*EMPTY*/ {
  }
;

lock_type:
  ACCESS SHARE {
  }
  | ROW SHARE {
  }
  | ROW EXCLUSIVE {
  }
  | SHARE UPDATE EXCLUSIVE {
  }
  | SHARE {
  }
  | SHARE ROW EXCLUSIVE {
  }
  | EXCLUSIVE {
  }
  | ACCESS EXCLUSIVE {
  }
;

opt_nowait:
  NOWAIT                { $$ = true; }
  | /*EMPTY*/           { $$ = false; }
    ;

opt_nowait_or_skip:
  NOWAIT                { }
  | SKIP LOCKED         { }
  | /*EMPTY*/           { }
;


/*****************************************************************************
 *
 *    QUERY:
 *        UpdateStmt (UPDATE)
 *
 *****************************************************************************/

UpdateStmt:
  opt_with_clause UPDATE relation_expr_opt_alias SET set_clause_list
  from_clause where_or_current_clause returning_clause {
  }
;

set_clause_list:
  set_clause {
  }
  | set_clause_list ',' set_clause {
  }
;

set_clause:
  single_set_clause {
  }
  | multiple_set_clause {
  }
;

single_set_clause:
  set_target '=' ctext_expr {
  }
;

/*
 * Ideally, we'd accept any row-valued a_expr as RHS of a multiple_set_clause.
 * However, per SQL spec the row-constructor case must allow DEFAULT as a row
 * member, and it's pretty unclear how to do that (unless perhaps we allow
 * DEFAULT in any a_expr and let parse analysis sort it out later?).  For the
 * moment, the planner/executor only support a subquery as a multiassignment
 * source anyhow, so we need only accept ctext_row and subqueries here.
 */
multiple_set_clause:
  '(' set_target_list ')' '=' ctext_row {
  }
  | '(' set_target_list ')' '=' select_with_parens {
  }
;

set_target:
  ColId opt_indirection {
  }
;

set_target_list:
  set_target {
  }
  | set_target_list ',' set_target {
  }
;


/*****************************************************************************
 *
 *    QUERY:
 *        CURSOR STATEMENTS
 *
 *****************************************************************************/
DeclareCursorStmt:
  DECLARE cursor_name cursor_options CURSOR opt_hold FOR SelectStmt {
  }
;

cursor_name:
  name {
    $$ = $1;
  }
;

cursor_options:
  /*EMPTY*/                     { $$ = 0; }
  | cursor_options NO SCROLL    { $$ = $1 | CURSOR_OPT_NO_SCROLL; }
  | cursor_options SCROLL       { $$ = $1 | CURSOR_OPT_SCROLL; }
  | cursor_options BINARY       { $$ = $1 | CURSOR_OPT_BINARY; }
  | cursor_options INSENSITIVE  { $$ = $1 | CURSOR_OPT_INSENSITIVE; }
;

opt_hold:
  /* EMPTY */                   { $$ = 0; }
  | WITH HOLD                   { $$ = CURSOR_OPT_HOLD; }
  | WITHOUT HOLD                { $$ = 0; }
;

/*****************************************************************************
 *
 *    QUERY:
 *        SELECT STATEMENTS
 *
 *****************************************************************************/

/* A complete SELECT statement looks like this.
 *
 * The rule returns either a single SelectStmt node or a tree of them,
 * representing a set-operation tree.
 *
 * There is an ambiguity when a sub-SELECT is within an a_expr and there
 * are excess parentheses: do the parentheses belong to the sub-SELECT or
 * to the surrounding a_expr?  We don't really care, but bison wants to know.
 * To resolve the ambiguity, we are careful to define the grammar so that
 * the decision is staved off as long as possible: as long as we can keep
 * absorbing parentheses into the sub-SELECT, we will do so, and only when
 * it's no longer possible to do that will we decide that parens belong to
 * the expression.  For example, in "SELECT (((SELECT 2)) + 3)" the extra
 * parentheses are treated as part of the sub-select.  The necessity of doing
 * it that way is shown by "SELECT (((SELECT 2)) UNION SELECT 2)".  Had we
 * parsed "((SELECT 2))" as an a_expr, it'd be too late to go back to the
 * SELECT viewpoint when we see the UNION.
 *
 * This approach is implemented by defining a nonterminal select_with_parens,
 * which represents a SELECT with at least one outer layer of parentheses,
 * and being careful to use select_with_parens, never '(' SelectStmt ')',
 * in the expression grammar.  We will then have shift-reduce conflicts
 * which we can resolve in favor of always treating '(' <select> ')' as
 * a select_with_parens.  To resolve the conflicts, the productions that
 * conflict with the select_with_parens productions are manually given
 * precedences lower than the precedence of ')', thereby ensuring that we
 * shift ')' (and then reduce to select_with_parens) rather than trying to
 * reduce the inner <select> nonterminal to something else.  We use UMINUS
 * precedence for this, which is a fairly arbitrary choice.
 *
 * To be able to define select_with_parens itself without ambiguity, we need
 * a nonterminal select_no_parens that represents a SELECT structure with no
 * outermost parentheses.  This is a little bit tedious, but it works.
 *
 * In non-expression contexts, we use SelectStmt which can represent a SELECT
 * with or without outer parentheses.
 */
SelectStmt:
  select_no_parens        %prec UMINUS
  | select_with_parens    %prec UMINUS
;

select_with_parens:
  '(' select_no_parens ')'          { $$ = $2; }
  | '(' select_with_parens ')'      { $$ = $2; }
;

/*
 * This rule parses the equivalent of the standard's <query expression>.
 * The duplicative productions are annoying, but hard to get rid of without
 * creating shift/reduce conflicts.
 *
 *  The locking clause (FOR UPDATE etc) may be before or after LIMIT/OFFSET.
 *  In <=7.2.X, LIMIT/OFFSET had to be after FOR UPDATE
 *  We now support both orderings, but prefer LIMIT/OFFSET before the locking
 * clause.
 *  2002-08-28 bjm
 */
select_no_parens:
  simple_select {
  }
  | select_clause sort_clause {
  }
  | select_clause opt_sort_clause for_locking_clause opt_select_limit {
  }
  | select_clause opt_sort_clause select_limit opt_for_locking_clause {
  }
  | with_clause select_clause {
  }
  | with_clause select_clause sort_clause {
  }
  | with_clause select_clause opt_sort_clause for_locking_clause opt_select_limit {
  }
  | with_clause select_clause opt_sort_clause select_limit opt_for_locking_clause {
  }
;

select_clause:
  simple_select                 { $$ = $1; }
  | select_with_parens          { $$ = $1; }
;

/*
 * This rule parses SELECT statements that can appear within set operations,
 * including UNION, INTERSECT and EXCEPT.  '(' and ')' can be used to specify
 * the ordering of the set operations.  Without '(' and ')' we want the
 * operations to be ordered per the precedence specs at the head of this file.
 *
 * As with select_no_parens, simple_select cannot have outer parentheses,
 * but can have parenthesized subclauses.
 *
 * Note that sort clauses cannot be included at this level --- SQL requires
 *    SELECT foo UNION SELECT bar ORDER BY baz
 * to be parsed as
 *    (SELECT foo UNION SELECT bar) ORDER BY baz
 * not
 *    SELECT foo UNION (SELECT bar ORDER BY baz)
 * Likewise for WITH, FOR UPDATE and LIMIT.  Therefore, those clauses are
 * described as part of the select_no_parens production, not simple_select.
 * This does not limit functionality, because you can reintroduce these
 * clauses inside parentheses.
 *
 * NOTE: only the leftmost component SelectStmt should have INTO.
 * However, this is not checked by the grammar; parse analysis must check it.
 */
simple_select:
  SELECT opt_all_clause opt_target_list into_clause from_clause where_clause
  group_clause having_clause window_clause {
  }
  | SELECT distinct_clause target_list into_clause from_clause where_clause
  group_clause having_clause window_clause {
  }
  | values_clause {
  }
  | TABLE relation_expr {
  }
  | select_clause UNION all_or_distinct select_clause {
  }
  | select_clause INTERSECT all_or_distinct select_clause {
  }
  | select_clause EXCEPT all_or_distinct select_clause {
  }
;

/*
 * SQL standard WITH clause looks like:
 *
 * WITH [ RECURSIVE ] <query name> [ (<column>,...) ]
 *    AS (query) [ SEARCH or CYCLE clause ]
 *
 * We don't currently support the SEARCH or CYCLE clause.
 *
 * Recognizing WITH_LA here allows a CTE to be named TIME or ORDINALITY.
 */
with_clause:
  WITH cte_list {
  }
  | WITH_LA cte_list {
  }
  | WITH RECURSIVE cte_list {
  }
;

cte_list:
  common_table_expr {
  }
  | cte_list ',' common_table_expr {
  }
;

common_table_expr:
  name opt_name_list AS '(' PreparableStmt ')' {
  }
;

opt_with_clause:
  with_clause               { $$ = $1; }
  | /*EMPTY*/               { $$ = NULL; }
;

into_clause:
  INTO OptTempTableName {
  }
  | /*EMPTY*/ {
  }
;

/*
 * Redundancy here is needed to avoid shift/reduce conflicts,
 * since TEMP is not a reserved word.  See also OptTemp.
 */
OptTempTableName:
  TEMPORARY opt_table qualified_name {
  }
  | TEMP opt_table qualified_name {
  }
  | LOCAL TEMPORARY opt_table qualified_name {
  }
  | LOCAL TEMP opt_table qualified_name {
  }
  | GLOBAL TEMPORARY opt_table qualified_name {
  }
  | GLOBAL TEMP opt_table qualified_name {
  }
  | UNLOGGED opt_table qualified_name {
  }
  | TABLE qualified_name {
  }
  | qualified_name {
  }
;

opt_table:
  TABLE                     {}
  | /*EMPTY*/               {}
;

all_or_distinct:
  ALL                       { $$ = true; }
  | DISTINCT                { $$ = false; }
  | /*EMPTY*/               { $$ = false; }
;

/* We use (NIL) as a placeholder to indicate that all target expressions
 * should be placed in the DISTINCT list during parsetree analysis.
 */
distinct_clause:
  DISTINCT {
  }
  | DISTINCT ON '(' expr_list ')' {
  }
;

opt_all_clause:
  ALL                       { }
  | /*EMPTY*/               { }
;

opt_sort_clause:
  sort_clause               { $$ = $1;}
  | /*EMPTY*/               { }
;

sort_clause:
  ORDER BY sortby_list      { $$ = $3; }
;

sortby_list:
  sortby {
  }
  | sortby_list ',' sortby {
  }
;

sortby:
  a_expr USING qual_all_Op opt_nulls_order {
  }
  | a_expr opt_asc_desc opt_nulls_order {
  }
;

select_limit:
  limit_clause offset_clause {
  }
  | offset_clause limit_clause {
  }
  | limit_clause {
  }
  | offset_clause {
  }
;

opt_select_limit:
  select_limit            { $$ = $1; }
  | /* EMPTY */ {
  }
;

limit_clause:
  LIMIT select_limit_value {
    $$ = $2;
  }
  | LIMIT select_limit_value ',' select_offset_value {
  }
  /* SQL:2008 syntax */
  | FETCH first_or_next opt_select_fetch_first_value row_or_rows ONLY {
  }
;

offset_clause:
  OFFSET select_offset_value {
  }
  /* SQL:2008 syntax */
  | OFFSET select_offset_value2 row_or_rows {
  }
;

select_limit_value:
  a_expr {
  }
  | ALL {
  }
;

select_offset_value:
  a_expr { $$ = $1; }
;

/*
 * Allowing full expressions without parentheses causes various parsing
 * problems with the trailing ROW/ROWS key words.  SQL only calls for
 * constants, so we allow the rest only with parentheses.  If omitted,
 * default to 1.
 */
opt_select_fetch_first_value:
  SignedIconst {
  }
  | '(' a_expr ')' {
    $$ = $2;
  }
  | /*EMPTY*/ {
  }
;

/*
 * Again, the trailing ROW/ROWS in this case prevent the full expression
 * syntax.  c_expr is the best we can do.
 */
select_offset_value2:
  c_expr {
    $$ = $1;
  }
;

/* noise words */
row_or_rows:
  ROW {
    $$ = 0;
  }
  | ROWS {
    $$ = 0;
  }
;

first_or_next:
  FIRST_P                 { $$ = 0; }
  | NEXT                  { $$ = 0; }
;


/*
 * This syntax for group_clause tries to follow the spec quite closely.
 * However, the spec allows only column references, not expressions,
 * which introduces an ambiguity between implicit row constructors
 * (a,b) and lists of column references.
 *
 * We handle this by using the a_expr production for what the spec calls
 * <ordinary grouping set>, which in the spec represents either one column
 * reference or a parenthesized list of column references. Then, we check the
 * top node of the a_expr to see if it's an implicit RowExpr, and if so, just
 * grab and use the list, discarding the node. (this is done in parse analysis,
 * not here)
 *
 * (we abuse the row_format field of RowExpr to distinguish implicit and
 * explicit row constructors; it's debatable if anyone sanely wants to use them
 * in a group clause, but if they have a reason to, we make it possible.)
 *
 * Each item in the group_clause list is either an expression tree or a
 * GroupingSet node of some type.
 */
group_clause:
  GROUP_P BY group_by_list        { $$ = $3; }
  | /*EMPTY*/                     { }
    ;

group_by_list:
  group_by_item {
  }
  | group_by_list ',' group_by_item {
  }
;

group_by_item:
  a_expr                        { $$ = $1; }
  | empty_grouping_set          { $$ = $1; }
  | cube_clause                 { $$ = $1; }
  | rollup_clause               { $$ = $1; }
  | grouping_sets_clause        { $$ = $1; }
;

empty_grouping_set:
  '(' ')' {
  }
;

/*
 * These hacks rely on setting precedence of CUBE and ROLLUP below that of '(',
 * so that they shift in these rules rather than reducing the conflicting
 * unreserved_keyword rule.
 */

rollup_clause:
  ROLLUP '(' expr_list ')' {
  }
;

cube_clause:
  CUBE '(' expr_list ')' {
  }
;

grouping_sets_clause:
  GROUPING SETS '(' group_by_list ')' {
  }
;

having_clause:
  HAVING a_expr               { $$ = $2; }
  | /*EMPTY*/                 { $$ = NULL; }
;

for_locking_clause:
  for_locking_items           { $$ = $1; }
  | FOR READ ONLY             { }
;

opt_for_locking_clause:
  for_locking_clause          { $$ = $1; }
  | /* EMPTY */               { }
;

for_locking_items:
  for_locking_item {
  }
  | for_locking_items for_locking_item  {
  }
;

for_locking_item:
  for_locking_strength locked_rels_list opt_nowait_or_skip {
  }
;

for_locking_strength:
  FOR UPDATE                  { }
  | FOR NO KEY UPDATE         { }
  | FOR SHARE                 { }
  | FOR KEY SHARE             { }
;

locked_rels_list:
  OF qualified_name_list      { $$ = $2; }
  | /* EMPTY */               { }
;


values_clause:
  VALUES ctext_row {
  }
  | values_clause ',' ctext_row {
  }
;

/*****************************************************************************
 *
 *  clauses common to all Optimizable Stmts:
 *    from_clause   - allow list of both JOIN expressions and table names
 *    where_clause  - qualifications for joins or restrictions
 *
 *****************************************************************************/

from_clause:
  FROM from_list            { $$ = $2; }
  | /*EMPTY*/               { }
;

from_list:
  table_ref {
  }
  | from_list ',' table_ref {
  }
;

/*
 * table_ref is where an alias clause can be attached.
 */
table_ref:
  relation_expr opt_alias_clause {
  }
  | relation_expr opt_alias_clause tablesample_clause {
  }
  | func_table func_alias_clause {
  }
  | LATERAL_P func_table func_alias_clause {
  }
  | select_with_parens opt_alias_clause {
  }
  | LATERAL_P select_with_parens opt_alias_clause {
  }
  | joined_table {
  }
  | '(' joined_table ')' alias_clause {
  }
;


/*
 * It may seem silly to separate joined_table from table_ref, but there is
 * method in SQL's madness: if you don't do it this way you get reduce-
 * reduce conflicts, because it's not clear to the parser generator whether
 * to expect alias_clause after ')' or not.  For the same reason we must
 * treat 'JOIN' and 'join_type JOIN' separately, rather than allowing
 * join_type to expand to empty; if we try it, the parser generator can't
 * figure out when to reduce an empty join_type right after table_ref.
 *
 * Note that a CROSS JOIN is the same as an unqualified
 * INNER JOIN, and an INNER JOIN/ON has the same shape
 * but a qualification expression to limit membership.
 * A NATURAL JOIN implicitly matches column names between
 * tables and the shape is determined by which columns are
 * in common. We'll collect columns during the later transformations.
 */

joined_table:
  '(' joined_table ')' {
  }
  | table_ref CROSS JOIN table_ref {
  }
  | table_ref join_type JOIN table_ref join_qual {
  }
  | table_ref JOIN table_ref join_qual {
  }
  | table_ref NATURAL join_type JOIN table_ref {
  }
  | table_ref NATURAL JOIN table_ref {
  }
;

alias_clause:
  AS ColId '(' name_list ')' {
  }
  | AS ColId {
  }
  | ColId '(' name_list ')' {
  }
  | ColId {
  }
;

opt_alias_clause:
  alias_clause            { $$ = $1; }
  | /*EMPTY*/             { $$ = NULL; }
    ;

/*
 * func_alias_clause can include both an Alias and a coldeflist, so we make it
 * return a 2-element list that gets disassembled by calling production.
 */
func_alias_clause:
  alias_clause {
  }
  | AS '(' TableFuncElementList ')' {
  }
  | AS ColId '(' TableFuncElementList ')' {
  }
  | ColId '(' TableFuncElementList ')' {
  }
  | /*EMPTY*/ {
  }
;

join_type:
  FULL join_outer             { $$ = JOIN_FULL; }
  | LEFT join_outer           { $$ = JOIN_LEFT; }
  | RIGHT join_outer          { $$ = JOIN_RIGHT; }
  | INNER_P                   { $$ = JOIN_INNER; }
;

/* OUTER is just noise... */
join_outer:
  OUTER_P                     { $$ = NULL; }
  | /*EMPTY*/                 { $$ = NULL; }
;

/* JOIN qualification clauses
 * Possibilities are:
 *  USING ( column list ) allows only unqualified column names,
 *              which must match between tables.
 *  ON expr allows more general qualifications.
 *
 * We return USING as a List node, while an ON-expr will not be a List.
 */

join_qual:
  USING '(' name_list ')' {
  }
  | ON a_expr {
    $$ = $2;
  }
;


relation_expr:
  qualified_name {
  }
  | qualified_name '*' {
  }
  | ONLY qualified_name {
  }
  | ONLY '(' qualified_name ')' {
  }
;


relation_expr_list:
  relation_expr {
  }
  | relation_expr_list ',' relation_expr {
  }
;


/*
 * Given "UPDATE foo set set ...", we have to decide without looking any
 * further ahead whether the first "set" is an alias or the UPDATE's SET
 * keyword.  Since "set" is allowed as a column name both interpretations
 * are feasible.  We resolve the shift/reduce conflict by giving the first
 * relation_expr_opt_alias production a higher precedence than the SET token
 * has, causing the parser to prefer to reduce, in effect assuming that the
 * SET is not an alias.
 */
relation_expr_opt_alias:
  relation_expr %prec UMINUS  {
    $$ = $1;
  }
  | relation_expr ColId {
  }
  | relation_expr AS ColId {
  }
;

/*
 * TABLESAMPLE decoration in a FROM item
 */
tablesample_clause:
  TABLESAMPLE func_name '(' expr_list ')' opt_repeatable_clause {
  }
;

opt_repeatable_clause:
  REPEATABLE '(' a_expr ')' {
  }
  | /*EMPTY*/ {
    $$ = NULL;
  }
;

/*
 * func_table represents a function invocation in a FROM list. It can be
 * a plain function call, like "foo(...)", or a ROWS FROM expression with
 * one or more function calls, "ROWS FROM (foo(...), bar(...))",
 * optionally with WITH ORDINALITY attached.
 * In the ROWS FROM syntax, a column definition list can be given for each
 * function, for example:
 *     ROWS FROM (foo() AS (foo_res_a text, foo_res_b text),
 *                bar() AS (bar_res_a text, bar_res_b text))
 * It's also possible to attach a column definition list to the RangeFunction
 * as a whole, but that's handled by the table_ref production.
 */
func_table:
  func_expr_windowless opt_ordinality {
  }
  | ROWS FROM '(' rowsfrom_list ')' opt_ordinality {
  }
;

rowsfrom_item:
  func_expr_windowless opt_col_def_list {
  }
;

rowsfrom_list:
  rowsfrom_item {
  }
  | rowsfrom_list ',' rowsfrom_item {
  }
;

opt_col_def_list:
  AS '(' TableFuncElementList ')' {
  }
  | /*EMPTY*/ {
  }
;

opt_ordinality:
  WITH_LA ORDINALITY            { $$ = true; }
  | /*EMPTY*/                   { $$ = false; }
;


where_clause:
  WHERE a_expr                  { $$ = $2; }
  | /*EMPTY*/                   { $$ = NULL; }
;

/* variant for UPDATE and DELETE */
where_or_current_clause:
  WHERE a_expr {
    $$ = $2;
  }
  | WHERE CURRENT_P OF cursor_name {
  }
  | /*EMPTY*/ {
    $$ = NULL;
  }
;


OptTableFuncElementList:
  TableFuncElementList {
    $$ = $1;
  }
  | /*EMPTY*/ {
  }
;

TableFuncElementList:
  TableFuncElement {
  }
  | TableFuncElementList ',' TableFuncElement {
  }
;

TableFuncElement:
  ColId Typename opt_collate_clause {
  }
;

/*****************************************************************************
 *
 *  Type syntax
 *    SQL introduces a large amount of type-specific syntax.
 *    Define individual clauses to handle these cases, and use
 *     the generic case to handle regular type-extensible Postgres syntax.
 *    - thomas 1997-10-10
 *
 *****************************************************************************/

Typename:
  SimpleTypename opt_array_bounds {
  }
  | SETOF SimpleTypename opt_array_bounds {
  }
  /* SQL standard syntax, currently only one-dimensional */
  | SimpleTypename ARRAY '[' Iconst ']' {
  }
  | SETOF SimpleTypename ARRAY '[' Iconst ']' {
  }
  | SimpleTypename ARRAY {
  }
  | SETOF SimpleTypename ARRAY {
  }
;

opt_array_bounds:
  opt_array_bounds '[' ']' {
  }
  | opt_array_bounds '[' Iconst ']' {
  }
  | /*EMPTY*/ {
  }
;

SimpleTypename:
  GenericType {
    $$ = $1;
  }
  | Numeric {
    $$ = $1;
  }
  | Bit {
    $$ = $1;
  }
  | Character {
    $$ = $1;
  }
  | ConstDatetime {
  }
  | ConstInterval opt_interval {
  }
  | ConstInterval '(' Iconst ')' {
  }
;

/* We have a separate ConstTypename to allow defaulting fixed-length
 * types such as CHAR() and BIT() to an unspecified length.
 * SQL9x requires that these default to a length of one, but this
 * makes no sense for constructs like CHAR 'hi' and BIT '0101',
 * where there is an obvious better choice to make.
 * Note that ConstInterval is not included here since it must
 * be pushed up higher in the rules to accommodate the postfix
 * options (e.g. INTERVAL '1' YEAR). Likewise, we have to handle
 * the generic-type-name case in AExprConst to avoid premature
 * reduce/reduce conflicts against function names.
 */
ConstTypename:
  Numeric                     { $$ = $1; }
  | ConstBit                  { $$ = $1; }
  | ConstCharacter            { $$ = $1; }
  | ConstDatetime             { $$ = $1; }
;

/*
 * GenericType covers all type names that don't have special syntax mandated
 * by the standard, including qualified names.  We also allow type modifiers.
 * To avoid parsing conflicts against function invocations, the modifiers
 * have to be shown as expr_list here, but parse analysis will only accept
 * constants for them.
 */
GenericType:
  type_function_name opt_type_modifiers {
  }
  | type_function_name attrs opt_type_modifiers {
  }
;

opt_type_modifiers:
  '(' expr_list ')' {
    $$ = $2;
  }
  | /* EMPTY */ {
  }
;

/*
 * SQL numeric data types
 */
Numeric:
  INT_P {
  }
  | INTEGER {
  }
  | SMALLINT {
  }
  | BIGINT {
  }
  | REAL {
  }
  | FLOAT_P opt_float {
  }
  | DOUBLE_P PRECISION {
  }
  | DECIMAL_P opt_type_modifiers {
  }
  | DEC opt_type_modifiers {
  }
  | NUMERIC opt_type_modifiers {
  }
  | BOOLEAN_P {
  }
;

opt_float:
  '(' Iconst ')' {
  }
  | /*EMPTY*/ {
  }
;

/*
 * SQL bit-field data types
 * The following implements BIT() and BIT VARYING().
 */
Bit:
  BitWithLength {
    $$ = $1;
  }
  | BitWithoutLength {
    $$ = $1;
  }
;

/* ConstBit is like Bit except "BIT" defaults to unspecified length */
/* See notes for ConstCharacter, which addresses same issue for "CHAR" */
ConstBit:
  BitWithLength {
    $$ = $1;
  }
  | BitWithoutLength {
  }
;

BitWithLength:
  BIT opt_varying '(' expr_list ')' {
  }
;

BitWithoutLength:
  BIT opt_varying {
  }
;

/*
 * SQL character data types
 * The following implements CHAR() and VARCHAR().
 */
Character:
  CharacterWithLength {
    $$ = $1;
  }
  | CharacterWithoutLength {
    $$ = $1;
  }
;

ConstCharacter:
  CharacterWithLength {
    $$ = $1;
  }
  | CharacterWithoutLength {
  }
;

CharacterWithLength:
  character '(' Iconst ')' opt_charset {
  }
;

CharacterWithoutLength:
  character opt_charset {
  }
;

character:
  CHARACTER opt_varying {
  }
  | CHAR_P opt_varying {
  }
  | VARCHAR {
  }
  | NATIONAL CHARACTER opt_varying {
  }
  | NATIONAL CHAR_P opt_varying {
  }
  | NCHAR opt_varying {
  }
;

opt_varying:
  VARYING                   { $$ = true; }
  | /*EMPTY*/               { $$ = false; }
;

opt_charset:
  CHARACTER SET ColId       { $$ = $3; }
  | /*EMPTY*/               { $$ = NULL; }
;

/*
 * SQL date/time types
 */
ConstDatetime:
  TIMESTAMP '(' Iconst ')' opt_timezone {
  }
  | TIMESTAMP opt_timezone {
  }
  | TIME '(' Iconst ')' opt_timezone {
  }
  | TIME opt_timezone {
  }
  ;

ConstInterval:
  INTERVAL {
  }
;

opt_timezone:
  WITH_LA TIME ZONE             { $$ = true; }
  | WITHOUT TIME ZONE           { $$ = false; }
  | /*EMPTY*/                   { $$ = false; }
    ;

opt_interval:
  YEAR_P {
  }
  | MONTH_P {
  }
  | DAY_P {
  }
  | HOUR_P {
  }
  | MINUTE_P {
  }
  | interval_second {
  }
  | YEAR_P TO MONTH_P {
  }
  | DAY_P TO HOUR_P {
  }
  | DAY_P TO MINUTE_P {
  }
  | DAY_P TO interval_second {
  }
  | HOUR_P TO MINUTE_P {
  }
  | HOUR_P TO interval_second {
  }
  | MINUTE_P TO interval_second {
  }
  | /*EMPTY*/ {
  }
;

interval_second:
  SECOND_P {
  } | SECOND_P '(' Iconst ')' {
  }
;


/*****************************************************************************
 *
 *  expression grammar
 *
 *****************************************************************************/

/*
 * General expressions
 * This is the heart of the expression syntax.
 *
 * We have two expression types: a_expr is the unrestricted kind, and
 * b_expr is a subset that must be used in some places to avoid shift/reduce
 * conflicts.  For example, we can't do BETWEEN as "BETWEEN a_expr AND a_expr"
 * because that use of AND conflicts with AND as a boolean operator.  So,
 * b_expr is used in BETWEEN and we remove boolean keywords from b_expr.
 *
 * Note that '(' a_expr ')' is a b_expr, so an unrestricted expression can
 * always be used by surrounding it with parens.
 *
 * c_expr is all the productions that are common to a_expr and b_expr;
 * it's factored out just to eliminate redundant coding.
 *
 * Be careful of productions involving more than one terminal token.
 * By default, bison will assign such productions the precedence of their
 * last terminal, but in nearly all cases you want it to be the precedence
 * of the first terminal instead; otherwise you will not get the behavior
 * you expect!  So we use %prec annotations freely to set precedences.
 */
a_expr:
  c_expr {
  }
  | a_expr TYPECAST Typename {
  }
  | a_expr COLLATE any_name {
  }
  | a_expr AT TIME ZONE a_expr      %prec AT {
  }
  /*
   * These operators must be called out explicitly in order to make use
   * of bison's automatic operator-precedence handling.  All other
   * operator names are handled by the generic productions using "Op",
   * below; and all those operators will have the same precedence.
   *
   * If you add more explicitly-known operators, be sure to add them
   * also to b_expr and to the MathOp list below.
   */
  | '+' a_expr          %prec UMINUS {
  }
  | '-' a_expr          %prec UMINUS {
  }
  | a_expr '+' a_expr {
  }
  | a_expr '-' a_expr {
  }
  | a_expr '*' a_expr {
  }
  | a_expr '/' a_expr {
  }
  | a_expr '%' a_expr {
  }
  | a_expr '^' a_expr {
  }
  | a_expr '<' a_expr {
  }
  | a_expr '>' a_expr {
  }
  | a_expr '=' a_expr {
  }
  | a_expr LESS_EQUALS a_expr {
  }
  | a_expr GREATER_EQUALS a_expr {
  }
  | a_expr NOT_EQUALS a_expr {
  }
  | a_expr qual_Op a_expr       %prec Op {
  }
  | qual_Op a_expr          %prec Op {
  }
  | a_expr qual_Op          %prec POSTFIXOP {
  }
  | a_expr AND a_expr {
  }
  | a_expr OR a_expr {
  }
  | NOT a_expr {
  }
  | NOT_LA a_expr           %prec NOT {
  }
  | a_expr LIKE a_expr {
  }
  | a_expr LIKE a_expr ESCAPE a_expr          %prec LIKE {
  }
  | a_expr NOT_LA LIKE a_expr             %prec NOT_LA {
  }
  | a_expr NOT_LA LIKE a_expr ESCAPE a_expr     %prec NOT_LA {
  }
  | a_expr ILIKE a_expr {
  }
  | a_expr ILIKE a_expr ESCAPE a_expr         %prec ILIKE {
  }
  | a_expr NOT_LA ILIKE a_expr            %prec NOT_LA {
  }
  | a_expr NOT_LA ILIKE a_expr ESCAPE a_expr      %prec NOT_LA {
  }
  | a_expr SIMILAR TO a_expr              %prec SIMILAR {
  }
  | a_expr SIMILAR TO a_expr ESCAPE a_expr      %prec SIMILAR {
  }
  | a_expr NOT_LA SIMILAR TO a_expr         %prec NOT_LA {
  }
  | a_expr NOT_LA SIMILAR TO a_expr ESCAPE a_expr   %prec NOT_LA {
  }
  /* NullTest clause
   * Define SQL-style Null test clause.
   * Allow two forms described in the standard:
   *  a IS NULL
   *  a IS NOT NULL
   * Allow two SQL extensions
   *  a ISNULL
   *  a NOTNULL
   */
  | a_expr IS NULL_P              %prec IS {
  }
  | a_expr ISNULL {
  }
  | a_expr IS NOT NULL_P            %prec IS {
  }
  | a_expr NOTNULL {
  }
  | row OVERLAPS row {
  }
  | a_expr IS TRUE_P              %prec IS {
  }
  | a_expr IS NOT TRUE_P            %prec IS {
  }
  | a_expr IS FALSE_P             %prec IS {
  }
  | a_expr IS NOT FALSE_P           %prec IS {
  }
  | a_expr IS UNKNOWN             %prec IS {
  }
  | a_expr IS DISTINCT FROM a_expr      %prec IS {
  }
  | a_expr IS NOT DISTINCT FROM a_expr    %prec IS {
  }
  | a_expr IS OF '(' type_list ')'      %prec IS {
  }
  | a_expr IS NOT OF '(' type_list ')'    %prec IS {
  }
  | a_expr BETWEEN opt_asymmetric b_expr AND a_expr   %prec BETWEEN {
  }
  | a_expr NOT_LA BETWEEN opt_asymmetric b_expr AND a_expr %prec NOT_LA {
  }
  | a_expr BETWEEN SYMMETRIC b_expr AND a_expr      %prec BETWEEN {
  }
  | a_expr NOT_LA BETWEEN SYMMETRIC b_expr AND a_expr   %prec NOT_LA {
  }
  | a_expr IN_P in_expr {
  }
  | a_expr NOT_LA IN_P in_expr            %prec NOT_LA {
  }
  | a_expr subquery_Op sub_type select_with_parens  %prec Op {
  }
  | a_expr subquery_Op sub_type '(' a_expr ')'    %prec Op {
  }
  | UNIQUE select_with_parens {
  }
  | a_expr IS DOCUMENT_P          %prec IS {
  }
  | a_expr IS NOT DOCUMENT_P        %prec IS {
  }
;

/*
 * Restricted expressions
 *
 * b_expr is a subset of the complete expression syntax defined by a_expr.
 *
 * Presently, AND, NOT, IS, and IN are the a_expr keywords that would
 * cause trouble in the places where b_expr is used.  For simplicity, we
 * just eliminate all the boolean-keyword-operator productions from b_expr.
 */
b_expr:
  c_expr {
  }
  | b_expr TYPECAST Typename {
  }
  | '+' b_expr          %prec UMINUS {
  }
  | '-' b_expr          %prec UMINUS {
  }
  | b_expr '+' b_expr {
  }
  | b_expr '-' b_expr {
  }
  | b_expr '*' b_expr {
  }
  | b_expr '/' b_expr {
  }
  | b_expr '%' b_expr {
  }
  | b_expr '^' b_expr {
  }
  | b_expr '<' b_expr {
  }
  | b_expr '>' b_expr {
  }
  | b_expr '=' b_expr {
  }
  | b_expr LESS_EQUALS b_expr {
  }
  | b_expr GREATER_EQUALS b_expr {
  }
  | b_expr NOT_EQUALS b_expr {
  }
  | b_expr qual_Op b_expr       %prec Op {
  }
  | qual_Op b_expr          %prec Op {
  }
  | b_expr qual_Op          %prec POSTFIXOP {
  }
  | b_expr IS DISTINCT FROM b_expr    %prec IS {
  }
  | b_expr IS NOT DISTINCT FROM b_expr  %prec IS {
  }
  | b_expr IS OF '(' type_list ')'    %prec IS {
  }
  | b_expr IS NOT OF '(' type_list ')'  %prec IS {
  }
  | b_expr IS DOCUMENT_P          %prec IS {
  }
  | b_expr IS NOT DOCUMENT_P        %prec IS {
  }
;

/*
 * Productions that can be used in both a_expr and b_expr.
 *
 * Note: productions that refer recursively to a_expr or b_expr mostly
 * cannot appear here.  However, it's OK to refer to a_exprs that occur
 * inside parentheses, such as function arguments; that cannot introduce
 * ambiguity to the b_expr syntax.
 */
c_expr:
  columnref {
  }
  | AexprConst {
  }
  | PARAM opt_indirection {
  }
  | '(' a_expr ')' opt_indirection {
  }
  | case_expr {
  }
  | func_expr {
  }
  | select_with_parens      %prec UMINUS {
  }
  | select_with_parens indirection {
  }
  | EXISTS select_with_parens {
  }
  | ARRAY select_with_parens {
  }
  | ARRAY array_expr {
  }
  | explicit_row {
  }
  | implicit_row {
  }
  | GROUPING '(' expr_list ')' {
  }
;

func_application:
  func_name '(' ')' {
  }
  | func_name '(' func_arg_list opt_sort_clause ')' {
  }
  | func_name '(' VARIADIC func_arg_expr opt_sort_clause ')' {
  }
  | func_name '(' func_arg_list ',' VARIADIC func_arg_expr opt_sort_clause ')' {
  }
  | func_name '(' ALL func_arg_list opt_sort_clause ')' {
  }
  | func_name '(' DISTINCT func_arg_list opt_sort_clause ')' {
  }
  | func_name '(' '*' ')' {
  }
;


/*
 * func_expr and its cousin func_expr_windowless are split out from c_expr just
 * so that we have classifications for "everything that is a function call or
 * looks like one".  This isn't very important, but it saves us having to
 * document which variants are legal in places like "FROM function()" or the
 * backwards-compatible functional-index syntax for CREATE INDEX.
 * (Note that many of the special SQL functions wouldn't actually make any
 * sense as functional index entries, but we ignore that consideration here.)
 */
func_expr:
  func_application within_group_clause filter_clause over_clause {
  }
  | func_expr_common_subexpr {
  }
;

/*
 * As func_expr but does not accept WINDOW functions directly
 * (but they can still be contained in arguments for functions etc).
 * Use this when window expressions are not allowed, where needed to
 * disambiguate the grammar (e.g. in CREATE INDEX).
 */
func_expr_windowless:
  func_application {
    $$ = $1;
  }
  | func_expr_common_subexpr {
    $$ = $1;
  }
;

/*
 * Special expressions that are considered to be functions.
 */
func_expr_common_subexpr:
  COLLATION FOR '(' a_expr ')' {
  }
  | CURRENT_DATE {
  }
  | CURRENT_TIME {
  }
  | CURRENT_TIME '(' Iconst ')' {
  }
  | CURRENT_TIMESTAMP {
  }
  | CURRENT_TIMESTAMP '(' Iconst ')' {
  }
  | LOCALTIME {
  }
  | LOCALTIME '(' Iconst ')' {
  }
  | LOCALTIMESTAMP {
  }
  | LOCALTIMESTAMP '(' Iconst ')' {
  }
  | CURRENT_ROLE {
  }
  | CURRENT_USER {
  }
  | SESSION_USER {
  }
  | USER {
  }
  | CURRENT_CATALOG {
  }
  | CURRENT_SCHEMA {
  }
  | CAST '(' a_expr AS Typename ')' {
  }
  | EXTRACT '(' extract_list ')' {
  }
  | OVERLAY '(' overlay_list ')' {
  }
  | POSITION '(' position_list ')' {
  }
  | SUBSTRING '(' substr_list ')' {
  }
  | TREAT '(' a_expr AS Typename ')' {
  }
  | TRIM '(' BOTH trim_list ')' {
  }
  | TRIM '(' LEADING trim_list ')' {
  }
  | TRIM '(' TRAILING trim_list ')' {
  }
  | TRIM '(' trim_list ')' {
  }
  | NULLIF '(' a_expr ',' a_expr ')' {
  }
  | COALESCE '(' expr_list ')' {
  }
  | GREATEST '(' expr_list ')' {
  }
  | LEAST '(' expr_list ')' {
  }
  | XMLCONCAT '(' expr_list ')' {
  }
  | XMLELEMENT '(' NAME_P ColLabel ')' {
  }
  | XMLELEMENT '(' NAME_P ColLabel ',' xml_attributes ')' {
  }
  | XMLELEMENT '(' NAME_P ColLabel ',' expr_list ')' {
  }
  | XMLELEMENT '(' NAME_P ColLabel ',' xml_attributes ',' expr_list ')' {
  }
  | XMLEXISTS '(' c_expr xmlexists_argument ')' {
  }
  | XMLFOREST '(' xml_attribute_list ')' {
  }
  | XMLPARSE '(' document_or_content a_expr xml_whitespace_option ')' {
  }
  | XMLPI '(' NAME_P ColLabel ')' {
  }
  | XMLPI '(' NAME_P ColLabel ',' a_expr ')' {
  }
  | XMLROOT '(' a_expr ',' xml_root_version opt_xml_root_standalone ')' {
  }
  | XMLSERIALIZE '(' document_or_content a_expr AS SimpleTypename ')' {
  }
;

/*
 * SQL/XML support
 */
xml_root_version:
  VERSION_P a_expr {
    $$ = $2;
  }
  | VERSION_P NO VALUE_P {
  }
;

opt_xml_root_standalone:
  ',' STANDALONE_P YES_P {
  }
  | ',' STANDALONE_P NO {
  }
  | ',' STANDALONE_P NO VALUE_P {
  }
  | /*EMPTY*/ {
  }
;

xml_attributes:
  XMLATTRIBUTES '(' xml_attribute_list ')' {
  }
;

xml_attribute_list:
  xml_attribute_el {
  }
  | xml_attribute_list ',' xml_attribute_el {
  }
;

xml_attribute_el:
  a_expr AS ColLabel {
  }
  | a_expr {
  }
;

document_or_content:
  DOCUMENT_P                { }
  | CONTENT_P               { }
;

xml_whitespace_option:
  PRESERVE WHITESPACE_P     { $$ = true; }
  | STRIP_P WHITESPACE_P    { $$ = false; }
  | /*EMPTY*/               { $$ = false; }
    ;

/* We allow several variants for SQL and other compatibility. */
xmlexists_argument:
  PASSING c_expr {
    $$ = $2;
  }
  | PASSING c_expr BY REF {
    $$ = $2;
  }
  | PASSING BY REF c_expr {
    $$ = $4;
  }
  | PASSING BY REF c_expr BY REF {
    $$ = $4;
  }
;


/*
 * Aggregate decoration clauses
 */
within_group_clause:
  WITHIN GROUP_P '(' sort_clause ')' {
    $$ = $4;
  }
  | /*EMPTY*/ {
  }
;

filter_clause:
  FILTER '(' WHERE a_expr ')' {
    $$ = $4;
  }
  | /*EMPTY*/ {
    $$ = NULL;
  }
;


/*
 * Window Definitions
 */
window_clause:
  WINDOW window_definition_list {
    $$ = $2;
  }
  | /*EMPTY*/ {
  }
;

window_definition_list:
  window_definition {
  }
  | window_definition_list ',' window_definition {
  }
;

window_definition:
  ColId AS window_specification {
  }
;

over_clause:
  OVER window_specification {
    $$ = $2;
  }
  | OVER ColId {
  }
  | /*EMPTY*/ {
    $$ = NULL;
  }
;

window_specification:
  '(' opt_existing_window_name opt_partition_clause
  opt_sort_clause opt_frame_clause ')' {
  }
;

/*
 * If we see PARTITION, RANGE, or ROWS as the first token after the '('
 * of a window_specification, we want the assumption to be that there is
 * no existing_window_name; but those keywords are unreserved and so could
 * be ColIds.  We fix this by making them have the same precedence as IDENT
 * and giving the empty production here a slightly higher precedence, so
 * that the shift/reduce conflict is resolved in favor of reducing the rule.
 * These keywords are thus precluded from being an existing_window_name but
 * are not reserved for any other purpose.
 */
opt_existing_window_name:
  ColId {
    $$ = $1;
  }
  | /*EMPTY*/       %prec Op {
    $$ = NULL;
  }
;

opt_partition_clause:
  PARTITION BY expr_list {
    $$ = $3;
  }
  | /*EMPTY*/ {
  }
;

/*
 * For frame clauses, we return a WindowDef, but only some fields are used:
 * frameOptions, startOffset, and endOffset.
 *
 * This is only a subset of the full SQL:2008 frame_clause grammar.
 * We don't support <window frame exclusion> yet.
 */
opt_frame_clause:
  RANGE frame_extent {
  }
  | ROWS frame_extent {
  }
  | /*EMPTY*/ {
  }
;

frame_extent:
  frame_bound {
  }
  | BETWEEN frame_bound AND frame_bound {
  }
;

/*
 * This is used for both frame start and frame end, with output set up on
 * the assumption it's frame start; the frame_extent productions must reject
 * invalid cases.
 */
frame_bound:
  UNBOUNDED PRECEDING {
  }
  | UNBOUNDED FOLLOWING {
  }
  | CURRENT_P ROW {
  }
  | a_expr PRECEDING {
  }
  | a_expr FOLLOWING {
  }
;


/*
 * Supporting nonterminals for expressions.
 */

/* Explicit row production.
 *
 * SQL99 allows an optional ROW keyword, so we can now do single-element rows
 * without conflicting with the parenthesized a_expr production.  Without the
 * ROW keyword, there must be more than one a_expr inside the parens.
 */
row:
  ROW '(' expr_list ')' {
    $$ = $3;
  }
  | ROW '(' ')' {
  }
  | '(' expr_list ',' a_expr ')' {
  }
;

explicit_row:
  ROW '(' expr_list ')' {
    $$ = $3;
  }
  | ROW '(' ')' {
  }
;

implicit_row:
  '(' expr_list ',' a_expr ')' {
  }
;

sub_type:
  ANY                       { }
  | SOME                    { }
  | ALL                     { }
;

all_Op:
  Op                        { $$ = $1; }
  | MathOp                  { $$ = $1; }
;

MathOp:
  '+'                       { $$ = "+"; }
  | '-'                     { $$ = "-"; }
  | '*'                     { $$ = "*"; }
  | '/'                     { $$ = "/"; }
  | '%'                     { $$ = "%"; }
  | '^'                     { $$ = "^"; }
  | '<'                     { $$ = "<"; }
  | '>'                     { $$ = ">"; }
  | '='                     { $$ = "="; }
  | LESS_EQUALS             { $$ = "<="; }
  | GREATER_EQUALS          { $$ = ">="; }
  | NOT_EQUALS              { $$ = "<>"; }
;

qual_Op:
  Op {
  }
  | OPERATOR '(' any_operator ')' {
  }
;

qual_all_Op:
  all_Op {
  }
  | OPERATOR '(' any_operator ')' {
  }
;

subquery_Op:
  all_Op {
  }
  | OPERATOR '(' any_operator ')' {
  }
  | LIKE {
  }
  | NOT_LA LIKE {
  }
  | ILIKE {
  }
  | NOT_LA ILIKE {
  }

  /* cannot put SIMILAR TO here, because SIMILAR TO is a hack.
   * the regular expression is preprocessed by a function (similar_escape),
   * and the ~ operator for posix regular expressions is used.
   *        x SIMILAR TO y     ->    x ~ similar_escape(y)
   * this transformation is made on the fly by the parser upwards.
   * however the SubLink structure which handles any/some/all stuff
   * is not ready for such a thing.
   */
;

expr_list:
  a_expr {
  }
  | expr_list ',' a_expr {
  }
;

/* function arguments can have names */
func_arg_list:
  func_arg_expr {
  }
  | func_arg_list ',' func_arg_expr {
  }
;

func_arg_expr:
  a_expr {
  }
  | param_name COLON_EQUALS a_expr {
  }
  | param_name EQUALS_GREATER a_expr {
  }
;

type_list:
  Typename {
  }
  | type_list ',' Typename {
  }
;

array_expr:
  '[' expr_list ']' {
  }
  | '[' array_expr_list ']' {
  }
  | '[' ']' {
  }
;

array_expr_list:
  array_expr {
    // $$ = list_make1($1);
  }
  | array_expr_list ',' array_expr
  {
    // $$ = lappend($1, $3);
  }
;


extract_list:
  extract_arg FROM a_expr {
  }
  | /*EMPTY*/ {
  }
;

/* Allow delimited string Sconst in extract_arg as an SQL extension.
 * - thomas 2001-04-12
 */
extract_arg:
  IDENT                   { $$ = $1; }
  | YEAR_P                { $$ = "year"; }
  | MONTH_P               { $$ = "month"; }
  | DAY_P                 { $$ = "day"; }
  | HOUR_P                { $$ = "hour"; }
  | MINUTE_P              { $$ = "minute"; }
  | SECOND_P              { $$ = "second"; }
  | Sconst                { $$ = $1; }
;

/* OVERLAY() arguments
 * SQL99 defines the OVERLAY() function:
 * o overlay(text placing text from int for int)
 * o overlay(text placing text from int)
 * and similarly for binary strings
 */
overlay_list:
  a_expr overlay_placing substr_from substr_for {
    // $$ = list_make4($1, $2, $3, $4);
  }
  | a_expr overlay_placing substr_from {
    // $$ = list_make3($1, $2, $3);
  }
;

overlay_placing:
  PLACING a_expr {
    $$ = $2;
  }
;

/* position_list uses b_expr not a_expr to avoid conflict with general IN */

position_list:
  b_expr IN_P b_expr {
  }
  | /*EMPTY*/ {
  }
;

/* SUBSTRING() arguments
 * SQL9x defines a specific syntax for arguments to SUBSTRING():
 * o substring(text from int for int)
 * o substring(text from int) get entire string from starting point "int"
 * o substring(text for int) get first "int" characters of string
 * o substring(text from pattern) get entire string matching pattern
 * o substring(text from pattern for escape) same with specified escape char
 * We also want to support generic substring functions which accept
 * the usual generic list of arguments. So we will accept both styles
 * here, and convert the SQL9x style to the generic list for further
 * processing. - thomas 2000-11-28
 */
substr_list:
  a_expr substr_from substr_for {
    // $$ = list_make3($1, $2, $3);
  }
  | a_expr substr_for substr_from {
    /* not legal per SQL99, but might as well allow it */
    // $$ = list_make3($1, $3, $2);
  }
  | a_expr substr_from {
    // $$ = list_make2($1, $2);
  }
  | a_expr substr_for {
  }
  | expr_list {
    $$ = $1;
  }
  | /*EMPTY*/ {
  }
;

substr_from:
  FROM a_expr { $$ = $2; }
;

substr_for:
  FOR a_expr { $$ = $2; }
;

trim_list:
  a_expr FROM expr_list {
    // $$ = lappend($3, $1);
  }
  | FROM expr_list {
    $$ = $2;
  }
  | expr_list {
    $$ = $1;
  }
;

in_expr:
  select_with_parens {
  }
  | '(' expr_list ')' {
  }
;

/*
 * Define SQL-style CASE clause.
 * - Full specification
 *  CASE WHEN a = b THEN c ... ELSE d END
 * - Implicit argument
 *  CASE a WHEN b THEN c ... ELSE d END
 */
case_expr:
   CASE case_arg when_clause_list case_default END_P {
   }
;

when_clause_list:
  /* There must be at least one */
  when_clause {
    // $$ = list_make1($1);
  }
  | when_clause_list when_clause {
    // $$ = lappend($1, $2);
  }
;

when_clause:
  WHEN a_expr THEN a_expr {
  }
;

case_default:
  ELSE a_expr                   { $$ = $2; }
  | /*EMPTY*/                   { $$ = NULL; }
;

case_arg:
  a_expr                        { $$ = $1; }
  | /*EMPTY*/                   { $$ = NULL; }
;

columnref:
  ColId {
  }
  | ColId indirection {
  }
;

indirection_el:
  '.' attr_name {
  }
  | '.' '*' {
  }
  | '[' a_expr ']' {
  }
  | '[' a_expr ':' a_expr ']' {
  }
;

indirection:
  indirection_el {
    // $$ = list_make1($1);
  }
  | indirection indirection_el {
    // $$ = lappend($1, $2);
  }
;

opt_indirection:
  /*EMPTY*/ {
  }
  | opt_indirection indirection_el {
  }
;

opt_asymmetric: ASYMMETRIC
  | /*EMPTY*/
;

/*
 * The SQL spec defines "contextually typed value expressions" and
 * "contextually typed row value constructors", which for our purposes
 * are the same as "a_expr" and "row" except that DEFAULT can appear at
 * the top level.
 */

ctext_expr:
  a_expr {
  }
  | DEFAULT {
  }
;

ctext_expr_list:
  ctext_expr {
    // $$ = list_make1($1);
  }
  | ctext_expr_list ',' ctext_expr {
    // $$ = lappend($1, $3);
  }
;

/*
 * We should allow ROW '(' ctext_expr_list ')' too, but that seems to require
 * making VALUES a fully reserved word, which will probably break more apps
 * than allowing the noise-word is worth.
 */
ctext_row:
  '(' ctext_expr_list ')' {
    $$ = $2;
  }
;


/*****************************************************************************
 *
 *  target list for SELECT
 *
 *****************************************************************************/

opt_target_list:
  target_list {
    $$ = $1; }
  | /* EMPTY */ { }
;

target_list:
  target_el {
    // $$ = list_make1($1);
  }
  | target_list ',' target_el {
    // $$ = lappend($1, $3);
  }
;

target_el:
  a_expr AS ColLabel {
  }
  /*
   * We support omitting AS only for column labels that aren't
   * any known keyword.  There is an ambiguity against postfix
   * operators: is "a ! b" an infix expression, or a postfix
   * expression and a column label?  We prefer to resolve this
   * as an infix expression, which we accomplish by assigning
   * IDENT a precedence higher than POSTFIXOP.
   */
  | a_expr IDENT {
  }
  | a_expr {
  }
  | '*' {
  }
;


/*****************************************************************************
 *
 *  Names and constants
 *
 *****************************************************************************/

qualified_name_list:
  qualified_name {
    // $$ = list_make1($1);
  }
  | qualified_name_list ',' qualified_name {
    // $$ = lappend($1, $3);
  }
;

/*
 * The production for a qualified relation name has to exactly match the
 * production for a qualified func_name, because in a FROM clause we cannot
 * tell which we are parsing until we see what comes after it ('(' for a
 * func_name, something else for a relation). Therefore we allow 'indirection'
 * which may contain subscripts, and reject that case in the C code.
 */
qualified_name:
  ColId {
  }
  | ColId indirection {
  }
;

name_list:
  name {
    // $$ = list_make1(makeString($1));
  }
  | name_list ',' name {
    // $$ = lappend($1, makeString($3));
  }
;


name:          ColId            { $$ = $1; };

database_name: ColId            { $$ = $1; };

access_method: ColId            { $$ = $1; };

attr_name:     ColLabel         { $$ = $1; };

index_name:    ColId            { $$ = $1; };

file_name:     Sconst           { $$ = $1; };

/*
 * The production for a qualified func_name has to exactly match the
 * production for a qualified columnref, because we cannot tell which we
 * are parsing until we see what comes after it ('(' or Sconst for a func_name,
 * anything else for a columnref).  Therefore we allow 'indirection' which
 * may contain subscripts, and reject that case in the C code.  (If we
 * ever implement SQL99-like methods, such syntax may actually become legal!)
 */
func_name:
  type_function_name {
  }
  | ColId indirection {
  }
;


/*
 * Constants
 */
AexprConst:
  Iconst {
  }
  | FCONST {
  }
  | Sconst {
  }
  | BCONST {
  }
  | XCONST {
  }
  | func_name Sconst {
  }
  | func_name '(' func_arg_list opt_sort_clause ')' Sconst {
  }
  | ConstTypename Sconst {
  }
  | ConstInterval Sconst opt_interval {
  }
  | ConstInterval '(' Iconst ')' Sconst {
  }
  | TRUE_P {
  }
  | FALSE_P {
  }
  | NULL_P {
  }
;

Iconst:   ICONST                  { $$ = $1; };
Sconst:   SCONST                  { $$ = $1; };

SignedIconst:
  Iconst                          { $$ = $1; }
  | '+' Iconst                    { $$ = + $2; }
  | '-' Iconst                    { $$ = - $2; }
;

/* Role specifications */
RoleId:
  RoleSpec {
  }
;

RoleSpec:
  NonReservedWord {
  }
  | CURRENT_USER {
  }
  | SESSION_USER {
  }
;

role_list:
  RoleSpec                        { }
  | role_list ',' RoleSpec        { }
;

/*
 * Name classification hierarchy.
 *
 * IDENT is the lexeme returned by the lexer for identifiers that match
 * no known keyword.  In most cases, we can accept certain keywords as
 * names, not only IDENTs.  We prefer to accept as many such keywords
 * as possible to minimize the impact of "reserved words" on programmers.
 * So, we divide names into several possible classes.  The classification
 * is chosen in part to make keywords acceptable as names wherever possible.
 */

/* Column identifier --- names that can be column, table, etc names.
 */
ColId:
  IDENT                         { $$ = parser_strdup($1); }
  | unreserved_keyword          { $$ = parser_strdup($1); }
  | col_name_keyword            { $$ = parser_strdup($1); }
;

/* Type/function identifier --- names that can be type or function names.
 */
type_function_name:
   IDENT                        { $$ = parser_strdup($1); }
  | unreserved_keyword          { $$ = parser_strdup($1); }
  | type_func_name_keyword      { $$ = parser_strdup($1); }
;

/* Any not-fully-reserved word --- these names can be, eg, role names.
 */
NonReservedWord:
  IDENT                         { $$ = parser_strdup($1); }
  | unreserved_keyword          { $$ = parser_strdup($1); }
  | col_name_keyword            { $$ = parser_strdup($1); }
  | type_func_name_keyword      { $$ = parser_strdup($1); }
;

/* Column label --- allowed labels in "AS" clauses.
 * This presently includes *all* Postgres keywords.
 */
ColLabel:
  IDENT                         { $$ = parser_strdup($1); }
  | unreserved_keyword          { $$ = parser_strdup($1); }
  | col_name_keyword            { $$ = parser_strdup($1); }
  | type_func_name_keyword      { $$ = parser_strdup($1); }
  | reserved_keyword            { $$ = parser_strdup($1); }
;


%%

void GramProcessor::error(const location_type& l, const std::string& m) {
  parser_->Error(l, m);
}
