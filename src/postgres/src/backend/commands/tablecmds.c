/*-------------------------------------------------------------------------
 *
 * tablecmds.c
 *	  Commands for creating and altering table structures and settings
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/commands/tablecmds.c
 *
 * The following only applies to changes made to this file as part of
 * YugaByte development.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you
 * may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.  See the License for the specific language governing
 * permissions and limitations under the License.
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/attmap.h"
#include "access/genam.h"
#include "access/heapam.h"
#include "access/heapam_xlog.h"
#include "access/multixact.h"
#include "access/reloptions.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/tableam.h"
#include "access/toast_compression.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "access/xloginsert.h"
#include "catalog/catalog.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/namespace.h"
#include "catalog/objectaccess.h"
#include "catalog/partition.h"
#include "catalog/pg_am.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_depend.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_largeobject.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/storage.h"
#include "catalog/storage_xlog.h"
#include "catalog/toasting.h"
#include "commands/cluster.h"
#include "commands/comment.h"
#include "commands/defrem.h"
#include "commands/event_trigger.h"
#include "commands/policy.h"
#include "commands/sequence.h"
#include "commands/tablecmds.h"
#include "commands/tablespace.h"
#include "commands/trigger.h"
#include "commands/typecmds.h"
#include "commands/user.h"
#include "executor/executor.h"
#include "foreign/fdwapi.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/parsenodes.h"
#include "optimizer/optimizer.h"
#include "parser/parse_clause.h"
#include "parser/parse_coerce.h"
#include "parser/parse_collate.h"
#include "parser/parse_expr.h"
#include "parser/parse_oper.h"
#include "parser/parse_relation.h"
#include "parser/parse_type.h"
#include "parser/parse_utilcmd.h"
#include "parser/parser.h"
#include "partitioning/partbounds.h"
#include "partitioning/partdesc.h"
#include "pgstat.h"
#include "rewrite/rewriteDefine.h"
#include "rewrite/rewriteHandler.h"
#include "rewrite/rewriteManip.h"
#include "storage/bufmgr.h"
#include "storage/lmgr.h"
#include "storage/lock.h"
#include "storage/predicate.h"
#include "storage/smgr.h"
#include "tcop/utility.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/partcache.h"
#include "utils/relcache.h"
#include "utils/ruleutils.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/timestamp.h"
#include "utils/typcache.h"

/* YB includes. */
#include "pg_yb_utils.h"
#include "catalog/binary_upgrade.h"
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_policy.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_shdepend.h"
#include "catalog/pg_shdepend_d.h"
#include "catalog/pg_yb_tablegroup_d.h"
#include "commands/dbcommands.h"
#include "commands/tablegroup.h"
#include "commands/view.h"
#include "commands/ybccmds.h"
#include "executor/ybcModifyTable.h"
#include "parser/analyze.h"
#include "pg_yb_utils.h"
#include "statistics/statistics.h"
#include "utils/plancache.h"
#include "utils/regproc.h"

/*
 * ON COMMIT action list
 */
typedef struct OnCommitItem
{
	Oid			relid;			/* relid of relation */
	OnCommitAction oncommit;	/* what to do at end of xact */

	/*
	 * If this entry was created during the current transaction,
	 * creating_subid is the ID of the creating subxact; if created in a prior
	 * transaction, creating_subid is zero.  If deleted during the current
	 * transaction, deleting_subid is the ID of the deleting subxact; if no
	 * deletion request is pending, deleting_subid is zero.
	 */
	SubTransactionId creating_subid;
	SubTransactionId deleting_subid;
} OnCommitItem;

static List *on_commits = NIL;


/*
 * State information for ALTER TABLE
 *
 * The pending-work queue for an ALTER TABLE is a List of AlteredTableInfo
 * structs, one for each table modified by the operation (the named table
 * plus any child tables that are affected).  We save lists of subcommands
 * to apply to this table (possibly modified by parse transformation steps);
 * these lists will be executed in Phase 2.  If a Phase 3 step is needed,
 * necessary information is stored in the constraints and newvals lists.
 *
 * Phase 2 is divided into multiple passes; subcommands are executed in
 * a pass determined by subcommand type.
 */

#define AT_PASS_UNSET			-1	/* UNSET will cause ERROR */
#define AT_PASS_DROP			0	/* DROP (all flavors) */
#define AT_PASS_ALTER_TYPE		1	/* ALTER COLUMN TYPE */
#define AT_PASS_OLD_INDEX		2	/* re-add existing indexes */
#define AT_PASS_OLD_CONSTR		3	/* re-add existing constraints */
/* We could support a RENAME COLUMN pass here, but not currently used */
#define AT_PASS_ADD_COL			4	/* ADD COLUMN */
#define AT_PASS_ADD_CONSTR		5	/* ADD constraints (initial examination) */
#define AT_PASS_COL_ATTRS		6	/* set column attributes, eg NOT NULL */
#define AT_PASS_ADD_INDEXCONSTR	7	/* ADD index-based constraints */
#define AT_PASS_ADD_INDEX		8	/* ADD indexes */
#define AT_PASS_ADD_OTHERCONSTR	9	/* ADD other constraints, defaults */
#define AT_PASS_MISC			10	/* other stuff */
#define AT_NUM_PASSES			11

typedef struct AlteredTableInfo
{
	/* Information saved before any work commences: */
	Oid			relid;			/* Relation to work on */
	char		relkind;		/* Its relkind */
	TupleDesc	oldDesc;		/* Pre-modification tuple descriptor */

	/*
	 * Transiently set during Phase 2, normally set to NULL.
	 *
	 * ATRewriteCatalogs sets this when it starts, and closes when ATExecCmd
	 * returns control.  This can be exploited by ATExecCmd subroutines to
	 * close/reopen across transaction boundaries.
	 */
	Relation	rel;

	/* Information saved by Phase 1 for Phase 2: */
	List	   *subcmds[AT_NUM_PASSES]; /* Lists of AlterTableCmd */
	/* Information saved by Phases 1/2 for Phase 3: */
	List	   *constraints;	/* List of NewConstraint */
	List	   *newvals;		/* List of NewColumnValue */
	List	   *afterStmts;		/* List of utility command parsetrees */
	bool		verify_new_notnull; /* T if we should recheck NOT NULL */
	int			rewrite;		/* Reason for forced rewrite, if any */
	Oid			newAccessMethod;	/* new access method; 0 means no change */
	Oid			newTableSpace;	/* new tablespace; 0 means no change */
	bool		chgPersistence; /* T if SET LOGGED/UNLOGGED is used */
	char		newrelpersistence;	/* if above is true */
	Expr	   *partition_constraint;	/* for attach partition validation */
	/* true, if validating default due to some other attach/detach */
	bool		validate_default;
	/* Objects to rebuild after completing ALTER TYPE operations */
	List	   *changedConstraintOids;	/* OIDs of constraints to rebuild */
	List	   *changedConstraintDefs;	/* string definitions of same */
	List	   *changedIndexOids;	/* OIDs of indexes to rebuild */
	List	   *changedIndexDefs;	/* string definitions of same */
	char	   *replicaIdentityIndex;	/* index to reset as REPLICA IDENTITY */
	char	   *clusterOnIndex; /* index to use for CLUSTER */
	List	   *changedStatisticsOids;	/* OIDs of statistics to rebuild */
	List	   *changedStatisticsDefs;	/* string definitions of same */
	bool		yb_skip_copy_split_options;
	/* true if we need to skip copying split options during table rewrite */
} AlteredTableInfo;

/* Struct describing one new constraint to check in Phase 3 scan */
/* Note: new NOT NULL constraints are handled elsewhere */
typedef struct NewConstraint
{
	char	   *name;			/* Constraint name, or NULL if none */
	ConstrType	contype;		/* CHECK or FOREIGN */
	Oid			refrelid;		/* PK rel, if FOREIGN */
	Oid			refindid;		/* OID of PK's index, if FOREIGN */
	Oid			conid;			/* OID of pg_constraint entry, if FOREIGN */
	Node	   *qual;			/* Check expr or CONSTR_FOREIGN Constraint */
	ExprState  *qualstate;		/* Execution state for CHECK expr */
} NewConstraint;

/*
 * Struct describing one new column value that needs to be computed during
 * Phase 3 copy (this could be either a new column with a non-null default, or
 * a column that we're changing the type of).  Columns without such an entry
 * are just copied from the old table during ATRewriteTable.  Note that the
 * expr is an expression over *old* table values, except when is_generated
 * is true; then it is an expression over columns of the *new* tuple.
 */
typedef struct NewColumnValue
{
	AttrNumber	attnum;			/* which column */
	Expr	   *expr;			/* expression to compute */
	ExprState  *exprstate;		/* execution state */
	bool		is_generated;	/* is it a GENERATED expression? */
} NewColumnValue;

/*
 * Error-reporting support for RemoveRelations
 */
struct dropmsgstrings
{
	char		kind;
	int			nonexistent_code;
	const char *nonexistent_msg;
	const char *skipping_msg;
	const char *nota_msg;
	const char *drophint_msg;
};

static const struct dropmsgstrings dropmsgstringarray[] = {
	{RELKIND_RELATION,
		ERRCODE_UNDEFINED_TABLE,
		gettext_noop("table \"%s\" does not exist"),
		gettext_noop("table \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a table"),
	gettext_noop("Use DROP TABLE to remove a table.")},
	{RELKIND_SEQUENCE,
		ERRCODE_UNDEFINED_TABLE,
		gettext_noop("sequence \"%s\" does not exist"),
		gettext_noop("sequence \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a sequence"),
	gettext_noop("Use DROP SEQUENCE to remove a sequence.")},
	{RELKIND_VIEW,
		ERRCODE_UNDEFINED_TABLE,
		gettext_noop("view \"%s\" does not exist"),
		gettext_noop("view \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a view"),
	gettext_noop("Use DROP VIEW to remove a view.")},
	{RELKIND_MATVIEW,
		ERRCODE_UNDEFINED_TABLE,
		gettext_noop("materialized view \"%s\" does not exist"),
		gettext_noop("materialized view \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a materialized view"),
	gettext_noop("Use DROP MATERIALIZED VIEW to remove a materialized view.")},
	{RELKIND_INDEX,
		ERRCODE_UNDEFINED_OBJECT,
		gettext_noop("index \"%s\" does not exist"),
		gettext_noop("index \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not an index"),
	gettext_noop("Use DROP INDEX to remove an index.")},
	{RELKIND_COMPOSITE_TYPE,
		ERRCODE_UNDEFINED_OBJECT,
		gettext_noop("type \"%s\" does not exist"),
		gettext_noop("type \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a type"),
	gettext_noop("Use DROP TYPE to remove a type.")},
	{RELKIND_FOREIGN_TABLE,
		ERRCODE_UNDEFINED_OBJECT,
		gettext_noop("foreign table \"%s\" does not exist"),
		gettext_noop("foreign table \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a foreign table"),
	gettext_noop("Use DROP FOREIGN TABLE to remove a foreign table.")},
	{RELKIND_PARTITIONED_TABLE,
		ERRCODE_UNDEFINED_TABLE,
		gettext_noop("table \"%s\" does not exist"),
		gettext_noop("table \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not a table"),
	gettext_noop("Use DROP TABLE to remove a table.")},
	{RELKIND_PARTITIONED_INDEX,
		ERRCODE_UNDEFINED_OBJECT,
		gettext_noop("index \"%s\" does not exist"),
		gettext_noop("index \"%s\" does not exist, skipping"),
		gettext_noop("\"%s\" is not an index"),
	gettext_noop("Use DROP INDEX to remove an index.")},
	{'\0', 0, NULL, NULL, NULL, NULL}
};

/* communication between RemoveRelations and RangeVarCallbackForDropRelation */
struct DropRelationCallbackState
{
	/* These fields are set by RemoveRelations: */
	char		expected_relkind;
	LOCKMODE	heap_lockmode;
	/* These fields are state to track which subsidiary locks are held: */
	Oid			heapOid;
	Oid			partParentOid;
	/* These fields are passed back by RangeVarCallbackForDropRelation: */
	char		actual_relkind;
	char		actual_relpersistence;
};

/* Alter table target-type flags for ATSimplePermissions */
#define		ATT_TABLE				0x0001
#define		ATT_VIEW				0x0002
#define		ATT_MATVIEW				0x0004
#define		ATT_INDEX				0x0008
#define		ATT_COMPOSITE_TYPE		0x0010
#define		ATT_FOREIGN_TABLE		0x0020
#define		ATT_PARTITIONED_INDEX	0x0040
#define		ATT_SEQUENCE			0x0080

/*
 * ForeignTruncateInfo
 *
 * Information related to truncation of foreign tables.  This is used for
 * the elements in a hash table. It uses the server OID as lookup key,
 * and includes a per-server list of all foreign tables involved in the
 * truncation.
 */
typedef struct ForeignTruncateInfo
{
	Oid			serverid;
	List	   *rels;
} ForeignTruncateInfo;

/*
 * Partition tables are expected to be dropped when the parent partitioned
 * table gets dropped. Hence for partitioning we use AUTO dependency.
 * Otherwise, for regular inheritance use NORMAL dependency.
 */
#define child_dependency_type(child_is_partition)	\
	((child_is_partition) ? DEPENDENCY_AUTO : DEPENDENCY_NORMAL)

static void truncate_check_rel(Oid relid, Form_pg_class reltuple);
static void truncate_check_perms(Oid relid, Form_pg_class reltuple);
static void truncate_check_activity(Relation rel);
static void RangeVarCallbackForTruncate(const RangeVar *relation,
										Oid relId, Oid oldRelId, void *arg);
static List *MergeAttributes(List *schema, List *supers, char relpersistence,
							 bool is_partition, List **supconstr);
static bool MergeCheckConstraint(List *constraints, char *name, Node *expr);
static void MergeAttributesIntoExisting(Relation child_rel, Relation parent_rel);
static void MergeConstraintsIntoExisting(Relation child_rel, Relation parent_rel);
static void StoreCatalogInheritance(Oid relationId, List *supers,
									bool child_is_partition);
static void StoreCatalogInheritance1(Oid relationId, Oid parentOid,
									 int32 seqNumber, Relation inhRelation,
									 bool child_is_partition);
static int	findAttrByName(const char *attributeName, List *schema);
static void AlterIndexNamespaces(Relation classRel, Relation rel,
								 Oid oldNspOid, Oid newNspOid, ObjectAddresses *objsMoved);
static void AlterSeqNamespaces(Relation classRel, Relation rel,
							   Oid oldNspOid, Oid newNspOid, ObjectAddresses *objsMoved,
							   LOCKMODE lockmode);
static ObjectAddress ATExecAlterConstraint(Relation rel, AlterTableCmd *cmd,
										   bool recurse, bool recursing, LOCKMODE lockmode);
static bool ATExecAlterConstrRecurse(Constraint *cmdcon, Relation conrel, Relation tgrel,
									 Relation rel, HeapTuple contuple, List **otherrelids,
									 LOCKMODE lockmode);
static ObjectAddress ATExecValidateConstraint(List **wqueue,
											  Relation rel, char *constrName,
											  bool recurse, bool recursing, LOCKMODE lockmode);
static int	transformColumnNameList(Oid relId, List *colList,
									int16 *attnums, Oid *atttypids);
static int	transformFkeyGetPrimaryKey(Relation pkrel, Oid *indexOid,
									   List **attnamelist,
									   int16 *attnums, Oid *atttypids,
									   Oid *opclasses);
static Oid	transformFkeyCheckAttrs(Relation pkrel,
									int numattrs, int16 *attnums,
									Oid *opclasses);
static void checkFkeyPermissions(Relation rel, int16 *attnums, int natts);
static CoercionPathType findFkeyCast(Oid targetTypeId, Oid sourceTypeId,
									 Oid *funcid);
static void validateForeignKeyConstraint(char *conname,
										 Relation rel, Relation pkrel,
										 Oid pkindOid, Oid constraintOid);
static void ATController(AlterTableStmt *parsetree,
						 Relation rel, List *cmds, bool recurse, LOCKMODE lockmode,
						 AlterTableUtilityContext *context);
static void ATPrepCmd(List **wqueue, Relation rel, AlterTableCmd *cmd,
					  bool recurse, bool recursing, LOCKMODE lockmode,
					  AlterTableUtilityContext *context);
static void ATRewriteCatalogs(List **wqueue, LOCKMODE lockmode,
							  AlterTableUtilityContext *context,
							  List **rollbackHandles,
							  List *volatile *handles);
static void ATExecCmd(List **wqueue, AlteredTableInfo *tab,
					  AlterTableCmd *cmd, LOCKMODE lockmode, int cur_pass,
					  AlterTableUtilityContext *context);
static AlterTableCmd *ATParseTransformCmd(List **wqueue, AlteredTableInfo *tab,
										  Relation rel, AlterTableCmd *cmd,
										  bool recurse, LOCKMODE lockmode,
										  int cur_pass,
										  AlterTableUtilityContext *context);
static void ATRewriteTables(AlterTableStmt *parsetree,
							List **wqueue, LOCKMODE lockmode,
							AlterTableUtilityContext *context);
static void ATRewriteTable(AlteredTableInfo *tab, Oid OIDNewHeap, LOCKMODE lockmode);
static AlteredTableInfo *ATGetQueueEntry(List **wqueue, Relation rel);
static void ATSimplePermissions(AlterTableType cmdtype, Relation rel, int allowed_targets);
static void ATSimpleRecursion(List **wqueue, Relation rel,
							  AlterTableCmd *cmd, bool recurse, LOCKMODE lockmode,
							  AlterTableUtilityContext *context);
static void ATCheckPartitionsNotInUse(Relation rel, LOCKMODE lockmode);
static void ATTypedTableRecursion(List **wqueue, Relation rel, AlterTableCmd *cmd,
								  LOCKMODE lockmode,
								  AlterTableUtilityContext *context);
static List *find_typed_table_dependencies(Oid typeOid, const char *typeName,
										   DropBehavior behavior);
static void ATPrepAddColumn(List **wqueue, Relation rel, bool recurse, bool recursing,
							bool is_view, AlterTableCmd *cmd, LOCKMODE lockmode,
							AlterTableUtilityContext *context);
static ObjectAddress ATExecAddColumn(List **wqueue, AlteredTableInfo *tab,
									 Relation rel, AlterTableCmd **cmd,
									 bool recurse, bool recursing,
									 LOCKMODE lockmode, int cur_pass,
									 AlterTableUtilityContext *context);
static bool check_for_column_name_collision(Relation rel, const char *colname,
											bool if_not_exists);
static void add_column_datatype_dependency(Oid relid, int32 attnum, Oid typid);
static void add_column_collation_dependency(Oid relid, int32 attnum, Oid collid);
static void ATPrepDropNotNull(Relation rel, bool recurse, bool recursing);
static ObjectAddress ATExecDropNotNull(Relation rel, const char *colName, LOCKMODE lockmode);
static void ATPrepSetNotNull(List **wqueue, Relation rel,
							 AlterTableCmd *cmd, bool recurse, bool recursing,
							 LOCKMODE lockmode,
							 AlterTableUtilityContext *context);
static ObjectAddress ATExecSetNotNull(AlteredTableInfo *tab, Relation rel,
									  const char *colName, LOCKMODE lockmode);
static void ATExecCheckNotNull(AlteredTableInfo *tab, Relation rel,
							   const char *colName, LOCKMODE lockmode);
static bool NotNullImpliedByRelConstraints(Relation rel, Form_pg_attribute attr);
static bool ConstraintImpliedByRelConstraint(Relation scanrel,
											 List *testConstraint, List *provenConstraint);
static ObjectAddress ATExecColumnDefault(Relation rel, const char *colName,
										 Node *newDefault, LOCKMODE lockmode);
static ObjectAddress ATExecCookedColumnDefault(Relation rel, AttrNumber attnum,
											   Node *newDefault);
static ObjectAddress ATExecAddIdentity(Relation rel, const char *colName,
									   Node *def, LOCKMODE lockmode);
static ObjectAddress ATExecSetIdentity(Relation rel, const char *colName,
									   Node *def, LOCKMODE lockmode);
static ObjectAddress ATExecDropIdentity(Relation rel, const char *colName, bool missing_ok, LOCKMODE lockmode);
static void ATPrepDropExpression(Relation rel, AlterTableCmd *cmd, bool recurse, bool recursing, LOCKMODE lockmode);
static ObjectAddress ATExecDropExpression(Relation rel, const char *colName, bool missing_ok, LOCKMODE lockmode);
static ObjectAddress ATExecSetStatistics(Relation rel, const char *colName, int16 colNum,
										 Node *newValue, LOCKMODE lockmode);
static ObjectAddress ATExecSetOptions(Relation rel, const char *colName,
									  Node *options, bool isReset, LOCKMODE lockmode);
static ObjectAddress ATExecSetStorage(Relation rel, const char *colName,
									  Node *newValue, LOCKMODE lockmode);
static void ATPrepDropColumn(List **wqueue, Relation rel, bool recurse, bool recursing,
							 AlterTableCmd *cmd, LOCKMODE lockmode,
							 AlterTableUtilityContext *context);
static ObjectAddress ATExecDropColumn(List **wqueue, AlteredTableInfo *yb_tab,
									  Relation rel, const char *colName,
									  DropBehavior behavior,
									  bool recurse, bool recursing,
									  bool missing_ok, LOCKMODE lockmode,
									  ObjectAddresses *addrs);
static ObjectAddress ATExecAddIndex(List **yb_wqueue, AlteredTableInfo *tab, Relation *rel,
									IndexStmt *stmt, bool is_rebuild, LOCKMODE lockmode);
static ObjectAddress ATExecAddStatistics(AlteredTableInfo *tab, Relation rel,
										 CreateStatsStmt *stmt, bool is_rebuild, LOCKMODE lockmode);
static ObjectAddress ATExecAddConstraint(List **wqueue,
										 AlteredTableInfo *tab, Relation rel,
										 Constraint *newConstraint, bool recurse, bool is_readd,
										 LOCKMODE lockmode);
static char *ChooseForeignKeyConstraintNameAddition(List *colnames);
static ObjectAddress ATExecAddIndexConstraint(AlteredTableInfo *tab, Relation rel,
											  IndexStmt *stmt, LOCKMODE lockmode,
											  List **yb_wqueue);
static ObjectAddress ATAddCheckConstraint(List **wqueue,
										  AlteredTableInfo *tab, Relation rel,
										  Constraint *constr,
										  bool recurse, bool recursing, bool is_readd,
										  LOCKMODE lockmode);
static ObjectAddress ATAddForeignKeyConstraint(List **wqueue, AlteredTableInfo *tab,
											   Relation rel, Constraint *fkconstraint,
											   bool recurse, bool recursing,
											   LOCKMODE lockmode);
static ObjectAddress addFkRecurseReferenced(List **wqueue, Constraint *fkconstraint,
											Relation rel, Relation pkrel, Oid indexOid, Oid parentConstr,
											int numfks, int16 *pkattnum, int16 *fkattnum,
											Oid *pfeqoperators, Oid *ppeqoperators, Oid *ffeqoperators,
											int numfkdelsetcols, int16 *fkdelsetcols,
											bool old_check_ok,
											Oid parentDelTrigger, Oid parentUpdTrigger);
static void validateFkOnDeleteSetColumns(int numfks, const int16 *fkattnums,
										 int numfksetcols, const int16 *fksetcolsattnums,
										 List *fksetcols);
static void addFkRecurseReferencing(List **wqueue, Constraint *fkconstraint,
									Relation rel, Relation pkrel, Oid indexOid, Oid parentConstr,
									int numfks, int16 *pkattnum, int16 *fkattnum,
									Oid *pfeqoperators, Oid *ppeqoperators, Oid *ffeqoperators,
									int numfkdelsetcols, int16 *fkdelsetcols,
									bool old_check_ok, LOCKMODE lockmode,
									Oid parentInsTrigger, Oid parentUpdTrigger);
static void CloneForeignKeyConstraints(List **wqueue, Relation parentRel,
									   Relation partitionRel);
static void CloneFkReferenced(Relation parentRel, Relation partitionRel);
static void CloneFkReferencing(List **wqueue, Relation parentRel,
							   Relation partRel);
static void createForeignKeyCheckTriggers(Oid myRelOid, Oid refRelOid,
										  Constraint *fkconstraint, Oid constraintOid,
										  Oid indexOid,
										  Oid parentInsTrigger, Oid parentUpdTrigger,
										  Oid *insertTrigOid, Oid *updateTrigOid);
static void createForeignKeyActionTriggers(Relation rel, Oid refRelOid,
										   Constraint *fkconstraint, Oid constraintOid,
										   Oid indexOid,
										   Oid parentDelTrigger, Oid parentUpdTrigger,
										   Oid *deleteTrigOid, Oid *updateTrigOid);
static bool tryAttachPartitionForeignKey(ForeignKeyCacheInfo *fk,
										 Oid partRelid,
										 Oid parentConstrOid, int numfks,
										 AttrNumber *mapped_conkey, AttrNumber *confkey,
										 Oid *conpfeqop,
										 Oid parentInsTrigger,
										 Oid parentUpdTrigger,
										 Relation trigrel);
static void GetForeignKeyActionTriggers(Relation trigrel,
										Oid conoid, Oid confrelid, Oid conrelid,
										Oid *deleteTriggerOid,
										Oid *updateTriggerOid);
static void GetForeignKeyCheckTriggers(Relation trigrel,
									   Oid conoid, Oid confrelid, Oid conrelid,
									   Oid *insertTriggerOid,
									   Oid *updateTriggerOid);
static void ATExecDropConstraint(List **yb_wqueue, AlteredTableInfo *tab, Relation *mutable_rel,
								 const char *constrName,
								 DropBehavior behavior,
								 bool recurse, bool recursing,
								 bool missing_ok, LOCKMODE lockmode);
static void ATPrepAlterColumnType(List **wqueue,
								  AlteredTableInfo *tab, Relation rel,
								  bool recurse, bool recursing,
								  AlterTableCmd *cmd, LOCKMODE lockmode,
								  AlterTableUtilityContext *context);
static bool ATColumnChangeRequiresRewrite(Node *expr, AttrNumber varattno);
static ObjectAddress ATExecAlterColumnType(AlteredTableInfo *tab,
										   Relation			*yb_mutable_rel,
										   AlterTableCmd	*cmd,
										   LOCKMODE			 lockmode);
static void RememberConstraintForRebuilding(Oid conoid, AlteredTableInfo *tab);
static void RememberIndexForRebuilding(Oid indoid, AlteredTableInfo *tab);
static void RememberStatisticsForRebuilding(Oid indoid, AlteredTableInfo *tab);
static void ATPostAlterTypeCleanup(List **wqueue, AlteredTableInfo *tab,
								   LOCKMODE lockmode);
static void ATPostAlterTypeParse(Oid oldId, Oid oldRelId, Oid refRelId,
								 char *cmd, List **wqueue, LOCKMODE lockmode,
								 bool rewrite);
static void RebuildConstraintComment(AlteredTableInfo *tab, int pass,
									 Oid objid, Relation rel, List *domname,
									 const char *conname);
static void TryReuseIndex(Oid oldId, IndexStmt *stmt);
static void TryReuseForeignKey(Oid oldId, Constraint *con);
static ObjectAddress ATExecAlterColumnGenericOptions(Relation rel, const char *colName,
													 List *options, LOCKMODE lockmode);
static void change_owner_fix_column_acls(Oid relationOid,
										 Oid oldOwnerId, Oid newOwnerId);
static void change_owner_recurse_to_sequences(Oid relationOid,
											  Oid newOwnerId, LOCKMODE lockmode);
static ObjectAddress ATExecClusterOn(Relation rel, const char *indexName,
									 LOCKMODE lockmode);
static void ATExecDropCluster(Relation rel, LOCKMODE lockmode);
static void ATPrepSetAccessMethod(AlteredTableInfo *tab, Relation rel, const char *amname);
static bool ATPrepChangePersistence(Relation rel, bool toLogged);
static void ATPrepSetTableSpace(AlteredTableInfo *tab, Relation rel,
								const char *tablespacename, LOCKMODE lockmode,
								bool yb_cascade);
static void ATExecSetTableSpace(Oid tableOid, Oid newTableSpace, LOCKMODE lockmode);
static void ATExecSetTableSpaceNoStorage(Relation rel, Oid newTableSpace);
static void ATExecSetRelOptions(Relation rel, List *defList,
								AlterTableType operation,
								LOCKMODE lockmode);
static void ATExecEnableDisableTrigger(Relation rel, const char *trigname,
									   char fires_when, bool skip_system, bool recurse,
									   LOCKMODE lockmode);
static void ATExecEnableDisableRule(Relation rel, const char *rulename,
									char fires_when, LOCKMODE lockmode);
static void ATPrepAddInherit(Relation child_rel);
static ObjectAddress ATExecAddInherit(Relation child_rel, RangeVar *parent, LOCKMODE lockmode);
static ObjectAddress ATExecDropInherit(Relation rel, RangeVar *parent, LOCKMODE lockmode);
static void drop_parent_dependency(Oid relid, Oid refclassid, Oid refobjid,
								   DependencyType deptype);
static ObjectAddress ATExecAddOf(Relation rel, const TypeName *ofTypename, LOCKMODE lockmode);
static void ATExecDropOf(Relation rel, LOCKMODE lockmode);
static void ATExecReplicaIdentity(Relation rel, ReplicaIdentityStmt *stmt, LOCKMODE lockmode);
static void ATExecGenericOptions(Relation rel, List *options);
static void ATExecSetRowSecurity(Relation rel, bool rls);
static void ATExecForceNoForceRowSecurity(Relation rel, bool force_rls);
static ObjectAddress ATExecSetCompression(AlteredTableInfo *tab, Relation rel,
										  const char *column, Node *newValue, LOCKMODE lockmode);

static void index_copy_data(Relation rel, RelFileNode newrnode);
static const char *storage_name(char c);

static void RangeVarCallbackForDropRelation(const RangeVar *rel, Oid relOid,
											Oid oldRelOid, void *arg);
static void RangeVarCallbackForAlterRelation(const RangeVar *rv, Oid relid,
											 Oid oldrelid, void *arg);
static PartitionSpec *transformPartitionSpec(Relation rel, PartitionSpec *partspec, char *strategy);
static void ComputePartitionAttrs(ParseState *pstate, Relation rel, List *partParams, AttrNumber *partattrs,
								  List **partexprs, Oid *partopclass, Oid *partcollation, char strategy);
static void CreateInheritance(Relation child_rel, Relation parent_rel);
static void RemoveInheritance(Relation child_rel, Relation parent_rel,
							  bool allow_detached);
static ObjectAddress ATExecAttachPartition(List **wqueue, Relation rel,
										   PartitionCmd *cmd,
										   AlterTableUtilityContext *context);
static void AttachPartitionEnsureIndexes(Relation rel, Relation attachrel,
										 List **yb_wqueue);
static void QueuePartitionConstraintValidation(List **wqueue, Relation scanrel,
											   List *partConstraint,
											   bool validate_default);
static void CloneRowTriggersToPartition(Relation parent, Relation partition);
static void DetachAddConstraintIfNeeded(List **wqueue, Relation partRel);
static void DropClonedTriggersFromPartition(Oid partitionId);
static ObjectAddress ATExecDetachPartition(List **wqueue, AlteredTableInfo *tab,
										   Relation rel, RangeVar *name,
										   bool concurrent);
static void DetachPartitionFinalize(Relation rel, Relation partRel,
									bool concurrent, Oid defaultPartOid);
static ObjectAddress ATExecDetachPartitionFinalize(Relation rel, RangeVar *name);
static ObjectAddress ATExecAttachPartitionIdx(List **wqueue, Relation rel,
											  RangeVar *name);
static void validatePartitionedIndex(Relation partedIdx, Relation partedTbl);
static void refuseDupeIndexAttach(Relation parentIdx, Relation partIdx,
								  Relation partitionTbl);
static List *GetParentedForeignKeyRefs(Relation partition);
static void ATDetachCheckNoForeignKeyRefs(Relation partition);
static char GetAttributeCompression(Oid atttypid, char *compression);

static Relation
YbATCloneRelationSetPrimaryKey(Relation old_rel, IndexStmt *stmt,
							   ObjectAddress *result_addr);
static Relation YbATCloneRelationSetColumnType(Relation old_rel,
											   const char *altered_column_name,
											   Oid altered_collation_id,
											   TypeName *altered_type_name,
											   List *new_column_values);
static bool YbATIsRangePk(SortByDir ordering, bool is_colocated,
						  bool is_tablegroup);
static void YbATSetPKRewriteChildPartitions(List **yb_wqueue,
											AlteredTableInfo *tab,
											bool skip_copy_split_options);
static void YbATCopyIndexSplitOptions(Oid oldId, IndexStmt *stmt,
									  AlteredTableInfo *tab);
static void YbATInvalidateTableCacheAfterAlter(List *ybAlteredTableIds);
/* ----------------------------------------------------------------
 *		DefineRelation
 *				Creates a new relation.
 *
 * stmt carries parsetree information from an ordinary CREATE TABLE statement.
 * The other arguments are used to extend the behavior for other cases:
 * relkind: relkind to assign to the new relation
 * ownerId: if not InvalidOid, use this as the new relation's owner.
 * typaddress: if not null, it's set to the pg_type entry's address.
 * queryString: for error reporting
 *
 * Note that permissions checks are done against current user regardless of
 * ownerId.  A nonzero ownerId is used when someone is creating a relation
 * "on behalf of" someone else, so we still want to see that the current user
 * has permissions to do it.
 *
 * If successful, returns the address of the new relation.
 * ----------------------------------------------------------------
 */
ObjectAddress
DefineRelation(CreateStmt *stmt, char relkind, Oid ownerId,
			   ObjectAddress *typaddress, const char *queryString)
{
	char		relname[NAMEDATALEN];
	Oid			namespaceId;
	Oid			relationId = InvalidOid;
	Oid			tablespaceId;
	Relation	rel;
	TupleDesc	descriptor;
	List	   *inheritOids;
	List	   *old_constraints;
	List	   *rawDefaults;
	List	   *cookedDefaults;
	Datum		reloptions;
	ListCell   *listptr;
	AttrNumber	attnum;
	bool		partitioned;
	static char *validnsps[] = HEAP_RELOPT_NAMESPACES;
	Oid			ofTypeId;
	ObjectAddress address;
	LOCKMODE	parentLockmode;
	const char *accessMethod = NULL;
	Oid			accessMethodId = InvalidOid;

	/* YB variables. */
	Oid			rowTypeId = InvalidOid;
	bool		relisshared = false;
	bool		use_initdb_acl = false;
	ListCell   *opt_cell;

	/*
	 * Truncate relname to appropriate length (probably a waste of time, as
	 * parser should have done this already).
	 */
	strlcpy(relname, stmt->relation->relname, NAMEDATALEN);

	/*
	 * Check consistency of arguments
	 */
	if (IsYugaByteEnabled() && stmt->relation->relpersistence == RELPERSISTENCE_UNLOGGED)
	{
		/* UNLOGGED persistence is NO-OP in YugabyteDB. */
		ereport(NOTICE,
				(errmsg("unlogged option is currently ignored in YugabyteDB, "
								"all non-temp relations will be logged")));
		stmt->relation->relpersistence = RELPERSISTENCE_PERMANENT;
	}

	if (stmt->oncommit != ONCOMMIT_NOOP
		&& stmt->relation->relpersistence != RELPERSISTENCE_TEMP)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("ON COMMIT can only be used on temporary tables")));

	if (stmt->partspec != NULL)
	{
		if (relkind != RELKIND_RELATION)
			elog(ERROR, "unexpected relkind: %d", (int) relkind);

		relkind = RELKIND_PARTITIONED_TABLE;
		partitioned = true;
	}
	else
		partitioned = false;

	if (IsYugaByteEnabled() && stmt->tablespacename &&
		stmt->relation->relpersistence == RELPERSISTENCE_TEMP)
	{
		/*
		 * Disable setting tablespaces for temporary tables in Yugabyte
		 * clusters.
		 */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot set tablespaces for temporary tables")));
	}

	/*
	 * Look up the namespace in which we are supposed to create the relation,
	 * check we have permission to create there, lock it against concurrent
	 * drop, and mark stmt->relation as RELPERSISTENCE_TEMP if a temporary
	 * namespace is selected.
	 */
	namespaceId =
		RangeVarGetAndCheckCreationNamespace(stmt->relation, NoLock, NULL);

	/*
	 * Security check: disallow creating temp tables from security-restricted
	 * code.  This is needed because calling code might not expect untrusted
	 * tables to appear in pg_temp at the front of its search path.
	 */
	if (stmt->relation->relpersistence == RELPERSISTENCE_TEMP
		&& InSecurityRestrictedOperation())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("cannot create temporary table within security-restricted operation")));

	if (IsYugaByteEnabled() &&
		stmt->relation->relpersistence == RELPERSISTENCE_TEMP)
		YBCForceAllowCatalogModifications(true);

	/*
	 * Determine the lockmode to use when scanning parents.  A self-exclusive
	 * lock is needed here.
	 *
	 * For regular inheritance, if two backends attempt to add children to the
	 * same parent simultaneously, and that parent has no pre-existing
	 * children, then both will attempt to update the parent's relhassubclass
	 * field, leading to a "tuple concurrently updated" error.  Also, this
	 * interlocks against a concurrent ANALYZE on the parent table, which
	 * might otherwise be attempting to clear the parent's relhassubclass
	 * field, if its previous children were recently dropped.
	 *
	 * If the child table is a partition, then we instead grab an exclusive
	 * lock on the parent because its partition descriptor will be changed by
	 * addition of the new partition.
	 */
	parentLockmode = (stmt->partbound != NULL ? AccessExclusiveLock :
					  ShareUpdateExclusiveLock);

	/* Determine the list of OIDs of the parents. */
	inheritOids = NIL;
	foreach(listptr, stmt->inhRelations)
	{
		RangeVar   *rv = (RangeVar *) lfirst(listptr);
		Oid			parentOid;

		parentOid = RangeVarGetRelid(rv, parentLockmode, false);

		/*
		 * Reject duplications in the list of parents.
		 */
		if (list_member_oid(inheritOids, parentOid))
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_TABLE),
					 errmsg("relation \"%s\" would be inherited from more than once",
							get_rel_name(parentOid))));

		inheritOids = lappend_oid(inheritOids, parentOid);
	}

	/*
	 * Select tablespace to use: an explicitly indicated one, or (in the case
	 * of a partitioned table) the parent's, if it has one.
	 */
	if (stmt->tablespacename)
	{
		tablespaceId = get_tablespace_oid(stmt->tablespacename, false);

		if (partitioned && tablespaceId == MyDatabaseTableSpace)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot specify default tablespace for partitioned relations")));
	}
	else if (stmt->partbound)
	{
		/*
		 * For partitions, when no other tablespace is specified, we default
		 * the tablespace to the parent partitioned table's.
		 */
		Assert(list_length(inheritOids) == 1);
		tablespaceId = get_rel_tablespace(linitial_oid(inheritOids));
	}
	else
		tablespaceId = InvalidOid;

	/* still nothing? use the default */
	if (!OidIsValid(tablespaceId))
		tablespaceId = GetDefaultTablespace(stmt->relation->relpersistence,
											partitioned);

	/* Check permissions except when using database's default */
	if (OidIsValid(tablespaceId) && tablespaceId != MyDatabaseTableSpace)
	{
		AclResult	aclresult;

		aclresult = pg_tablespace_aclcheck(tablespaceId, GetUserId(),
										   ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_TABLESPACE,
						   get_tablespace_name(tablespaceId));
	}

	if (tablespaceId == GLOBALTABLESPACE_OID)
	{
		if (IsYugaByteEnabled())
		{
			if (!IsYsqlUpgrade)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("shared relations cannot be created outside of YSQL upgrade")));
			else if (!IsCatalogNamespace(namespaceId))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("shared relations can only be created in pg_catalog")));
			else if (stmt->partbound)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("shared relations can not be partitioned")));
			else if (relkind != RELKIND_RELATION)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("only ordinary tables may be shared")));

			relisshared = true;

			/*
			 * If not superuser, ensure having CREATE privileges over template1 -
			 * this is where DocDB would actually store the shared relation.
			 */
			if (!superuser())
			{
				AclResult	aclresult;

				aclresult = pg_database_aclcheck(Template1DbOid, GetUserId(), ACL_CREATE);

				if (aclresult != ACLCHECK_OK)
					aclcheck_error(aclresult, OBJECT_DATABASE,
								   get_database_name(Template1DbOid));
			}

			/*
			 * We actually need a lot more checks - e.g. that shared relation
			 * is not temporary, does not have tablegroup, etc.
			 * But since this is needed for YSQL upgrade only, we more or less
			 * assume "user" knows what he's doing.
			 */
		}
		else
		{
			/* In all cases disallow placing user relations in pg_global */
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					 errmsg("only shared relations can be placed in pg_global tablespace")));
		}
	}

	/*
	 * In a colocated database, tablegroups are created under the hood.
	 * Disallow users from using the underlying tablegroups.
	 */
	if (MyDatabaseColocated && stmt->tablegroupname)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use tablegroups in a colocated database")));

	Oid tablegroupId = stmt->tablegroupname
		? get_tablegroup_oid(stmt->tablegroupname, false)
		: InvalidOid;

	if (IsYugaByteEnabled())
	{
		foreach(opt_cell, stmt->options)
		{
			DefElem *def = (DefElem *) lfirst(opt_cell);

			/*
			 * A check in parse_utilcmd.c makes sure only one of these two options
			 * can be specified.
			 */
			if (strcmp(def->defname, "colocated") == 0 ||
				strcmp(def->defname, "colocation") == 0)
			{
				if (OidIsValid(tablegroupId))
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot use \'colocation=true/false\' with tablegroup")));
			}
		}
	}

	/*
	 * Check permissions for tablegroup. To create a table within a tablegroup, a user must
	 * either be a superuser, the owner of the tablegroup, or have create perms on it.
	 */
	if (OidIsValid(tablegroupId) && !pg_tablegroup_ownercheck(tablegroupId, GetUserId()))
	{
		AclResult  aclresult;

		aclresult = pg_tablegroup_aclcheck(tablegroupId, GetUserId(), ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_YBTABLEGROUP,
						   get_tablegroup_name(tablegroupId));
	}



	Oid colocation_id = YbGetColocationIdFromRelOptions(stmt->options);

	/* Identify user ID that will own the table */
	if (!OidIsValid(ownerId))
		ownerId = (IsYugaByteEnabled() && IsYsqlUpgrade && IsCatalogNamespace(namespaceId))
					? BOOTSTRAP_SUPERUSERID
					: GetUserId();

	/*
	 * Parse and validate reloptions, if any.
	 */
	reloptions = transformRelOptions((Datum) 0, stmt->options, NULL, validnsps,
									 true, false);

	switch (relkind)
	{
		case RELKIND_VIEW:
			(void) view_reloptions(reloptions, true);
			break;
		case RELKIND_PARTITIONED_TABLE:
			(void) partitioned_table_reloptions(reloptions, true);
			break;
		default:
			(void) heap_reloptions(relkind, reloptions, true);
	}

	reloptions = ybExcludeNonPersistentReloptions(reloptions);

	if (stmt->ofTypename)
	{
		AclResult	aclresult;

		ofTypeId = typenameTypeId(NULL, stmt->ofTypename);

		aclresult = pg_type_aclcheck(ofTypeId, GetUserId(), ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error_type(aclresult, ofTypeId);
	}
	else
		ofTypeId = InvalidOid;

	/*
	 * Look up inheritance ancestors and generate relation schema, including
	 * inherited attributes.  (Note that stmt->tableElts is destructively
	 * modified by MergeAttributes.)
	 */
	stmt->tableElts =
		MergeAttributes(stmt->tableElts, inheritOids,
						stmt->relation->relpersistence,
						stmt->partbound != NULL,
						&old_constraints);

	/*
	 * Create a tuple descriptor from the relation schema.  Note that this
	 * deals with column names, types, and NOT NULL constraints, but not
	 * default values or CHECK constraints; we handle those below.
	 */
	descriptor = BuildDescForRelation(stmt->tableElts);

	/*
	 * Find columns with default values and prepare for insertion of the
	 * defaults.  Pre-cooked (that is, inherited) defaults go into a list of
	 * CookedConstraint structs that we'll pass to heap_create_with_catalog,
	 * while raw defaults go into a list of RawColumnDefault structs that will
	 * be processed by AddRelationNewConstraints.  (We can't deal with raw
	 * expressions until we can do transformExpr.)
	 *
	 * We can set the atthasdef flags now in the tuple descriptor; this just
	 * saves StoreAttrDefault from having to do an immediate update of the
	 * pg_attribute rows.
	 */
	rawDefaults = NIL;
	cookedDefaults = NIL;
	attnum = 0;

	foreach(listptr, stmt->tableElts)
	{
		ColumnDef  *colDef = lfirst(listptr);
		Form_pg_attribute attr;

		attnum++;
		attr = TupleDescAttr(descriptor, attnum - 1);

		if (colDef->raw_default != NULL)
		{
			RawColumnDefault *rawEnt;

			Assert(colDef->cooked_default == NULL);

			rawEnt = (RawColumnDefault *) palloc(sizeof(RawColumnDefault));
			rawEnt->attnum = attnum;
			rawEnt->raw_default = colDef->raw_default;
			rawEnt->missingMode = false;
			rawEnt->generated = colDef->generated;
			rawDefaults = lappend(rawDefaults, rawEnt);
			attr->atthasdef = true;
		}
		else if (colDef->cooked_default != NULL)
		{
			CookedConstraint *cooked;

			cooked = (CookedConstraint *) palloc(sizeof(CookedConstraint));
			cooked->contype = CONSTR_DEFAULT;
			cooked->conoid = InvalidOid;	/* until created */
			cooked->name = NULL;
			cooked->attnum = attnum;
			cooked->expr = colDef->cooked_default;
			cooked->skip_validation = false;
			cooked->is_local = true;	/* not used for defaults */
			cooked->inhcount = 0;	/* ditto */
			cooked->is_no_inherit = false;
			cookedDefaults = lappend(cookedDefaults, cooked);
			attr->atthasdef = true;
		}

		if (colDef->identity)
			attr->attidentity = colDef->identity;

		if (colDef->generated)
			attr->attgenerated = colDef->generated;

		if (colDef->compression)
			attr->attcompression = GetAttributeCompression(attr->atttypid,
														   colDef->compression);
	}

	/*
	 * If the statement hasn't specified an access method, but we're defining
	 * a type of relation that needs one, use the default.
	 */
	if (stmt->accessMethod != NULL)
	{
		accessMethod = stmt->accessMethod;

		if (partitioned)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("specifying a table access method is not supported on a partitioned table")));
	}
	else if (RELKIND_HAS_TABLE_AM(relkind))
		accessMethod = default_table_access_method;

	/* look up the access method, verify it is for a table */
	if (accessMethod != NULL)
		accessMethodId = get_table_am_oid(accessMethod, false);

	/* Handles WITH (table_oid = x). */
	if (IsYugaByteEnabled()) {
		relationId = GetTableOidFromRelOptions(
			stmt->options, tablespaceId, stmt->relation->relpersistence);

		/* Handles WITH (row_type_oid = x). */
		rowTypeId = GetRowTypeOidFromRelOptions(stmt->options);

		if (relkind == RELKIND_RELATION)
		{
			use_initdb_acl = IsYsqlUpgrade && IsCatalogNamespace(namespaceId);
		}
		else
		{
			/* Handles WITH (use_initdb_acl = x). */
			use_initdb_acl = YbGetUseInitdbAclFromRelOptions(stmt->options);
			if (use_initdb_acl && !(IsYsqlUpgrade && IsCatalogNamespace(namespaceId)))
			{
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("use_initdb_acl cannot be used outside of YSQL upgrade for pg_catalog")));
			}
		}
	}

	/*
	 * Create the relation.  Inherited defaults and constraints are passed in
	 * for immediate handling --- since they don't need parsing, they can be
	 * stored immediately.
	 */
	relationId = heap_create_with_catalog(relname,
										  namespaceId,
										  tablespaceId,
										  tablegroupId,
										  relationId,
										  rowTypeId,
										  ofTypeId,
										  ownerId,
										  accessMethodId,
										  descriptor,
										  list_concat(cookedDefaults,
													  old_constraints),
										  relkind,
										  stmt->relation->relpersistence,
										  relisshared,
										  false,
										  stmt->oncommit,
										  reloptions,
										  true,
										  allowSystemTableMods,
										  false,
										  InvalidOid,
										  typaddress,
										  use_initdb_acl);

	/*
	 * We must bump the command counter to make the newly-created relation
	 * tuple visible for opening.
	 */
	CommandCounterIncrement();

	/*
	 * Open the new relation and acquire exclusive lock on it.  This isn't
	 * really necessary for locking out other backends (since they can't see
	 * the new rel anyway until we commit), but it keeps the lock manager from
	 * complaining about deadlock risks.
	 */
	rel = relation_open(relationId, AccessExclusiveLock);

	if (IsYugaByteEnabled())
	{
		CheckIsYBSupportedRelationByKind(relkind);
		YBCCreateTable(stmt, relname, relkind, descriptor,
					   relationId,
					   namespaceId, tablegroupId, colocation_id, tablespaceId,
					   YbGetRelfileNodeId(rel),
					   InvalidOid /* oldRelfileNodeId */,
					   false /* isTruncate */);
	}

	/*
	 * Now add any newly specified column default and generation expressions
	 * to the new relation.  These are passed to us in the form of raw
	 * parsetrees; we need to transform them to executable expression trees
	 * before they can be added. The most convenient way to do that is to
	 * apply the parser's transformExpr routine, but transformExpr doesn't
	 * work unless we have a pre-existing relation. So, the transformation has
	 * to be postponed to this final step of CREATE TABLE.
	 *
	 * This needs to be before processing the partitioning clauses because
	 * those could refer to generated columns.
	 */
	if (rawDefaults)
		AddRelationNewConstraints(rel, rawDefaults, NIL,
								  true, true, false, queryString);

	/*
	 * Make column generation expressions visible for use by partitioning.
	 */
	CommandCounterIncrement();

	/* Process and store partition bound, if any. */
	if (stmt->partbound)
	{
		PartitionBoundSpec *bound;
		ParseState *pstate;
		Oid			parentId = linitial_oid(inheritOids),
					defaultPartOid;
		Relation	parent,
					defaultRel = NULL;
		ParseNamespaceItem *nsitem;

		/* Already have strong enough lock on the parent */
		parent = table_open(parentId, NoLock);

		/*
		 * We are going to try to validate the partition bound specification
		 * against the partition key of parentRel, so it better have one.
		 */
		if (parent->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("\"%s\" is not partitioned",
							RelationGetRelationName(parent))));

		/*
		 * The partition constraint of the default partition depends on the
		 * partition bounds of every other partition. It is possible that
		 * another backend might be about to execute a query on the default
		 * partition table, and that the query relies on previously cached
		 * default partition constraints. We must therefore take a table lock
		 * strong enough to prevent all queries on the default partition from
		 * proceeding until we commit and send out a shared-cache-inval notice
		 * that will make them update their index lists.
		 *
		 * Order of locking: The relation being added won't be visible to
		 * other backends until it is committed, hence here in
		 * DefineRelation() the order of locking the default partition and the
		 * relation being added does not matter. But at all other places we
		 * need to lock the default relation before we lock the relation being
		 * added or removed i.e. we should take the lock in same order at all
		 * the places such that lock parent, lock default partition and then
		 * lock the partition so as to avoid a deadlock.
		 */
		defaultPartOid =
			get_default_oid_from_partdesc(RelationGetPartitionDesc(parent,
																   true));
		if (OidIsValid(defaultPartOid))
			defaultRel = table_open(defaultPartOid, AccessExclusiveLock);

		/* Transform the bound values */
		pstate = make_parsestate(NULL);
		pstate->p_sourcetext = queryString;

		/*
		 * Add an nsitem containing this relation, so that transformExpr
		 * called on partition bound expressions is able to report errors
		 * using a proper context.
		 */
		nsitem = addRangeTableEntryForRelation(pstate, rel, AccessShareLock,
											   NULL, false, false);
		addNSItemToQuery(pstate, nsitem, false, true, true);

		bound = transformPartitionBound(pstate, parent, stmt->partbound);

		/*
		 * Check first that the new partition's bound is valid and does not
		 * overlap with any of existing partitions of the parent.
		 */
		check_new_partition_bound(relname, parent, bound, pstate);

		/*
		 * If the default partition exists, its partition constraints will
		 * change after the addition of this new partition such that it won't
		 * allow any row that qualifies for this new partition. So, check that
		 * the existing data in the default partition satisfies the constraint
		 * as it will exist after adding this partition.
		 */
		if (OidIsValid(defaultPartOid))
		{
			check_default_partition_contents(parent, defaultRel, bound);

			if (IsYugaByteEnabled())
			{
				/*
				* Increment the schema version of the default partition to
				* ensure concurrent operations do not insert any data matching
				* this new partition into the default partition.
				*/
				YBCPgStatement alter_cmd_handle = NULL;
				HandleYBStatus(
					YBCPgNewAlterTable(
						YBCGetDatabaseOidByRelid(defaultPartOid),
						YbGetRelfileNodeId(defaultRel),
						&alter_cmd_handle));
				HandleYBStatus(
					YBCPgAlterTableIncrementSchemaVersion(alter_cmd_handle));
				YBCExecAlterTable(alter_cmd_handle, defaultPartOid);
			}

			/* Keep the lock until commit. */
			table_close(defaultRel, NoLock);
		}

		/* Update the pg_class entry. */
		StorePartitionBound(rel, parent, bound);

		table_close(parent, NoLock);
	}

	/* Store inheritance information for new rel. */
	StoreCatalogInheritance(relationId, inheritOids, stmt->partbound != NULL);

	/*
	 * Process the partitioning specification (if any) and store the partition
	 * key information into the catalog.
	 */
	if (partitioned)
	{
		ParseState *pstate;
		char		strategy;
		int			partnatts;
		AttrNumber	partattrs[PARTITION_MAX_KEYS];
		Oid			partopclass[PARTITION_MAX_KEYS];
		Oid			partcollation[PARTITION_MAX_KEYS];
		List	   *partexprs = NIL;

		pstate = make_parsestate(NULL);
		pstate->p_sourcetext = queryString;

		partnatts = list_length(stmt->partspec->partParams);

		/* Protect fixed-size arrays here and in executor */
		if (partnatts > PARTITION_MAX_KEYS)
			ereport(ERROR,
					(errcode(ERRCODE_TOO_MANY_COLUMNS),
					 errmsg("cannot partition using more than %d columns",
							PARTITION_MAX_KEYS)));

		/*
		 * We need to transform the raw parsetrees corresponding to partition
		 * expressions into executable expression trees.  Like column defaults
		 * and CHECK constraints, we could not have done the transformation
		 * earlier.
		 */
		stmt->partspec = transformPartitionSpec(rel, stmt->partspec,
												&strategy);

		ComputePartitionAttrs(pstate, rel, stmt->partspec->partParams,
							  partattrs, &partexprs, partopclass,
							  partcollation, strategy);

		StorePartitionKey(rel, strategy, partnatts, partattrs, partexprs,
						  partopclass, partcollation);

		/* make it all visible */
		CommandCounterIncrement();
	}

	/*
	 * If we're creating a partition, create now all the indexes, triggers,
	 * FKs defined in the parent.
	 *
	 * We can't do it earlier, because DefineIndex wants to know the partition
	 * key which we just stored.
	 */
	if (stmt->partbound)
	{
		Oid			parentId = linitial_oid(inheritOids);
		Relation	parent;
		List	   *idxlist;
		ListCell   *cell;

		/* Already have strong enough lock on the parent */
		parent = table_open(parentId, NoLock);
		idxlist = RelationGetIndexList(parent);

		/*
		 * For each index in the parent table, create one in the partition
		 */
		foreach(cell, idxlist)
		{
			Relation	idxRel = index_open(lfirst_oid(cell), AccessShareLock);
			AttrMap    *attmap;
			IndexStmt  *idxstmt;
			Oid			constraintOid;

			if (rel->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
			{
				if (idxRel->rd_index->indisunique)
					ereport(ERROR,
							(errcode(ERRCODE_WRONG_OBJECT_TYPE),
							 errmsg("cannot create foreign partition of partitioned table \"%s\"",
									RelationGetRelationName(parent)),
							 errdetail("Table \"%s\" contains indexes that are unique.",
									   RelationGetRelationName(parent))));
				else
				{
					index_close(idxRel, AccessShareLock);
					continue;
				}
			}

			attmap = build_attrmap_by_name(RelationGetDescr(rel),
										   RelationGetDescr(parent),
										   false /* yb_ignore_type_mismatch */);
			idxstmt =
				generateClonedIndexStmt(NULL, idxRel,
										attmap, &constraintOid);
			DefineIndex(RelationGetRelid(rel),
						idxstmt,
						InvalidOid,
						RelationGetRelid(idxRel),
						constraintOid,
						false, false, false, false, false);

			index_close(idxRel, AccessShareLock);
		}

		list_free(idxlist);

		/*
		 * If there are any row-level triggers, clone them to the new
		 * partition.
		 */
		if (parent->trigdesc != NULL)
			CloneRowTriggersToPartition(parent, rel);

		/*
		 * And foreign keys too.  Note that because we're freshly creating the
		 * table, there is no need to verify these new constraints.
		 */
		CloneForeignKeyConstraints(NULL, parent, rel);

		table_close(parent, NoLock);
	}

	/*
	 * Now add any newly specified CHECK constraints to the new relation. Same
	 * as for defaults above, but these need to come after partitioning is set
	 * up.
	 */
	if (stmt->constraints)
		AddRelationNewConstraints(rel, NIL, stmt->constraints,
								  true, true, false, queryString);

	ObjectAddressSet(address, RelationRelationId, relationId);

	/*
	 * Clean up.  We keep lock on new relation (although it shouldn't be
	 * visible to anyone else anyway, until commit).
	 */
	relation_close(rel, NoLock);

	return address;
}

/*
 * Emit the right error or warning message for a "DROP" command issued on a
 * non-existent relation
 */
static void
DropErrorMsgNonExistent(RangeVar *rel, char rightkind, bool missing_ok)
{
	const struct dropmsgstrings *rentry;

	if (rel->schemaname != NULL &&
		!OidIsValid(LookupNamespaceNoError(rel->schemaname)))
	{
		if (!missing_ok)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_SCHEMA),
					 errmsg("schema \"%s\" does not exist", rel->schemaname)));
		}
		else
		{
			ereport(NOTICE,
					(errmsg("schema \"%s\" does not exist, skipping",
							rel->schemaname)));
		}
		return;
	}

	for (rentry = dropmsgstringarray; rentry->kind != '\0'; rentry++)
	{
		if (rentry->kind == rightkind)
		{
			if (!missing_ok)
			{
				ereport(ERROR,
						(errcode(rentry->nonexistent_code),
						 errmsg(rentry->nonexistent_msg, rel->relname)));
			}
			else
			{
				ereport(NOTICE, (errmsg(rentry->skipping_msg, rel->relname)));
				break;
			}
		}
	}

	Assert(rentry->kind != '\0');	/* Should be impossible */
}

/*
 * Emit the right error message for a "DROP" command issued on a
 * relation of the wrong type
 */
static void
DropErrorMsgWrongType(const char *relname, char wrongkind, char rightkind)
{
	const struct dropmsgstrings *rentry;
	const struct dropmsgstrings *wentry;

	for (rentry = dropmsgstringarray; rentry->kind != '\0'; rentry++)
		if (rentry->kind == rightkind)
			break;
	Assert(rentry->kind != '\0');

	for (wentry = dropmsgstringarray; wentry->kind != '\0'; wentry++)
		if (wentry->kind == wrongkind)
			break;
	/* wrongkind could be something we don't have in our table... */

	ereport(ERROR,
			(errcode(ERRCODE_WRONG_OBJECT_TYPE),
			 errmsg(rentry->nota_msg, relname),
			 (wentry->kind != '\0') ? errhint("%s", _(wentry->drophint_msg)) : 0));
}

/*
 * RemoveRelations
 *		Implements DROP TABLE, DROP INDEX, DROP SEQUENCE, DROP VIEW,
 *		DROP MATERIALIZED VIEW, DROP FOREIGN TABLE
 */
void
RemoveRelations(DropStmt *drop)
{
	ObjectAddresses *objects;
	char		relkind;
	ListCell   *cell;
	int			flags = 0;
	LOCKMODE	lockmode = AccessExclusiveLock;

	/* DROP CONCURRENTLY uses a weaker lock, and has some restrictions */
	if (drop->concurrent)
	{
		/*
		 * Note that for temporary relations this lock may get upgraded later
		 * on, but as no other session can access a temporary relation, this
		 * is actually fine.
		 */
		lockmode = ShareUpdateExclusiveLock;
		Assert(drop->removeType == OBJECT_INDEX);
		if (list_length(drop->objects) != 1)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("DROP INDEX CONCURRENTLY does not support dropping multiple objects")));
		if (drop->behavior == DROP_CASCADE)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("DROP INDEX CONCURRENTLY does not support CASCADE")));
	}

	/*
	 * First we identify all the relations, then we delete them in a single
	 * performMultipleDeletions() call.  This is to avoid unwanted DROP
	 * RESTRICT errors if one of the relations depends on another.
	 */

	/* Determine required relkind */
	switch (drop->removeType)
	{
		case OBJECT_TABLE:
			relkind = RELKIND_RELATION;
			break;

		case OBJECT_INDEX:
			relkind = RELKIND_INDEX;
			break;

		case OBJECT_SEQUENCE:
			relkind = RELKIND_SEQUENCE;
			break;

		case OBJECT_VIEW:
			relkind = RELKIND_VIEW;
			break;

		case OBJECT_MATVIEW:
			relkind = RELKIND_MATVIEW;
			break;

		case OBJECT_FOREIGN_TABLE:
			relkind = RELKIND_FOREIGN_TABLE;
			break;

		default:
			elog(ERROR, "unrecognized drop object type: %d",
				 (int) drop->removeType);
			relkind = 0;		/* keep compiler quiet */
			break;
	}

	/* Lock and validate each relation; build a list of object addresses */
	objects = new_object_addresses();

	bool only_temp_tables = IsYugaByteEnabled() && relkind == RELKIND_RELATION;
	foreach(cell, drop->objects)
	{
		RangeVar   *rel = makeRangeVarFromNameList((List *) lfirst(cell));
		Oid			relOid;
		ObjectAddress obj;
		struct DropRelationCallbackState state;

		/*
		 * These next few steps are a great deal like relation_openrv, but we
		 * don't bother building a relcache entry since we don't need it.
		 *
		 * Check for shared-cache-inval messages before trying to access the
		 * relation.  This is needed to cover the case where the name
		 * identifies a rel that has been dropped and recreated since the
		 * start of our transaction: if we don't flush the old syscache entry,
		 * then we'll latch onto that entry and suffer an error later.
		 */
		AcceptInvalidationMessages();

		/* Look up the appropriate relation using namespace search. */
		state.expected_relkind = relkind;
		state.heap_lockmode = drop->concurrent ?
			ShareUpdateExclusiveLock : AccessExclusiveLock;
		/* We must initialize these fields to show that no locks are held: */
		state.heapOid = InvalidOid;
		state.partParentOid = InvalidOid;

		relOid = RangeVarGetRelidExtended(rel, lockmode, RVR_MISSING_OK,
										  RangeVarCallbackForDropRelation,
										  (void *) &state);

		/* Not there? */
		if (!OidIsValid(relOid))
		{
			DropErrorMsgNonExistent(rel, relkind, drop->missing_ok);
			continue;
		}

		/*
		 * Decide if concurrent mode needs to be used here or not.  The
		 * callback retrieved the rel's persistence for us.
		 */
		if (drop->concurrent &&
			state.actual_relpersistence != RELPERSISTENCE_TEMP)
		{
			Assert(list_length(drop->objects) == 1 &&
				   drop->removeType == OBJECT_INDEX);
			flags |= PERFORM_DELETION_CONCURRENTLY;
		}

		/*
		 * Concurrent index drop cannot be used with partitioned indexes,
		 * either.
		 */
		if ((flags & PERFORM_DELETION_CONCURRENTLY) != 0 &&
			state.actual_relkind == RELKIND_PARTITIONED_INDEX)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot drop partitioned index \"%s\" concurrently",
							rel->relname)));

		/*
		 * If we're told to drop a partitioned index, we must acquire lock on
		 * all the children of its parent partitioned table before proceeding.
		 * Otherwise we'd try to lock the child index partitions before their
		 * tables, leading to potential deadlock against other sessions that
		 * will lock those objects in the other order.
		 */
		if (state.actual_relkind == RELKIND_PARTITIONED_INDEX)
			(void) find_all_inheritors(state.heapOid,
									   state.heap_lockmode,
									   NULL);

		if (only_temp_tables)
		{
			Relation relation = table_open(relOid, NoLock);
			only_temp_tables = relation->rd_rel->relpersistence ==
							   RELPERSISTENCE_TEMP;
			table_close(relation, NoLock);
		}

		/* OK, we're ready to delete this one */
		obj.classId = RelationRelationId;
		obj.objectId = relOid;
		obj.objectSubId = 0;

		add_exact_object_address(&obj, objects);
	}

	if (only_temp_tables)
		YBCForceAllowCatalogModifications(true);

	performMultipleDeletions(objects, drop->behavior, flags);

	free_object_addresses(objects);
}

/*
 * Before acquiring a table lock, check whether we have sufficient rights.
 * In the case of DROP INDEX, also try to lock the table before the index.
 * Also, if the table to be dropped is a partition, we try to lock the parent
 * first.
 */
static void
RangeVarCallbackForDropRelation(const RangeVar *rel, Oid relOid, Oid oldRelOid,
								void *arg)
{
	HeapTuple	tuple;
	struct DropRelationCallbackState *state;
	char		expected_relkind;
	bool		is_partition;
	Form_pg_class classform;
	LOCKMODE	heap_lockmode;
	bool		invalid_system_index = false;

	state = (struct DropRelationCallbackState *) arg;
	heap_lockmode = state->heap_lockmode;

	/*
	 * If we previously locked some other index's heap, and the name we're
	 * looking up no longer refers to that relation, release the now-useless
	 * lock.
	 */
	if (relOid != oldRelOid && OidIsValid(state->heapOid))
	{
		UnlockRelationOid(state->heapOid, heap_lockmode);
		state->heapOid = InvalidOid;
	}

	/*
	 * Similarly, if we previously locked some other partition's heap, and the
	 * name we're looking up no longer refers to that relation, release the
	 * now-useless lock.
	 */
	if (relOid != oldRelOid && OidIsValid(state->partParentOid))
	{
		UnlockRelationOid(state->partParentOid, AccessExclusiveLock);
		state->partParentOid = InvalidOid;
	}

	/* Didn't find a relation, so no need for locking or permission checks. */
	if (!OidIsValid(relOid))
		return;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relOid));
	if (!HeapTupleIsValid(tuple))
		return;					/* concurrently dropped, so nothing to do */
	classform = (Form_pg_class) GETSTRUCT(tuple);
	is_partition = classform->relispartition;

	/* Pass back some data to save lookups in RemoveRelations */
	state->actual_relkind = classform->relkind;
	state->actual_relpersistence = classform->relpersistence;

	/*
	 * Both RELKIND_RELATION and RELKIND_PARTITIONED_TABLE are OBJECT_TABLE,
	 * but RemoveRelations() can only pass one relkind for a given relation.
	 * It chooses RELKIND_RELATION for both regular and partitioned tables.
	 * That means we must be careful before giving the wrong type error when
	 * the relation is RELKIND_PARTITIONED_TABLE.  An equivalent problem
	 * exists with indexes.
	 */
	if (classform->relkind == RELKIND_PARTITIONED_TABLE)
		expected_relkind = RELKIND_RELATION;
	else if (classform->relkind == RELKIND_PARTITIONED_INDEX)
		expected_relkind = RELKIND_INDEX;
	else
		expected_relkind = classform->relkind;

	if (state->expected_relkind != expected_relkind)
		DropErrorMsgWrongType(rel->relname, classform->relkind,
							  state->expected_relkind);

	/* Allow DROP to either table owner or schema owner */
	if (!pg_class_ownercheck(relOid, GetUserId()) &&
		!pg_namespace_ownercheck(classform->relnamespace, GetUserId()) &&
		!IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER,
					   get_relkind_objtype(classform->relkind),
					   rel->relname);

	/*
	 * Check the case of a system index that might have been invalidated by a
	 * failed concurrent process and allow its drop. For the time being, this
	 * only concerns indexes of toast relations that became invalid during a
	 * REINDEX CONCURRENTLY process.
	 */
	if (IsSystemClass(relOid, classform) && classform->relkind == RELKIND_INDEX)
	{
		HeapTuple	locTuple;
		Form_pg_index indexform;
		bool		indisvalid;

		locTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(relOid));
		if (!HeapTupleIsValid(locTuple))
		{
			ReleaseSysCache(tuple);
			return;
		}

		indexform = (Form_pg_index) GETSTRUCT(locTuple);
		indisvalid = indexform->indisvalid;
		ReleaseSysCache(locTuple);

		/* Mark object as being an invalid index of system catalogs */
		if (!indisvalid)
			invalid_system_index = true;
	}

	/* In the case of an invalid index, it is fine to bypass this check */
	if (!invalid_system_index && !allowSystemTableMods && IsSystemClass(relOid, classform))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						rel->relname)));

	ReleaseSysCache(tuple);

	/*
	 * In DROP INDEX, attempt to acquire lock on the parent table before
	 * locking the index.  index_drop() will need this anyway, and since
	 * regular queries lock tables before their indexes, we risk deadlock if
	 * we do it the other way around.  No error if we don't find a pg_index
	 * entry, though --- the relation may have been dropped.  Note that this
	 * code will execute for either plain or partitioned indexes.
	 */
	if (expected_relkind == RELKIND_INDEX &&
		relOid != oldRelOid)
	{
		state->heapOid = IndexGetRelation(relOid, true);
		if (OidIsValid(state->heapOid))
			LockRelationOid(state->heapOid, heap_lockmode);
	}

	/*
	 * Similarly, if the relation is a partition, we must acquire lock on its
	 * parent before locking the partition.  That's because queries lock the
	 * parent before its partitions, so we risk deadlock if we do it the other
	 * way around.
	 */
	if (is_partition && relOid != oldRelOid)
	{
		state->partParentOid = get_partition_parent(relOid, true);
		if (OidIsValid(state->partParentOid))
			LockRelationOid(state->partParentOid, AccessExclusiveLock);
	}
}

/*
 * ExecuteTruncate
 *		Executes a TRUNCATE command.
 *
 * This is a multi-relation truncate.  We first open and grab exclusive
 * lock on all relations involved, checking permissions and otherwise
 * verifying that the relation is OK for truncation.  Note that if relations
 * are foreign tables, at this stage, we have not yet checked that their
 * foreign data in external data sources are OK for truncation.  These are
 * checked when foreign data are actually truncated later.  In CASCADE mode,
 * relations having FK references to the targeted relations are automatically
 * added to the group; in RESTRICT mode, we check that all FK references are
 * internal to the group that's being truncated.  Finally all the relations
 * are truncated and reindexed.
 */
void
ExecuteTruncate(TruncateStmt *stmt)
{
	List	   *rels = NIL;
	List	   *relids = NIL;
	List	   *relids_logged = NIL;
	ListCell   *cell;

	/*
	 * Open, exclusive-lock, and check all the explicitly-specified relations
	 */
	foreach(cell, stmt->relations)
	{
		RangeVar   *rv = lfirst(cell);
		Relation	rel;
		bool		recurse = rv->inh;
		Oid			myrelid;
		LOCKMODE	lockmode = AccessExclusiveLock;

		myrelid = RangeVarGetRelidExtended(rv, lockmode,
										   0, RangeVarCallbackForTruncate,
										   NULL);

		/* don't throw error for "TRUNCATE foo, foo" */
		if (list_member_oid(relids, myrelid))
			continue;

		/* open the relation, we already hold a lock on it */
		rel = table_open(myrelid, NoLock);

		/*
		 * RangeVarGetRelidExtended() has done most checks with its callback,
		 * but other checks with the now-opened Relation remain.
		 */
		truncate_check_activity(rel);

		rels = lappend(rels, rel);
		relids = lappend_oid(relids, myrelid);

		/* Log this relation only if needed for logical decoding */
		if (RelationIsLogicallyLogged(rel))
			relids_logged = lappend_oid(relids_logged, myrelid);

		if (recurse)
		{
			ListCell   *child;
			List	   *children;

			children = find_all_inheritors(myrelid, lockmode, NULL);

			foreach(child, children)
			{
				Oid			childrelid = lfirst_oid(child);

				if (list_member_oid(relids, childrelid))
					continue;

				/* find_all_inheritors already got lock */
				rel = table_open(childrelid, NoLock);

				/*
				 * It is possible that the parent table has children that are
				 * temp tables of other backends.  We cannot safely access
				 * such tables (because of buffering issues), and the best
				 * thing to do is to silently ignore them.  Note that this
				 * check is the same as one of the checks done in
				 * truncate_check_activity() called below, still it is kept
				 * here for simplicity.
				 */
				if (RELATION_IS_OTHER_TEMP(rel))
				{
					table_close(rel, lockmode);
					continue;
				}

				/*
				 * Inherited TRUNCATE commands perform access permission
				 * checks on the parent table only. So we skip checking the
				 * children's permissions and don't call
				 * truncate_check_perms() here.
				 */
				truncate_check_rel(RelationGetRelid(rel), rel->rd_rel);
				truncate_check_activity(rel);

				rels = lappend(rels, rel);
				relids = lappend_oid(relids, childrelid);

				/* Log this relation only if needed for logical decoding */
				if (RelationIsLogicallyLogged(rel))
					relids_logged = lappend_oid(relids_logged, childrelid);
			}
		}
		else if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot truncate only a partitioned table"),
					 errhint("Do not specify the ONLY keyword, or use TRUNCATE ONLY on the partitions directly.")));
	}

	ExecuteTruncateGuts(rels, relids, relids_logged,
						stmt->behavior, stmt->restart_seqs);

	/* And close the rels */
	foreach(cell, rels)
	{
		Relation	rel = (Relation) lfirst(cell);

		table_close(rel, NoLock);
	}
}

/*
 * ExecuteTruncateGuts
 *
 * Internal implementation of TRUNCATE.  This is called by the actual TRUNCATE
 * command (see above) as well as replication subscribers that execute a
 * replicated TRUNCATE action.
 *
 * explicit_rels is the list of Relations to truncate that the command
 * specified.  relids is the list of Oids corresponding to explicit_rels.
 * relids_logged is the list of Oids (a subset of relids) that require
 * WAL-logging.  This is all a bit redundant, but the existing callers have
 * this information handy in this form.
 */
void
ExecuteTruncateGuts(List *explicit_rels,
					List *relids,
					List *relids_logged,
					DropBehavior behavior, bool restart_seqs)
{
	List	   *rels;
	List	   *seq_relids = NIL;
	HTAB	   *ft_htab = NULL;
	EState	   *estate;
	ResultRelInfo *resultRelInfos;
	ResultRelInfo *resultRelInfo;
	SubTransactionId mySubid;
	ListCell   *cell;
	Oid		   *logrelids;

	/*
	 * Check the explicitly-specified relations.
	 *
	 * In CASCADE mode, suck in all referencing relations as well.  This
	 * requires multiple iterations to find indirectly-dependent relations. At
	 * each phase, we need to exclusive-lock new rels before looking for their
	 * dependencies, else we might miss something.  Also, we check each rel as
	 * soon as we open it, to avoid a faux pas such as holding lock for a long
	 * time on a rel we have no permissions for.
	 */
	rels = list_copy(explicit_rels);
	if (behavior == DROP_CASCADE)
	{
		for (;;)
		{
			List	   *newrelids;

			newrelids = heap_truncate_find_FKs(relids);
			if (newrelids == NIL)
				break;			/* nothing else to add */

			foreach(cell, newrelids)
			{
				Oid			relid = lfirst_oid(cell);
				Relation	rel;

				rel = table_open(relid, AccessExclusiveLock);
				ereport(NOTICE,
						(errmsg("truncate cascades to table \"%s\"",
								RelationGetRelationName(rel))));
				truncate_check_rel(relid, rel->rd_rel);
				truncate_check_perms(relid, rel->rd_rel);
				truncate_check_activity(rel);
				rels = lappend(rels, rel);
				relids = lappend_oid(relids, relid);

				/* Log this relation only if needed for logical decoding */
				if (RelationIsLogicallyLogged(rel))
					relids_logged = lappend_oid(relids_logged, relid);
			}
		}
	}

	/*
	 * Check foreign key references.  In CASCADE mode, this should be
	 * unnecessary since we just pulled in all the references; but as a
	 * cross-check, do it anyway if in an Assert-enabled build.
	 */
#ifdef USE_ASSERT_CHECKING
	heap_truncate_check_FKs(rels, false);
#else
	if (behavior == DROP_RESTRICT)
		heap_truncate_check_FKs(rels, false);
#endif

	/*
	 * If we are asked to restart sequences, find all the sequences, lock them
	 * (we need AccessExclusiveLock for ResetSequence), and check permissions.
	 * We want to do this early since it's pointless to do all the truncation
	 * work only to fail on sequence permissions.
	 */
	if (restart_seqs)
	{
		foreach(cell, rels)
		{
			Relation	rel = (Relation) lfirst(cell);
			List	   *seqlist = getOwnedSequences(RelationGetRelid(rel));
			ListCell   *seqcell;

			foreach(seqcell, seqlist)
			{
				Oid			seq_relid = lfirst_oid(seqcell);
				Relation	seq_rel;

				seq_rel = relation_open(seq_relid, AccessExclusiveLock);

				/* This check must match AlterSequence! */
				if (!pg_class_ownercheck(seq_relid, GetUserId()))
					aclcheck_error(ACLCHECK_NOT_OWNER, OBJECT_SEQUENCE,
								   RelationGetRelationName(seq_rel));

				seq_relids = lappend_oid(seq_relids, seq_relid);

				relation_close(seq_rel, NoLock);
			}
		}
	}

	/* Prepare to catch AFTER triggers. */
	AfterTriggerBeginQuery();

	/*
	 * To fire triggers, we'll need an EState as well as a ResultRelInfo for
	 * each relation.  We don't need to call ExecOpenIndices, though.
	 *
	 * We put the ResultRelInfos in the es_opened_result_relations list, even
	 * though we don't have a range table and don't populate the
	 * es_result_relations array.  That's a bit bogus, but it's enough to make
	 * ExecGetTriggerResultRel() find them.
	 */
	estate = CreateExecutorState();
	resultRelInfos = (ResultRelInfo *)
		palloc(list_length(rels) * sizeof(ResultRelInfo));
	resultRelInfo = resultRelInfos;
	foreach(cell, rels)
	{
		Relation	rel = (Relation) lfirst(cell);

		InitResultRelInfo(resultRelInfo,
						  rel,
						  0,	/* dummy rangetable index */
						  NULL,
						  0);
		estate->es_opened_result_relations =
			lappend(estate->es_opened_result_relations, resultRelInfo);
		resultRelInfo++;
	}

	/*
	 * Process all BEFORE STATEMENT TRUNCATE triggers before we begin
	 * truncating (this is because one of them might throw an error). Also, if
	 * we were to allow them to prevent statement execution, that would need
	 * to be handled here.
	 */
	resultRelInfo = resultRelInfos;
	foreach(cell, rels)
	{
		ExecBSTruncateTriggers(estate, resultRelInfo);
		resultRelInfo++;
	}

	/*
	 * OK, truncate each table.
	 */
	mySubid = GetCurrentSubTransactionId();

	foreach(cell, rels)
	{
		Relation	rel = (Relation) lfirst(cell);

		/* Skip partitioned tables as there is nothing to do */
		if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
			continue;

		/*
		 * Build the lists of foreign tables belonging to each foreign server
		 * and pass each list to the foreign data wrapper's callback function,
		 * so that each server can truncate its all foreign tables in bulk.
		 * Each list is saved as a single entry in a hash table that uses the
		 * server OID as lookup key.
		 */
		if (rel->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
		{
			Oid			serverid = GetForeignServerIdByRelId(RelationGetRelid(rel));
			bool		found;
			ForeignTruncateInfo *ft_info;

			/* First time through, initialize hashtable for foreign tables */
			if (!ft_htab)
			{
				HASHCTL		hctl;

				memset(&hctl, 0, sizeof(HASHCTL));
				hctl.keysize = sizeof(Oid);
				hctl.entrysize = sizeof(ForeignTruncateInfo);
				hctl.hcxt = GetCurrentMemoryContext();

				ft_htab = hash_create("TRUNCATE for Foreign Tables",
									  32,	/* start small and extend */
									  &hctl,
									  HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
			}

			/* Find or create cached entry for the foreign table */
			ft_info = hash_search(ft_htab, &serverid, HASH_ENTER, &found);
			if (!found)
			{
				ft_info->serverid = serverid;
				ft_info->rels = NIL;
			}

			/*
			 * Save the foreign table in the entry of the server that the
			 * foreign table belongs to.
			 */
			ft_info->rels = lappend(ft_info->rels, rel);
			continue;
		}

		/*
		 * Normally, we need a transaction-safe truncation here.  However, if
		 * the table was either created in the current (sub)transaction or has
		 * a new relfilenode in the current (sub)transaction, then we can just
		 * truncate it in-place, because a rollback would cause the whole
		 * table or the current physical file to be thrown away anyway.
		 * YB: Check if the unsafe truncate method should be used.
		 */
		if (YbUseUnsafeTruncate(rel))
			YbUnsafeTruncate(rel);
		else if (rel->rd_createSubid == mySubid ||
				 rel->rd_newRelfilenodeSubid == mySubid || !IsYBRelation(rel))
		{
			/* Immediate, non-rollbackable truncation is OK */
			heap_truncate_one_rel(rel);
		}
		else
		{
			Oid			heap_relid;
			Oid			toast_relid;
			ReindexParams reindex_params = {0};

			/*
			 * This effectively deletes all rows in the table, and may be done
			 * in a serializable transaction.  In that case we must record a
			 * rw-conflict in to this transaction from each transaction
			 * holding a predicate lock on the table.
			 */
			CheckTableForSerializableConflictIn(rel);

			/*
			 * Need the full transaction-safe pushups.
			 *
			 * Create a new empty storage file for the relation, and assign it
			 * as the relfilenode value. The old storage file is scheduled for
			 * deletion at commit.
			 */
			RelationSetNewRelfilenode(rel, rel->rd_rel->relpersistence,
									  false /* yb_copy_split_options */);

			heap_relid = RelationGetRelid(rel);

			/*
			 * The same for the toast table, if any.
			 */
			toast_relid = rel->rd_rel->reltoastrelid;
			if (OidIsValid(toast_relid))
			{
				Relation	toastrel = relation_open(toast_relid,
													 AccessExclusiveLock);

				RelationSetNewRelfilenode(toastrel,
										  toastrel->rd_rel->relpersistence,
										  false /* yb_copy_split_options */);
				table_close(toastrel, NoLock);
			}

			/*
			 * Reconstruct the indexes to match, and we're done.
			 */
			reindex_relation(heap_relid, REINDEX_REL_PROCESS_TOAST,
							 &reindex_params,
							 true /* is_yb_table_rewrite */,
							 false /* yb_copy_split_options */);
		}

		pgstat_count_truncate(rel);
	}

	/* Now go through the hash table, and truncate foreign tables */
	if (ft_htab)
	{
		ForeignTruncateInfo *ft_info;
		HASH_SEQ_STATUS seq;

		hash_seq_init(&seq, ft_htab);

		PG_TRY();
		{
			while ((ft_info = hash_seq_search(&seq)) != NULL)
			{
				FdwRoutine *routine = GetFdwRoutineByServerId(ft_info->serverid);

				/* truncate_check_rel() has checked that already */
				Assert(routine->ExecForeignTruncate != NULL);

				routine->ExecForeignTruncate(ft_info->rels,
											 behavior,
											 restart_seqs);
			}
		}
		PG_FINALLY();
		{
			hash_destroy(ft_htab);
		}
		PG_END_TRY();
	}

	/*
	 * Restart owned sequences if we were asked to.
	 */
	foreach(cell, seq_relids)
	{
		Oid			seq_relid = lfirst_oid(cell);

		ResetSequence(seq_relid);
	}

	/*
	 * Write a WAL record to allow this set of actions to be logically
	 * decoded.
	 *
	 * Assemble an array of relids so we can write a single WAL record for the
	 * whole action.
	 */
	if (list_length(relids_logged) > 0)
	{
		xl_heap_truncate xlrec;
		int			i = 0;

		/* should only get here if wal_level >= logical */
		Assert(XLogLogicalInfoActive());

		logrelids = palloc(list_length(relids_logged) * sizeof(Oid));
		foreach(cell, relids_logged)
			logrelids[i++] = lfirst_oid(cell);

		xlrec.dbId = MyDatabaseId;
		xlrec.nrelids = list_length(relids_logged);
		xlrec.flags = 0;
		if (behavior == DROP_CASCADE)
			xlrec.flags |= XLH_TRUNCATE_CASCADE;
		if (restart_seqs)
			xlrec.flags |= XLH_TRUNCATE_RESTART_SEQS;

		XLogBeginInsert();
		XLogRegisterData((char *) &xlrec, SizeOfHeapTruncate);
		XLogRegisterData((char *) logrelids, list_length(relids_logged) * sizeof(Oid));

		XLogSetRecordFlags(XLOG_INCLUDE_ORIGIN);

		(void) XLogInsert(RM_HEAP_ID, XLOG_HEAP_TRUNCATE);
	}

	/*
	 * Process all AFTER STATEMENT TRUNCATE triggers.
	 */
	resultRelInfo = resultRelInfos;
	foreach(cell, rels)
	{
		ExecASTruncateTriggers(estate, resultRelInfo);
		resultRelInfo++;
	}

	/* Handle queued AFTER triggers */
	AfterTriggerEndQuery(estate);

	/* We can clean up the EState now */
	FreeExecutorState(estate);

	/*
	 * Close any rels opened by CASCADE (can't do this while EState still
	 * holds refs)
	 */
	rels = list_difference_ptr(rels, explicit_rels);
	foreach(cell, rels)
	{
		Relation	rel = (Relation) lfirst(cell);

		table_close(rel, NoLock);
	}
}

/*
 * Check that a given relation is safe to truncate.  Subroutine for
 * ExecuteTruncate() and RangeVarCallbackForTruncate().
 */
static void
truncate_check_rel(Oid relid, Form_pg_class reltuple)
{
	char	   *relname = NameStr(reltuple->relname);

	/*
	 * Only allow truncate on regular tables, foreign tables using foreign
	 * data wrappers supporting TRUNCATE and partitioned tables (although, the
	 * latter are only being included here for the following checks; no
	 * physical truncation will occur in their case.).
	 */
	if (reltuple->relkind == RELKIND_FOREIGN_TABLE)
	{
		Oid			serverid = GetForeignServerIdByRelId(relid);
		FdwRoutine *fdwroutine = GetFdwRoutineByServerId(serverid);

		if (!fdwroutine->ExecForeignTruncate)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot truncate foreign table \"%s\"",
							relname)));
	}
	else if (reltuple->relkind != RELKIND_RELATION &&
			 reltuple->relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a table", relname)));

	/*
	 * Most system catalogs can't be truncated at all, or at least not unless
	 * allow_system_table_mods=on. As an exception, however, we allow
	 * pg_largeobject to be truncated as part of pg_upgrade, because we need
	 * to change its relfilenode to match the old cluster, and allowing a
	 * TRUNCATE command to be executed is the easiest way of doing that.
	 */
	if (!allowSystemTableMods && IsSystemClass(relid, reltuple)
		&& (!IsBinaryUpgrade || relid != LargeObjectRelationId))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						relname)));

	InvokeObjectTruncateHook(relid);
}

/*
 * Check that current user has the permission to truncate given relation.
 */
static void
truncate_check_perms(Oid relid, Form_pg_class reltuple)
{
	char	   *relname = NameStr(reltuple->relname);
	AclResult	aclresult;

	/* Permissions checks */
	aclresult = pg_class_aclcheck(relid, GetUserId(), ACL_TRUNCATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, get_relkind_objtype(reltuple->relkind),
					   relname);
}

/*
 * Set of extra sanity checks to check if a given relation is safe to
 * truncate.  This is split with truncate_check_rel() as
 * RangeVarCallbackForTruncate() cannot open a Relation yet.
 */
static void
truncate_check_activity(Relation rel)
{
	/*
	 * Don't allow truncate on temp tables of other backends ... their local
	 * buffer manager is not going to cope.
	 */
	if (RELATION_IS_OTHER_TEMP(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot truncate temporary tables of other sessions")));

	/*
	 * Also check for active uses of the relation in the current transaction,
	 * including open scans and pending AFTER trigger events.
	 */
	CheckTableNotInUse(rel, "TRUNCATE");
}

/*
 * storage_name
 *	  returns the name corresponding to a typstorage/attstorage enum value
 */
static const char *
storage_name(char c)
{
	switch (c)
	{
		case TYPSTORAGE_PLAIN:
			return "PLAIN";
		case TYPSTORAGE_EXTERNAL:
			return "EXTERNAL";
		case TYPSTORAGE_EXTENDED:
			return "EXTENDED";
		case TYPSTORAGE_MAIN:
			return "MAIN";
		default:
			return "???";
	}
}

/*----------
 * MergeAttributes
 *		Returns new schema given initial schema and superclasses.
 *
 * Input arguments:
 * 'schema' is the column/attribute definition for the table. (It's a list
 *		of ColumnDef's.) It is destructively changed.
 * 'supers' is a list of OIDs of parent relations, already locked by caller.
 * 'relpersistence' is the persistence type of the table.
 * 'is_partition' tells if the table is a partition.
 *
 * Output arguments:
 * 'supconstr' receives a list of constraints belonging to the parents,
 *		updated as necessary to be valid for the child.
 *
 * Return value:
 * Completed schema list.
 *
 * Notes:
 *	  The order in which the attributes are inherited is very important.
 *	  Intuitively, the inherited attributes should come first. If a table
 *	  inherits from multiple parents, the order of those attributes are
 *	  according to the order of the parents specified in CREATE TABLE.
 *
 *	  Here's an example:
 *
 *		create table person (name text, age int4, location point);
 *		create table emp (salary int4, manager text) inherits(person);
 *		create table student (gpa float8) inherits (person);
 *		create table stud_emp (percent int4) inherits (emp, student);
 *
 *	  The order of the attributes of stud_emp is:
 *
 *							person {1:name, 2:age, 3:location}
 *							/	 \
 *			   {6:gpa}	student   emp {4:salary, 5:manager}
 *							\	 /
 *						   stud_emp {7:percent}
 *
 *	   If the same attribute name appears multiple times, then it appears
 *	   in the result table in the proper location for its first appearance.
 *
 *	   Constraints (including NOT NULL constraints) for the child table
 *	   are the union of all relevant constraints, from both the child schema
 *	   and parent tables.
 *
 *	   The default value for a child column is defined as:
 *		(1) If the child schema specifies a default, that value is used.
 *		(2) If neither the child nor any parent specifies a default, then
 *			the column will not have a default.
 *		(3) If conflicting defaults are inherited from different parents
 *			(and not overridden by the child), an error is raised.
 *		(4) Otherwise the inherited default is used.
 *		Rule (3) is new in Postgres 7.1; in earlier releases you got a
 *		rather arbitrary choice of which parent default to use.
 *----------
 */
static List *
MergeAttributes(List *schema, List *supers, char relpersistence,
				bool is_partition, List **supconstr)
{
	List	   *inhSchema = NIL;
	List	   *constraints = NIL;
	bool		have_bogus_defaults = false;
	int			child_attno;
	static Node bogus_marker = {0}; /* marks conflicting defaults */
	List	   *saved_schema = NIL;
	ListCell   *entry;

	/*
	 * Check for and reject tables with too many columns. We perform this
	 * check relatively early for two reasons: (a) we don't run the risk of
	 * overflowing an AttrNumber in subsequent code (b) an O(n^2) algorithm is
	 * okay if we're processing <= 1600 columns, but could take minutes to
	 * execute if the user attempts to create a table with hundreds of
	 * thousands of columns.
	 *
	 * Note that we also need to check that we do not exceed this figure after
	 * including columns from inherited relations.
	 */
	if (list_length(schema) > MaxHeapAttributeNumber)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_COLUMNS),
				 errmsg("tables can have at most %d columns",
						MaxHeapAttributeNumber)));

	/*
	 * Check for duplicate names in the explicit list of attributes.
	 *
	 * Although we might consider merging such entries in the same way that we
	 * handle name conflicts for inherited attributes, it seems to make more
	 * sense to assume such conflicts are errors.
	 *
	 * We don't use foreach() here because we have two nested loops over the
	 * schema list, with possible element deletions in the inner one.  If we
	 * used foreach_delete_current() it could only fix up the state of one of
	 * the loops, so it seems cleaner to use looping over list indexes for
	 * both loops.  Note that any deletion will happen beyond where the outer
	 * loop is, so its index never needs adjustment.
	 */
	for (int coldefpos = 0; coldefpos < list_length(schema); coldefpos++)
	{
		ColumnDef  *coldef = list_nth_node(ColumnDef, schema, coldefpos);

		if (!is_partition && coldef->typeName == NULL)
		{
			/*
			 * Typed table column option that does not belong to a column from
			 * the type.  This works because the columns from the type come
			 * first in the list.  (We omit this check for partition column
			 * lists; those are processed separately below.)
			 */
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" does not exist",
							coldef->colname)));
		}

		/* restpos scans all entries beyond coldef; incr is in loop body */
		for (int restpos = coldefpos + 1; restpos < list_length(schema);)
		{
			ColumnDef  *restdef = list_nth_node(ColumnDef, schema, restpos);

			if (strcmp(coldef->colname, restdef->colname) == 0)
			{
				if (coldef->is_from_type)
				{
					/*
					 * merge the column options into the column from the type
					 */
					coldef->is_not_null = restdef->is_not_null;
					coldef->raw_default = restdef->raw_default;
					coldef->cooked_default = restdef->cooked_default;
					coldef->constraints = restdef->constraints;
					coldef->is_from_type = false;
					schema = list_delete_nth_cell(schema, restpos);
				}
				else
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_COLUMN),
							 errmsg("column \"%s\" specified more than once",
									coldef->colname)));
			}
			else
				restpos++;
		}
	}

	/*
	 * In case of a partition, there are no new column definitions, only dummy
	 * ColumnDefs created for column constraints.  Set them aside for now and
	 * process them at the end.
	 */
	if (is_partition)
	{
		saved_schema = schema;
		schema = NIL;
	}

	/*
	 * Scan the parents left-to-right, and merge their attributes to form a
	 * list of inherited attributes (inhSchema).  Also check to see if we need
	 * to inherit an OID column.
	 */
	child_attno = 0;
	foreach(entry, supers)
	{
		Oid			parent = lfirst_oid(entry);
		Relation	relation;
		TupleDesc	tupleDesc;
		TupleConstr *constr;
		AttrMap    *newattmap;
		List	   *inherited_defaults;
		List	   *cols_with_defaults;
		AttrNumber	parent_attno;
		ListCell   *lc1;
		ListCell   *lc2;

		/* caller already got lock */
		relation = table_open(parent, NoLock);

		/*
		 * Check for active uses of the parent partitioned table in the
		 * current transaction, such as being used in some manner by an
		 * enclosing command.
		 */
		if (is_partition)
			CheckTableNotInUse(relation, "CREATE TABLE .. PARTITION OF");

		/*
		 * We do not allow partitioned tables and partitions to participate in
		 * regular inheritance.
		 */
		if (relation->rd_rel->relkind == RELKIND_PARTITIONED_TABLE &&
			!is_partition)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot inherit from partitioned table \"%s\"",
							RelationGetRelationName(relation))));
		if (relation->rd_rel->relispartition && !is_partition)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot inherit from partition \"%s\"",
							RelationGetRelationName(relation))));

		if (relation->rd_rel->relkind != RELKIND_RELATION &&
			relation->rd_rel->relkind != RELKIND_FOREIGN_TABLE &&
			relation->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("inherited relation \"%s\" is not a table or foreign table",
							RelationGetRelationName(relation))));

		/*
		 * If the parent is permanent, so must be all of its partitions.  Note
		 * that inheritance allows that case.
		 */
		if (is_partition &&
			relation->rd_rel->relpersistence != RELPERSISTENCE_TEMP &&
			relpersistence == RELPERSISTENCE_TEMP)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot create a temporary relation as partition of permanent relation \"%s\"",
							RelationGetRelationName(relation))));

		/* Permanent rels cannot inherit from temporary ones */
		if (relpersistence != RELPERSISTENCE_TEMP &&
			relation->rd_rel->relpersistence == RELPERSISTENCE_TEMP)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg(!is_partition
							? "cannot inherit from temporary relation \"%s\""
							: "cannot create a permanent relation as partition of temporary relation \"%s\"",
							RelationGetRelationName(relation))));

		/* If existing rel is temp, it must belong to this session */
		if (relation->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
			!relation->rd_islocaltemp)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg(!is_partition
							? "cannot inherit from temporary relation of another session"
							: "cannot create as partition of temporary relation of another session")));

		/*
		 * We should have an UNDER permission flag for this, but for now,
		 * demand that creator of a child table own the parent.
		 */
		if (!pg_class_ownercheck(RelationGetRelid(relation), GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(relation->rd_rel->relkind),
						   RelationGetRelationName(relation));

		tupleDesc = RelationGetDescr(relation);
		constr = tupleDesc->constr;

		/*
		 * newattmap->attnums[] will contain the child-table attribute numbers
		 * for the attributes of this parent table.  (They are not the same
		 * for parents after the first one, nor if we have dropped columns.)
		 */
		newattmap = make_attrmap(tupleDesc->natts);

		/* We can't process inherited defaults until newattmap is complete. */
		inherited_defaults = cols_with_defaults = NIL;

		for (parent_attno = 1; parent_attno <= tupleDesc->natts;
			 parent_attno++)
		{
			Form_pg_attribute attribute = TupleDescAttr(tupleDesc,
														parent_attno - 1);
			char	   *attributeName = NameStr(attribute->attname);
			int			exist_attno;
			ColumnDef  *def;

			/*
			 * Ignore dropped columns in the parent.
			 */
			if (attribute->attisdropped)
				continue;		/* leave newattmap->attnums entry as zero */

			/*
			 * Does it conflict with some previously inherited column?
			 */
			exist_attno = findAttrByName(attributeName, inhSchema);
			if (exist_attno > 0)
			{
				Oid			defTypeId;
				int32		deftypmod;
				Oid			defCollId;

				/*
				 * Yes, try to merge the two column definitions. They must
				 * have the same type, typmod, and collation.
				 */
				ereport(NOTICE,
						(errmsg("merging multiple inherited definitions of column \"%s\"",
								attributeName)));
				def = (ColumnDef *) list_nth(inhSchema, exist_attno - 1);
				typenameTypeIdAndMod(NULL, def->typeName, &defTypeId, &deftypmod);
				if (defTypeId != attribute->atttypid ||
					deftypmod != attribute->atttypmod)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("inherited column \"%s\" has a type conflict",
									attributeName),
							 errdetail("%s versus %s",
									   format_type_with_typemod(defTypeId,
																deftypmod),
									   format_type_with_typemod(attribute->atttypid,
																attribute->atttypmod))));
				defCollId = GetColumnDefCollation(NULL, def, defTypeId);
				if (defCollId != attribute->attcollation)
					ereport(ERROR,
							(errcode(ERRCODE_COLLATION_MISMATCH),
							 errmsg("inherited column \"%s\" has a collation conflict",
									attributeName),
							 errdetail("\"%s\" versus \"%s\"",
									   get_collation_name(defCollId),
									   get_collation_name(attribute->attcollation))));

				/* Copy/check storage parameter */
				if (def->storage == 0)
					def->storage = attribute->attstorage;
				else if (def->storage != attribute->attstorage)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("inherited column \"%s\" has a storage parameter conflict",
									attributeName),
							 errdetail("%s versus %s",
									   storage_name(def->storage),
									   storage_name(attribute->attstorage))));

				/* Copy/check compression parameter */
				if (CompressionMethodIsValid(attribute->attcompression))
				{
					const char *compression =
					GetCompressionMethodName(attribute->attcompression);

					if (def->compression == NULL)
						def->compression = pstrdup(compression);
					else if (strcmp(def->compression, compression) != 0)
						ereport(ERROR,
								(errcode(ERRCODE_DATATYPE_MISMATCH),
								 errmsg("column \"%s\" has a compression method conflict",
										attributeName),
								 errdetail("%s versus %s", def->compression, compression)));
				}

				def->inhcount++;
				/* Merge of NOT NULL constraints = OR 'em together */
				def->is_not_null |= attribute->attnotnull;
				/* Default and other constraints are handled below */
				newattmap->attnums[parent_attno - 1] = exist_attno;

				/* Check for GENERATED conflicts */
				if (def->generated != attribute->attgenerated)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("inherited column \"%s\" has a generation conflict",
									attributeName)));
			}
			else
			{
				/*
				 * No, create a new inherited column
				 */
				def = makeNode(ColumnDef);
				def->colname = pstrdup(attributeName);
				def->typeName = makeTypeNameFromOid(attribute->atttypid,
													attribute->atttypmod);
				def->inhcount = 1;
				def->is_local = false;
				def->is_not_null = attribute->attnotnull;
				def->is_from_type = false;
				def->storage = attribute->attstorage;
				def->raw_default = NULL;
				def->cooked_default = NULL;
				def->generated = attribute->attgenerated;
				def->collClause = NULL;
				def->collOid = attribute->attcollation;
				def->constraints = NIL;
				def->location = -1;
				if (CompressionMethodIsValid(attribute->attcompression))
					def->compression =
						pstrdup(GetCompressionMethodName(attribute->attcompression));
				else
					def->compression = NULL;
				inhSchema = lappend(inhSchema, def);
				newattmap->attnums[parent_attno - 1] = ++child_attno;
			}

			/*
			 * Locate default if any
			 */
			if (attribute->atthasdef)
			{
				Node	   *this_default = NULL;

				/* Find default in constraint structure */
				if (constr != NULL)
				{
					AttrDefault *attrdef = constr->defval;

					for (int i = 0; i < constr->num_defval; i++)
					{
						if (attrdef[i].adnum == parent_attno)
						{
							this_default = stringToNode(attrdef[i].adbin);
							break;
						}
					}
				}
				if (this_default == NULL)
					elog(ERROR, "default expression not found for attribute %d of relation \"%s\"",
						 parent_attno, RelationGetRelationName(relation));

				/*
				 * If it's a GENERATED default, it might contain Vars that
				 * need to be mapped to the inherited column(s)' new numbers.
				 * We can't do that till newattmap is ready, so just remember
				 * all the inherited default expressions for the moment.
				 */
				inherited_defaults = lappend(inherited_defaults, this_default);
				cols_with_defaults = lappend(cols_with_defaults, def);
			}
		}

		/*
		 * Now process any inherited default expressions, adjusting attnos
		 * using the completed newattmap map.
		 */
		forboth(lc1, inherited_defaults, lc2, cols_with_defaults)
		{
			Node	   *this_default = (Node *) lfirst(lc1);
			ColumnDef  *def = (ColumnDef *) lfirst(lc2);
			bool		found_whole_row;

			/* Adjust Vars to match new table's column numbering */
			this_default = map_variable_attnos(this_default,
											   1, 0,
											   newattmap,
											   InvalidOid, &found_whole_row);

			/*
			 * For the moment we have to reject whole-row variables.  We could
			 * convert them, if we knew the new table's rowtype OID, but that
			 * hasn't been assigned yet.  (A variable could only appear in a
			 * generation expression, so the error message is correct.)
			 */
			if (found_whole_row)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot convert whole-row table reference"),
						 errdetail("Generation expression for column \"%s\" contains a whole-row reference to table \"%s\".",
								   def->colname,
								   RelationGetRelationName(relation))));

			/*
			 * If we already had a default from some prior parent, check to
			 * see if they are the same.  If so, no problem; if not, mark the
			 * column as having a bogus default.  Below, we will complain if
			 * the bogus default isn't overridden by the child schema.
			 */
			Assert(def->raw_default == NULL);
			if (def->cooked_default == NULL)
				def->cooked_default = this_default;
			else if (!equal(def->cooked_default, this_default))
			{
				def->cooked_default = &bogus_marker;
				have_bogus_defaults = true;
			}
		}

		/*
		 * Now copy the CHECK constraints of this parent, adjusting attnos
		 * using the completed newattmap map.  Identically named constraints
		 * are merged if possible, else we throw error.
		 */
		if (constr && constr->num_check > 0)
		{
			ConstrCheck *check = constr->check;
			int			i;

			for (i = 0; i < constr->num_check; i++)
			{
				char	   *name = check[i].ccname;
				Node	   *expr;
				bool		found_whole_row;

				/* ignore if the constraint is non-inheritable */
				if (check[i].ccnoinherit)
					continue;

				/* Adjust Vars to match new table's column numbering */
				expr = map_variable_attnos(stringToNode(check[i].ccbin),
										   1, 0,
										   newattmap,
										   InvalidOid, &found_whole_row);

				/*
				 * For the moment we have to reject whole-row variables. We
				 * could convert them, if we knew the new table's rowtype OID,
				 * but that hasn't been assigned yet.
				 */
				if (found_whole_row)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot convert whole-row table reference"),
							 errdetail("Constraint \"%s\" contains a whole-row reference to table \"%s\".",
									   name,
									   RelationGetRelationName(relation))));

				/* check for duplicate */
				if (!MergeCheckConstraint(constraints, name, expr))
				{
					/* nope, this is a new one */
					CookedConstraint *cooked;

					cooked = (CookedConstraint *) palloc(sizeof(CookedConstraint));
					cooked->contype = CONSTR_CHECK;
					cooked->conoid = InvalidOid;	/* until created */
					cooked->name = pstrdup(name);
					cooked->attnum = 0; /* not used for constraints */
					cooked->expr = expr;
					cooked->skip_validation = false;
					cooked->is_local = false;
					cooked->inhcount = 1;
					cooked->is_no_inherit = false;
					constraints = lappend(constraints, cooked);
				}
			}
		}

		free_attrmap(newattmap);

		/*
		 * Close the parent rel, but keep our lock on it until xact commit.
		 * That will prevent someone else from deleting or ALTERing the parent
		 * before the child is committed.
		 */
		table_close(relation, NoLock);
	}

	/*
	 * If we had no inherited attributes, the result schema is just the
	 * explicitly declared columns.  Otherwise, we need to merge the declared
	 * columns into the inherited schema list.  Although, we never have any
	 * explicitly declared columns if the table is a partition.
	 */
	if (inhSchema != NIL)
	{
		int			schema_attno = 0;

		foreach(entry, schema)
		{
			ColumnDef  *newdef = lfirst(entry);
			char	   *attributeName = newdef->colname;
			int			exist_attno;

			schema_attno++;

			/*
			 * Does it conflict with some previously inherited column?
			 */
			exist_attno = findAttrByName(attributeName, inhSchema);
			if (exist_attno > 0)
			{
				ColumnDef  *def;
				Oid			defTypeId,
							newTypeId;
				int32		deftypmod,
							newtypmod;
				Oid			defcollid,
							newcollid;

				/*
				 * Partitions have only one parent and have no column
				 * definitions of their own, so conflict should never occur.
				 */
				Assert(!is_partition);

				/*
				 * Yes, try to merge the two column definitions. They must
				 * have the same type, typmod, and collation.
				 */
				if (exist_attno == schema_attno)
					ereport(NOTICE,
							(errmsg("merging column \"%s\" with inherited definition",
									attributeName)));
				else
					ereport(NOTICE,
							(errmsg("moving and merging column \"%s\" with inherited definition", attributeName),
							 errdetail("User-specified column moved to the position of the inherited column.")));
				def = (ColumnDef *) list_nth(inhSchema, exist_attno - 1);
				typenameTypeIdAndMod(NULL, def->typeName, &defTypeId, &deftypmod);
				typenameTypeIdAndMod(NULL, newdef->typeName, &newTypeId, &newtypmod);
				if (defTypeId != newTypeId || deftypmod != newtypmod)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("column \"%s\" has a type conflict",
									attributeName),
							 errdetail("%s versus %s",
									   format_type_with_typemod(defTypeId,
																deftypmod),
									   format_type_with_typemod(newTypeId,
																newtypmod))));
				defcollid = GetColumnDefCollation(NULL, def, defTypeId);
				newcollid = GetColumnDefCollation(NULL, newdef, newTypeId);
				if (defcollid != newcollid)
					ereport(ERROR,
							(errcode(ERRCODE_COLLATION_MISMATCH),
							 errmsg("column \"%s\" has a collation conflict",
									attributeName),
							 errdetail("\"%s\" versus \"%s\"",
									   get_collation_name(defcollid),
									   get_collation_name(newcollid))));

				/*
				 * Identity is never inherited.  The new column can have an
				 * identity definition, so we always just take that one.
				 */
				def->identity = newdef->identity;

				/* Copy storage parameter */
				if (def->storage == 0)
					def->storage = newdef->storage;
				else if (newdef->storage != 0 && def->storage != newdef->storage)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("column \"%s\" has a storage parameter conflict",
									attributeName),
							 errdetail("%s versus %s",
									   storage_name(def->storage),
									   storage_name(newdef->storage))));

				/* Copy compression parameter */
				if (def->compression == NULL)
					def->compression = newdef->compression;
				else if (newdef->compression != NULL)
				{
					if (strcmp(def->compression, newdef->compression) != 0)
						ereport(ERROR,
								(errcode(ERRCODE_DATATYPE_MISMATCH),
								 errmsg("column \"%s\" has a compression method conflict",
										attributeName),
								 errdetail("%s versus %s", def->compression, newdef->compression)));
				}

				/* Mark the column as locally defined */
				def->is_local = true;
				/* Merge of NOT NULL constraints = OR 'em together */
				def->is_not_null |= newdef->is_not_null;

				/*
				 * Check for conflicts related to generated columns.
				 *
				 * If the parent column is generated, the child column must be
				 * unadorned and will be made a generated column.  (We could
				 * in theory allow the child column definition specifying the
				 * exact same generation expression, but that's a bit
				 * complicated to implement and doesn't seem very useful.)  We
				 * also check that the child column doesn't specify a default
				 * value or identity, which matches the rules for a single
				 * column in parse_util.c.
				 */
				if (def->generated)
				{
					if (newdef->generated)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_COLUMN_DEFINITION),
								 errmsg("child column \"%s\" specifies generation expression",
										def->colname),
								 errhint("Omit the generation expression in the definition of the child table column to inherit the generation expression from the parent table.")));
					if (newdef->raw_default && !newdef->generated)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_COLUMN_DEFINITION),
								 errmsg("column \"%s\" inherits from generated column but specifies default",
										def->colname)));
					if (newdef->identity)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_COLUMN_DEFINITION),
								 errmsg("column \"%s\" inherits from generated column but specifies identity",
										def->colname)));
				}

				/*
				 * If the parent column is not generated, then take whatever
				 * the child column definition says.
				 */
				else
				{
					if (newdef->generated)
						def->generated = newdef->generated;
				}

				/* If new def has a default, override previous default */
				if (newdef->raw_default != NULL)
				{
					def->raw_default = newdef->raw_default;
					def->cooked_default = newdef->cooked_default;
				}
			}
			else
			{
				/*
				 * No, attach new column to result schema
				 */
				inhSchema = lappend(inhSchema, newdef);
			}
		}

		schema = inhSchema;

		/*
		 * Check that we haven't exceeded the legal # of columns after merging
		 * in inherited columns.
		 */
		if (list_length(schema) > MaxHeapAttributeNumber)
			ereport(ERROR,
					(errcode(ERRCODE_TOO_MANY_COLUMNS),
					 errmsg("tables can have at most %d columns",
							MaxHeapAttributeNumber)));
	}

	/*
	 * Now that we have the column definition list for a partition, we can
	 * check whether the columns referenced in the column constraint specs
	 * actually exist.  Also, we merge NOT NULL and defaults into each
	 * corresponding column definition.
	 */
	if (is_partition)
	{
		foreach(entry, saved_schema)
		{
			ColumnDef  *restdef = lfirst(entry);
			bool		found = false;
			ListCell   *l;

			foreach(l, schema)
			{
				ColumnDef  *coldef = lfirst(l);

				if (strcmp(coldef->colname, restdef->colname) == 0)
				{
					found = true;
					coldef->is_not_null |= restdef->is_not_null;

					/*
					 * Override the parent's default value for this column
					 * (coldef->cooked_default) with the partition's local
					 * definition (restdef->raw_default), if there's one. It
					 * should be physically impossible to get a cooked default
					 * in the local definition or a raw default in the
					 * inherited definition, but make sure they're nulls, for
					 * future-proofing.
					 */
					Assert(restdef->cooked_default == NULL);
					Assert(coldef->raw_default == NULL);
					if (restdef->raw_default)
					{
						coldef->raw_default = restdef->raw_default;
						coldef->cooked_default = NULL;
					}
				}
			}

			/* complain for constraints on columns not in parent */
			if (!found)
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_COLUMN),
						 errmsg("column \"%s\" does not exist",
								restdef->colname)));
		}
	}

	/*
	 * If we found any conflicting parent default values, check to make sure
	 * they were overridden by the child.
	 */
	if (have_bogus_defaults)
	{
		foreach(entry, schema)
		{
			ColumnDef  *def = lfirst(entry);

			if (def->cooked_default == &bogus_marker)
			{
				if (def->generated)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_COLUMN_DEFINITION),
							 errmsg("column \"%s\" inherits conflicting generation expressions",
									def->colname)));
				else
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_COLUMN_DEFINITION),
							 errmsg("column \"%s\" inherits conflicting default values",
									def->colname),
							 errhint("To resolve the conflict, specify a default explicitly.")));
			}
		}
	}

	*supconstr = constraints;
	return schema;
}


/*
 * MergeCheckConstraint
 *		Try to merge an inherited CHECK constraint with previous ones
 *
 * If we inherit identically-named constraints from multiple parents, we must
 * merge them, or throw an error if they don't have identical definitions.
 *
 * constraints is a list of CookedConstraint structs for previous constraints.
 *
 * Returns true if merged (constraint is a duplicate), or false if it's
 * got a so-far-unique name, or throws error if conflict.
 */
static bool
MergeCheckConstraint(List *constraints, char *name, Node *expr)
{
	ListCell   *lc;

	foreach(lc, constraints)
	{
		CookedConstraint *ccon = (CookedConstraint *) lfirst(lc);

		Assert(ccon->contype == CONSTR_CHECK);

		/* Non-matching names never conflict */
		if (strcmp(ccon->name, name) != 0)
			continue;

		if (equal(expr, ccon->expr))
		{
			/* OK to merge */
			ccon->inhcount++;
			return true;
		}

		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("check constraint name \"%s\" appears multiple times but with different expressions",
						name)));
	}

	return false;
}


/*
 * StoreCatalogInheritance
 *		Updates the system catalogs with proper inheritance information.
 *
 * supers is a list of the OIDs of the new relation's direct ancestors.
 */
static void
StoreCatalogInheritance(Oid relationId, List *supers,
						bool child_is_partition)
{
	Relation	relation;
	int32		seqNumber;
	ListCell   *entry;

	/*
	 * sanity checks
	 */
	AssertArg(OidIsValid(relationId));

	if (supers == NIL)
		return;

	/*
	 * Store INHERITS information in pg_inherits using direct ancestors only.
	 * Also enter dependencies on the direct ancestors, and make sure they are
	 * marked with relhassubclass = true.
	 *
	 * (Once upon a time, both direct and indirect ancestors were found here
	 * and then entered into pg_ipl.  Since that catalog doesn't exist
	 * anymore, there's no need to look for indirect ancestors.)
	 */
	relation = table_open(InheritsRelationId, RowExclusiveLock);

	seqNumber = 1;
	foreach(entry, supers)
	{
		Oid			parentOid = lfirst_oid(entry);

		StoreCatalogInheritance1(relationId, parentOid, seqNumber, relation,
								 child_is_partition);
		seqNumber++;
	}

	table_close(relation, RowExclusiveLock);
}

/*
 * Make catalog entries showing relationId as being an inheritance child
 * of parentOid.  inhRelation is the already-opened pg_inherits catalog.
 */
static void
StoreCatalogInheritance1(Oid relationId, Oid parentOid,
						 int32 seqNumber, Relation inhRelation,
						 bool child_is_partition)
{
	ObjectAddress childobject,
				parentobject;

	/* store the pg_inherits row */
	StoreSingleInheritance(relationId, parentOid, seqNumber);

	/*
	 * Store a dependency too
	 */
	parentobject.classId = RelationRelationId;
	parentobject.objectId = parentOid;
	parentobject.objectSubId = 0;
	childobject.classId = RelationRelationId;
	childobject.objectId = relationId;
	childobject.objectSubId = 0;

	recordDependencyOn(&childobject, &parentobject,
					   child_dependency_type(child_is_partition));

	/*
	 * Post creation hook of this inheritance. Since object_access_hook
	 * doesn't take multiple object identifiers, we relay oid of parent
	 * relation using auxiliary_id argument.
	 */
	InvokeObjectPostAlterHookArg(InheritsRelationId,
								 relationId, 0,
								 parentOid, false);

	/*
	 * Mark the parent as having subclasses.
	 */
	SetRelationHasSubclass(parentOid, true);
}

/*
 * Look for an existing schema entry with the given name.
 *
 * Returns the index (starting with 1) if attribute already exists in schema,
 * 0 if it doesn't.
 */
static int
findAttrByName(const char *attributeName, List *schema)
{
	ListCell   *s;
	int			i = 1;

	foreach(s, schema)
	{
		ColumnDef  *def = lfirst(s);

		if (strcmp(attributeName, def->colname) == 0)
			return i;

		i++;
	}
	return 0;
}


/*
 * SetRelationHasSubclass
 *		Set the value of the relation's relhassubclass field in pg_class.
 *
 * NOTE: caller must be holding an appropriate lock on the relation.
 * ShareUpdateExclusiveLock is sufficient.
 *
 * NOTE: an important side-effect of this operation is that an SI invalidation
 * message is sent out to all backends --- including me --- causing plans
 * referencing the relation to be rebuilt with the new list of children.
 * This must happen even if we find that no change is needed in the pg_class
 * row.
 */
void
SetRelationHasSubclass(Oid relationId, bool relhassubclass)
{
	Relation	relationRelation;
	HeapTuple	tuple;
	Form_pg_class classtuple;

	/*
	 * Fetch a modifiable copy of the tuple, modify it, update pg_class.
	 */
	relationRelation = table_open(RelationRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relationId));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relationId);
	classtuple = (Form_pg_class) GETSTRUCT(tuple);

	if (classtuple->relhassubclass != relhassubclass)
	{
		classtuple->relhassubclass = relhassubclass;
		CatalogTupleUpdate(relationRelation, &tuple->t_self, tuple);
	}
	else
	{
		/* no need to change tuple, but force relcache rebuild anyway */
		CacheInvalidateRelcacheByTuple(tuple);
	}

	heap_freetuple(tuple);
	table_close(relationRelation, RowExclusiveLock);
}

/*
 * CheckRelationTableSpaceMove
 *		Check if relation can be moved to new tablespace.
 *
 * NOTE: The caller must hold AccessExclusiveLock on the relation.
 *
 * Returns true if the relation can be moved to the new tablespace; raises
 * an error if it is not possible to do the move; returns false if the move
 * would have no effect.
 */
bool
CheckRelationTableSpaceMove(Relation rel, Oid newTableSpaceId)
{
	Oid			oldTableSpaceId;

	/*
	 * No work if no change in tablespace.  Note that MyDatabaseTableSpace is
	 * stored as 0.
	 */
	oldTableSpaceId = rel->rd_rel->reltablespace;
	if (newTableSpaceId == oldTableSpaceId ||
		(newTableSpaceId == MyDatabaseTableSpace && oldTableSpaceId == 0))
		return false;

	/*
	 * We cannot support moving mapped relations into different tablespaces.
	 * (In particular this eliminates all shared catalogs.)
	 */
	if (RelationIsMapped(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot move system relation \"%s\"",
						RelationGetRelationName(rel))));

	/* Cannot move a non-shared relation into pg_global */
	if (newTableSpaceId == GLOBALTABLESPACE_OID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("only shared relations can be placed in pg_global tablespace")));

	/*
	 * Do not allow moving temp tables of other backends ... their local
	 * buffer manager is not going to cope.
	 */
	if (RELATION_IS_OTHER_TEMP(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot move temporary tables of other sessions")));

	return true;
}

/*
 * SetRelationTableSpace
 *		Set new reltablespace and relfilenode in pg_class entry.
 *
 * newTableSpaceId is the new tablespace for the relation, and
 * newRelFileNode its new filenode.  If newRelFileNode is InvalidOid,
 * this field is not updated.
 *
 * NOTE: The caller must hold AccessExclusiveLock on the relation.
 *
 * The caller of this routine had better check if a relation can be
 * moved to this new tablespace by calling CheckRelationTableSpaceMove()
 * first, and is responsible for making the change visible with
 * CommandCounterIncrement().
 */
void
SetRelationTableSpace(Relation rel,
					  Oid newTableSpaceId,
					  Oid newRelFileNode)
{
	Relation	pg_class;
	HeapTuple	tuple;
	Form_pg_class rd_rel;
	Oid			reloid = RelationGetRelid(rel);

	Assert(CheckRelationTableSpaceMove(rel, newTableSpaceId));

	/* Get a modifiable copy of the relation's pg_class row. */
	pg_class = table_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(reloid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", reloid);
	rd_rel = (Form_pg_class) GETSTRUCT(tuple);

	/* Update the pg_class row. */
	rd_rel->reltablespace = (newTableSpaceId == MyDatabaseTableSpace) ?
		InvalidOid : newTableSpaceId;
	if (OidIsValid(newRelFileNode))
		rd_rel->relfilenode = newRelFileNode;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	/*
	 * Record dependency on tablespace.  This is only required for relations
	 * that have no physical storage.
	 */
	if (!RELKIND_HAS_STORAGE(rel->rd_rel->relkind))
		changeDependencyOnTablespace(RelationRelationId, reloid,
									 rd_rel->reltablespace);

	heap_freetuple(tuple);
	table_close(pg_class, RowExclusiveLock);
}

/*
 *		renameatt_check			- basic sanity checks before attribute rename
 */
static void
renameatt_check(Oid myrelid, Form_pg_class classform, bool recursing)
{
	char		relkind = classform->relkind;

	if (classform->reloftype && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot rename column of typed table")));

	/*
	 * Renaming the columns of sequences or toast tables doesn't actually
	 * break anything from the system's point of view, since internal
	 * references are by attnum.  But it doesn't seem right to allow users to
	 * change names that are hardcoded into the system, hence the following
	 * restriction.
	 */
	if (relkind != RELKIND_RELATION &&
		relkind != RELKIND_VIEW &&
		relkind != RELKIND_MATVIEW &&
		relkind != RELKIND_COMPOSITE_TYPE &&
		relkind != RELKIND_INDEX &&
		relkind != RELKIND_PARTITIONED_INDEX &&
		relkind != RELKIND_FOREIGN_TABLE &&
		relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot rename columns of relation \"%s\"",
						NameStr(classform->relname)),
				 errdetail_relkind_not_supported(relkind)));

	/*
	 * permissions checking.  only the owner of a class can change its schema.
	 */
	if (!pg_class_ownercheck(myrelid, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(myrelid)),
					   NameStr(classform->relname));
	if (!allowSystemTableMods && IsSystemClass(myrelid, classform))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						NameStr(classform->relname))));
}

/*
 *		renameatt_internal		- workhorse for renameatt
 *
 * Return value is the attribute number in the 'myrelid' relation.
 */
static AttrNumber
renameatt_internal(Oid myrelid,
				   const char *oldattname,
				   const char *newattname,
				   bool recurse,
				   bool recursing,
				   int expected_parents,
				   DropBehavior behavior)
{
	Relation	targetrelation;
	Relation	attrelation;
	HeapTuple	atttup;
	Form_pg_attribute attform;
	AttrNumber	attnum;

	/*
	 * Grab an exclusive lock on the target table, which we will NOT release
	 * until end of transaction.
	 */
	targetrelation = relation_open(myrelid, AccessExclusiveLock);
	renameatt_check(myrelid, RelationGetForm(targetrelation), recursing);

	/*
	 * if the 'recurse' flag is set then we are supposed to rename this
	 * attribute in all classes that inherit from 'relname' (as well as in
	 * 'relname').
	 *
	 * any permissions or problems with duplicate attributes will cause the
	 * whole transaction to abort, which is what we want -- all or nothing.
	 */
	if (recurse)
	{
		List	   *child_oids,
				   *child_numparents;
		ListCell   *lo,
				   *li;

		/*
		 * we need the number of parents for each child so that the recursive
		 * calls to renameatt() can determine whether there are any parents
		 * outside the inheritance hierarchy being processed.
		 */
		child_oids = find_all_inheritors(myrelid, AccessExclusiveLock,
										 &child_numparents);

		/*
		 * find_all_inheritors does the recursive search of the inheritance
		 * hierarchy, so all we have to do is process all of the relids in the
		 * list that it returns.
		 */
		forboth(lo, child_oids, li, child_numparents)
		{
			Oid			childrelid = lfirst_oid(lo);
			int			numparents = lfirst_int(li);

			if (childrelid == myrelid)
				continue;
			/* note we need not recurse again */
			renameatt_internal(childrelid, oldattname, newattname, false, true, numparents, behavior);
		}
	}
	else
	{
		/*
		 * If we are told not to recurse, there had better not be any child
		 * tables; else the rename would put them out of step.
		 *
		 * expected_parents will only be 0 if we are not already recursing.
		 */
		if (expected_parents == 0 &&
			find_inheritance_children(myrelid, NoLock) != NIL)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("inherited column \"%s\" must be renamed in child tables too",
							oldattname)));
	}

	/* rename attributes in typed tables of composite type */
	if (targetrelation->rd_rel->relkind == RELKIND_COMPOSITE_TYPE)
	{
		List	   *child_oids;
		ListCell   *lo;

		child_oids = find_typed_table_dependencies(targetrelation->rd_rel->reltype,
												   RelationGetRelationName(targetrelation),
												   behavior);

		foreach(lo, child_oids)
			renameatt_internal(lfirst_oid(lo), oldattname, newattname, true, true, 0, behavior);
	}

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);

	atttup = SearchSysCacheCopyAttName(myrelid, oldattname);
	if (!HeapTupleIsValid(atttup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" does not exist",
						oldattname)));
	attform = (Form_pg_attribute) GETSTRUCT(atttup);

	attnum = attform->attnum;
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot rename system column \"%s\"",
						oldattname)));

	/*
	 * if the attribute is inherited, forbid the renaming.  if this is a
	 * top-level call to renameatt(), then expected_parents will be 0, so the
	 * effect of this code will be to prohibit the renaming if the attribute
	 * is inherited at all.  if this is a recursive call to renameatt(),
	 * expected_parents will be the number of parents the current relation has
	 * within the inheritance hierarchy being processed, so we'll prohibit the
	 * renaming only if there are additional parents from elsewhere.
	 */
	if (attform->attinhcount > expected_parents)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot rename inherited column \"%s\"",
						oldattname)));

	/* new name should not already exist */
	(void) check_for_column_name_collision(targetrelation, newattname, false);

	/* apply the update */
	namestrcpy(&(attform->attname), newattname);

	CatalogTupleUpdate(attrelation, &atttup->t_self, atttup);

	InvokeObjectPostAlterHook(RelationRelationId, myrelid, attnum);

	heap_freetuple(atttup);

	table_close(attrelation, RowExclusiveLock);

	relation_close(targetrelation, NoLock); /* close rel but keep lock */

	return attnum;
}

/*
 * Perform permissions and integrity checks before acquiring a relation lock.
 */
static void
RangeVarCallbackForRenameAttribute(const RangeVar *rv, Oid relid, Oid oldrelid,
								   void *arg)
{
	HeapTuple	tuple;
	Form_pg_class form;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tuple))
		return;					/* concurrently dropped */
	form = (Form_pg_class) GETSTRUCT(tuple);
	renameatt_check(relid, form, false);
	ReleaseSysCache(tuple);
}

/*
 *		renameatt		- changes the name of an attribute in a relation
 *
 * The returned ObjectAddress is that of the renamed column.
 */
ObjectAddress
renameatt(RenameStmt *stmt)
{
	Oid			relid;
	AttrNumber	attnum;
	ObjectAddress address;

	/* lock level taken here should match renameatt_internal */
	relid = RangeVarGetRelidExtended(stmt->relation, AccessExclusiveLock,
									 stmt->missing_ok ? RVR_MISSING_OK : 0,
									 RangeVarCallbackForRenameAttribute,
									 NULL);

	if (!OidIsValid(relid))
	{
		ereport(NOTICE,
				(errmsg("relation \"%s\" does not exist, skipping",
						stmt->relation->relname)));
		return InvalidObjectAddress;
	}

	attnum =
		renameatt_internal(relid,
						   stmt->subname,	/* old att name */
						   stmt->newname,	/* new att name */
						   stmt->relation->inh, /* recursive? */
						   false,	/* recursing? */
						   0,	/* expected inhcount */
						   stmt->behavior);

	if (IsYugaByteEnabled())
	{
		YBCRename(stmt, relid);
		if (stmt->renameType == OBJECT_COLUMN)
		{
			ListCell *child;
			List *children = find_all_inheritors(relid, NoLock, NULL);
			foreach(child, children)
			{
				Oid childrelid = lfirst_oid(child);
				if (childrelid != relid)
					YBCRename(stmt, childrelid);
			}
		}
	}

	ObjectAddressSubSet(address, RelationRelationId, relid, attnum);

	return address;
}

/*
 * same logic as renameatt_internal
 */
static ObjectAddress
rename_constraint_internal(Oid myrelid,
						   Oid mytypid,
						   const char *oldconname,
						   const char *newconname,
						   bool recurse,
						   bool recursing,
						   int expected_parents)
{
	Relation	targetrelation = NULL;
	Oid			constraintOid;
	HeapTuple	tuple;
	Form_pg_constraint con;
	ObjectAddress address;

	AssertArg(!myrelid || !mytypid);

	if (mytypid)
	{
		constraintOid = get_domain_constraint_oid(mytypid, oldconname, false);
	}
	else
	{
		targetrelation = relation_open(myrelid, AccessExclusiveLock);

		/*
		 * don't tell it whether we're recursing; we allow changing typed
		 * tables here
		 */
		renameatt_check(myrelid, RelationGetForm(targetrelation), false);

		constraintOid = get_relation_constraint_oid(myrelid, oldconname, false);
	}

	tuple = SearchSysCache1(CONSTROID, ObjectIdGetDatum(constraintOid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for constraint %u",
			 constraintOid);
	con = (Form_pg_constraint) GETSTRUCT(tuple);

	if (myrelid && con->contype == CONSTRAINT_CHECK && !con->connoinherit)
	{
		if (recurse)
		{
			List	   *child_oids,
					   *child_numparents;
			ListCell   *lo,
					   *li;

			child_oids = find_all_inheritors(myrelid, AccessExclusiveLock,
											 &child_numparents);

			forboth(lo, child_oids, li, child_numparents)
			{
				Oid			childrelid = lfirst_oid(lo);
				int			numparents = lfirst_int(li);

				if (childrelid == myrelid)
					continue;

				rename_constraint_internal(childrelid, InvalidOid, oldconname, newconname, false, true, numparents);
			}
		}
		else
		{
			if (expected_parents == 0 &&
				find_inheritance_children(myrelid, NoLock) != NIL)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("inherited constraint \"%s\" must be renamed in child tables too",
								oldconname)));
		}

		if (con->coninhcount > expected_parents)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot rename inherited constraint \"%s\"",
							oldconname)));
	}

	if (con->conindid
		&& (con->contype == CONSTRAINT_PRIMARY
			|| con->contype == CONSTRAINT_UNIQUE
			|| con->contype == CONSTRAINT_EXCLUSION))
		/* rename the index; this renames the constraint as well */
		RenameRelationInternal(con->conindid, newconname, false, true);
	else
		RenameConstraintById(constraintOid, newconname);

	ObjectAddressSet(address, ConstraintRelationId, constraintOid);

	ReleaseSysCache(tuple);

	if (targetrelation)
	{
		/*
		 * Invalidate relcache so as others can see the new constraint name.
		 */
		CacheInvalidateRelcache(targetrelation);

		relation_close(targetrelation, NoLock); /* close rel but keep lock */
	}

	return address;
}

ObjectAddress
RenameConstraint(RenameStmt *stmt)
{
	Oid			relid = InvalidOid;
	Oid			typid = InvalidOid;

	if (stmt->renameType == OBJECT_DOMCONSTRAINT)
	{
		Relation	rel;
		HeapTuple	tup;

		typid = typenameTypeId(NULL, makeTypeNameFromNameList(castNode(List, stmt->object)));
		rel = table_open(TypeRelationId, RowExclusiveLock);
		tup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(tup))
			elog(ERROR, "cache lookup failed for type %u", typid);
		checkDomainOwner(tup);
		ReleaseSysCache(tup);
		table_close(rel, NoLock);
	}
	else
	{
		/* lock level taken here should match rename_constraint_internal */
		relid = RangeVarGetRelidExtended(stmt->relation, AccessExclusiveLock,
										 stmt->missing_ok ? RVR_MISSING_OK : 0,
										 RangeVarCallbackForRenameAttribute,
										 NULL);
		if (!OidIsValid(relid))
		{
			ereport(NOTICE,
					(errmsg("relation \"%s\" does not exist, skipping",
							stmt->relation->relname)));
			return InvalidObjectAddress;
		}
	}

	return
		rename_constraint_internal(relid, typid,
								   stmt->subname,
								   stmt->newname,
								   (stmt->relation &&
									stmt->relation->inh),	/* recursive? */
								   false,	/* recursing? */
								   0 /* expected inhcount */ );
}

/*
 * Execute ALTER TABLE/INDEX/SEQUENCE/VIEW/MATERIALIZED VIEW/FOREIGN TABLE
 * RENAME
 * When yb_is_internal_clone_rename is true we don't need to do a YB rename,
 * as this rename is a part of a table clone operation, and the relation
 * will be dropped after the clone operation is done anyway.
 */
ObjectAddress
RenameRelation(RenameStmt *stmt, bool yb_is_internal_clone_rename)
{
	bool		is_index_stmt = stmt->renameType == OBJECT_INDEX;
	Oid			relid;
	ObjectAddress address;
	Relation      rel;
	bool          needs_yb_rename;

	/*
	 * Grab an exclusive lock on the target table, index, sequence, view,
	 * materialized view, or foreign table, which we will NOT release until
	 * end of transaction.
	 *
	 * Lock level used here should match RenameRelationInternal, to avoid lock
	 * escalation.  However, because ALTER INDEX can be used with any relation
	 * type, we mustn't believe without verification.
	 */
	for (;;)
	{
		LOCKMODE	lockmode;
		char		relkind;
		bool		obj_is_index;

		lockmode = is_index_stmt ? ShareUpdateExclusiveLock : AccessExclusiveLock;

		relid = RangeVarGetRelidExtended(stmt->relation, lockmode,
										 stmt->missing_ok ? RVR_MISSING_OK : 0,
										 RangeVarCallbackForAlterRelation,
										 (void *) stmt);

		if (!OidIsValid(relid))
		{
			ereport(NOTICE,
					(errmsg("relation \"%s\" does not exist, skipping",
							stmt->relation->relname)));
			return InvalidObjectAddress;
		}

		/*
		 * We allow mismatched statement and object types (e.g., ALTER INDEX
		 * to rename a table), but we might've used the wrong lock level.  If
		 * that happens, retry with the correct lock level.  We don't bother
		 * if we already acquired AccessExclusiveLock with an index, however.
		 */
		relkind = get_rel_relkind(relid);
		obj_is_index = (relkind == RELKIND_INDEX ||
						relkind == RELKIND_PARTITIONED_INDEX);
		if (obj_is_index || is_index_stmt == obj_is_index)
			break;

		UnlockRelationOid(relid, lockmode);
		is_index_stmt = obj_is_index;
	}

	/* Do the work */
	RenameRelationInternal(relid, stmt->newname, false, is_index_stmt);

	/* YB rename is not needed for a primary key dummy index. */
	rel             = RelationIdGetRelation(relid);
	needs_yb_rename = IsYBRelation(rel) &&
					  !(rel->rd_rel->relkind == RELKIND_INDEX &&
						rel->rd_index->indisprimary) &&
					  rel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX
					  && !yb_is_internal_clone_rename;
	RelationClose(rel);

	/* Do the work */
	if (needs_yb_rename)
	{
		YBCRename(stmt, relid);
	}

	ObjectAddressSet(address, RelationRelationId, relid);

	return address;
}

/*
 *		RenameRelationInternal - change the name of a relation
 */
void
RenameRelationInternal(Oid myrelid, const char *newrelname, bool is_internal, bool is_index)
{
	Relation	targetrelation;
	Relation	relrelation;	/* for RELATION relation */
	HeapTuple	reltup;
	Form_pg_class relform;
	Oid			namespaceId;

	/*
	 * Grab a lock on the target relation, which we will NOT release until end
	 * of transaction.  We need at least a self-exclusive lock so that
	 * concurrent DDL doesn't overwrite the rename if they start updating
	 * while still seeing the old version.  The lock also guards against
	 * triggering relcache reloads in concurrent sessions, which might not
	 * handle this information changing under them.  For indexes, we can use a
	 * reduced lock level because RelationReloadIndexInfo() handles indexes
	 * specially.
	 */
	targetrelation = relation_open(myrelid, is_index ? ShareUpdateExclusiveLock : AccessExclusiveLock);
	namespaceId = RelationGetNamespace(targetrelation);

	/*
	 * Find relation's pg_class tuple, and make sure newrelname isn't in use.
	 */
	relrelation = table_open(RelationRelationId, RowExclusiveLock);

	reltup = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(myrelid));
	if (!HeapTupleIsValid(reltup))	/* shouldn't happen */
		elog(ERROR, "cache lookup failed for relation %u", myrelid);
	relform = (Form_pg_class) GETSTRUCT(reltup);

	if (get_relname_relid(newrelname, namespaceId) != InvalidOid)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_TABLE),
				 errmsg("relation \"%s\" already exists",
						newrelname)));

	/*
	 * RenameRelation is careful not to believe the caller's idea of the
	 * relation kind being handled.  We don't have to worry about this, but
	 * let's not be totally oblivious to it.  We can process an index as
	 * not-an-index, but not the other way around.
	 */
	Assert(!is_index ||
		   is_index == (targetrelation->rd_rel->relkind == RELKIND_INDEX ||
						targetrelation->rd_rel->relkind == RELKIND_PARTITIONED_INDEX));

	/*
	 * Update pg_class tuple with new relname.  (Scribbling on reltup is OK
	 * because it's a copy...)
	 */
	namestrcpy(&(relform->relname), newrelname);

	CatalogTupleUpdate(relrelation, &reltup->t_self, reltup);

	InvokeObjectPostAlterHookArg(RelationRelationId, myrelid, 0,
								 InvalidOid, is_internal);

	heap_freetuple(reltup);
	table_close(relrelation, RowExclusiveLock);

	/*
	 * Also rename the associated type, if any.
	 */
	if (OidIsValid(targetrelation->rd_rel->reltype))
		RenameTypeInternal(targetrelation->rd_rel->reltype,
						   newrelname, namespaceId);

	/*
	 * Also rename the associated constraint, if any.
	 */
	if (targetrelation->rd_rel->relkind == RELKIND_INDEX ||
		targetrelation->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
	{
		Oid			constraintId = get_index_constraint(myrelid);

		if (OidIsValid(constraintId))
			RenameConstraintById(constraintId, newrelname);
	}

	/*
	 * Close rel, but keep lock!
	 */
	relation_close(targetrelation, NoLock);
}

/*
 *		ResetRelRewrite - reset relrewrite
 */
void
ResetRelRewrite(Oid myrelid)
{
	Relation	relrelation;	/* for RELATION relation */
	HeapTuple	reltup;
	Form_pg_class relform;

	/*
	 * Find relation's pg_class tuple.
	 */
	relrelation = table_open(RelationRelationId, RowExclusiveLock);

	reltup = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(myrelid));
	if (!HeapTupleIsValid(reltup))	/* shouldn't happen */
		elog(ERROR, "cache lookup failed for relation %u", myrelid);
	relform = (Form_pg_class) GETSTRUCT(reltup);

	/*
	 * Update pg_class tuple.
	 */
	relform->relrewrite = InvalidOid;

	CatalogTupleUpdate(relrelation, &reltup->t_self, reltup);

	heap_freetuple(reltup);
	table_close(relrelation, RowExclusiveLock);
}

/*
 * Disallow ALTER TABLE (and similar commands) when the current backend has
 * any open reference to the target table besides the one just acquired by
 * the calling command; this implies there's an open cursor or active plan.
 * We need this check because our lock doesn't protect us against stomping
 * on our own foot, only other people's feet!
 *
 * For ALTER TABLE, the only case known to cause serious trouble is ALTER
 * COLUMN TYPE, and some changes are obviously pretty benign, so this could
 * possibly be relaxed to only error out for certain types of alterations.
 * But the use-case for allowing any of these things is not obvious, so we
 * won't work hard at it for now.
 *
 * We also reject these commands if there are any pending AFTER trigger events
 * for the rel.  This is certainly necessary for the rewriting variants of
 * ALTER TABLE, because they don't preserve tuple TIDs and so the pending
 * events would try to fetch the wrong tuples.  It might be overly cautious
 * in other cases, but again it seems better to err on the side of paranoia.
 *
 * REINDEX calls this with "rel" referencing the index to be rebuilt; here
 * we are worried about active indexscans on the index.  The trigger-event
 * check can be skipped, since we are doing no damage to the parent table.
 *
 * The statement name (eg, "ALTER TABLE") is passed for use in error messages.
 */
void
CheckTableNotInUse(Relation rel, const char *stmt)
{
	int			expected_refcnt;

	expected_refcnt = rel->rd_isnailed ? 2 : 1;
	if (rel->rd_refcnt != expected_refcnt)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_IN_USE),
		/* translator: first %s is a SQL command, eg ALTER TABLE */
				 errmsg("cannot %s \"%s\" because it is being used by active queries in this session",
						stmt, RelationGetRelationName(rel))));

	if (rel->rd_rel->relkind != RELKIND_INDEX &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX &&
		AfterTriggerPendingOnRel(RelationGetRelid(rel)))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_IN_USE),
		/* translator: first %s is a SQL command, eg ALTER TABLE */
				 errmsg("cannot %s \"%s\" because it has pending trigger events",
						stmt, RelationGetRelationName(rel))));
}

/*
 * AlterTableLookupRelation
 *		Look up, and lock, the OID for the relation named by an alter table
 *		statement.
 */
Oid
AlterTableLookupRelation(AlterTableStmt *stmt, LOCKMODE lockmode)
{
	return RangeVarGetRelidExtended(stmt->relation, lockmode,
									stmt->missing_ok ? RVR_MISSING_OK : 0,
									RangeVarCallbackForAlterRelation,
									(void *) stmt);
}

/*
 * AlterTable
 *		Execute ALTER TABLE, which can be a list of subcommands
 *
 * ALTER TABLE is performed in three phases:
 *		1. Examine subcommands and perform pre-transformation checking.
 *		2. Validate and transform subcommands, and update system catalogs.
 *		3. Scan table(s) to check new constraints, and optionally recopy
 *		   the data into new table(s).
 * Phase 3 is not performed unless one or more of the subcommands requires
 * it.  The intention of this design is to allow multiple independent
 * updates of the table schema to be performed with only one pass over the
 * data.
 *
 * ATPrepCmd performs phase 1.  A "work queue" entry is created for
 * each table to be affected (there may be multiple affected tables if the
 * commands traverse a table inheritance hierarchy).  Also we do preliminary
 * validation of the subcommands.  Because earlier subcommands may change
 * the catalog state seen by later commands, there are limits to what can
 * be done in this phase.  Generally, this phase acquires table locks,
 * checks permissions and relkind, and recurses to find child tables.
 *
 * ATRewriteCatalogs performs phase 2 for each affected table.
 * Certain subcommands need to be performed before others to avoid
 * unnecessary conflicts; for example, DROP COLUMN should come before
 * ADD COLUMN.  Therefore phase 1 divides the subcommands into multiple
 * lists, one for each logical "pass" of phase 2.
 *
 * ATRewriteTables performs phase 3 for those tables that need it.
 *
 * For most subcommand types, phases 2 and 3 do no explicit recursion,
 * since phase 1 already does it.  However, for certain subcommand types
 * it is only possible to determine how to recurse at phase 2 time; for
 * those cases, phase 1 sets the cmd->recurse flag (or, in some older coding,
 * changes the command subtype of a "Recurse" variant XXX to be cleaned up.)
 *
 * Thanks to the magic of MVCC, an error anywhere along the way rolls back
 * the whole operation; we don't have to do anything special to clean up.
 *
 * The caller must lock the relation, with an appropriate lock level
 * for the subcommands requested, using AlterTableGetLockLevel(stmt->cmds)
 * or higher. We pass the lock level down
 * so that we can apply it recursively to inherited tables. Note that the
 * lock level we want as we recurse might well be higher than required for
 * that specific subcommand. So we pass down the overall lock requirement,
 * rather than reassess it at lower levels.
 *
 * The caller also provides a "context" which is to be passed back to
 * utility.c when we need to execute a subcommand such as CREATE INDEX.
 * Some of the fields therein, such as the relid, are used here as well.
 */
void
AlterTable(AlterTableStmt *stmt, LOCKMODE lockmode,
		   AlterTableUtilityContext *context)
{
	Relation	rel;

	/* Caller is required to provide an adequate lock. */
	rel = relation_open(context->relid, NoLock);

	CheckTableNotInUse(rel, "ALTER TABLE");

	if (IsYugaByteEnabled() && stmt->relation->relpersistence ==
										  RELPERSISTENCE_TEMP)
		YBCForceAllowCatalogModifications(true);

	ATController(stmt, rel, stmt->cmds, stmt->relation->inh, lockmode, context);
}

/*
 * AlterTableInternal
 *
 * ALTER TABLE with target specified by OID
 *
 * We do not reject if the relation is already open, because it's quite
 * likely that one or more layers of caller have it open.  That means it
 * is unsafe to use this entry point for alterations that could break
 * existing query plans.  On the assumption it's not used for such, we
 * don't have to reject pending AFTER triggers, either.
 *
 * Also, since we don't have an AlterTableUtilityContext, this cannot be
 * used for any subcommand types that require parse transformation or
 * could generate subcommands that have to be passed to ProcessUtility.
 */
void
AlterTableInternal(Oid relid, List *cmds, bool recurse)
{
	Relation	rel;
	LOCKMODE	lockmode = AlterTableGetLockLevel(cmds);

	rel = relation_open(relid, lockmode);

	EventTriggerAlterTableRelid(relid);

	ATController(NULL, rel, cmds, recurse, lockmode, NULL);
}

/*
 * AlterTableGetLockLevel
 *
 * Sets the overall lock level required for the supplied list of subcommands.
 * Policy for doing this set according to needs of AlterTable(), see
 * comments there for overall explanation.
 *
 * Function is called before and after parsing, so it must give same
 * answer each time it is called. Some subcommands are transformed
 * into other subcommand types, so the transform must never be made to a
 * lower lock level than previously assigned. All transforms are noted below.
 *
 * Since this is called before we lock the table we cannot use table metadata
 * to influence the type of lock we acquire.
 *
 * There should be no lockmodes hardcoded into the subcommand functions. All
 * lockmode decisions for ALTER TABLE are made here only. The one exception is
 * ALTER TABLE RENAME which is treated as a different statement type T_RenameStmt
 * and does not travel through this section of code and cannot be combined with
 * any of the subcommands given here.
 *
 * Note that Hot Standby only knows about AccessExclusiveLocks on the primary
 * so any changes that might affect SELECTs running on standbys need to use
 * AccessExclusiveLocks even if you think a lesser lock would do, unless you
 * have a solution for that also.
 *
 * Also note that pg_dump uses only an AccessShareLock, meaning that anything
 * that takes a lock less than AccessExclusiveLock can change object definitions
 * while pg_dump is running. Be careful to check that the appropriate data is
 * derived by pg_dump using an MVCC snapshot, rather than syscache lookups,
 * otherwise we might end up with an inconsistent dump that can't restore.
 */
LOCKMODE
AlterTableGetLockLevel(List *cmds)
{
	/*
	 * This only works if we read catalog tables using MVCC snapshots.
	 */
	ListCell   *lcmd;
	LOCKMODE	lockmode = ShareUpdateExclusiveLock;

	foreach(lcmd, cmds)
	{
		AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lcmd);
		LOCKMODE	cmd_lockmode = AccessExclusiveLock; /* default for compiler */

		switch (cmd->subtype)
		{
				/*
				 * These subcommands rewrite the heap, so require full locks.
				 */
			case AT_AddColumn:	/* may rewrite heap, in some cases and visible
								 * to SELECT */
			case AT_SetAccessMethod:	/* must rewrite heap */
			case AT_SetTableSpace:	/* must rewrite heap */
			case AT_AlterColumnType:	/* must rewrite heap */
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * These subcommands may require addition of toast tables. If
				 * we add a toast table to a table currently being scanned, we
				 * might miss data added to the new toast table by concurrent
				 * insert transactions.
				 */
			case AT_SetStorage: /* may add toast tables, see
								 * ATRewriteCatalogs() */
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * Removing constraints can affect SELECTs that have been
				 * optimized assuming the constraint holds true. See also
				 * CloneFkReferenced.
				 */
			case AT_DropConstraint: /* as DROP INDEX */
			case AT_DropNotNull:	/* may change some SQL plans */
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * Subcommands that may be visible to concurrent SELECTs
				 */
			case AT_DropColumn: /* change visible to SELECT */
			case AT_AddColumnToView:	/* CREATE VIEW */
			case AT_DropOids:	/* used to equiv to DropColumn */
			case AT_EnableAlwaysRule:	/* may change SELECT rules */
			case AT_EnableReplicaRule:	/* may change SELECT rules */
			case AT_EnableRule: /* may change SELECT rules */
			case AT_DisableRule:	/* may change SELECT rules */
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * Changing owner may remove implicit SELECT privileges
				 */
			case AT_ChangeOwner:	/* change visible to SELECT */
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * Changing foreign table options may affect optimization.
				 */
			case AT_GenericOptions:
			case AT_AlterColumnGenericOptions:
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * These subcommands affect write operations only.
				 */
			case AT_EnableTrig:
			case AT_EnableAlwaysTrig:
			case AT_EnableReplicaTrig:
			case AT_EnableTrigAll:
			case AT_EnableTrigUser:
			case AT_DisableTrig:
			case AT_DisableTrigAll:
			case AT_DisableTrigUser:
				cmd_lockmode = ShareRowExclusiveLock;
				break;

				/*
				 * These subcommands affect write operations only. XXX
				 * Theoretically, these could be ShareRowExclusiveLock.
				 */
			case AT_ColumnDefault:
			case AT_CookedColumnDefault:
			case AT_AlterConstraint:
			case AT_AddIndex:	/* from ADD CONSTRAINT */
			case AT_AddIndexConstraint:
			case AT_ReplicaIdentity:
			case AT_SetNotNull:
			case AT_EnableRowSecurity:
			case AT_DisableRowSecurity:
			case AT_ForceRowSecurity:
			case AT_NoForceRowSecurity:
			case AT_AddIdentity:
			case AT_DropIdentity:
			case AT_SetIdentity:
			case AT_DropExpression:
			case AT_SetCompression:
				cmd_lockmode = AccessExclusiveLock;
				break;

			case AT_AddConstraint:
			case AT_AddConstraintRecurse:	/* becomes AT_AddConstraint */
			case AT_ReAddConstraint:	/* becomes AT_AddConstraint */
			case AT_ReAddDomainConstraint:	/* becomes AT_AddConstraint */
				if (IsA(cmd->def, Constraint))
				{
					Constraint *con = (Constraint *) cmd->def;

					switch (con->contype)
					{
						case CONSTR_EXCLUSION:
						case CONSTR_PRIMARY:
						case CONSTR_UNIQUE:

							/*
							 * Cases essentially the same as CREATE INDEX. We
							 * could reduce the lock strength to ShareLock if
							 * we can work out how to allow concurrent catalog
							 * updates. XXX Might be set down to
							 * ShareRowExclusiveLock but requires further
							 * analysis.
							 */
							cmd_lockmode = AccessExclusiveLock;
							break;
						case CONSTR_FOREIGN:

							/*
							 * We add triggers to both tables when we add a
							 * Foreign Key, so the lock level must be at least
							 * as strong as CREATE TRIGGER.
							 */
							cmd_lockmode = ShareRowExclusiveLock;
							break;

						default:
							cmd_lockmode = AccessExclusiveLock;
					}
				}
				break;

				/*
				 * These subcommands affect inheritance behaviour. Queries
				 * started before us will continue to see the old inheritance
				 * behaviour, while queries started after we commit will see
				 * new behaviour. No need to prevent reads or writes to the
				 * subtable while we hook it up though. Changing the TupDesc
				 * may be a problem, so keep highest lock.
				 */
			case AT_AddInherit:
			case AT_DropInherit:
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * These subcommands affect implicit row type conversion. They
				 * have affects similar to CREATE/DROP CAST on queries. don't
				 * provide for invalidating parse trees as a result of such
				 * changes, so we keep these at AccessExclusiveLock.
				 */
			case AT_AddOf:
			case AT_DropOf:
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * Only used by CREATE OR REPLACE VIEW which must conflict
				 * with an SELECTs currently using the view.
				 */
			case AT_ReplaceRelOptions:
				cmd_lockmode = AccessExclusiveLock;
				break;

				/*
				 * These subcommands affect general strategies for performance
				 * and maintenance, though don't change the semantic results
				 * from normal data reads and writes. Delaying an ALTER TABLE
				 * behind currently active writes only delays the point where
				 * the new strategy begins to take effect, so there is no
				 * benefit in waiting. In this case the minimum restriction
				 * applies: we don't currently allow concurrent catalog
				 * updates.
				 */
			case AT_SetStatistics:	/* Uses MVCC in getTableAttrs() */
			case AT_ClusterOn:	/* Uses MVCC in getIndexes() */
			case AT_DropCluster:	/* Uses MVCC in getIndexes() */
			case AT_SetOptions: /* Uses MVCC in getTableAttrs() */
			case AT_ResetOptions:	/* Uses MVCC in getTableAttrs() */
				cmd_lockmode = ShareUpdateExclusiveLock;
				break;

			case AT_SetLogged:
			case AT_SetUnLogged:
				cmd_lockmode = AccessExclusiveLock;
				break;

			case AT_ValidateConstraint: /* Uses MVCC in getConstraints() */
				cmd_lockmode = ShareUpdateExclusiveLock;
				break;

				/*
				 * Rel options are more complex than first appears. Options
				 * are set here for tables, views and indexes; for historical
				 * reasons these can all be used with ALTER TABLE, so we can't
				 * decide between them using the basic grammar.
				 */
			case AT_SetRelOptions:	/* Uses MVCC in getIndexes() and
									 * getTables() */
			case AT_ResetRelOptions:	/* Uses MVCC in getIndexes() and
										 * getTables() */
				cmd_lockmode = AlterTableGetRelOptionsLockLevel((List *) cmd->def);
				break;

			case AT_AttachPartition:
				cmd_lockmode = ShareUpdateExclusiveLock;
				break;

			case AT_DetachPartition:
				if (((PartitionCmd *) cmd->def)->concurrent)
					cmd_lockmode = ShareUpdateExclusiveLock;
				else
					cmd_lockmode = AccessExclusiveLock;
				break;

			case AT_DetachPartitionFinalize:
				cmd_lockmode = ShareUpdateExclusiveLock;
				break;

			case AT_CheckNotNull:

				/*
				 * This only examines the table's schema; but lock must be
				 * strong enough to prevent concurrent DROP NOT NULL.
				 */
				cmd_lockmode = AccessShareLock;
				break;

			default:			/* oops */
				elog(ERROR, "unrecognized alter table type: %d",
					 (int) cmd->subtype);
				break;
		}

		/*
		 * Take the greatest lockmode from any subcommand
		 */
		if (cmd_lockmode > lockmode)
			lockmode = cmd_lockmode;
	}

	return lockmode;
}

/*
 * ATController provides top level control over the phases.
 *
 * parsetree is passed in to allow it to be passed to event triggers
 * when requested.
 */
static void
ATController(AlterTableStmt *parsetree,
			 Relation rel, List *cmds, bool recurse, LOCKMODE lockmode,
			 AlterTableUtilityContext *context)
{
	List	   *wqueue = NIL;
	ListCell   *lcmd;

	/* Phase 1: preliminary examination of commands, create work queue */
	foreach(lcmd, cmds)
	{
		AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lcmd);

		ATPrepCmd(&wqueue, rel, cmd, recurse, false, lockmode, context);
	}

	/* Close the relation, but keep lock until commit */
	relation_close(rel, NoLock);

	/* Phase 2: update system catalogs */
	List *rollbackHandles = NIL;
	List *volatile ybAlteredTableIds = NIL;
	PG_TRY();
	{
		/*
		 * ATRewriteCatalogs also executes changes to DocDB.
		 * If Phase 3 fails, rollbackHandle will specify how to rollback the
		 * changes done to DocDB.
		 */
		ATRewriteCatalogs(&wqueue, lockmode, context, &rollbackHandles, &ybAlteredTableIds);
	}
	PG_CATCH();
	{
		YbATInvalidateTableCacheAfterAlter(ybAlteredTableIds);
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Phase 3: scan/rewrite tables as needed, and run afterStmts */
	PG_TRY();
	{
		ATRewriteTables(parsetree, &wqueue, lockmode, context);
		YbATInvalidateTableCacheAfterAlter(ybAlteredTableIds);
	}
	PG_CATCH();
	{
		if (!YbDdlRollbackEnabled())
		{
			/*
			 * The new way of doing ddl rollback is disabled, fall back to the
			 * old way of doing best-effort rollback which may not always
			 * succeed (e.g., in case of network failure or PG crash).
			 */
			ListCell *lc = NULL;
			foreach(lc, rollbackHandles)
			{
				YBCPgStatement handle = (YBCPgStatement) lfirst(lc);
				YBCExecAlterTable(handle, RelationGetRelid(rel));
			}
		}
		YbATInvalidateTableCacheAfterAlter(ybAlteredTableIds);
		PG_RE_THROW();
	}
	PG_END_TRY();
}

/*
 * ATPrepCmd
 *
 * Traffic cop for ALTER TABLE Phase 1 operations, including simple
 * recursion and permission checks.
 *
 * Caller must have acquired appropriate lock type on relation already.
 * This lock should be held until commit.
 */
static void
ATPrepCmd(List **wqueue, Relation rel, AlterTableCmd *cmd,
		  bool recurse, bool recursing, LOCKMODE lockmode,
		  AlterTableUtilityContext *context)
{
	AlteredTableInfo *tab;
	int			pass = AT_PASS_UNSET;

	/* Find or create work queue entry for this table */
	tab = ATGetQueueEntry(wqueue, rel);

	/*
	 * Setting the flag for ADD PRIMARY KEY use case
	 * at the index adding phase prior to copying the command.
	 */
	if (cmd->subtype == AT_AddIndex && IsA(cmd->def, IndexStmt))
	{
		IndexStmt *index = (IndexStmt *) cmd->def;
		if (index->primary)
			cmd->yb_is_add_primary_key = true;
	}

	/*
	 * Disallow any ALTER TABLE other than ALTER TABLE DETACH FINALIZE on
	 * partitions that are pending detach.
	 */
	if (rel->rd_rel->relispartition &&
		cmd->subtype != AT_DetachPartitionFinalize &&
		PartitionHasPendingDetach(RelationGetRelid(rel)))
		ereport(ERROR,
				errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				errmsg("cannot alter partition \"%s\" with an incomplete detach",
					   RelationGetRelationName(rel)),
				errhint("Use ALTER TABLE ... DETACH PARTITION ... FINALIZE to complete the pending detach operation."));

	/*
	 * Copy the original subcommand for each table, so we can scribble on it.
	 * This avoids conflicts when different child tables need to make
	 * different parse transformations (for example, the same column may have
	 * different column numbers in different children).
	 */
	cmd = copyObject(cmd);

	/*
	 * Do permissions and relkind checking, recursion to child tables if
	 * needed, and any additional phase-1 processing needed.  (But beware of
	 * adding any processing that looks at table details that another
	 * subcommand could change.  In some cases we reject multiple subcommands
	 * that could try to change the same state in contrary ways.)
	 */
	switch (cmd->subtype)
	{
		case AT_AddColumn:		/* ADD COLUMN */
			ATSimplePermissions(cmd->subtype, rel,
								ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE);
			ATPrepAddColumn(wqueue, rel, recurse, recursing, false, cmd,
							lockmode, context);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_ADD_COL;
			break;
		case AT_AddColumnToView:	/* add column via CREATE OR REPLACE VIEW */
			ATSimplePermissions(cmd->subtype, rel, ATT_VIEW);
			ATPrepAddColumn(wqueue, rel, recurse, recursing, true, cmd,
							lockmode, context);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_ADD_COL;
			break;
		case AT_ColumnDefault:	/* ALTER COLUMN DEFAULT */

			/*
			 * We allow defaults on views so that INSERT into a view can have
			 * default-ish behavior.  This works because the rewriter
			 * substitutes default values into INSERTs before it expands
			 * rules.
			 */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
			/* No command-specific prep needed */
			pass = cmd->def ? AT_PASS_ADD_OTHERCONSTR : AT_PASS_DROP;
			break;
		case AT_CookedColumnDefault:	/* add a pre-cooked default */
			/* This is currently used only in CREATE TABLE */
			/* (so the permission check really isn't necessary) */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			pass = AT_PASS_ADD_OTHERCONSTR;
			break;
		case AT_AddIdentity:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			pass = AT_PASS_ADD_OTHERCONSTR;
			break;
		case AT_SetIdentity:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			/* This should run after AddIdentity, so do it in MISC pass */
			pass = AT_PASS_MISC;
			break;
		case AT_DropIdentity:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			pass = AT_PASS_DROP;
			break;
		case AT_DropNotNull:	/* ALTER COLUMN DROP NOT NULL */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			ATPrepDropNotNull(rel, recurse, recursing);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
			pass = AT_PASS_DROP;
			break;
		case AT_SetNotNull:		/* ALTER COLUMN SET NOT NULL */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Need command-specific recursion decision */
			ATPrepSetNotNull(wqueue, rel, cmd, recurse, recursing,
							 lockmode, context);
			pass = AT_PASS_COL_ATTRS;
			break;
		case AT_CheckNotNull:	/* check column is already marked NOT NULL */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
			/* No command-specific prep needed */
			pass = AT_PASS_COL_ATTRS;
			break;
		case AT_DropExpression: /* ALTER COLUMN DROP EXPRESSION */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
			ATPrepDropExpression(rel, cmd, recurse, recursing, lockmode);
			pass = AT_PASS_DROP;
			break;
		case AT_SetStatistics:	/* ALTER COLUMN SET STATISTICS */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW | ATT_INDEX | ATT_PARTITIONED_INDEX | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_SetOptions:		/* ALTER COLUMN SET ( options ) */
		case AT_ResetOptions:	/* ALTER COLUMN RESET ( options ) */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			pass = AT_PASS_MISC;
			break;
		case AT_SetStorage:		/* ALTER COLUMN SET STORAGE */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_SetCompression: /* ALTER COLUMN SET COMPRESSION */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_DropColumn:		/* DROP COLUMN */
			ATSimplePermissions(cmd->subtype, rel,
								ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE);
			ATPrepDropColumn(wqueue, rel, recurse, recursing, cmd,
							 lockmode, context);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_DROP;
			break;
		case AT_AddIndex:		/* ADD INDEX */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_ADD_INDEX;
			break;
		case AT_AddConstraint:	/* ADD CONSTRAINT */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Recursion occurs during execution phase */
			/* No command-specific prep needed except saving recurse flag */
			if (recurse)
				cmd->subtype = AT_AddConstraintRecurse;
			pass = AT_PASS_ADD_CONSTR;
			break;
		case AT_AddIndexConstraint: /* ADD CONSTRAINT USING INDEX */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_ADD_INDEXCONSTR;
			break;
		case AT_DropConstraint: /* DROP CONSTRAINT */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			ATCheckPartitionsNotInUse(rel, lockmode);
			/* Other recursion occurs during execution phase */
			/* No command-specific prep needed except saving recurse flag */
			if (recurse)
				cmd->subtype = AT_DropConstraintRecurse;
			pass = AT_PASS_DROP;
			break;
		case AT_AlterColumnType:	/* ALTER COLUMN TYPE */
			ATSimplePermissions(cmd->subtype, rel,
								ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE);
			/* See comments for ATPrepAlterColumnType */
			cmd = ATParseTransformCmd(wqueue, tab, rel, cmd, recurse, lockmode,
									  AT_PASS_UNSET, context);
			Assert(cmd != NULL);
			/* Performs own recursion */
			ATPrepAlterColumnType(wqueue, tab, rel, recurse, recursing, cmd,
								  lockmode, context);
			pass = AT_PASS_ALTER_TYPE;
			break;
		case AT_AlterColumnGenericOptions:
			ATSimplePermissions(cmd->subtype, rel, ATT_FOREIGN_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_ChangeOwner:	/* ALTER OWNER */
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_ClusterOn:		/* CLUSTER ON */
		case AT_DropCluster:	/* SET WITHOUT CLUSTER */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW);
			/* These commands never recurse */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_SetLogged:		/* SET LOGGED */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_SEQUENCE);
			if (tab->chgPersistence)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot change persistence setting twice")));
			tab->chgPersistence = ATPrepChangePersistence(rel, true);
			/* force rewrite if necessary; see comment in ATRewriteTables */
			if (tab->chgPersistence)
			{
				tab->rewrite |= AT_REWRITE_ALTER_PERSISTENCE;
				tab->newrelpersistence = RELPERSISTENCE_PERMANENT;
			}
			pass = AT_PASS_MISC;
			break;
		case AT_SetUnLogged:	/* SET UNLOGGED */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_SEQUENCE);
			if (tab->chgPersistence)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot change persistence setting twice")));
			tab->chgPersistence = ATPrepChangePersistence(rel, false);

			if (IsYugaByteEnabled())
			{
				/* UNLOGGED persistence is NO-OP in YB. */
				tab->chgPersistence = false;
				ereport(NOTICE,
						(errmsg("unlogged option is currently ignored in YugabyteDB, "
										"all non-temp relations will be logged")));
			}

			/* force rewrite if necessary; see comment in ATRewriteTables */
			if (tab->chgPersistence)
			{
				tab->rewrite |= AT_REWRITE_ALTER_PERSISTENCE;
				tab->newrelpersistence = RELPERSISTENCE_UNLOGGED;
			}
			pass = AT_PASS_MISC;
			break;
		case AT_DropOids:		/* SET WITHOUT OIDS */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			pass = AT_PASS_DROP;
			break;
		case AT_SetAccessMethod:	/* SET ACCESS METHOD */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW);

			/* partitioned tables don't have an access method */
			if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("cannot change access method of a partitioned table")));

			/* check if another access method change was already requested */
			if (OidIsValid(tab->newAccessMethod))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot have multiple SET ACCESS METHOD subcommands")));

			ATPrepSetAccessMethod(tab, rel, cmd->name);
			pass = AT_PASS_MISC;	/* does not matter; no work in Phase 2 */
			break;
		case AT_SetTableSpace:	/* SET TABLESPACE */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW | ATT_INDEX |
								ATT_PARTITIONED_INDEX);
			/* This command never recurses */
			ATPrepSetTableSpace(tab, rel, cmd->name, lockmode, cmd->yb_cascade);
			pass = AT_PASS_MISC;	/* doesn't actually matter */
			break;
		case AT_SetRelOptions:	/* SET (...) */
		case AT_ResetRelOptions:	/* RESET (...) */
		case AT_ReplaceRelOptions:	/* reset them all, then set just these */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_VIEW | ATT_MATVIEW | ATT_INDEX);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_AddInherit:		/* INHERIT */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			ATPrepAddInherit(rel);
			pass = AT_PASS_MISC;
			break;
		case AT_DropInherit:	/* NO INHERIT */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_AlterConstraint:	/* ALTER CONSTRAINT */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_MISC;
			break;
		case AT_ValidateConstraint: /* VALIDATE CONSTRAINT */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Recursion occurs during execution phase */
			/* No command-specific prep needed except saving recurse flag */
			if (recurse)
				cmd->subtype = AT_ValidateConstraintRecurse;
			pass = AT_PASS_MISC;
			break;
		case AT_ReplicaIdentity:	/* REPLICA IDENTITY ... */
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_MATVIEW);
			pass = AT_PASS_MISC;
			/* This command never recurses */
			/* No command-specific prep needed */
			break;
		case AT_EnableTrig:		/* ENABLE TRIGGER variants */
		case AT_EnableAlwaysTrig:
		case AT_EnableReplicaTrig:
		case AT_EnableTrigAll:
		case AT_EnableTrigUser:
		case AT_DisableTrig:	/* DISABLE TRIGGER variants */
		case AT_DisableTrigAll:
		case AT_DisableTrigUser:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Set up recursion for phase 2; no other prep needed */
			if (recurse)
				cmd->recurse = true;
			pass = AT_PASS_MISC;
			break;
		case AT_EnableRule:		/* ENABLE/DISABLE RULE variants */
		case AT_EnableAlwaysRule:
		case AT_EnableReplicaRule:
		case AT_DisableRule:
		case AT_AddOf:			/* OF */
		case AT_DropOf:			/* NOT OF */
		case AT_EnableRowSecurity:
		case AT_DisableRowSecurity:
		case AT_ForceRowSecurity:
		case AT_NoForceRowSecurity:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE);
			/* These commands never recurse */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_GenericOptions:
			ATSimplePermissions(cmd->subtype, rel, ATT_FOREIGN_TABLE);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_AttachPartition:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE | ATT_PARTITIONED_INDEX);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_DetachPartition:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_DetachPartitionFinalize:
			ATSimplePermissions(cmd->subtype, rel, ATT_TABLE);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		default:				/* oops */
			elog(ERROR, "unrecognized alter table type: %d",
				 (int) cmd->subtype);
			pass = AT_PASS_UNSET;	/* keep compiler quiet */
			break;
	}
	Assert(pass > AT_PASS_UNSET);

	/* Add the subcommand to the appropriate list for phase 2 */
	tab->subcmds[pass] = lappend(tab->subcmds[pass], cmd);
}

/*
 * ATRewriteCatalogs
 *
 * Traffic cop for ALTER TABLE Phase 2 operations.  Subcommands are
 * dispatched in a "safe" execution order (designed to avoid unnecessary
 * conflicts).
 */
static void
ATRewriteCatalogs(List **wqueue, LOCKMODE lockmode,
				  AlterTableUtilityContext *context,
				  List **rollbackHandles,
				  List *volatile *ybAlteredTableIds)
{
	int			pass;
	ListCell   *ltab;

	/*
	 * Prepare the YB alter statement handle -- need to call this before the
	 * system catalogs are changed below (since it looks up table metadata).
	 *
	 * One wqueue entry corresponds to one relation, "main" one occupying
	 * the first slot.
	 *
	 * In future we might want to process all relations.
	 */
	AlteredTableInfo* info       = (AlteredTableInfo *) linitial(*wqueue);
	Oid               main_relid = info->relid;
	YBCPgStatement rollbackHandle = NULL;
	List *handles = YBCPrepareAlterTable(info->subcmds,
										 AT_NUM_PASSES,
										 main_relid,
										 &rollbackHandle,
										 false /* isPartitionOfAlteredTable */,
										 ybAlteredTableIds);
	if (rollbackHandle)
		*rollbackHandles = lappend(*rollbackHandles, rollbackHandle);

	/*
	 * If this is a partitioned table, prepare alter table handles for all
	 * the partitions as well.
	 */
	List *children = find_all_inheritors(main_relid, NoLock, NULL);
	ListCell   *child;
	foreach(child, children)
	{
		Oid childrelid = lfirst_oid(child);
		/*
		 * find_all_inheritors also returns the oid of the table itself.
		 * Skip it, as we have already added handles for it.
		 */
		if (childrelid == main_relid)
			continue;
		YBCPgStatement childRollbackHandle = NULL;
		List *child_handles = YBCPrepareAlterTable(info->subcmds,
												   AT_NUM_PASSES,
												   childrelid,
												   &childRollbackHandle,
												   true /*isPartitionOfAlteredTable */,
												   ybAlteredTableIds);
		ListCell *listcell = NULL;
		foreach(listcell, child_handles)
		{
			YBCPgStatement child = (YBCPgStatement) lfirst(listcell);
			handles = lappend(handles, child);
		}
		if (childRollbackHandle)
			*rollbackHandles = lappend(*rollbackHandles, childRollbackHandle);
	}

	bool yb_table_cloned = false;
	/*
	 * We process all the tables "in parallel", one pass at a time.  This is
	 * needed because we may have to propagate work from one table to another
	 * (specifically, ALTER TYPE on a foreign key's PK has to dispatch the
	 * re-adding of the foreign key constraint to the other table).  Work can
	 * only be propagated into later passes, however.
	 */
	ListCell *lc = NULL;
	for (pass = 0; pass < AT_NUM_PASSES; pass++)
	{
		Oid relfilenode_id;
		/*
		 * Execute the YB alter table (if needed).
		 *
		 * Must call this after syscatalog updates succeed (e.g. dependencies are
		 * checked) since we do not support rollback of YB alter operations yet.
		 *
		 * However, we must also do this before we start adding indexes because
		 * the column in question might not be there yet.
		 */
		if (pass == AT_PASS_ADD_INDEX)
		{
			foreach(lc, handles)
			{
				YBCPgStatement handle = (YBCPgStatement) lfirst(lc);
				YBCExecAlterTable(handle, main_relid);
			}
		}

		/* Go through each table that needs to be processed */
		foreach(ltab, *wqueue)
		{
			AlteredTableInfo *tab = (AlteredTableInfo *) lfirst(ltab);
			List	   *subcmds = tab->subcmds[pass];
			ListCell   *lcmd;

			if (subcmds == NIL)
				continue;

			/*
			 * Open the relation and store it in tab.  This allows subroutines
			 * close and reopen, if necessary.  Appropriate lock was obtained
			 * by phase 1, needn't get it again.
			 */
			tab->rel = relation_open(tab->relid, NoLock);

			foreach(lcmd, subcmds)
			{
				/* Do not clang-format because this is PG code */
				/* clang-format off */
				ATExecCmd(wqueue, tab,
						  lfirst_node(AlterTableCmd, lcmd),
						  lockmode, pass, context);
				/* clang-format on */

				/*
				 * YB Note: The following only applies to the old ADD/DROP PK
				 * and ALTER TYPE code. This code path will be removed in a
				 * future release.
				 * It is possible that we create a new table in ATExecCmd, so we
				 * need to update main_relid and later update the rest of our
				 * commands so that they know to use the new relid
				 */
				if (IsYBRelation(tab->rel) && !yb_enable_alter_table_rewrite &&
					(pass == AT_PASS_ALTER_TYPE || pass == AT_PASS_ADD_INDEX))
				{
					main_relid = RelationGetRelid(tab->rel);
					relfilenode_id = YbGetRelfileNodeId(tab->rel);
					yb_table_cloned = true;
				}
			}

			/*
			 * After the ALTER TYPE pass, do cleanup work (this is not done in
			 * ATExecAlterColumnType since it should be done only once if
			 * multiple columns of a table are altered).
			 *
			 * Do not perform this work if it is a YB relation because we will
			 * have created an entirely new table, so there is no need for
			 * cleanup.
			 */
			if (pass == AT_PASS_ALTER_TYPE &&
				(!IsYBRelation(tab->rel) || yb_enable_alter_table_rewrite))
				ATPostAlterTypeCleanup(wqueue, tab, lockmode);

			if (tab->rel)
			{
				relation_close(tab->rel, NoLock);
				tab->rel = NULL;
			}
		}


		/*
		 * If we have an entirely new relation, the table id for all remaining
		 * commands needs to be updated
		 */
		if (yb_table_cloned)
		{
			foreach (lc, handles)
			{
				YBCPgStatement handle = (YBCPgStatement) lfirst(lc);
				YBCPgAlterTableSetTableId(
					handle, YBCGetDatabaseOidByRelid(main_relid),
					relfilenode_id);
			}
			yb_table_cloned = false;
		}
	}

	/* YugaByte doesn't support toast tables. */
	if (IsYugaByteEnabled())
		return;

	/* Check to see if a toast table must be added. */
	foreach(ltab, *wqueue)
	{
		AlteredTableInfo *tab = (AlteredTableInfo *) lfirst(ltab);

		/*
		 * If the table is source table of ATTACH PARTITION command, we did
		 * not modify anything about it that will change its toasting
		 * requirement, so no need to check.
		 */
		if (((tab->relkind == RELKIND_RELATION ||
			  tab->relkind == RELKIND_PARTITIONED_TABLE) &&
			 tab->partition_constraint == NULL) ||
			tab->relkind == RELKIND_MATVIEW)
			AlterTableCreateToastTable(tab->relid, (Datum) 0, lockmode);
	}
}

/*
 * ATExecCmd: dispatch a subcommand to appropriate execution routine.
 *
 * YB NOTE: Will replace passed relation with another one in case it was re-created.
 */
static void
ATExecCmd(List **wqueue, AlteredTableInfo *tab,
		  AlterTableCmd *cmd, LOCKMODE lockmode, int cur_pass,
		  AlterTableUtilityContext *context)
{
	ObjectAddress address = InvalidObjectAddress;
	Relation	rel = tab->rel;
	Relation	*mutable_rel = &tab->rel;

	switch (cmd->subtype)
	{
		case AT_AddColumn:		/* ADD COLUMN */
		case AT_AddColumnToView:	/* add column via CREATE OR REPLACE VIEW */
			address = ATExecAddColumn(wqueue, tab, rel, &cmd,
									  false, false,
									  lockmode, cur_pass, context);
			break;
		case AT_AddColumnRecurse:
			address = ATExecAddColumn(wqueue, tab, rel, &cmd,
									  true, false,
									  lockmode, cur_pass, context);
			break;
		case AT_ColumnDefault:	/* ALTER COLUMN DEFAULT */
			address = ATExecColumnDefault(rel, cmd->name, cmd->def, lockmode);
			break;
		case AT_CookedColumnDefault:	/* add a pre-cooked default */
			address = ATExecCookedColumnDefault(rel, cmd->num, cmd->def);
			break;
		case AT_AddIdentity:
			cmd = ATParseTransformCmd(wqueue, tab, rel, cmd, false, lockmode,
									  cur_pass, context);
			Assert(cmd != NULL);
			address = ATExecAddIdentity(rel, cmd->name, cmd->def, lockmode);
			break;
		case AT_SetIdentity:
			cmd = ATParseTransformCmd(wqueue, tab, rel, cmd, false, lockmode,
									  cur_pass, context);
			Assert(cmd != NULL);
			address = ATExecSetIdentity(rel, cmd->name, cmd->def, lockmode);
			break;
		case AT_DropIdentity:
			address = ATExecDropIdentity(rel, cmd->name, cmd->missing_ok, lockmode);
			break;
		case AT_DropNotNull:	/* ALTER COLUMN DROP NOT NULL */
			address = ATExecDropNotNull(rel, cmd->name, lockmode);
			break;
		case AT_SetNotNull:		/* ALTER COLUMN SET NOT NULL */
			address = ATExecSetNotNull(tab, rel, cmd->name, lockmode);
			break;
		case AT_CheckNotNull:	/* check column is already marked NOT NULL */
			ATExecCheckNotNull(tab, rel, cmd->name, lockmode);
			break;
		case AT_DropExpression:
			address = ATExecDropExpression(rel, cmd->name, cmd->missing_ok, lockmode);
			break;
		case AT_SetStatistics:	/* ALTER COLUMN SET STATISTICS */
			address = ATExecSetStatistics(rel, cmd->name, cmd->num, cmd->def, lockmode);
			break;
		case AT_SetOptions:		/* ALTER COLUMN SET ( options ) */
			address = ATExecSetOptions(rel, cmd->name, cmd->def, false, lockmode);
			break;
		case AT_ResetOptions:	/* ALTER COLUMN RESET ( options ) */
			address = ATExecSetOptions(rel, cmd->name, cmd->def, true, lockmode);
			break;
		case AT_SetStorage:		/* ALTER COLUMN SET STORAGE */
			address = ATExecSetStorage(rel, cmd->name, cmd->def, lockmode);
			break;
		case AT_SetCompression:
			address = ATExecSetCompression(tab, rel, cmd->name, cmd->def,
										   lockmode);
			break;
		case AT_DropColumn:		/* DROP COLUMN */
			address = ATExecDropColumn(wqueue, tab, rel, cmd->name,
									   cmd->behavior, false, false,
									   cmd->missing_ok, lockmode,
									   NULL);
			break;
		case AT_DropColumnRecurse:	/* DROP COLUMN with recursion */
			address = ATExecDropColumn(wqueue, tab, rel, cmd->name,
									   cmd->behavior, true, false,
									   cmd->missing_ok, lockmode,
									   NULL);
			break;
		case AT_AddIndex:		/* ADD INDEX */
			address = ATExecAddIndex(wqueue, tab, mutable_rel,
									 (IndexStmt *) cmd->def, false,
									 lockmode);
			break;
		case AT_ReAddIndex:		/* ADD INDEX */
			address = ATExecAddIndex(wqueue, tab, mutable_rel, (IndexStmt *) cmd->def, true,
									 lockmode);
			break;
		case AT_ReAddStatistics:	/* ADD STATISTICS */
			address = ATExecAddStatistics(tab, rel, (CreateStatsStmt *) cmd->def,
										  true, lockmode);
			break;
		case AT_AddConstraint:	/* ADD CONSTRAINT */
			/* Transform the command only during initial examination */
			if (cur_pass == AT_PASS_ADD_CONSTR)
				cmd = ATParseTransformCmd(wqueue, tab, rel, cmd,
										  false, lockmode,
										  cur_pass, context);
			/* Depending on constraint type, might be no more work to do now */
			if (cmd != NULL)
				address =
					ATExecAddConstraint(wqueue, tab, rel,
										(Constraint *) cmd->def,
										false, false, lockmode);
			break;
		case AT_AddConstraintRecurse:	/* ADD CONSTRAINT with recursion */
			/* Transform the command only during initial examination */
			if (cur_pass == AT_PASS_ADD_CONSTR)
				cmd = ATParseTransformCmd(wqueue, tab, rel, cmd,
										  true, lockmode,
										  cur_pass, context);
			/* Depending on constraint type, might be no more work to do now */
			if (cmd != NULL)
				address =
					ATExecAddConstraint(wqueue, tab, rel,
										(Constraint *) cmd->def,
										true, false, lockmode);
			break;
		case AT_ReAddConstraint:	/* Re-add pre-existing check constraint */
			address =
				ATExecAddConstraint(wqueue, tab, rel, (Constraint *) cmd->def,
									true, true, lockmode);
			break;
		case AT_ReAddDomainConstraint:	/* Re-add pre-existing domain check
										 * constraint */
			address =
				AlterDomainAddConstraint(((AlterDomainStmt *) cmd->def)->typeName,
										 ((AlterDomainStmt *) cmd->def)->def,
										 NULL);
			break;
		case AT_ReAddComment:	/* Re-add existing comment */
			address = CommentObject((CommentStmt *) cmd->def);
			break;
		case AT_AddIndexConstraint: /* ADD CONSTRAINT USING INDEX */
			address = ATExecAddIndexConstraint(tab, rel, (IndexStmt *) cmd->def,
											   lockmode, wqueue);
			break;
		case AT_AlterConstraint:	/* ALTER CONSTRAINT */
			address = ATExecAlterConstraint(rel, cmd, false, false, lockmode);
			break;
		case AT_ValidateConstraint: /* VALIDATE CONSTRAINT */
			address = ATExecValidateConstraint(wqueue, rel, cmd->name, false,
											   false, lockmode);
			break;
		case AT_ValidateConstraintRecurse:	/* VALIDATE CONSTRAINT with
											 * recursion */
			address = ATExecValidateConstraint(wqueue, rel, cmd->name, true,
											   false, lockmode);
			break;
		case AT_DropConstraint: /* DROP CONSTRAINT */
			ATExecDropConstraint(wqueue, tab, mutable_rel, cmd->name, cmd->behavior,
								 false, false,
								 cmd->missing_ok, lockmode);
			break;
		case AT_DropConstraintRecurse:	/* DROP CONSTRAINT with recursion */
			ATExecDropConstraint(wqueue, tab, mutable_rel, cmd->name, cmd->behavior,
								 true, false,
								 cmd->missing_ok, lockmode);
			break;
		case AT_AlterColumnType:	/* ALTER COLUMN TYPE */
			/* parse transformation was done earlier */
			address = ATExecAlterColumnType(tab, mutable_rel, cmd, lockmode);
			break;
		case AT_AlterColumnGenericOptions:	/* ALTER COLUMN OPTIONS */
			address =
				ATExecAlterColumnGenericOptions(rel, cmd->name,
												(List *) cmd->def, lockmode);
			break;
		case AT_ChangeOwner:	/* ALTER OWNER */
			ATExecChangeOwner(RelationGetRelid(rel),
							  get_rolespec_oid(cmd->newowner, false),
							  false, lockmode);
			break;
		case AT_ClusterOn:		/* CLUSTER ON */
			address = ATExecClusterOn(rel, cmd->name, lockmode);
			break;
		case AT_DropCluster:	/* SET WITHOUT CLUSTER */
			ATExecDropCluster(rel, lockmode);
			break;
		case AT_SetLogged:		/* SET LOGGED */
		case AT_SetUnLogged:	/* SET UNLOGGED */
			break;
		case AT_DropOids:		/* SET WITHOUT OIDS */
			/* nothing to do here, oid columns don't exist anymore */
			break;
		case AT_SetAccessMethod:	/* SET ACCESS METHOD */
			/* handled specially in Phase 3 */
			break;
		case AT_SetTableSpace:	/* SET TABLESPACE */

			/*
			 * Only do this for partitioned tables and indexes or when Yugabyte is
			 * enabled, for which this is just a catalog change.  Other relation types
			 * which have storage are handled by Phase 3.
			 */
			if (IsYBRelation(rel) ||
				rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE ||
				rel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
				ATExecSetTableSpaceNoStorage(rel, tab->newTableSpace);

			break;
		case AT_SetRelOptions:	/* SET (...) */
		case AT_ResetRelOptions:	/* RESET (...) */
		case AT_ReplaceRelOptions:	/* replace entire option list */
			ATExecSetRelOptions(rel, (List *) cmd->def, cmd->subtype, lockmode);
			break;
		case AT_EnableTrig:		/* ENABLE TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_FIRES_ON_ORIGIN, false,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_EnableAlwaysTrig:	/* ENABLE ALWAYS TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_FIRES_ALWAYS, false,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_EnableReplicaTrig:	/* ENABLE REPLICA TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_FIRES_ON_REPLICA, false,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_DisableTrig:	/* DISABLE TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_DISABLED, false,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_EnableTrigAll:	/* ENABLE TRIGGER ALL */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_FIRES_ON_ORIGIN, false,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_DisableTrigAll: /* DISABLE TRIGGER ALL */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_DISABLED, false,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_EnableTrigUser: /* ENABLE TRIGGER USER */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_FIRES_ON_ORIGIN, true,
									   cmd->recurse,
									   lockmode);
			break;
		case AT_DisableTrigUser:	/* DISABLE TRIGGER USER */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_DISABLED, true,
									   cmd->recurse,
									   lockmode);
			break;

		case AT_EnableRule:		/* ENABLE RULE name */
			ATExecEnableDisableRule(rel, cmd->name,
									RULE_FIRES_ON_ORIGIN, lockmode);
			break;
		case AT_EnableAlwaysRule:	/* ENABLE ALWAYS RULE name */
			ATExecEnableDisableRule(rel, cmd->name,
									RULE_FIRES_ALWAYS, lockmode);
			break;
		case AT_EnableReplicaRule:	/* ENABLE REPLICA RULE name */
			ATExecEnableDisableRule(rel, cmd->name,
									RULE_FIRES_ON_REPLICA, lockmode);
			break;
		case AT_DisableRule:	/* DISABLE RULE name */
			ATExecEnableDisableRule(rel, cmd->name,
									RULE_DISABLED, lockmode);
			break;

		case AT_AddInherit:
			address = ATExecAddInherit(rel, (RangeVar *) cmd->def, lockmode);
			break;
		case AT_DropInherit:
			address = ATExecDropInherit(rel, (RangeVar *) cmd->def, lockmode);
			break;
		case AT_AddOf:
			address = ATExecAddOf(rel, (TypeName *) cmd->def, lockmode);
			break;
		case AT_DropOf:
			ATExecDropOf(rel, lockmode);
			break;
		case AT_ReplicaIdentity:
			ATExecReplicaIdentity(rel, (ReplicaIdentityStmt *) cmd->def, lockmode);
			break;
		case AT_EnableRowSecurity:
			ATExecSetRowSecurity(rel, true);
			break;
		case AT_DisableRowSecurity:
			ATExecSetRowSecurity(rel, false);
			break;
		case AT_ForceRowSecurity:
			ATExecForceNoForceRowSecurity(rel, true);
			break;
		case AT_NoForceRowSecurity:
			ATExecForceNoForceRowSecurity(rel, false);
			break;
		case AT_GenericOptions:
			ATExecGenericOptions(rel, (List *) cmd->def);
			break;
		case AT_AttachPartition:
			cmd = ATParseTransformCmd(wqueue, tab, rel, cmd, false, lockmode,
									  cur_pass, context);
			Assert(cmd != NULL);
			if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
				ATExecAttachPartition(wqueue, rel, (PartitionCmd *) cmd->def,
									  context);
			else
				ATExecAttachPartitionIdx(wqueue, rel,
										 ((PartitionCmd *) cmd->def)->name);
			break;
		case AT_DetachPartition:
			cmd = ATParseTransformCmd(wqueue, tab, rel, cmd, false, lockmode,
									  cur_pass, context);
			Assert(cmd != NULL);
			/* ATPrepCmd ensures it must be a table */
			Assert(rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE);
			ATExecDetachPartition(wqueue, tab, rel,
								  ((PartitionCmd *) cmd->def)->name,
								  ((PartitionCmd *) cmd->def)->concurrent);
			break;
		case AT_DetachPartitionFinalize:
			ATExecDetachPartitionFinalize(rel, ((PartitionCmd *) cmd->def)->name);
			break;
		default:				/* oops */
			elog(ERROR, "unrecognized alter table type: %d",
				 (int) cmd->subtype);
			break;
	}

	/*
	 * Report the subcommand to interested event triggers.
	 */
	if (cmd)
		EventTriggerCollectAlterTableSubcmd((Node *) cmd, address);

	/*
	 * Bump the command counter to ensure the next subcommand in the sequence
	 * can see the changes so far
	 */
	CommandCounterIncrement();
}

/*
 * ATParseTransformCmd: perform parse transformation for one subcommand
 *
 * Returns the transformed subcommand tree, if there is one, else NULL.
 *
 * The parser may hand back additional AlterTableCmd(s) and/or other
 * utility statements, either before or after the original subcommand.
 * Other AlterTableCmds are scheduled into the appropriate slot of the
 * AlteredTableInfo (they had better be for later passes than the current one).
 * Utility statements that are supposed to happen before the AlterTableCmd
 * are executed immediately.  Those that are supposed to happen afterwards
 * are added to the tab->afterStmts list to be done at the very end.
 */
static AlterTableCmd *
ATParseTransformCmd(List **wqueue, AlteredTableInfo *tab, Relation rel,
					AlterTableCmd *cmd, bool recurse, LOCKMODE lockmode,
					int cur_pass, AlterTableUtilityContext *context)
{
	AlterTableCmd *newcmd = NULL;
	AlterTableStmt *atstmt = makeNode(AlterTableStmt);
	List	   *beforeStmts;
	List	   *afterStmts;
	ListCell   *lc;

	/* Gin up an AlterTableStmt with just this subcommand and this table */
	atstmt->relation =
		makeRangeVar(get_namespace_name(RelationGetNamespace(rel)),
					 pstrdup(RelationGetRelationName(rel)),
					 -1);
	atstmt->relation->inh = recurse;
	atstmt->cmds = list_make1(cmd);
	atstmt->objtype = OBJECT_TABLE; /* needn't be picky here */
	atstmt->missing_ok = false;

	/* Transform the AlterTableStmt */
	atstmt = transformAlterTableStmt(RelationGetRelid(rel),
									 atstmt,
									 context->queryString,
									 &beforeStmts,
									 &afterStmts);

	/* Execute any statements that should happen before these subcommand(s) */
	foreach(lc, beforeStmts)
	{
		Node	   *stmt = (Node *) lfirst(lc);

		ProcessUtilityForAlterTable(stmt, context);
		CommandCounterIncrement();
	}

	/* Examine the transformed subcommands and schedule them appropriately */
	foreach(lc, atstmt->cmds)
	{
		AlterTableCmd *cmd2 = lfirst_node(AlterTableCmd, lc);
		int			pass;

		/*
		 * This switch need only cover the subcommand types that can be added
		 * by parse_utilcmd.c; otherwise, we'll use the default strategy of
		 * executing the subcommand immediately, as a substitute for the
		 * original subcommand.  (Note, however, that this does cause
		 * AT_AddConstraint subcommands to be rescheduled into later passes,
		 * which is important for index and foreign key constraints.)
		 *
		 * We assume we needn't do any phase-1 checks for added subcommands.
		 */
		switch (cmd2->subtype)
		{
			case AT_SetNotNull:
				/* Need command-specific recursion decision */
				ATPrepSetNotNull(wqueue, rel, cmd2,
								 recurse, false,
								 lockmode, context);
				pass = AT_PASS_COL_ATTRS;
				break;
			case AT_AddIndex:
				/* This command never recurses */
				/* No command-specific prep needed */
				pass = AT_PASS_ADD_INDEX;
				break;
			case AT_AddIndexConstraint:
				/* This command never recurses */
				/* No command-specific prep needed */
				pass = AT_PASS_ADD_INDEXCONSTR;
				break;
			case AT_AddConstraint:
				/* Recursion occurs during execution phase */
				if (recurse)
					cmd2->subtype = AT_AddConstraintRecurse;
				switch (castNode(Constraint, cmd2->def)->contype)
				{
					case CONSTR_PRIMARY:
					case CONSTR_UNIQUE:
					case CONSTR_EXCLUSION:
						pass = AT_PASS_ADD_INDEXCONSTR;
						break;
					default:
						pass = AT_PASS_ADD_OTHERCONSTR;
						break;
				}
				break;
			case AT_AlterColumnGenericOptions:
				/* This command never recurses */
				/* No command-specific prep needed */
				pass = AT_PASS_MISC;
				break;
			default:
				pass = cur_pass;
				break;
		}

		if (pass < cur_pass)
		{
			/* Cannot schedule into a pass we already finished */
			elog(ERROR, "ALTER TABLE scheduling failure: too late for pass %d",
				 pass);
		}
		else if (pass > cur_pass)
		{
			/* OK, queue it up for later */
			tab->subcmds[pass] = lappend(tab->subcmds[pass], cmd2);
		}
		else
		{
			/*
			 * We should see at most one subcommand for the current pass,
			 * which is the transformed version of the original subcommand.
			 */
			if (newcmd == NULL && cmd->subtype == cmd2->subtype)
			{
				/* Found the transformed version of our subcommand */
				newcmd = cmd2;
			}
			else
				elog(ERROR, "ALTER TABLE scheduling failure: bogus item for pass %d",
					 pass);
		}
	}

	/* Queue up any after-statements to happen at the end */
	tab->afterStmts = list_concat(tab->afterStmts, afterStmts);

	return newcmd;
}

/*
 * ATRewriteTables: ALTER TABLE phase 3
 */
static void
ATRewriteTables(AlterTableStmt *parsetree, List **wqueue, LOCKMODE lockmode,
				AlterTableUtilityContext *context)
{
	ListCell   *ltab;

	/* Go through each table that needs to be checked or rewritten */
	foreach(ltab, *wqueue)
	{
		AlteredTableInfo *tab = (AlteredTableInfo *) lfirst(ltab);

		/*
		 * Relations without storage may be ignored here.
		 * Foreign tables have no storage, nor do partitioned tables and indexes.
		 * YB: We do not need to rewrite tables during upgrade because we
		 * link the DocDB table with the data on master.
		 */
		if (!RELKIND_HAS_STORAGE(tab->relkind) ||
			(IsYBRelationById(tab->relid) && IsBinaryUpgrade))
			continue;
		/*
		 * YB Note: The following only applies to the old ALTER TYPE code.
		 * This code path will be removed in a future release.
		 * If this command involved altering a column type, then we have created
		 * an entirely new table so there is no need to rewrite it here.
		 *
		 * Note that Postgres executes the trigger before rewriting the table,
		 * but for YB relations we will already have cloned the table, which is
		 * the equivalent of a rewrite, by the time we reach here so the trigger
		 * is executed after the rewrite.
		 */
		if (IsYBRelationById(tab->relid) && !yb_enable_alter_table_rewrite
			&& tab->subcmds[AT_PASS_ALTER_TYPE] != NIL && tab->rewrite > 0)
		{
			if (parsetree)
				EventTriggerTableRewrite((Node *) parsetree, tab->relid,
										 tab->rewrite);
			continue;
		}

		/*
		 * When rewriting a temp relation, we need to execute the PG
		 * transaction handling code-paths, as PG uses the transaction ID to
		 * determine what rows are visible to a specific transaction.
		 */
		if (!IsYBRelationById(tab->relid) && tab->rewrite > 0)
			SetTxnWithPGRel();

		/*
		 * If we change column data types, the operation has to be propagated
		 * to tables that use this table's rowtype as a column type.
		 * tab->newvals will also be non-NULL in the case where we're adding a
		 * column with a default.  We choose to forbid that case as well,
		 * since composite types might eventually support defaults.
		 *
		 * (Eventually we'll probably need to check for composite type
		 * dependencies even when we're just scanning the table without a
		 * rewrite, but at the moment a composite type does not enforce any
		 * constraints, so it's not necessary/appropriate to enforce them just
		 * during ALTER.)
		 */
		if (tab->newvals != NIL || tab->rewrite > 0)
		{
			Relation	rel;

			rel = table_open(tab->relid, NoLock);
			find_composite_type_dependencies(rel->rd_rel->reltype, rel, NULL);
			table_close(rel, NoLock);
		}

		/*
		 * We only need to rewrite the table if at least one column needs to
		 * be recomputed, or we are changing its persistence or access method.
		 *
		 * There are two reasons for requiring a rewrite when changing
		 * persistence: on one hand, we need to ensure that the buffers
		 * belonging to each of the two relations are marked with or without
		 * BM_PERMANENT properly.  On the other hand, since rewriting creates
		 * and assigns a new relfilenode, we automatically create or drop an
		 * init fork for the relation as appropriate.
		 */
		if (tab->rewrite > 0 && tab->relkind != RELKIND_SEQUENCE)
		{
			/* Build a temporary relation and copy data */
			Relation	OldHeap;
			Oid			OIDNewHeap;
			Oid			NewAccessMethod;
			Oid			NewTableSpace;
			char		persistence;

			OldHeap = table_open(tab->relid, NoLock);

			/*
			 * We don't support rewriting of system catalogs; there are too
			 * many corner cases and too little benefit.  In particular this
			 * is certainly not going to work for mapped catalogs.
			 */
			if (IsSystemRelation(OldHeap))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot rewrite system relation \"%s\"",
								RelationGetRelationName(OldHeap))));

			if (RelationIsUsedAsCatalogTable(OldHeap))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot rewrite table \"%s\" used as a catalog table",
								RelationGetRelationName(OldHeap))));

			if (IsYBBackedRelation(OldHeap) && !yb_enable_alter_table_rewrite)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("Rewriting of YB table is not yet implemented"),
						 errhint("See https://github.com/yugabyte/yugabyte-db/issues/13278. "
						         "React with thumbs up to raise its priority")));

			if (IsYBRelation(OldHeap) && !YBSuppressUnsafeAlterNotice())
				ereport(NOTICE,
						(errmsg("table rewrite may lead to inconsistencies"),
						 errdetail("Concurrent DMLs may not be reflected in"
								   " the new table."),
						 errhint("See https://github.com/yugabyte/yugabyte-db/"
								 "issues/19860. "
								 "Set 'ysql_suppress_unsafe_alter_notice'"
								 " yb-tserver gflag to true to suppress this"
								 " notice.")));

			/*
			 * Don't allow rewrite on temp tables of other backends ... their
			 * local buffer manager is not going to cope.
			 */
			if (RELATION_IS_OTHER_TEMP(OldHeap))
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot rewrite temporary tables of other sessions")));

			/*
			 * Select destination tablespace (same as original unless user
			 * requested a change)
			 */
			if (tab->newTableSpace)
				NewTableSpace = tab->newTableSpace;
			else
				NewTableSpace = OldHeap->rd_rel->reltablespace;

			/*
			 * Select destination access method (same as original unless user
			 * requested a change)
			 */
			if (OidIsValid(tab->newAccessMethod))
				NewAccessMethod = tab->newAccessMethod;
			else
				NewAccessMethod = OldHeap->rd_rel->relam;

			/*
			 * Select persistence of transient table (same as original unless
			 * user requested a change)
			 */
			persistence = tab->chgPersistence ?
				tab->newrelpersistence : OldHeap->rd_rel->relpersistence;

			table_close(OldHeap, NoLock);

			/*
			 * Fire off an Event Trigger now, before actually rewriting the
			 * table.
			 *
			 * We don't support Event Trigger for nested commands anywhere,
			 * here included, and parsetree is given NULL when coming from
			 * AlterTableInternal.
			 *
			 * And fire it only once.
			 */
			if (parsetree)
				EventTriggerTableRewrite((Node *) parsetree,
										 tab->relid,
										 tab->rewrite);

			/*
			 * Create transient table that will receive the modified data.
			 *
			 * Ensure it is marked correctly as logged or unlogged.  We have
			 * to do this here so that buffers for the new relfilenode will
			 * have the right persistence set, and at the same time ensure
			 * that the original filenode's buffers will get read in with the
			 * correct setting (i.e. the original one).  Otherwise a rollback
			 * after the rewrite would possibly result with buffers for the
			 * original filenode having the wrong persistence setting.
			 *
			 * NB: This relies on swap_relation_files() also swapping the
			 * persistence. That wouldn't work for pg_class, but that can't be
			 * unlogged anyway.
			 */
			OIDNewHeap = make_new_heap(tab->relid, NewTableSpace, NewAccessMethod,
									   persistence, lockmode,
									   !tab->yb_skip_copy_split_options
									   /* yb_copy_split_options */);

			/*
			 * Copy the heap data into the new table with the desired
			 * modifications, and test the current data within the table
			 * against new constraints generated by ALTER TABLE commands.
			 */
			ATRewriteTable(tab, OIDNewHeap, lockmode);

			/*
			 * Swap the physical files of the old and new heaps, then rebuild
			 * indexes and discard the old heap.  We can use RecentXmin for
			 * the table's new relfrozenxid because we rewrote all the tuples
			 * in ATRewriteTable, so no older Xid remains in the table.  Also,
			 * we never try to swap toast tables by content, since we have no
			 * interest in letting this code work on system catalogs.
			 */
			finish_heap_swap(tab->relid, OIDNewHeap,
							 false, false, true,
							 !OidIsValid(tab->newTableSpace),
							 RecentXmin,
							 ReadNextMultiXactId(),
							 persistence,
							 true /* yb_copy_split_options */);

			InvokeObjectPostAlterHook(RelationRelationId, tab->relid, 0);
		}
		else if (tab->rewrite > 0 && tab->relkind == RELKIND_SEQUENCE)
		{
			if (tab->chgPersistence)
				SequenceChangePersistence(tab->relid, tab->newrelpersistence);
		}
		else
		{
			/*
			 * If required, test the current data within the table against new
			 * constraints generated by ALTER TABLE commands, but don't
			 * rebuild data.
			 */
			if (tab->constraints != NIL || tab->verify_new_notnull ||
				tab->partition_constraint != NULL)
				ATRewriteTable(tab, InvalidOid, lockmode);

			/*
			 * If we had SET TABLESPACE but no reason to reconstruct tuples,
			 * just do a block-by-block copy.
			 */
			if (tab->newTableSpace && !IsYBRelationById(tab->relid))
				ATExecSetTableSpace(tab->relid, tab->newTableSpace, lockmode);
		}

		/*
		 * Also change persistence of owned sequences, so that it matches the
		 * table persistence.
		 */
		if (tab->chgPersistence)
		{
			List	   *seqlist = getOwnedSequences(tab->relid);
			ListCell   *lc;

			foreach(lc, seqlist)
			{
				Oid			seq_relid = lfirst_oid(lc);

				SequenceChangePersistence(seq_relid, tab->newrelpersistence);
			}
		}
	}

	/*
	 * Foreign key constraints are checked in a final pass, since (a) it's
	 * generally best to examine each one separately, and (b) it's at least
	 * theoretically possible that we have changed both relations of the
	 * foreign key, and we'd better have finished both rewrites before we try
	 * to read the tables.
	 */
	foreach(ltab, *wqueue)
	{
		AlteredTableInfo *tab = (AlteredTableInfo *) lfirst(ltab);
		Relation	rel = NULL;
		ListCell   *lcon;

		/*
		 * Relations without storage may be ignored here too.
		 * YB: We can also ignore YB relations during upgrade because their
		 * constraints are already validated by the previous version.
		 */
		if (!RELKIND_HAS_STORAGE(tab->relkind) ||
			(IsYBRelationById(tab->relid) && IsBinaryUpgrade))
			continue;

		foreach(lcon, tab->constraints)
		{
			NewConstraint *con = lfirst(lcon);

			if (con->contype == CONSTR_FOREIGN)
			{
				Constraint *fkconstraint = (Constraint *) con->qual;
				Relation	refrel;

				if (rel == NULL)
				{
					/* Long since locked, no need for another */
					rel = table_open(tab->relid, NoLock);
				}

				refrel = table_open(con->refrelid, RowShareLock);

				validateForeignKeyConstraint(fkconstraint->conname, rel, refrel,
											 con->refindid,
											 con->conid);

				/*
				 * No need to mark the constraint row as validated, we did
				 * that when we inserted the row earlier.
				 */

				table_close(refrel, NoLock);
			}
		}

		if (rel)
			table_close(rel, NoLock);
	}

	/* Finally, run any afterStmts that were queued up */
	foreach(ltab, *wqueue)
	{
		AlteredTableInfo *tab = (AlteredTableInfo *) lfirst(ltab);
		ListCell   *lc;

		foreach(lc, tab->afterStmts)
		{
			Node	   *stmt = (Node *) lfirst(lc);

			ProcessUtilityForAlterTable(stmt, context);
			CommandCounterIncrement();
		}
	}
}

/*
 * ATRewriteTable: scan or rewrite one table
 *
 * OIDNewHeap is InvalidOid if we don't need to rewrite
 */
static void
ATRewriteTable(AlteredTableInfo *tab, Oid OIDNewHeap, LOCKMODE lockmode)
{
	Relation	oldrel;
	Relation	newrel;
	TupleDesc	oldTupDesc;
	TupleDesc	newTupDesc;
	bool		needscan = false;
	List	   *notnull_attrs;
	int			i;
	ListCell   *l;
	EState	   *estate;
	CommandId	mycid;
	BulkInsertState bistate;
	int			ti_options;
	ExprState  *partqualstate = NULL;

	/*
	 * Open the relation(s).  We have surely already locked the existing
	 * table.
	 */
	oldrel = table_open(tab->relid, NoLock);
	oldTupDesc = tab->oldDesc;
	newTupDesc = RelationGetDescr(oldrel);	/* includes all mods */

	if (OidIsValid(OIDNewHeap))
		newrel = table_open(OIDNewHeap, lockmode);
	else
		newrel = NULL;

	/*
	 * Prepare a BulkInsertState and options for table_tuple_insert.  The FSM
	 * is empty, so don't bother using it.
	 */
	if (newrel)
	{
		mycid = GetCurrentCommandId(true);
		bistate = GetBulkInsertState();
		ti_options = TABLE_INSERT_SKIP_FSM;
	}
	else
	{
		/* keep compiler quiet about using these uninitialized */
		mycid = 0;
		bistate = NULL;
		ti_options = 0;
	}

	/*
	 * Generate the constraint and default execution states
	 */

	estate = CreateExecutorState();

	/* Build the needed expression execution states */
	foreach(l, tab->constraints)
	{
		NewConstraint *con = lfirst(l);

		switch (con->contype)
		{
			case CONSTR_CHECK:
				needscan = true;
				con->qualstate = ExecPrepareExpr((Expr *) con->qual, estate);
				break;
			case CONSTR_FOREIGN:
				/* Nothing to do here */
				break;
			default:
				elog(ERROR, "unrecognized constraint type: %d",
					 (int) con->contype);
		}
	}

	/* Build expression execution states for partition check quals */
	if (tab->partition_constraint)
	{
		needscan = true;
		partqualstate = ExecPrepareExpr(tab->partition_constraint, estate);
	}

	foreach(l, tab->newvals)
	{
		NewColumnValue *ex = lfirst(l);

		/* expr already planned */
		ex->exprstate = ExecInitExpr((Expr *) ex->expr, NULL);
	}

	notnull_attrs = NIL;
	if (newrel || tab->verify_new_notnull)
	{
		/*
		 * If we are rebuilding the tuples OR if we added any new but not
		 * verified NOT NULL constraints, check all not-null constraints. This
		 * is a bit of overkill but it minimizes risk of bugs, and
		 * heap_attisnull is a pretty cheap test anyway.
		 */
		for (i = 0; i < newTupDesc->natts; i++)
		{
			Form_pg_attribute attr = TupleDescAttr(newTupDesc, i);

			if (attr->attnotnull && !attr->attisdropped)
				notnull_attrs = lappend_int(notnull_attrs, i);
		}
		if (notnull_attrs)
			needscan = true;
	}

	if (newrel || needscan)
	{
		ExprContext *econtext;
		TupleTableSlot *oldslot;
		TupleTableSlot *newslot;
		TableScanDesc scan;
		MemoryContext oldCxt;
		List	   *dropped_attrs = NIL;
		ListCell   *lc;
		Snapshot	snapshot;

		if (newrel)
			ereport(DEBUG1,
					(errmsg_internal("rewriting table \"%s\"",
									 RelationGetRelationName(oldrel))));
		else
			ereport(DEBUG1,
					(errmsg_internal("verifying table \"%s\"",
									 RelationGetRelationName(oldrel))));

		if (newrel)
		{
			/*
			 * All predicate locks on the tuples or pages are about to be made
			 * invalid, because we move tuples around.  Promote them to
			 * relation locks.
			 */
			TransferPredicateLocksToHeapRelation(oldrel);
		}

		econtext = GetPerTupleExprContext(estate);

		/*
		 * Create necessary tuple slots. When rewriting, two slots are needed,
		 * otherwise one suffices. In the case where one slot suffices, we
		 * need to use the new tuple descriptor, otherwise some constraints
		 * can't be evaluated.  Note that even when the tuple layout is the
		 * same and no rewrite is required, the tupDescs might not be
		 * (consider ADD COLUMN without a default).
		 */
		if (tab->rewrite)
		{
			Assert(newrel != NULL);
			oldslot = MakeSingleTupleTableSlot(oldTupDesc,
											   table_slot_callbacks(oldrel));
			newslot = MakeSingleTupleTableSlot(newTupDesc,
											   table_slot_callbacks(newrel));

			/*
			 * Set all columns in the new slot to NULL initially, to ensure
			 * columns added as part of the rewrite are initialized to NULL.
			 * That is necessary as tab->newvals will not contain an
			 * expression for columns with a NULL default, e.g. when adding a
			 * column without a default together with a column with a default
			 * requiring an actual rewrite.
			 */
			ExecStoreAllNullTuple(newslot);
		}
		else
		{
			oldslot = MakeSingleTupleTableSlot(newTupDesc,
											   table_slot_callbacks(oldrel));
			newslot = NULL;
		}

		/*
		 * Any attributes that are dropped according to the new tuple
		 * descriptor can be set to NULL. We precompute the list of dropped
		 * attributes to avoid needing to do so in the per-tuple loop.
		 */
		for (i = 0; i < newTupDesc->natts; i++)
		{
			if (TupleDescAttr(newTupDesc, i)->attisdropped)
				dropped_attrs = lappend_int(dropped_attrs, i);
		}

		/*
		 * Scan through the rows, generating a new row if needed and then
		 * checking all the constraints.
		 */
		snapshot = RegisterSnapshot(GetLatestSnapshot());
		/*
		 * For YB relations under going a table rewrite due to ALTER TYPE,
		 * set up the YB scan using the old tuple desc.
		 */
		if (IsYBRelation(oldrel) && tab->rewrite & AT_REWRITE_COLUMN_REWRITE)
			oldrel->rd_att = oldTupDesc;
		scan = table_beginscan(oldrel, snapshot, 0, NULL);

		/*
		 * Switch to per-tuple memory context and reset it for each tuple
		 * produced, so we don't leak memory.
		 */
		oldCxt = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

		while (table_scan_getnextslot(scan, ForwardScanDirection, oldslot))
		{
			TupleTableSlot *insertslot;

			if (tab->rewrite > 0)
			{
				/* Extract data from old tuple */
				slot_getallattrs(oldslot);
				ExecClearTuple(newslot);

				/* copy attributes */
				memcpy(newslot->tts_values, oldslot->tts_values,
					   sizeof(Datum) * oldslot->tts_nvalid);
				memcpy(newslot->tts_isnull, oldslot->tts_isnull,
					   sizeof(bool) * oldslot->tts_nvalid);

				/* Set dropped attributes to null in new tuple */
				foreach(lc, dropped_attrs)
					newslot->tts_isnull[lfirst_int(lc)] = true;

				/*
				 * Constraints and GENERATED expressions might reference the
				 * tableoid column, so fill tts_tableOid with the desired
				 * value.  (We must do this each time, because it gets
				 * overwritten with newrel's OID during storing.)
				 */
				newslot->tts_tableOid = RelationGetRelid(oldrel);

				/*
				 * Process supplied expressions to replace selected columns.
				 *
				 * First, evaluate expressions whose inputs come from the old
				 * tuple.
				 */
				econtext->ecxt_scantuple = oldslot;

				foreach(l, tab->newvals)
				{
					NewColumnValue *ex = lfirst(l);

					if (ex->is_generated)
						continue;

					newslot->tts_values[ex->attnum - 1]
						= ExecEvalExpr(ex->exprstate,
									   econtext,
									   &newslot->tts_isnull[ex->attnum - 1]);
				}

				ExecStoreVirtualTuple(newslot);

				/*
				 * Now, evaluate any expressions whose inputs come from the
				 * new tuple.  We assume these columns won't reference each
				 * other, so that there's no ordering dependency.
				 */
				econtext->ecxt_scantuple = newslot;

				foreach(l, tab->newvals)
				{
					NewColumnValue *ex = lfirst(l);

					if (!ex->is_generated)
						continue;

					newslot->tts_values[ex->attnum - 1]
						= ExecEvalExpr(ex->exprstate,
									   econtext,
									   &newslot->tts_isnull[ex->attnum - 1]);
				}

				insertslot = newslot;
			}
			else
			{
				/*
				 * If there's no rewrite, old and new table are guaranteed to
				 * have the same AM, so we can just use the old slot to verify
				 * new constraints etc.
				 */
				insertslot = oldslot;
			}

			/* Now check any constraints on the possibly-changed tuple */
			econtext->ecxt_scantuple = insertslot;

			foreach(l, notnull_attrs)
			{
				int			attn = lfirst_int(l);

				if (slot_attisnull(insertslot, attn + 1))
				{
					Form_pg_attribute attr = TupleDescAttr(newTupDesc, attn);

					ereport(ERROR,
							(errcode(ERRCODE_NOT_NULL_VIOLATION),
							 errmsg("column \"%s\" of relation \"%s\" contains null values",
									NameStr(attr->attname),
									RelationGetRelationName(oldrel)),
							 errtablecol(oldrel, attn + 1)));
				}
			}

			foreach(l, tab->constraints)
			{
				NewConstraint *con = lfirst(l);

				switch (con->contype)
				{
					case CONSTR_CHECK:
						if (!ExecCheck(con->qualstate, econtext))
						{
							/* If YugaByte is enabled, the add constraint operation is not atomic.
							 * So we must delete the relevant entries from the catalog tables. */
							/*
							 * YB_TODO(fizaa) : clean this up -- this condition
							 * is incomplete as we don't want to drop the
							 * constraint if we are performing the check
							 * as part of a VALIDATE CONSTRAINT operation.
							 */
							if (IsYugaByteEnabled() && !YbDdlRollbackEnabled())
							{
								/*
								 * Even though we pass oldrel as a reference, it won't change
								 * for CHECK constraint.
								 */
								Relation oldrel_prev PG_USED_FOR_ASSERTS_ONLY = oldrel;
								ATExecDropConstraint(NULL, tab, &oldrel, con->name, DROP_RESTRICT, true,
													 false, false, lockmode);
								Assert(oldrel == oldrel_prev);
							}
							ereport(ERROR,
									(errcode(ERRCODE_CHECK_VIOLATION),
									 errmsg("check constraint \"%s\" of relation \"%s\" is violated by some row",
											con->name,
											RelationGetRelationName(oldrel)),
									 errtableconstraint(oldrel, con->name)));
						}
						break;
					case CONSTR_FOREIGN:
						/* Nothing to do here */
						break;
					default:
						elog(ERROR, "unrecognized constraint type: %d",
							 (int) con->contype);
				}
			}

			if (partqualstate && !ExecCheck(partqualstate, econtext))
			{
				if (tab->validate_default)
					ereport(ERROR,
							(errcode(ERRCODE_CHECK_VIOLATION),
							 errmsg("updated partition constraint for default partition \"%s\" would be violated by some row",
									RelationGetRelationName(oldrel)),
							 errtable(oldrel)));
				else
					ereport(ERROR,
							(errcode(ERRCODE_CHECK_VIOLATION),
							 errmsg("partition constraint of relation \"%s\" is violated by some row",
									RelationGetRelationName(oldrel)),
							 errtable(oldrel)));
			}

			/* Write the tuple out to the new relation */
			if (newrel)
			{
				if (IsYBRelation(newrel))
					YBCExecuteInsert(newrel,
					                 newslot,
									 ONCONFLICT_NONE);
				else
					table_tuple_insert(newrel, insertslot, mycid,
									   ti_options, bistate);
			}

			ResetExprContext(econtext);

			CHECK_FOR_INTERRUPTS();
		}

		MemoryContextSwitchTo(oldCxt);
		table_endscan(scan);
		UnregisterSnapshot(snapshot);

		if (IsYBRelation(oldrel) && tab->rewrite & AT_REWRITE_COLUMN_REWRITE)
			/* Revert back to the new tuple desc */
			oldrel->rd_att = newTupDesc;

		ExecDropSingleTupleTableSlot(oldslot);
		if (newslot)
			ExecDropSingleTupleTableSlot(newslot);
	}

	FreeExecutorState(estate);

	table_close(oldrel, NoLock);
	if (newrel)
	{
		FreeBulkInsertState(bistate);

		table_finish_bulk_insert(newrel, ti_options);

		table_close(newrel, NoLock);
	}
}

/*
 * ATGetQueueEntry: find or create an entry in the ALTER TABLE work queue
 */
static AlteredTableInfo *
ATGetQueueEntry(List **wqueue, Relation rel)
{
	Oid			relid = RelationGetRelid(rel);
	AlteredTableInfo *tab;
	ListCell   *ltab;

	foreach(ltab, *wqueue)
	{
		tab = (AlteredTableInfo *) lfirst(ltab);
		if (tab->relid == relid)
			return tab;
	}

	/*
	 * Not there, so add it.  Note that we make a copy of the relation's
	 * existing descriptor before anything interesting can happen to it.
	 */
	tab = (AlteredTableInfo *) palloc0(sizeof(AlteredTableInfo));
	tab->relid = relid;
	tab->rel = NULL;			/* set later */
	tab->relkind = rel->rd_rel->relkind;
	tab->oldDesc = CreateTupleDescCopyConstr(RelationGetDescr(rel));
	tab->newAccessMethod = InvalidOid;
	tab->newTableSpace = InvalidOid;
	tab->newrelpersistence = RELPERSISTENCE_PERMANENT;
	tab->chgPersistence = false;

	*wqueue = lappend(*wqueue, tab);

	return tab;
}

static const char *
alter_table_type_to_string(AlterTableType cmdtype)
{
	switch (cmdtype)
	{
		case AT_AddColumn:
		case AT_AddColumnRecurse:
		case AT_AddColumnToView:
			return "ADD COLUMN";
		case AT_ColumnDefault:
		case AT_CookedColumnDefault:
			return "ALTER COLUMN ... SET DEFAULT";
		case AT_DropNotNull:
			return "ALTER COLUMN ... DROP NOT NULL";
		case AT_SetNotNull:
			return "ALTER COLUMN ... SET NOT NULL";
		case AT_DropExpression:
			return "ALTER COLUMN ... DROP EXPRESSION";
		case AT_CheckNotNull:
			return NULL;		/* not real grammar */
		case AT_SetStatistics:
			return "ALTER COLUMN ... SET STATISTICS";
		case AT_SetOptions:
			return "ALTER COLUMN ... SET";
		case AT_ResetOptions:
			return "ALTER COLUMN ... RESET";
		case AT_SetStorage:
			return "ALTER COLUMN ... SET STORAGE";
		case AT_SetCompression:
			return "ALTER COLUMN ... SET COMPRESSION";
		case AT_DropColumn:
		case AT_DropColumnRecurse:
			return "DROP COLUMN";
		case AT_AddIndex:
		case AT_ReAddIndex:
			return NULL;		/* not real grammar */
		case AT_AddConstraint:
		case AT_AddConstraintRecurse:
		case AT_ReAddConstraint:
		case AT_ReAddDomainConstraint:
		case AT_AddIndexConstraint:
			return "ADD CONSTRAINT";
		case AT_AlterConstraint:
			return "ALTER CONSTRAINT";
		case AT_ValidateConstraint:
		case AT_ValidateConstraintRecurse:
			return "VALIDATE CONSTRAINT";
		case AT_DropConstraint:
		case AT_DropConstraintRecurse:
			return "DROP CONSTRAINT";
		case AT_ReAddComment:
			return NULL;		/* not real grammar */
		case AT_AlterColumnType:
			return "ALTER COLUMN ... SET DATA TYPE";
		case AT_AlterColumnGenericOptions:
			return "ALTER COLUMN ... OPTIONS";
		case AT_ChangeOwner:
			return "OWNER TO";
		case AT_ClusterOn:
			return "CLUSTER ON";
		case AT_DropCluster:
			return "SET WITHOUT CLUSTER";
		case AT_SetAccessMethod:
			return "SET ACCESS METHOD";
		case AT_SetLogged:
			return "SET LOGGED";
		case AT_SetUnLogged:
			return "SET UNLOGGED";
		case AT_DropOids:
			return "SET WITHOUT OIDS";
		case AT_SetTableSpace:
			return "SET TABLESPACE";
		case AT_SetRelOptions:
			return "SET";
		case AT_ResetRelOptions:
			return "RESET";
		case AT_ReplaceRelOptions:
			return NULL;		/* not real grammar */
		case AT_EnableTrig:
			return "ENABLE TRIGGER";
		case AT_EnableAlwaysTrig:
			return "ENABLE ALWAYS TRIGGER";
		case AT_EnableReplicaTrig:
			return "ENABLE REPLICA TRIGGER";
		case AT_DisableTrig:
			return "DISABLE TRIGGER";
		case AT_EnableTrigAll:
			return "ENABLE TRIGGER ALL";
		case AT_DisableTrigAll:
			return "DISABLE TRIGGER ALL";
		case AT_EnableTrigUser:
			return "ENABLE TRIGGER USER";
		case AT_DisableTrigUser:
			return "DISABLE TRIGGER USER";
		case AT_EnableRule:
			return "ENABLE RULE";
		case AT_EnableAlwaysRule:
			return "ENABLE ALWAYS RULE";
		case AT_EnableReplicaRule:
			return "ENABLE REPLICA RULE";
		case AT_DisableRule:
			return "DISABLE RULE";
		case AT_AddInherit:
			return "INHERIT";
		case AT_DropInherit:
			return "NO INHERIT";
		case AT_AddOf:
			return "OF";
		case AT_DropOf:
			return "NOT OF";
		case AT_ReplicaIdentity:
			return "REPLICA IDENTITY";
		case AT_EnableRowSecurity:
			return "ENABLE ROW SECURITY";
		case AT_DisableRowSecurity:
			return "DISABLE ROW SECURITY";
		case AT_ForceRowSecurity:
			return "FORCE ROW SECURITY";
		case AT_NoForceRowSecurity:
			return "NO FORCE ROW SECURITY";
		case AT_GenericOptions:
			return "OPTIONS";
		case AT_AttachPartition:
			return "ATTACH PARTITION";
		case AT_DetachPartition:
			return "DETACH PARTITION";
		case AT_DetachPartitionFinalize:
			return "DETACH PARTITION ... FINALIZE";
		case AT_AddIdentity:
			return "ALTER COLUMN ... ADD IDENTITY";
		case AT_SetIdentity:
			return "ALTER COLUMN ... SET";
		case AT_DropIdentity:
			return "ALTER COLUMN ... DROP IDENTITY";
		case AT_ReAddStatistics:
			return NULL;		/* not real grammar */
	}

	return NULL;
}

/*
 * ATSimplePermissions
 *
 * - Ensure that it is a relation (or possibly a view)
 * - Ensure this user is the owner
 * - Ensure that it is not a system table
 */
static void
ATSimplePermissions(AlterTableType cmdtype, Relation rel, int allowed_targets)
{
	int			actual_target;

	switch (rel->rd_rel->relkind)
	{
		case RELKIND_RELATION:
		case RELKIND_PARTITIONED_TABLE:
			actual_target = ATT_TABLE;
			break;
		case RELKIND_VIEW:
			actual_target = ATT_VIEW;
			break;
		case RELKIND_MATVIEW:
			actual_target = ATT_MATVIEW;
			break;
		case RELKIND_INDEX:
			actual_target = ATT_INDEX;
			break;
		case RELKIND_PARTITIONED_INDEX:
			actual_target = ATT_PARTITIONED_INDEX;
			break;
		case RELKIND_COMPOSITE_TYPE:
			actual_target = ATT_COMPOSITE_TYPE;
			break;
		case RELKIND_FOREIGN_TABLE:
			actual_target = ATT_FOREIGN_TABLE;
			break;
		case RELKIND_SEQUENCE:
			actual_target = ATT_SEQUENCE;
			break;
		default:
			actual_target = 0;
			break;
	}

	/* Wrong target type? */
	if ((actual_target & allowed_targets) == 0)
	{
		const char *action_str = alter_table_type_to_string(cmdtype);

		if (action_str)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
			/* translator: %s is a group of some SQL keywords */
					 errmsg("ALTER action %s cannot be performed on relation \"%s\"",
							action_str, RelationGetRelationName(rel)),
					 errdetail_relkind_not_supported(rel->rd_rel->relkind)));
		else
			/* internal error? */
			elog(ERROR, "invalid ALTER action attempted on relation \"%s\"",
				 RelationGetRelationName(rel));
	}

	/* Permissions checks */
	if (!pg_class_ownercheck(RelationGetRelid(rel), GetUserId()) && !IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(rel->rd_rel->relkind),
					   RelationGetRelationName(rel));

	if (!allowSystemTableMods && IsSystemRelation(rel))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						RelationGetRelationName(rel))));
}

/*
 * ATSimpleRecursion
 *
 * Simple table recursion sufficient for most ALTER TABLE operations.
 * All direct and indirect children are processed in an unspecified order.
 * Note that if a child inherits from the original table via multiple
 * inheritance paths, it will be visited just once.
 */
static void
ATSimpleRecursion(List **wqueue, Relation rel,
				  AlterTableCmd *cmd, bool recurse, LOCKMODE lockmode,
				  AlterTableUtilityContext *context)
{
	/*
	 * Propagate to children, if desired and if there are (or might be) any
	 * children.
	 */
	if (recurse && rel->rd_rel->relhassubclass)
	{
		Oid			relid = RelationGetRelid(rel);
		ListCell   *child;
		List	   *children;

		children = find_all_inheritors(relid, lockmode, NULL);

		/*
		 * find_all_inheritors does the recursive search of the inheritance
		 * hierarchy, so all we have to do is process all of the relids in the
		 * list that it returns.
		 */
		foreach(child, children)
		{
			Oid			childrelid = lfirst_oid(child);
			Relation	childrel;

			if (childrelid == relid)
				continue;
			/* find_all_inheritors already got lock */
			childrel = relation_open(childrelid, NoLock);
			CheckTableNotInUse(childrel, "ALTER TABLE");
			ATPrepCmd(wqueue, childrel, cmd, false, true, lockmode, context);
			relation_close(childrel, NoLock);
		}
	}
}

/*
 * Obtain list of partitions of the given table, locking them all at the given
 * lockmode and ensuring that they all pass CheckTableNotInUse.
 *
 * This function is a no-op if the given relation is not a partitioned table;
 * in particular, nothing is done if it's a legacy inheritance parent.
 */
static void
ATCheckPartitionsNotInUse(Relation rel, LOCKMODE lockmode)
{
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		List	   *inh;
		ListCell   *cell;

		inh = find_all_inheritors(RelationGetRelid(rel), lockmode, NULL);
		/* first element is the parent rel; must ignore it */
		for_each_from(cell, inh, 1)
		{
			Relation	childrel;

			/* find_all_inheritors already got lock */
			childrel = table_open(lfirst_oid(cell), NoLock);
			CheckTableNotInUse(childrel, "ALTER TABLE");
			table_close(childrel, NoLock);
		}
		list_free(inh);
	}
}

/*
 * ATTypedTableRecursion
 *
 * Propagate ALTER TYPE operations to the typed tables of that type.
 * Also check the RESTRICT/CASCADE behavior.  Given CASCADE, also permit
 * recursion to inheritance children of the typed tables.
 */
static void
ATTypedTableRecursion(List **wqueue, Relation rel, AlterTableCmd *cmd,
					  LOCKMODE lockmode, AlterTableUtilityContext *context)
{
	ListCell   *child;
	List	   *children;

	Assert(rel->rd_rel->relkind == RELKIND_COMPOSITE_TYPE);

	children = find_typed_table_dependencies(rel->rd_rel->reltype,
											 RelationGetRelationName(rel),
											 cmd->behavior);

	foreach(child, children)
	{
		Oid			childrelid = lfirst_oid(child);
		Relation	childrel;

		childrel = relation_open(childrelid, lockmode);
		CheckTableNotInUse(childrel, "ALTER TABLE");
		ATPrepCmd(wqueue, childrel, cmd, true, true, lockmode, context);
		relation_close(childrel, NoLock);
	}
}


/*
 * find_composite_type_dependencies
 *
 * Check to see if the type "typeOid" is being used as a column in some table
 * (possibly nested several levels deep in composite types, arrays, etc!).
 * Eventually, we'd like to propagate the check or rewrite operation
 * into such tables, but for now, just error out if we find any.
 *
 * Caller should provide either the associated relation of a rowtype,
 * or a type name (not both) for use in the error message, if any.
 *
 * Note that "typeOid" is not necessarily a composite type; it could also be
 * another container type such as an array or range, or a domain over one of
 * these things.  The name of this function is therefore somewhat historical,
 * but it's not worth changing.
 *
 * We assume that functions and views depending on the type are not reasons
 * to reject the ALTER.  (How safe is this really?)
 */
void
find_composite_type_dependencies(Oid typeOid, Relation origRelation,
								 const char *origTypeName)
{
	Relation	depRel;
	ScanKeyData key[2];
	SysScanDesc depScan;
	HeapTuple	depTup;

	/* since this function recurses, it could be driven to stack overflow */
	check_stack_depth();

	/*
	 * We scan pg_depend to find those things that depend on the given type.
	 * (We assume we can ignore refobjsubid for a type.)
	 */
	depRel = table_open(DependRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_depend_refclassid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(TypeRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_refobjid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(typeOid));

	depScan = systable_beginscan(depRel, DependReferenceIndexId, true,
								 NULL, 2, key);

	while (HeapTupleIsValid(depTup = systable_getnext(depScan)))
	{
		Form_pg_depend pg_depend = (Form_pg_depend) GETSTRUCT(depTup);
		Relation	rel;
		Form_pg_attribute att;

		/* Check for directly dependent types */
		if (pg_depend->classid == TypeRelationId)
		{
			/*
			 * This must be an array, domain, or range containing the given
			 * type, so recursively check for uses of this type.  Note that
			 * any error message will mention the original type not the
			 * container; this is intentional.
			 */
			find_composite_type_dependencies(pg_depend->objid,
											 origRelation, origTypeName);
			continue;
		}

		/* Else, ignore dependees that aren't user columns of relations */
		/* (we assume system columns are never of interesting types) */
		if (pg_depend->classid != RelationRelationId ||
			pg_depend->objsubid <= 0)
			continue;

		rel = relation_open(pg_depend->objid, AccessShareLock);
		att = TupleDescAttr(rel->rd_att, pg_depend->objsubid - 1);

		if (rel->rd_rel->relkind == RELKIND_RELATION ||
			rel->rd_rel->relkind == RELKIND_MATVIEW ||
			rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		{
			if (origTypeName)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type \"%s\" because column \"%s.%s\" uses it",
								origTypeName,
								RelationGetRelationName(rel),
								NameStr(att->attname))));
			else if (origRelation->rd_rel->relkind == RELKIND_COMPOSITE_TYPE)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type \"%s\" because column \"%s.%s\" uses it",
								RelationGetRelationName(origRelation),
								RelationGetRelationName(rel),
								NameStr(att->attname))));
			else if (origRelation->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter foreign table \"%s\" because column \"%s.%s\" uses its row type",
								RelationGetRelationName(origRelation),
								RelationGetRelationName(rel),
								NameStr(att->attname))));
			else
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter table \"%s\" because column \"%s.%s\" uses its row type",
								RelationGetRelationName(origRelation),
								RelationGetRelationName(rel),
								NameStr(att->attname))));
		}
		else if (OidIsValid(rel->rd_rel->reltype))
		{
			/*
			 * A view or composite type itself isn't a problem, but we must
			 * recursively check for indirect dependencies via its rowtype.
			 */
			find_composite_type_dependencies(rel->rd_rel->reltype,
											 origRelation, origTypeName);
		}

		relation_close(rel, AccessShareLock);
	}

	systable_endscan(depScan);

	relation_close(depRel, AccessShareLock);
}


/*
 * find_typed_table_dependencies
 *
 * Check to see if a composite type is being used as the type of a
 * typed table.  Abort if any are found and behavior is RESTRICT.
 * Else return the list of tables.
 */
static List *
find_typed_table_dependencies(Oid typeOid, const char *typeName, DropBehavior behavior)
{
	Relation	classRel;
	ScanKeyData key[1];
	TableScanDesc scan;
	HeapTuple	tuple;
	List	   *result = NIL;

	classRel = table_open(RelationRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_class_reloftype,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(typeOid));

	scan = table_beginscan_catalog(classRel, 1, key);

	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Form_pg_class classform = (Form_pg_class) GETSTRUCT(tuple);

		if (behavior == DROP_RESTRICT)
			ereport(ERROR,
					(errcode(ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST),
					 errmsg("cannot alter type \"%s\" because it is the type of a typed table",
							typeName),
					 errhint("Use ALTER ... CASCADE to alter the typed tables too.")));
		else
			result = lappend_oid(result, classform->oid);
	}

	table_endscan(scan);
	table_close(classRel, AccessShareLock);

	return result;
}


/*
 * check_of_type
 *
 * Check whether a type is suitable for CREATE TABLE OF/ALTER TABLE OF.  If it
 * isn't suitable, throw an error.  Currently, we require that the type
 * originated with CREATE TYPE AS.  We could support any row type, but doing so
 * would require handling a number of extra corner cases in the DDL commands.
 * (Also, allowing domain-over-composite would open up a can of worms about
 * whether and how the domain's constraints should apply to derived tables.)
 */
void
check_of_type(HeapTuple typetuple)
{
	Form_pg_type typ = (Form_pg_type) GETSTRUCT(typetuple);
	bool		typeOk = false;

	if (typ->typtype == TYPTYPE_COMPOSITE)
	{
		Relation	typeRelation;

		Assert(OidIsValid(typ->typrelid));
		typeRelation = relation_open(typ->typrelid, AccessShareLock);
		typeOk = (typeRelation->rd_rel->relkind == RELKIND_COMPOSITE_TYPE);

		/*
		 * Close the parent rel, but keep our AccessShareLock on it until xact
		 * commit.  That will prevent someone else from deleting or ALTERing
		 * the type before the typed table creation/conversion commits.
		 */
		relation_close(typeRelation, NoLock);
	}
	if (!typeOk)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("type %s is not a composite type",
						format_type_be(typ->oid))));
}


/*
 * ALTER TABLE ADD COLUMN
 *
 * Adds an additional attribute to a relation making the assumption that
 * CHECK, NOT NULL, and FOREIGN KEY constraints will be removed from the
 * AT_AddColumn AlterTableCmd by parse_utilcmd.c and added as independent
 * AlterTableCmd's.
 *
 * ADD COLUMN cannot use the normal ALTER TABLE recursion mechanism, because we
 * have to decide at runtime whether to recurse or not depending on whether we
 * actually add a column or merely merge with an existing column.  (We can't
 * check this in a static pre-pass because it won't handle multiple inheritance
 * situations correctly.)
 */
static void
ATPrepAddColumn(List **wqueue, Relation rel, bool recurse, bool recursing,
				bool is_view, AlterTableCmd *cmd, LOCKMODE lockmode,
				AlterTableUtilityContext *context)
{
	if (rel->rd_rel->reloftype && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot add column to typed table")));

	if (rel->rd_rel->relkind == RELKIND_COMPOSITE_TYPE)
		ATTypedTableRecursion(wqueue, rel, cmd, lockmode, context);

	if (recurse && !is_view)
		cmd->subtype = AT_AddColumnRecurse;
}

/*
 * Add a column to a table.  The return value is the address of the
 * new column in the parent relation.
 *
 * cmd is pass-by-ref so that we can replace it with the parse-transformed
 * copy (but that happens only after we check for IF NOT EXISTS).
 */
static ObjectAddress
ATExecAddColumn(List **wqueue, AlteredTableInfo *tab, Relation rel,
				AlterTableCmd **cmd,
				bool recurse, bool recursing,
				LOCKMODE lockmode, int cur_pass,
				AlterTableUtilityContext *context)
{
	Oid			myrelid = RelationGetRelid(rel);
	ColumnDef  *colDef = castNode(ColumnDef, (*cmd)->def);
	bool		if_not_exists = (*cmd)->missing_ok;
	Relation	pgclass,
				attrdesc;
	HeapTuple	reltup;
	FormData_pg_attribute attribute;
	int			newattnum;
	char		relkind;
	HeapTuple	typeTuple;
	Oid			typeOid;
	int32		typmod;
	Oid			collOid;
	Form_pg_type tform;
	Expr	   *defval;
	List	   *children;
	ListCell   *child;
	AlterTableCmd *childcmd;
	AclResult	aclresult;
	ObjectAddress address;
	TupleDesc	tupdesc;
	FormData_pg_attribute *aattr[] = {&attribute};

	/* At top level, permission check was done in ATPrepCmd, else do it */
	if (recursing)
		ATSimplePermissions((*cmd)->subtype, rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	if (rel->rd_rel->relispartition && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot add column to a partition")));

	attrdesc = table_open(AttributeRelationId, RowExclusiveLock);

	/*
	 * Are we adding the column to a recursion child?  If so, check whether to
	 * merge with an existing definition for the column.  If we do merge, we
	 * must not recurse.  Children will already have the column, and recursing
	 * into them would mess up attinhcount.
	 */
	if (colDef->inhcount > 0)
	{
		HeapTuple	tuple;

		/* Does child already have a column by this name? */
		tuple = SearchSysCacheCopyAttName(myrelid, colDef->colname);
		if (HeapTupleIsValid(tuple))
		{
			Form_pg_attribute childatt = (Form_pg_attribute) GETSTRUCT(tuple);
			Oid			ctypeId;
			int32		ctypmod;
			Oid			ccollid;

			/* Child column must match on type, typmod, and collation */
			typenameTypeIdAndMod(NULL, colDef->typeName, &ctypeId, &ctypmod);
			if (ctypeId != childatt->atttypid ||
				ctypmod != childatt->atttypmod)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("child table \"%s\" has different type for column \"%s\"",
								RelationGetRelationName(rel), colDef->colname)));
			ccollid = GetColumnDefCollation(NULL, colDef, ctypeId);
			if (ccollid != childatt->attcollation)
				ereport(ERROR,
						(errcode(ERRCODE_COLLATION_MISMATCH),
						 errmsg("child table \"%s\" has different collation for column \"%s\"",
								RelationGetRelationName(rel), colDef->colname),
						 errdetail("\"%s\" versus \"%s\"",
								   get_collation_name(ccollid),
								   get_collation_name(childatt->attcollation))));

			/* Bump the existing child att's inhcount */
			childatt->attinhcount++;
			CatalogTupleUpdate(attrdesc, &tuple->t_self, tuple);

			heap_freetuple(tuple);

			/* Inform the user about the merge */
			ereport(NOTICE,
					(errmsg("merging definition of column \"%s\" for child \"%s\"",
							colDef->colname, RelationGetRelationName(rel))));

			table_close(attrdesc, RowExclusiveLock);
			return InvalidObjectAddress;
		}
	}

	/* skip if the name already exists and if_not_exists is true */
	if (!check_for_column_name_collision(rel, colDef->colname, if_not_exists))
	{
		table_close(attrdesc, RowExclusiveLock);
		return InvalidObjectAddress;
	}

	/*
	 * Okay, we need to add the column, so go ahead and do parse
	 * transformation.  This can result in queueing up, or even immediately
	 * executing, subsidiary operations (such as creation of unique indexes);
	 * so we mustn't do it until we have made the if_not_exists check.
	 *
	 * When recursing, the command was already transformed and we needn't do
	 * so again.  Also, if context isn't given we can't transform.  (That
	 * currently happens only for AT_AddColumnToView; we expect that view.c
	 * passed us a ColumnDef that doesn't need work.)
	 */
	if (context != NULL && !recursing)
	{
		*cmd = ATParseTransformCmd(wqueue, tab, rel, *cmd, recurse, lockmode,
								   cur_pass, context);
		Assert(*cmd != NULL);
		colDef = castNode(ColumnDef, (*cmd)->def);
	}

	/*
	 * Cannot add identity column if table has children, because identity does
	 * not inherit.  (Adding column and identity separately will work.)
	 */
	if (colDef->identity &&
		recurse &&
		find_inheritance_children(myrelid, NoLock) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot recursively add identity column to table that has child tables")));

	pgclass = table_open(RelationRelationId, RowExclusiveLock);

	reltup = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(myrelid));
	if (!HeapTupleIsValid(reltup))
		elog(ERROR, "cache lookup failed for relation %u", myrelid);
	relkind = ((Form_pg_class) GETSTRUCT(reltup))->relkind;

	/* Determine the new attribute's number */
	newattnum = ((Form_pg_class) GETSTRUCT(reltup))->relnatts + 1;
	if (newattnum > MaxHeapAttributeNumber)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_COLUMNS),
				 errmsg("tables can have at most %d columns",
						MaxHeapAttributeNumber)));

	typeTuple = typenameType(NULL, colDef->typeName, &typmod);
	tform = (Form_pg_type) GETSTRUCT(typeTuple);
	typeOid = tform->oid;

	aclresult = pg_type_aclcheck(typeOid, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, typeOid);

	collOid = GetColumnDefCollation(NULL, colDef, typeOid);

	/* make sure datatype is legal for a column */
	CheckAttributeType(colDef->colname, typeOid, collOid,
					   list_make1_oid(rel->rd_rel->reltype),
					   0);

	/*
	 * Construct new attribute's pg_attribute entry.  (Variable-length fields
	 * are handled by InsertPgAttributeTuples().)
	 */
	attribute.attrelid = myrelid;
	namestrcpy(&(attribute.attname), colDef->colname);
	attribute.atttypid = typeOid;
	attribute.attstattarget = (newattnum > 0) ? -1 : 0;
	attribute.attlen = tform->typlen;
	attribute.attnum = newattnum;
	attribute.attndims = list_length(colDef->typeName->arrayBounds);
	attribute.atttypmod = typmod;
	attribute.attbyval = tform->typbyval;
	attribute.attalign = tform->typalign;
	attribute.attstorage = tform->typstorage;
	attribute.attcompression = GetAttributeCompression(typeOid,
													   colDef->compression);
	attribute.attnotnull = colDef->is_not_null;
	attribute.atthasdef = false;
	attribute.atthasmissing = false;
	attribute.attidentity = colDef->identity;
	attribute.attgenerated = colDef->generated;
	attribute.attisdropped = false;
	attribute.attislocal = colDef->is_local;
	attribute.attinhcount = colDef->inhcount;
	attribute.attcollation = collOid;

	ReleaseSysCache(typeTuple);

	tupdesc = CreateTupleDesc(lengthof(aattr), (FormData_pg_attribute **) &aattr);

	InsertPgAttributeTuples(attrdesc, tupdesc, myrelid, NULL, NULL,
							(((Form_pg_class) GETSTRUCT(reltup))->relisshared &&
							 !IsBootstrapProcessingMode()));


	table_close(attrdesc, RowExclusiveLock);

	/*
	 * Update pg_class tuple as appropriate
	 */
	((Form_pg_class) GETSTRUCT(reltup))->relnatts = newattnum;

	CatalogTupleUpdate(pgclass, &reltup->t_self, reltup);

	heap_freetuple(reltup);

	/* Post creation hook for new attribute */
	InvokeObjectPostCreateHook(RelationRelationId, myrelid, newattnum);

	table_close(pgclass, RowExclusiveLock);

	/* Make the attribute's catalog entry visible */
	CommandCounterIncrement();

	/*
	 * Store the DEFAULT, if any, in the catalogs
	 */
	if (colDef->raw_default)
	{
		RawColumnDefault *rawEnt;

		rawEnt = (RawColumnDefault *) palloc(sizeof(RawColumnDefault));
		rawEnt->attnum = attribute.attnum;
		rawEnt->raw_default = copyObject(colDef->raw_default);

		/*
		 * Attempt to skip a complete table rewrite by storing the specified
		 * DEFAULT value outside of the heap.  This may be disabled inside
		 * AddRelationNewConstraints if the optimization cannot be applied.
		 */
		rawEnt->missingMode = (!colDef->generated);

		rawEnt->generated = colDef->generated;

		/*
		 * This function is intended for CREATE TABLE, so it processes a
		 * _list_ of defaults, but we just do one.
		 */
		AddRelationNewConstraints(rel, list_make1(rawEnt), NIL,
								  false, true, false, NULL);

		/* Make the additional catalog changes visible */
		CommandCounterIncrement();

		/*
		 * Did the request for a missing value work? If not we'll have to do a
		 * rewrite
		 */
		if (!rawEnt->missingMode)
			tab->rewrite |= AT_REWRITE_DEFAULT_VAL;
	}

	/*
	 * Tell Phase 3 to fill in the default expression, if there is one.
	 *
	 * If there is no default, Phase 3 doesn't have to do anything, because
	 * that effectively means that the default is NULL.  The heap tuple access
	 * routines always check for attnum > # of attributes in tuple, and return
	 * NULL if so, so without any modification of the tuple data we will get
	 * the effect of NULL values in the new column.
	 *
	 * An exception occurs when the new column is of a domain type: the domain
	 * might have a NOT NULL constraint, or a check constraint that indirectly
	 * rejects nulls.  If there are any domain constraints then we construct
	 * an explicit NULL default value that will be passed through
	 * CoerceToDomain processing.  (This is a tad inefficient, since it causes
	 * rewriting the table which we really don't have to do, but the present
	 * design of domain processing doesn't offer any simple way of checking
	 * the constraints more directly.)
	 *
	 * Note: we use build_column_default, and not just the cooked default
	 * returned by AddRelationNewConstraints, so that the right thing happens
	 * when a datatype's default applies.
	 *
	 * Note: it might seem that this should happen at the end of Phase 2, so
	 * that the effects of subsequent subcommands can be taken into account.
	 * It's intentional that we do it now, though.  The new column should be
	 * filled according to what is said in the ADD COLUMN subcommand, so that
	 * the effects are the same as if this subcommand had been run by itself
	 * and the later subcommands had been issued in new ALTER TABLE commands.
	 *
	 * We can skip this entirely for relations without storage, since Phase 3
	 * is certainly not going to touch them.  System attributes don't have
	 * interesting defaults, either.
	 */
	if (RELKIND_HAS_STORAGE(relkind) && attribute.attnum > 0)
	{
		/*
		 * For an identity column, we can't use build_column_default(),
		 * because the sequence ownership isn't set yet.  So do it manually.
		 */
		if (colDef->identity)
		{
			NextValueExpr *nve = makeNode(NextValueExpr);

			nve->seqid = RangeVarGetRelid(colDef->identitySequence, NoLock, false);
			nve->typeId = typeOid;

			defval = (Expr *) nve;

			/* must do a rewrite for identity columns */
			tab->rewrite |= AT_REWRITE_DEFAULT_VAL;
		}
		else
			defval = (Expr *) build_column_default(rel, attribute.attnum);

		if (!defval && DomainHasConstraints(typeOid))
		{
			Oid			baseTypeId;
			int32		baseTypeMod;
			Oid			baseTypeColl;

			baseTypeMod = typmod;
			baseTypeId = getBaseTypeAndTypmod(typeOid, &baseTypeMod);
			baseTypeColl = get_typcollation(baseTypeId);
			defval = (Expr *) makeNullConst(baseTypeId, baseTypeMod, baseTypeColl);
			defval = (Expr *) coerce_to_target_type(NULL,
													(Node *) defval,
													baseTypeId,
													typeOid,
													typmod,
													COERCION_ASSIGNMENT,
													COERCE_IMPLICIT_CAST,
													-1);
			if (defval == NULL) /* should not happen */
				elog(ERROR, "failed to coerce base type to domain");
		}

		if (defval)
		{
			NewColumnValue *newval;

			newval = (NewColumnValue *) palloc0(sizeof(NewColumnValue));
			newval->attnum = attribute.attnum;
			newval->expr = expression_planner(defval);
			newval->is_generated = (colDef->generated != '\0');

			tab->newvals = lappend(tab->newvals, newval);
		}

		if (DomainHasConstraints(typeOid))
			tab->rewrite |= AT_REWRITE_DEFAULT_VAL;

		if (!TupleDescAttr(rel->rd_att, attribute.attnum - 1)->atthasmissing)
		{
			/*
			 * If the new column is NOT NULL, and there is no missing value,
			 * tell Phase 3 it needs to check for NULLs.
			 */
			tab->verify_new_notnull |= colDef->is_not_null;
		}
	}

	/*
	 * Add needed dependency entries for the new column.
	 */
	add_column_datatype_dependency(myrelid, newattnum, attribute.atttypid);
	add_column_collation_dependency(myrelid, newattnum, attribute.attcollation);

	/*
	 * Propagate to children as appropriate.  Unlike most other ALTER
	 * routines, we have to do this one level of recursion at a time; we can't
	 * use find_all_inheritors to do it in one pass.
	 */
	children =
		find_inheritance_children(RelationGetRelid(rel), lockmode);

	/*
	 * If we are told not to recurse, there had better not be any child
	 * tables; else the addition would put them out of step.
	 */
	if (children && !recurse)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("column must be added to child tables too")));

	/* Children should see column as singly inherited */
	if (!recursing)
	{
		childcmd = copyObject(*cmd);
		colDef = castNode(ColumnDef, childcmd->def);
		colDef->inhcount = 1;
		colDef->is_local = false;
	}
	else
		childcmd = *cmd;		/* no need to copy again */

	foreach(child, children)
	{
		Oid			childrelid = lfirst_oid(child);
		Relation	childrel;
		AlteredTableInfo *childtab;

		/* find_inheritance_children already got lock */
		childrel = table_open(childrelid, NoLock);
		CheckTableNotInUse(childrel, "ALTER TABLE");

		/* Find or create work queue entry for this table */
		childtab = ATGetQueueEntry(wqueue, childrel);

		/* Recurse to child; return value is ignored */
		ATExecAddColumn(wqueue, childtab, childrel,
						&childcmd, recurse, true,
						lockmode, cur_pass, context);

		table_close(childrel, NoLock);
	}

	ObjectAddressSubSet(address, RelationRelationId, myrelid, newattnum);
	return address;
}

/*
 * If a new or renamed column will collide with the name of an existing
 * column and if_not_exists is false then error out, else do nothing.
 */
static bool
check_for_column_name_collision(Relation rel, const char *colname,
								bool if_not_exists)
{
	HeapTuple	attTuple;
	int			attnum;

	/*
	 * this test is deliberately not attisdropped-aware, since if one tries to
	 * add a column matching a dropped column name, it's gonna fail anyway.
	 */
	attTuple = SearchSysCache2(ATTNAME,
							   ObjectIdGetDatum(RelationGetRelid(rel)),
							   PointerGetDatum(colname));
	if (!HeapTupleIsValid(attTuple))
		return true;

	attnum = ((Form_pg_attribute) GETSTRUCT(attTuple))->attnum;
	ReleaseSysCache(attTuple);

	/*
	 * We throw a different error message for conflicts with system column
	 * names, since they are normally not shown and the user might otherwise
	 * be confused about the reason for the conflict.
	 */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_COLUMN),
				 errmsg("column name \"%s\" conflicts with a system column name",
						colname)));
	else
	{
		if (if_not_exists)
		{
			ereport(NOTICE,
					(errcode(ERRCODE_DUPLICATE_COLUMN),
					 errmsg("column \"%s\" of relation \"%s\" already exists, skipping",
							colname, RelationGetRelationName(rel))));
			return false;
		}

		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" already exists",
						colname, RelationGetRelationName(rel))));
	}

	return true;
}

/*
 * Install a column's dependency on its datatype.
 */
static void
add_column_datatype_dependency(Oid relid, int32 attnum, Oid typid)
{
	ObjectAddress myself,
				referenced;

	myself.classId = RelationRelationId;
	myself.objectId = relid;
	myself.objectSubId = attnum;
	referenced.classId = TypeRelationId;
	referenced.objectId = typid;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
}

/*
 * Install a column's dependency on its collation.
 */
static void
add_column_collation_dependency(Oid relid, int32 attnum, Oid collid)
{
	ObjectAddress myself,
				referenced;

	/* We know the default collation is pinned, so don't bother recording it */
	if (OidIsValid(collid) && collid != DEFAULT_COLLATION_OID)
	{
		myself.classId = RelationRelationId;
		myself.objectId = relid;
		myself.objectSubId = attnum;
		referenced.classId = CollationRelationId;
		referenced.objectId = collid;
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
	}
}

/*
 * ALTER TABLE ALTER COLUMN DROP NOT NULL
 */

static void
ATPrepDropNotNull(Relation rel, bool recurse, bool recursing)
{
	/*
	 * If the parent is a partitioned table, like check constraints, we do not
	 * support removing the NOT NULL while partitions exist.
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		PartitionDesc partdesc = RelationGetPartitionDesc(rel, true);

		Assert(partdesc != NULL);
		if (partdesc->nparts > 0 && !recurse && !recursing)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot remove constraint from only the partitioned table when partitions exist"),
					 errhint("Do not specify the ONLY keyword.")));
	}
}

/*
 * Return the address of the modified column.  If the column was already
 * nullable, InvalidObjectAddress is returned.
 */
static ObjectAddress
ATExecDropNotNull(Relation rel, const char *colName, LOCKMODE lockmode)
{
	HeapTuple	tuple;
	Form_pg_attribute attTup;
	AttrNumber	attnum;
	Relation	attr_rel;
	List	   *indexoidlist;
	ListCell   *indexoidscan;
	ObjectAddress address;

	/*
	 * lookup the attribute
	 */
	attr_rel = table_open(AttributeRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));
	attTup = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = attTup->attnum;

	/* Prevent them from altering a system attribute */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	if (attTup->attidentity)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("column \"%s\" of relation \"%s\" is an identity column",
						colName, RelationGetRelationName(rel))));

	/*
	 * Check that the attribute is not in a primary key or in an index used as
	 * a replica identity.
	 *
	 * Note: we'll throw error even if the pkey index is not valid.
	 */

	/* Loop over all indexes on the relation */
	indexoidlist = RelationGetIndexList(rel);

	foreach(indexoidscan, indexoidlist)
	{
		Oid			indexoid = lfirst_oid(indexoidscan);
		HeapTuple	indexTuple;
		Form_pg_index indexStruct;
		int			i;

		indexTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexoid));
		if (!HeapTupleIsValid(indexTuple))
			elog(ERROR, "cache lookup failed for index %u", indexoid);
		indexStruct = (Form_pg_index) GETSTRUCT(indexTuple);

		/*
		 * If the index is not a primary key or an index used as replica
		 * identity, skip the check.
		 */
		if (indexStruct->indisprimary || indexStruct->indisreplident)
		{
			/*
			 * Loop over each attribute in the primary key or the index used
			 * as replica identity and see if it matches the to-be-altered
			 * attribute.
			 */
			for (i = 0; i < indexStruct->indnkeyatts; i++)
			{
				if (indexStruct->indkey.values[i] == attnum)
				{
					if (indexStruct->indisprimary)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
								 errmsg("column \"%s\" is in a primary key",
										colName)));
					else
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
								 errmsg("column \"%s\" is in index used as replica identity",
										colName)));
				}
			}
		}

		ReleaseSysCache(indexTuple);
	}

	list_free(indexoidlist);

	/* If rel is partition, shouldn't drop NOT NULL if parent has the same */
	if (rel->rd_rel->relispartition)
	{
		Oid			parentId = get_partition_parent(RelationGetRelid(rel), false);
		Relation	parent = table_open(parentId, AccessShareLock);
		TupleDesc	tupDesc = RelationGetDescr(parent);
		AttrNumber	parent_attnum;

		parent_attnum = get_attnum(parentId, colName);
		if (TupleDescAttr(tupDesc, parent_attnum - 1)->attnotnull)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("column \"%s\" is marked NOT NULL in parent table",
							colName)));
		table_close(parent, AccessShareLock);
	}

	/*
	 * Okay, actually perform the catalog change ... if needed
	 */
	if (attTup->attnotnull)
	{
		attTup->attnotnull = false;

		CatalogTupleUpdate(attr_rel, &tuple->t_self, tuple);

		ObjectAddressSubSet(address, RelationRelationId,
							RelationGetRelid(rel), attnum);
	}
	else
		address = InvalidObjectAddress;

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel), attnum);

	table_close(attr_rel, RowExclusiveLock);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN SET NOT NULL
 */

static void
ATPrepSetNotNull(List **wqueue, Relation rel,
				 AlterTableCmd *cmd, bool recurse, bool recursing,
				 LOCKMODE lockmode, AlterTableUtilityContext *context)
{
	/*
	 * If we're already recursing, there's nothing to do; the topmost
	 * invocation of ATSimpleRecursion already visited all children.
	 */
	if (recursing)
		return;

	/*
	 * If the target column is already marked NOT NULL, we can skip recursing
	 * to children, because their columns should already be marked NOT NULL as
	 * well.  But there's no point in checking here unless the relation has
	 * some children; else we can just wait till execution to check.  (If it
	 * does have children, however, this can save taking per-child locks
	 * unnecessarily.  This greatly improves concurrency in some parallel
	 * restore scenarios.)
	 *
	 * Unfortunately, we can only apply this optimization to partitioned
	 * tables, because traditional inheritance doesn't enforce that child
	 * columns be NOT NULL when their parent is.  (That's a bug that should
	 * get fixed someday.)
	 */
	if (rel->rd_rel->relhassubclass &&
		rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		HeapTuple	tuple;
		bool		attnotnull;

		tuple = SearchSysCacheAttName(RelationGetRelid(rel), cmd->name);

		/* Might as well throw the error now, if name is bad */
		if (!HeapTupleIsValid(tuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" of relation \"%s\" does not exist",
							cmd->name, RelationGetRelationName(rel))));

		attnotnull = ((Form_pg_attribute) GETSTRUCT(tuple))->attnotnull;
		ReleaseSysCache(tuple);
		if (attnotnull)
			return;
	}

	/*
	 * If we have ALTER TABLE ONLY ... SET NOT NULL on a partitioned table,
	 * apply ALTER TABLE ... CHECK NOT NULL to every child.  Otherwise, use
	 * normal recursion logic.
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE &&
		!recurse)
	{
		AlterTableCmd *newcmd = makeNode(AlterTableCmd);

		newcmd->subtype = AT_CheckNotNull;
		newcmd->name = pstrdup(cmd->name);
		ATSimpleRecursion(wqueue, rel, newcmd, true, lockmode, context);
	}
	else
		ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode, context);
}

/*
 * Return the address of the modified column.  If the column was already NOT
 * NULL, InvalidObjectAddress is returned.
 */
static ObjectAddress
ATExecSetNotNull(AlteredTableInfo *tab, Relation rel,
				 const char *colName, LOCKMODE lockmode)
{
	HeapTuple	tuple;
	AttrNumber	attnum;
	Relation	attr_rel;
	ObjectAddress address;

	/*
	 * lookup the attribute
	 */
	attr_rel = table_open(AttributeRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	attnum = ((Form_pg_attribute) GETSTRUCT(tuple))->attnum;

	/* Prevent them from altering a system attribute */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	/*
	 * Okay, actually perform the catalog change ... if needed
	 */
	if (!((Form_pg_attribute) GETSTRUCT(tuple))->attnotnull)
	{
		((Form_pg_attribute) GETSTRUCT(tuple))->attnotnull = true;

		CatalogTupleUpdate(attr_rel, &tuple->t_self, tuple);

		/*
		 * Ordinarily phase 3 must ensure that no NULLs exist in columns that
		 * are set NOT NULL; however, if we can find a constraint which proves
		 * this then we can skip that.  We needn't bother looking if we've
		 * already found that we must verify some other NOT NULL constraint.
		 */
		if (!tab->verify_new_notnull &&
			!NotNullImpliedByRelConstraints(rel, (Form_pg_attribute) GETSTRUCT(tuple)))
		{
			/* Tell Phase 3 it needs to test the constraint */
			tab->verify_new_notnull = true;
		}

		ObjectAddressSubSet(address, RelationRelationId,
							RelationGetRelid(rel), attnum);
	}
	else
		address = InvalidObjectAddress;

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel), attnum);

	table_close(attr_rel, RowExclusiveLock);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN CHECK NOT NULL
 *
 * This doesn't exist in the grammar, but we generate AT_CheckNotNull
 * commands against the partitions of a partitioned table if the user
 * writes ALTER TABLE ONLY ... SET NOT NULL on the partitioned table,
 * or tries to create a primary key on it (which internally creates
 * AT_SetNotNull on the partitioned table).   Such a command doesn't
 * allow us to actually modify any partition, but we want to let it
 * go through if the partitions are already properly marked.
 *
 * In future, this might need to adjust the child table's state, likely
 * by incrementing an inheritance count for the attnotnull constraint.
 * For now we need only check for the presence of the flag.
 */
static void
ATExecCheckNotNull(AlteredTableInfo *tab, Relation rel,
				   const char *colName, LOCKMODE lockmode)
{
	HeapTuple	tuple;

	tuple = SearchSysCacheAttName(RelationGetRelid(rel), colName);

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	if (!((Form_pg_attribute) GETSTRUCT(tuple))->attnotnull)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("constraint must be added to child tables too"),
				 errdetail("Column \"%s\" of relation \"%s\" is not already NOT NULL.",
						   colName, RelationGetRelationName(rel)),
				 errhint("Do not specify the ONLY keyword.")));

	ReleaseSysCache(tuple);
}

/*
 * NotNullImpliedByRelConstraints
 *		Does rel's existing constraints imply NOT NULL for the given attribute?
 */
static bool
NotNullImpliedByRelConstraints(Relation rel, Form_pg_attribute attr)
{
	NullTest   *nnulltest = makeNode(NullTest);

	nnulltest->arg = (Expr *) makeVar(1,
									  attr->attnum,
									  attr->atttypid,
									  attr->atttypmod,
									  attr->attcollation,
									  0);
	nnulltest->nulltesttype = IS_NOT_NULL;

	/*
	 * argisrow = false is correct even for a composite column, because
	 * attnotnull does not represent a SQL-spec IS NOT NULL test in such a
	 * case, just IS DISTINCT FROM NULL.
	 */
	nnulltest->argisrow = false;
	nnulltest->location = -1;

	if (ConstraintImpliedByRelConstraint(rel, list_make1(nnulltest), NIL))
	{
		ereport(DEBUG1,
				(errmsg_internal("existing constraints on column \"%s.%s\" are sufficient to prove that it does not contain nulls",
								 RelationGetRelationName(rel), NameStr(attr->attname))));
		return true;
	}

	return false;
}

/*
 * ALTER TABLE ALTER COLUMN SET/DROP DEFAULT
 *
 * Return the address of the affected column.
 */
static ObjectAddress
ATExecColumnDefault(Relation rel, const char *colName,
					Node *newDefault, LOCKMODE lockmode)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);
	AttrNumber	attnum;
	ObjectAddress address;

	/*
	 * get the number of the attribute
	 */
	attnum = get_attnum(RelationGetRelid(rel), colName);
	if (attnum == InvalidAttrNumber)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	/* Prevent them from altering a system attribute */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	if (TupleDescAttr(tupdesc, attnum - 1)->attidentity)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("column \"%s\" of relation \"%s\" is an identity column",
						colName, RelationGetRelationName(rel)),
				 newDefault ? 0 : errhint("Use ALTER TABLE ... ALTER COLUMN ... DROP IDENTITY instead.")));

	if (TupleDescAttr(tupdesc, attnum - 1)->attgenerated)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("column \"%s\" of relation \"%s\" is a generated column",
						colName, RelationGetRelationName(rel)),
				 newDefault || TupleDescAttr(tupdesc, attnum - 1)->attgenerated != ATTRIBUTE_GENERATED_STORED ? 0 :
				 errhint("Use ALTER TABLE ... ALTER COLUMN ... DROP EXPRESSION instead.")));

	/*
	 * Remove any old default for the column.  We use RESTRICT here for
	 * safety, but at present we do not expect anything to depend on the
	 * default.
	 *
	 * We treat removing the existing default as an internal operation when it
	 * is preparatory to adding a new default, but as a user-initiated
	 * operation when the user asked for a drop.
	 */
	RemoveAttrDefault(RelationGetRelid(rel), attnum, DROP_RESTRICT, false,
					  newDefault != NULL);

	if (newDefault)
	{
		/* SET DEFAULT */
		RawColumnDefault *rawEnt;

		rawEnt = (RawColumnDefault *) palloc(sizeof(RawColumnDefault));
		rawEnt->attnum = attnum;
		rawEnt->raw_default = newDefault;
		rawEnt->missingMode = false;
		rawEnt->generated = '\0';

		/*
		 * This function is intended for CREATE TABLE, so it processes a
		 * _list_ of defaults, but we just do one.
		 */
		AddRelationNewConstraints(rel, list_make1(rawEnt), NIL,
								  false, true, false, NULL);
	}

	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	return address;
}

/*
 * Add a pre-cooked default expression.
 *
 * Return the address of the affected column.
 */
static ObjectAddress
ATExecCookedColumnDefault(Relation rel, AttrNumber attnum,
						  Node *newDefault)
{
	ObjectAddress address;

	/* We assume no checking is required */

	/*
	 * Remove any old default for the column.  We use RESTRICT here for
	 * safety, but at present we do not expect anything to depend on the
	 * default.  (In ordinary cases, there could not be a default in place
	 * anyway, but it's possible when combining LIKE with inheritance.)
	 */
	RemoveAttrDefault(RelationGetRelid(rel), attnum, DROP_RESTRICT, false,
					  true);

	(void) StoreAttrDefault(rel, attnum, newDefault, true, false);

	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	return address;
}

/*
 * ALTER TABLE ALTER COLUMN ADD IDENTITY
 *
 * Return the address of the affected column.
 */
static ObjectAddress
ATExecAddIdentity(Relation rel, const char *colName,
				  Node *def, LOCKMODE lockmode)
{
	Relation	attrelation;
	HeapTuple	tuple;
	Form_pg_attribute attTup;
	AttrNumber	attnum;
	ObjectAddress address;
	ColumnDef  *cdef = castNode(ColumnDef, def);

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));
	attTup = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = attTup->attnum;

	/* Can't alter a system attribute */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	/*
	 * Creating a column as identity implies NOT NULL, so adding the identity
	 * to an existing column that is not NOT NULL would create a state that
	 * cannot be reproduced without contortions.
	 */
	if (!attTup->attnotnull)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("column \"%s\" of relation \"%s\" must be declared NOT NULL before identity can be added",
						colName, RelationGetRelationName(rel))));

	if (attTup->attidentity)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("column \"%s\" of relation \"%s\" is already an identity column",
						colName, RelationGetRelationName(rel))));

	if (attTup->atthasdef)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("column \"%s\" of relation \"%s\" already has a default value",
						colName, RelationGetRelationName(rel))));

	attTup->attidentity = cdef->identity;
	CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attTup->attnum);
	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	heap_freetuple(tuple);

	table_close(attrelation, RowExclusiveLock);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN SET { GENERATED or sequence options }
 *
 * Return the address of the affected column.
 */
static ObjectAddress
ATExecSetIdentity(Relation rel, const char *colName, Node *def, LOCKMODE lockmode)
{
	ListCell   *option;
	DefElem    *generatedEl = NULL;
	HeapTuple	tuple;
	Form_pg_attribute attTup;
	AttrNumber	attnum;
	Relation	attrelation;
	ObjectAddress address;

	foreach(option, castNode(List, def))
	{
		DefElem    *defel = lfirst_node(DefElem, option);

		if (strcmp(defel->defname, "generated") == 0)
		{
			if (generatedEl)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("conflicting or redundant options")));
			generatedEl = defel;
		}
		else
			elog(ERROR, "option \"%s\" not recognized",
				 defel->defname);
	}

	/*
	 * Even if there is nothing to change here, we run all the checks.  There
	 * will be a subsequent ALTER SEQUENCE that relies on everything being
	 * there.
	 */

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	attTup = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = attTup->attnum;

	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	if (!attTup->attidentity)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("column \"%s\" of relation \"%s\" is not an identity column",
						colName, RelationGetRelationName(rel))));

	if (generatedEl)
	{
		attTup->attidentity = defGetInt32(generatedEl);
		CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

		InvokeObjectPostAlterHook(RelationRelationId,
								  RelationGetRelid(rel),
								  attTup->attnum);
		ObjectAddressSubSet(address, RelationRelationId,
							RelationGetRelid(rel), attnum);
	}
	else
		address = InvalidObjectAddress;

	heap_freetuple(tuple);
	table_close(attrelation, RowExclusiveLock);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN DROP IDENTITY
 *
 * Return the address of the affected column.
 */
static ObjectAddress
ATExecDropIdentity(Relation rel, const char *colName, bool missing_ok, LOCKMODE lockmode)
{
	HeapTuple	tuple;
	Form_pg_attribute attTup;
	AttrNumber	attnum;
	Relation	attrelation;
	ObjectAddress address;
	Oid			seqid;
	ObjectAddress seqaddress;

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	attTup = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = attTup->attnum;

	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	if (!attTup->attidentity)
	{
		if (!missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("column \"%s\" of relation \"%s\" is not an identity column",
							colName, RelationGetRelationName(rel))));
		else
		{
			ereport(NOTICE,
					(errmsg("column \"%s\" of relation \"%s\" is not an identity column, skipping",
							colName, RelationGetRelationName(rel))));
			heap_freetuple(tuple);
			table_close(attrelation, RowExclusiveLock);
			return InvalidObjectAddress;
		}
	}

	attTup->attidentity = '\0';
	CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attTup->attnum);
	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	heap_freetuple(tuple);

	table_close(attrelation, RowExclusiveLock);

	/* drop the internal sequence */
	seqid = getIdentitySequence(RelationGetRelid(rel), attnum, false);
	deleteDependencyRecordsForClass(RelationRelationId, seqid,
									RelationRelationId, DEPENDENCY_INTERNAL);
	CommandCounterIncrement();
	seqaddress.classId = RelationRelationId;
	seqaddress.objectId = seqid;
	seqaddress.objectSubId = 0;
	performDeletion(&seqaddress, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN DROP EXPRESSION
 */
static void
ATPrepDropExpression(Relation rel, AlterTableCmd *cmd, bool recurse, bool recursing, LOCKMODE lockmode)
{
	/*
	 * Reject ONLY if there are child tables.  We could implement this, but it
	 * is a bit complicated.  GENERATED clauses must be attached to the column
	 * definition and cannot be added later like DEFAULT, so if a child table
	 * has a generation expression that the parent does not have, the child
	 * column will necessarily be an attlocal column.  So to implement ONLY
	 * here, we'd need extra code to update attislocal of the direct child
	 * tables, somewhat similar to how DROP COLUMN does it, so that the
	 * resulting state can be properly dumped and restored.
	 */
	if (!recurse &&
		find_inheritance_children(RelationGetRelid(rel), lockmode))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE / DROP EXPRESSION must be applied to child tables too")));

	/*
	 * Cannot drop generation expression from inherited columns.
	 */
	if (!recursing)
	{
		HeapTuple	tuple;
		Form_pg_attribute attTup;

		tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), cmd->name);
		if (!HeapTupleIsValid(tuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" of relation \"%s\" does not exist",
							cmd->name, RelationGetRelationName(rel))));

		attTup = (Form_pg_attribute) GETSTRUCT(tuple);

		if (attTup->attinhcount > 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot drop generation expression from inherited column")));
	}
}

/*
 * Return the address of the affected column.
 */
static ObjectAddress
ATExecDropExpression(Relation rel, const char *colName, bool missing_ok, LOCKMODE lockmode)
{
	HeapTuple	tuple;
	Form_pg_attribute attTup;
	AttrNumber	attnum;
	Relation	attrelation;
	Oid			attrdefoid;
	ObjectAddress address;

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	attTup = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = attTup->attnum;

	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	if (attTup->attgenerated != ATTRIBUTE_GENERATED_STORED)
	{
		if (!missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("column \"%s\" of relation \"%s\" is not a stored generated column",
							colName, RelationGetRelationName(rel))));
		else
		{
			ereport(NOTICE,
					(errmsg("column \"%s\" of relation \"%s\" is not a stored generated column, skipping",
							colName, RelationGetRelationName(rel))));
			heap_freetuple(tuple);
			table_close(attrelation, RowExclusiveLock);
			return InvalidObjectAddress;
		}
	}

	/*
	 * Mark the column as no longer generated.  (The atthasdef flag needs to
	 * get cleared too, but RemoveAttrDefault will handle that.)
	 */
	attTup->attgenerated = '\0';
	CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attnum);
	heap_freetuple(tuple);

	table_close(attrelation, RowExclusiveLock);

	/*
	 * Drop the dependency records of the GENERATED expression, in particular
	 * its INTERNAL dependency on the column, which would otherwise cause
	 * dependency.c to refuse to perform the deletion.
	 */
	attrdefoid = GetAttrDefaultOid(RelationGetRelid(rel), attnum);
	if (!OidIsValid(attrdefoid))
		elog(ERROR, "could not find attrdef tuple for relation %u attnum %d",
			 RelationGetRelid(rel), attnum);
	(void) deleteDependencyRecordsFor(AttrDefaultRelationId, attrdefoid, false);

	/* Make above changes visible */
	CommandCounterIncrement();

	/*
	 * Get rid of the GENERATED expression itself.  We use RESTRICT here for
	 * safety, but at present we do not expect anything to depend on the
	 * default.
	 */
	RemoveAttrDefault(RelationGetRelid(rel), attnum, DROP_RESTRICT,
					  false, false);

	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN SET STATISTICS
 *
 * Return value is the address of the modified column
 */
static ObjectAddress
ATExecSetStatistics(Relation rel, const char *colName, int16 colNum, Node *newValue, LOCKMODE lockmode)
{
	int			newtarget;
	Relation	attrelation;
	HeapTuple	tuple;
	Form_pg_attribute attrtuple;
	AttrNumber	attnum;
	ObjectAddress address;

	/*
	 * We allow referencing columns by numbers only for indexes, since table
	 * column numbers could contain gaps if columns are later dropped.
	 */
	if (rel->rd_rel->relkind != RELKIND_INDEX &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX &&
		!colName)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot refer to non-index column by number")));

	Assert(IsA(newValue, Integer));
	newtarget = intVal(newValue);

	/*
	 * Limit target to a sane range
	 */
	if (newtarget < -1)
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("statistics target %d is too low",
						newtarget)));
	}
	else if (newtarget > 10000)
	{
		newtarget = 10000;
		ereport(WARNING,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("lowering statistics target to %d",
						newtarget)));
	}

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);

	if (colName)
	{
		tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);

		if (!HeapTupleIsValid(tuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" of relation \"%s\" does not exist",
							colName, RelationGetRelationName(rel))));
	}
	else
	{
		tuple = SearchSysCacheCopyAttNum(RelationGetRelid(rel), colNum);

		if (!HeapTupleIsValid(tuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column number %d of relation \"%s\" does not exist",
							colNum, RelationGetRelationName(rel))));
	}

	attrtuple = (Form_pg_attribute) GETSTRUCT(tuple);

	attnum = attrtuple->attnum;
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	if (rel->rd_rel->relkind == RELKIND_INDEX ||
		rel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX)
	{
		if (attnum > rel->rd_index->indnkeyatts)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot alter statistics on included column \"%s\" of index \"%s\"",
							NameStr(attrtuple->attname), RelationGetRelationName(rel))));
		else if (rel->rd_index->indkey.values[attnum - 1] != 0)
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot alter statistics on non-expression column \"%s\" of index \"%s\"",
							NameStr(attrtuple->attname), RelationGetRelationName(rel)),
					 errhint("Alter statistics on table column instead.")));
	}

	attrtuple->attstattarget = newtarget;

	CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attrtuple->attnum);
	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	heap_freetuple(tuple);

	table_close(attrelation, RowExclusiveLock);

	return address;
}

/*
 * Return value is the address of the modified column
 */
static ObjectAddress
ATExecSetOptions(Relation rel, const char *colName, Node *options,
				 bool isReset, LOCKMODE lockmode)
{
	Relation	attrelation;
	HeapTuple	tuple,
				newtuple;
	Form_pg_attribute attrtuple;
	AttrNumber	attnum;
	Datum		datum,
				newOptions;
	bool		isnull;
	ObjectAddress address;
	Datum		repl_val[Natts_pg_attribute];
	bool		repl_null[Natts_pg_attribute];
	bool		repl_repl[Natts_pg_attribute];

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);

	tuple = SearchSysCacheAttName(RelationGetRelid(rel), colName);

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));
	attrtuple = (Form_pg_attribute) GETSTRUCT(tuple);

	attnum = attrtuple->attnum;
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	/* Generate new proposed attoptions (text array) */
	datum = SysCacheGetAttr(ATTNAME, tuple, Anum_pg_attribute_attoptions,
							&isnull);
	newOptions = transformRelOptions(isnull ? (Datum) 0 : datum,
									 castNode(List, options), NULL, NULL,
									 false, isReset);
	/* Validate new options */
	(void) attribute_reloptions(newOptions, true);

	/* Build new tuple. */
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));
	if (newOptions != (Datum) 0)
		repl_val[Anum_pg_attribute_attoptions - 1] = newOptions;
	else
		repl_null[Anum_pg_attribute_attoptions - 1] = true;
	repl_repl[Anum_pg_attribute_attoptions - 1] = true;
	newtuple = heap_modify_tuple(tuple, RelationGetDescr(attrelation),
								 repl_val, repl_null, repl_repl);

	/* Update system catalog. */
	CatalogTupleUpdate(attrelation, &newtuple->t_self, newtuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attrtuple->attnum);
	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);

	heap_freetuple(newtuple);

	ReleaseSysCache(tuple);

	table_close(attrelation, RowExclusiveLock);

	return address;
}

/*
 * Helper function for ATExecSetStorage and ATExecSetCompression
 *
 * Set the attstorage and/or attcompression fields for index columns
 * associated with the specified table column.
 */
static void
SetIndexStorageProperties(Relation rel, Relation attrelation,
						  AttrNumber attnum,
						  bool setstorage, char newstorage,
						  bool setcompression, char newcompression,
						  LOCKMODE lockmode)
{
	ListCell   *lc;

	foreach(lc, RelationGetIndexList(rel))
	{
		Oid			indexoid = lfirst_oid(lc);
		Relation	indrel;
		AttrNumber	indattnum = 0;
		HeapTuple	tuple;

		indrel = index_open(indexoid, lockmode);

		for (int i = 0; i < indrel->rd_index->indnatts; i++)
		{
			if (indrel->rd_index->indkey.values[i] == attnum)
			{
				indattnum = i + 1;
				break;
			}
		}

		if (indattnum == 0)
		{
			index_close(indrel, lockmode);
			continue;
		}

		tuple = SearchSysCacheCopyAttNum(RelationGetRelid(indrel), indattnum);

		if (HeapTupleIsValid(tuple))
		{
			Form_pg_attribute attrtuple = (Form_pg_attribute) GETSTRUCT(tuple);

			if (setstorage)
				attrtuple->attstorage = newstorage;

			if (setcompression)
				attrtuple->attcompression = newcompression;

			CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

			InvokeObjectPostAlterHook(RelationRelationId,
									  RelationGetRelid(rel),
									  attrtuple->attnum);

			heap_freetuple(tuple);
		}

		index_close(indrel, lockmode);
	}
}

/*
 * ALTER TABLE ALTER COLUMN SET STORAGE
 *
 * Return value is the address of the modified column
 */
static ObjectAddress
ATExecSetStorage(Relation rel, const char *colName, Node *newValue, LOCKMODE lockmode)
{
	char	   *storagemode;
	char		newstorage;
	Relation	attrelation;
	HeapTuple	tuple;
	Form_pg_attribute attrtuple;
	AttrNumber	attnum;
	ObjectAddress address;

	Assert(IsA(newValue, String));
	storagemode = strVal(newValue);

	if (pg_strcasecmp(storagemode, "plain") == 0)
		newstorage = TYPSTORAGE_PLAIN;
	else if (pg_strcasecmp(storagemode, "external") == 0)
		newstorage = TYPSTORAGE_EXTERNAL;
	else if (pg_strcasecmp(storagemode, "extended") == 0)
		newstorage = TYPSTORAGE_EXTENDED;
	else if (pg_strcasecmp(storagemode, "main") == 0)
		newstorage = TYPSTORAGE_MAIN;
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid storage type \"%s\"",
						storagemode)));
		newstorage = 0;			/* keep compiler quiet */
	}

	attrelation = table_open(AttributeRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);

	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));
	attrtuple = (Form_pg_attribute) GETSTRUCT(tuple);

	attnum = attrtuple->attnum;
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	/*
	 * safety check: do not allow toasted storage modes unless column datatype
	 * is TOAST-aware.
	 */
	if (newstorage == TYPSTORAGE_PLAIN || TypeIsToastable(attrtuple->atttypid))
		attrtuple->attstorage = newstorage;
	else
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("column data type %s can only have storage PLAIN",
						format_type_be(attrtuple->atttypid))));

	CatalogTupleUpdate(attrelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attrtuple->attnum);

	heap_freetuple(tuple);

	/*
	 * Apply the change to indexes as well (only for simple index columns,
	 * matching behavior of index.c ConstructTupleDescriptor()).
	 */
	SetIndexStorageProperties(rel, attrelation, attnum,
							  true, newstorage,
							  false, 0,
							  lockmode);

	table_close(attrelation, RowExclusiveLock);

	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	return address;
}


/*
 * ALTER TABLE DROP COLUMN
 *
 * DROP COLUMN cannot use the normal ALTER TABLE recursion mechanism,
 * because we have to decide at runtime whether to recurse or not depending
 * on whether attinhcount goes to zero or not.  (We can't check this in a
 * static pre-pass because it won't handle multiple inheritance situations
 * correctly.)
 */
static void
ATPrepDropColumn(List **wqueue, Relation rel, bool recurse, bool recursing,
				 AlterTableCmd *cmd, LOCKMODE lockmode,
				 AlterTableUtilityContext *context)
{
	if (rel->rd_rel->reloftype && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot drop column from typed table")));

	if (rel->rd_rel->relkind == RELKIND_COMPOSITE_TYPE)
		ATTypedTableRecursion(wqueue, rel, cmd, lockmode, context);

	if (recurse)
		cmd->subtype = AT_DropColumnRecurse;
}

/*
 * Drops column 'colName' from relation 'rel' and returns the address of the
 * dropped column.  The column is also dropped (or marked as no longer
 * inherited from relation) from the relation's inheritance children, if any.
 *
 * In the recursive invocations for inheritance child relations, instead of
 * dropping the column directly (if to be dropped at all), its object address
 * is added to 'addrs', which must be non-NULL in such invocations.  All
 * columns are dropped at the same time after all the children have been
 * checked recursively.
 */
static ObjectAddress
ATExecDropColumn(List **wqueue, AlteredTableInfo *yb_tab, Relation rel,
				 const char *colName,
				 DropBehavior behavior,
				 bool recurse, bool recursing,
				 bool missing_ok, LOCKMODE lockmode,
				 ObjectAddresses *addrs)
{
	HeapTuple	tuple;
	Form_pg_attribute targetatt;
	AttrNumber	attnum;
	List	   *children;
	ObjectAddress object;
	bool		is_expr;

	/* At top level, permission check was done in ATPrepCmd, else do it */
	if (recursing)
		ATSimplePermissions(AT_DropColumn, rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	/* Initialize addrs on the first invocation */
	Assert(!recursing || addrs != NULL);
	if (!recursing)
		addrs = new_object_addresses();

	/*
	 * get the number of the attribute
	 */
	tuple = SearchSysCacheAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
	{
		if (!missing_ok)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" of relation \"%s\" does not exist",
							colName, RelationGetRelationName(rel))));
		}
		else
		{
			ereport(NOTICE,
					(errmsg("column \"%s\" of relation \"%s\" does not exist, skipping",
							colName, RelationGetRelationName(rel))));
			return InvalidObjectAddress;
		}
	}
	targetatt = (Form_pg_attribute) GETSTRUCT(tuple);

	attnum = targetatt->attnum;

	/*
	 * In YB, dropping a key column requires a table rewrite.
	 */
	if (IsYBRelation(rel) && YbIsAttrPrimaryKeyColumn(rel, attnum))
	{
		/*
		 * In YB, the ADD/DROP primary key operation involves a table
		 * rewrite. So if this is partitioned table, we need to add
		 * its children to the work queue as well.
		 */
		if ((rel)->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
			YbATSetPKRewriteChildPartitions(wqueue,
				yb_tab, false /* skip_copy_split_options */);
		yb_tab->rewrite |= YB_AT_REWRITE_ALTER_PRIMARY_KEY;
	}

	/* Can't drop a system attribute, except OID */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot drop system column \"%s\"",
						colName)));

	/*
	 * Don't drop inherited columns, unless recursing (presumably from a drop
	 * of the parent column)
	 */
	if (targetatt->attinhcount > 0 && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot drop inherited column \"%s\"",
						colName)));

	/*
	 * Don't drop columns used in the partition key, either.  (If we let this
	 * go through, the key column's dependencies would cause a cascaded drop
	 * of the whole table, which is surely not what the user expected.)
	 */
	if (has_partition_attrs(rel,
							bms_make_singleton(attnum - YBGetFirstLowInvalidAttributeNumber(rel)),
							&is_expr))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot drop column \"%s\" because it is part of the partition key of relation \"%s\"",
						colName, RelationGetRelationName(rel))));

	ReleaseSysCache(tuple);

	/*
	 * Propagate to children as appropriate.  Unlike most other ALTER
	 * routines, we have to do this one level of recursion at a time; we can't
	 * use find_all_inheritors to do it in one pass.
	 */
	children =
		find_inheritance_children(RelationGetRelid(rel), lockmode);

	if (children)
	{
		Relation	attr_rel;
		ListCell   *child;

		/*
		 * In case of a partitioned table, the column must be dropped from the
		 * partitions as well.
		 */
		if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE && !recurse)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot drop column from only the partitioned table when partitions exist"),
					 errhint("Do not specify the ONLY keyword.")));

		attr_rel = table_open(AttributeRelationId, RowExclusiveLock);
		foreach(child, children)
		{
			Oid			childrelid = lfirst_oid(child);
			Relation	childrel;
			Form_pg_attribute childatt;

			/* find_inheritance_children already got lock */
			childrel = table_open(childrelid, NoLock);
			CheckTableNotInUse(childrel, "ALTER TABLE");

			tuple = SearchSysCacheCopyAttName(childrelid, colName);
			if (!HeapTupleIsValid(tuple))	/* shouldn't happen */
				elog(ERROR, "cache lookup failed for attribute \"%s\" of relation %u",
					 colName, childrelid);
			childatt = (Form_pg_attribute) GETSTRUCT(tuple);

			if (childatt->attinhcount <= 0) /* shouldn't happen */
				elog(ERROR, "relation %u has non-inherited attribute \"%s\"",
					 childrelid, colName);

			if (recurse)
			{
				/*
				 * If the child column has other definition sources, just
				 * decrement its inheritance count; if not, recurse to delete
				 * it.
				 */
				if (childatt->attinhcount == 1 && !childatt->attislocal)
				{
					/* Time to delete this child column, too */
					ATExecDropColumn(wqueue, yb_tab, childrel, colName,
									 behavior, true, true,
									 false, lockmode, addrs);
				}
				else
				{
					/* Child column must survive my deletion */
					childatt->attinhcount--;

					CatalogTupleUpdate(attr_rel, &tuple->t_self, tuple);

					/* Make update visible */
					CommandCounterIncrement();
				}
			}
			else
			{
				/*
				 * If we were told to drop ONLY in this table (no recursion),
				 * we need to mark the inheritors' attributes as locally
				 * defined rather than inherited.
				 */
				childatt->attinhcount--;
				childatt->attislocal = true;

				CatalogTupleUpdate(attr_rel, &tuple->t_self, tuple);

				/* Make update visible */
				CommandCounterIncrement();
			}

			heap_freetuple(tuple);

			table_close(childrel, NoLock);
		}
		table_close(attr_rel, RowExclusiveLock);
	}

	/* Add object to delete */
	object.classId = RelationRelationId;
	object.objectId = RelationGetRelid(rel);
	object.objectSubId = attnum;
	add_exact_object_address(&object, addrs);

	if (!recursing)
	{
		/* Recursion has ended, drop everything that was collected */
		/*
		 * YB: Skip YB drop on the column, as that will be handled separately by
		 * the ALTER TABLE flow.
		 */
		performMultipleDeletions(addrs, behavior,
			IsYugaByteEnabled() ? YB_SKIP_YB_DROP_COLUMN : 0);
		free_object_addresses(addrs);
	}

	return object;
}

/*
 * Retrieve attribute from pg_constraint sys cache, ensure it's not null and save it
 * to a variable provided in the first argument.
 */
#define YBGetNotNullConstraintAttr(tuple, attname) \
	__extension__ ({ \
		bool isnull; \
		Form_pg_constraint con_form = (Form_pg_constraint) GETSTRUCT(tuple); \
		Datum result = SysCacheGetAttr(CONSTROID, \
		                               tuple, \
		                               Anum_pg_constraint_##attname, \
		                               &isnull); \
		if (isnull) \
			elog(ERROR, "null " #attname " for constraint %u", con_form->oid); \
		result; \
	});

/*
 * ALTER TABLE ADD INDEX
 *
 * There is no such command in the grammar, but parse_utilcmd.c converts
 * UNIQUE and PRIMARY KEY constraints into AT_AddIndex subcommands.  This lets
 * us schedule creation of the index at the appropriate time during ALTER.
 *
 * Return value is the address of the new index.
 *
 * YB NOTE: Will replace passed relation with another one in case it was re-created.
 */
static ObjectAddress
ATExecAddIndex(List **yb_wqueue, AlteredTableInfo *tab, Relation *mutable_rel,
			   IndexStmt *stmt, bool is_rebuild, LOCKMODE lockmode)
{
	bool		check_rights;
	bool		skip_build;
	bool		quiet;
	ObjectAddress address;

	Assert(IsA(stmt, IndexStmt));
	Assert(!stmt->concurrent);

	/* The IndexStmt has already been through transformIndexStmt */
	Assert(stmt->transformed);

	/* suppress schema rights check when rebuilding existing index */
	check_rights = !is_rebuild;
	/* skip index build if phase 3 will do it or we're reusing an old one */
	skip_build = tab->rewrite > 0 || OidIsValid(stmt->oldNode);
	/* suppress notices when rebuilding existing index */
	quiet = is_rebuild;

	/*
	 * YB note:
	 * For a PRIMARY KEY index creation, this will create a dummy index.
	 * We're doing this before YbATCloneRelationSetPrimaryKey for it to run
	 * all necessary checks - columns existence and types, absence of nulls,
	 * etc.
	 */
	address = DefineIndex(RelationGetRelid(*mutable_rel),
						  stmt,
						  InvalidOid,	/* no predefined OID */
						  InvalidOid,	/* no parent index */
						  InvalidOid,	/* no parent constraint */
						  true, /* is_alter_table */
						  check_rights,
						  false,	/* check_not_in_use - we did it already */
						  skip_build,
						  quiet);
	if (IsYBRelation(*mutable_rel) && stmt->primary)
	{
		/*
		 * Use old rewrite approach for ADD/DROP PRIMARY KEY if
		 * yb_enable_alter_table_rewrite is false.
		 */
		if (!skip_build && !yb_enable_alter_table_rewrite)
		{
			/* Table will be re-created, along with the dummy PK index. */
			*mutable_rel =
				YbATCloneRelationSetPrimaryKey(*mutable_rel, stmt, &address);
			/*
			 * Update the table relid so that further passes will operate on
			 * the new table.
			 */
			tab->relid = (*mutable_rel)->rd_id;
		}
		else
		{
			YbGetTableProperties(*mutable_rel);
			/* Don't copy split options if we are creating a range key. */
			bool skip_copy_split_options = YbATIsRangePk(
				linitial_node(IndexElem, stmt->indexParams)->ordering,
				(*mutable_rel)->yb_table_properties->is_colocated, OidIsValid(
					(*mutable_rel)->yb_table_properties->tablegroup_oid));
			if (!skip_build)
			{
				/*
				 * In YB, the ADD/DROP primary key operation involves a table
				 * rewrite. So if this is partitioned table, we need to add
				 * its children to the work queue as well.
				 */
				if ((*mutable_rel)->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
					YbATSetPKRewriteChildPartitions(yb_wqueue,
						tab, skip_copy_split_options);
				tab->rewrite |= YB_AT_REWRITE_ALTER_PRIMARY_KEY;
			}
			tab->yb_skip_copy_split_options = tab->yb_skip_copy_split_options
				|| skip_copy_split_options;
		}
	}

	/*
	 * If TryReuseIndex() stashed a relfilenode for us, we used it for the new
	 * index instead of building from scratch.  Restore associated fields.
	 * This may store InvalidSubTransactionId in both fields, in which case
	 * relcache.c will assume it can rebuild the relcache entry.  Hence, do
	 * this after the CCI that made catalog rows visible to any rebuild.  The
	 * DROP of the old edition of this index will have scheduled the storage
	 * for deletion at commit, so cancel that pending deletion.
	 */
	if (OidIsValid(stmt->oldNode))
	{
		Relation	irel = index_open(address.objectId, NoLock);

		irel->rd_createSubid = stmt->oldCreateSubid;
		irel->rd_firstRelfilenodeSubid = stmt->oldFirstRelfilenodeSubid;
		RelationPreserveStorage(irel->rd_node, true);
		index_close(irel, NoLock);
	}

	return address;
}

/*
 * ALTER TABLE ADD STATISTICS
 *
 * This is no such command in the grammar, but we use this internally to add
 * AT_ReAddStatistics subcommands to rebuild extended statistics after a table
 * column type change.
 */
static ObjectAddress
ATExecAddStatistics(AlteredTableInfo *tab, Relation rel,
					CreateStatsStmt *stmt, bool is_rebuild, LOCKMODE lockmode)
{
	ObjectAddress address;

	Assert(IsA(stmt, CreateStatsStmt));

	/* The CreateStatsStmt has already been through transformStatsStmt */
	Assert(stmt->transformed);

	address = CreateStatistics(stmt);

	return address;
}

/*
 * ALTER TABLE ADD CONSTRAINT USING INDEX
 *
 * Returns the address of the new constraint.
 */
static ObjectAddress
ATExecAddIndexConstraint(AlteredTableInfo *tab, Relation rel,
						 IndexStmt *stmt, LOCKMODE lockmode,
						 List **yb_wqueue)
{
	Oid			index_oid = stmt->indexOid;
	Relation	indexRel;
	char	   *indexName;
	IndexInfo  *indexInfo;
	char	   *constraintName;
	char		constraintType;
	ObjectAddress address;
	bits16		flags;

	Assert(IsA(stmt, IndexStmt));
	Assert(OidIsValid(index_oid));
	Assert(stmt->isconstraint);

	/*
	 * Doing this on partitioned tables is not a simple feature to implement,
	 * so let's punt for now.
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("ALTER TABLE / ADD CONSTRAINT USING INDEX is not supported on partitioned tables")));

	indexRel = index_open(index_oid, AccessShareLock);

	indexName = pstrdup(RelationGetRelationName(indexRel));

	indexInfo = BuildIndexInfo(indexRel);

	/* this should have been checked at parse time */
	if (!indexInfo->ii_Unique)
		elog(ERROR, "index \"%s\" is not unique", indexName);

	/*
	 * YB: Adding a primary key requires table rewrite.
	 * We do not need to rewrite any children as this operation is not supported
	 * on partitioned tables (checked above).
	 */
	if (IsYBRelation(rel) && stmt->primary)
	{
		YbGetTableProperties(rel);
		/* Don't copy split options if we are creating a range key. */
		bool skip_copy_split_options = YbATIsRangePk(
			YbGetIndexKeySortOrdering(indexRel),
			rel->yb_table_properties->is_colocated, OidIsValid(
				rel->yb_table_properties->tablegroup_oid));
		tab->rewrite |= YB_AT_REWRITE_ALTER_PRIMARY_KEY;
		tab->yb_skip_copy_split_options = tab->yb_skip_copy_split_options
			|| skip_copy_split_options;
		/*
		 * Since this index is going to be upgraded to a pkey, we can drop the
		 * DocDB table associated with the secondary index.
		 */
		YBCDropIndex(indexRel);
	}

	/*
	 * Determine name to assign to constraint.  We require a constraint to
	 * have the same name as the underlying index; therefore, use the index's
	 * existing name as the default constraint name, and if the user
	 * explicitly gives some other name for the constraint, rename the index
	 * to match.
	 */
	constraintName = stmt->idxname;
	if (constraintName == NULL)
		constraintName = indexName;
	else if (strcmp(constraintName, indexName) != 0)
	{
		ereport(NOTICE,
				(errmsg("ALTER TABLE / ADD CONSTRAINT USING INDEX will rename index \"%s\" to \"%s\"",
						indexName, constraintName)));
		RenameRelationInternal(index_oid, constraintName, false, true);
	}

	/* Extra checks needed if making primary key */
	if (stmt->primary)
		index_check_primary_key(rel, indexInfo, true, stmt);

	/* Note we currently don't support EXCLUSION constraints here */
	if (stmt->primary)
		constraintType = CONSTRAINT_PRIMARY;
	else
		constraintType = CONSTRAINT_UNIQUE;

	/* Create the catalog entries for the constraint */
	flags = INDEX_CONSTR_CREATE_UPDATE_INDEX |
		INDEX_CONSTR_CREATE_REMOVE_OLD_DEPS |
		(stmt->initdeferred ? INDEX_CONSTR_CREATE_INIT_DEFERRED : 0) |
		(stmt->deferrable ? INDEX_CONSTR_CREATE_DEFERRABLE : 0) |
		(stmt->primary ? INDEX_CONSTR_CREATE_MARK_AS_PRIMARY : 0);

	address = index_constraint_create(rel,
									  index_oid,
									  InvalidOid,
									  indexInfo,
									  constraintName,
									  constraintType,
									  flags,
									  allowSystemTableMods,
									  false);	/* is_internal */

	index_close(indexRel, NoLock);

	return address;
}

/*
 * ALTER TABLE ADD CONSTRAINT
 *
 * Return value is the address of the new constraint; if no constraint was
 * added, InvalidObjectAddress is returned.
 */
static ObjectAddress
ATExecAddConstraint(List **wqueue, AlteredTableInfo *tab, Relation rel,
					Constraint *newConstraint, bool recurse, bool is_readd,
					LOCKMODE lockmode)
{
	ObjectAddress address = InvalidObjectAddress;

	Assert(IsA(newConstraint, Constraint));

	/*
	 * Currently, we only expect to see CONSTR_CHECK and CONSTR_FOREIGN nodes
	 * arriving here (see the preprocessing done in parse_utilcmd.c).  Use a
	 * switch anyway to make it easier to add more code later.
	 */
	switch (newConstraint->contype)
	{
		case CONSTR_CHECK:
			address =
				ATAddCheckConstraint(wqueue, tab, rel,
									 newConstraint, recurse, false, is_readd,
									 lockmode);
			break;

		case CONSTR_FOREIGN:

			/*
			 * Assign or validate constraint name
			 */
			if (newConstraint->conname)
			{
				if (ConstraintNameIsUsed(CONSTRAINT_RELATION,
										 RelationGetRelid(rel),
										 newConstraint->conname))
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_OBJECT),
							 errmsg("constraint \"%s\" for relation \"%s\" already exists",
									newConstraint->conname,
									RelationGetRelationName(rel))));
			}
			else
				newConstraint->conname =
					ChooseConstraintName(RelationGetRelationName(rel),
										 ChooseForeignKeyConstraintNameAddition(newConstraint->fk_attrs),
										 "fkey",
										 RelationGetNamespace(rel),
										 NIL);

			address = ATAddForeignKeyConstraint(wqueue, tab, rel,
												newConstraint,
												recurse, false,
												lockmode);
			break;

		default:
			elog(ERROR, "unrecognized constraint type: %d",
				 (int) newConstraint->contype);
	}

	return address;
}

/*
 * Generate the column-name portion of the constraint name for a new foreign
 * key given the list of column names that reference the referenced
 * table.  This will be passed to ChooseConstraintName along with the parent
 * table name and the "fkey" suffix.
 *
 * We know that less than NAMEDATALEN characters will actually be used, so we
 * can truncate the result once we've generated that many.
 *
 * XXX see also ChooseExtendedStatisticNameAddition and
 * ChooseIndexNameAddition.
 */
static char *
ChooseForeignKeyConstraintNameAddition(List *colnames)
{
	char		buf[NAMEDATALEN * 2];
	int			buflen = 0;
	ListCell   *lc;

	buf[0] = '\0';
	foreach(lc, colnames)
	{
		const char *name = strVal(lfirst(lc));

		if (buflen > 0)
			buf[buflen++] = '_';	/* insert _ between names */

		/*
		 * At this point we have buflen <= NAMEDATALEN.  name should be less
		 * than NAMEDATALEN already, but use strlcpy for paranoia.
		 */
		strlcpy(buf + buflen, name, NAMEDATALEN);
		buflen += strlen(buf + buflen);
		if (buflen >= NAMEDATALEN)
			break;
	}
	return pstrdup(buf);
}

/*
 * Add a check constraint to a single table and its children.  Returns the
 * address of the constraint added to the parent relation, if one gets added,
 * or InvalidObjectAddress otherwise.
 *
 * Subroutine for ATExecAddConstraint.
 *
 * We must recurse to child tables during execution, rather than using
 * ALTER TABLE's normal prep-time recursion.  The reason is that all the
 * constraints *must* be given the same name, else they won't be seen as
 * related later.  If the user didn't explicitly specify a name, then
 * AddRelationNewConstraints would normally assign different names to the
 * child constraints.  To fix that, we must capture the name assigned at
 * the parent table and pass that down.
 */
static ObjectAddress
ATAddCheckConstraint(List **wqueue, AlteredTableInfo *tab, Relation rel,
					 Constraint *constr, bool recurse, bool recursing,
					 bool is_readd, LOCKMODE lockmode)
{
	List	   *newcons;
	ListCell   *lcon;
	List	   *children;
	ListCell   *child;
	ObjectAddress address = InvalidObjectAddress;

	/* At top level, permission check was done in ATPrepCmd, else do it */
	if (recursing)
		ATSimplePermissions(AT_AddConstraint, rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	/*
	 * Call AddRelationNewConstraints to do the work, making sure it works on
	 * a copy of the Constraint so transformExpr can't modify the original. It
	 * returns a list of cooked constraints.
	 *
	 * If the constraint ends up getting merged with a pre-existing one, it's
	 * omitted from the returned list, which is what we want: we do not need
	 * to do any validation work.  That can only happen at child tables,
	 * though, since we disallow merging at the top level.
	 */
	newcons = AddRelationNewConstraints(rel, NIL,
										list_make1(copyObject(constr)),
										recursing | is_readd,	/* allow_merge */
										!recursing, /* is_local */
										is_readd,	/* is_internal */
										NULL);	/* queryString not available
												 * here */

	/* we don't expect more than one constraint here */
	Assert(list_length(newcons) <= 1);

	/* Add each to-be-validated constraint to Phase 3's queue */
	foreach(lcon, newcons)
	{
		CookedConstraint *ccon = (CookedConstraint *) lfirst(lcon);

		if (!ccon->skip_validation)
		{
			NewConstraint *newcon;

			newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
			newcon->name = ccon->name;
			newcon->contype = ccon->contype;
			newcon->qual = ccon->expr;

			tab->constraints = lappend(tab->constraints, newcon);
		}

		/* Save the actually assigned name if it was defaulted */
		if (constr->conname == NULL)
			constr->conname = ccon->name;

		ObjectAddressSet(address, ConstraintRelationId, ccon->conoid);
	}

	/* At this point we must have a locked-down name to use */
	Assert(constr->conname != NULL);

	/* Advance command counter in case same table is visited multiple times */
	CommandCounterIncrement();

	/*
	 * If the constraint got merged with an existing constraint, we're done.
	 * We mustn't recurse to child tables in this case, because they've
	 * already got the constraint, and visiting them again would lead to an
	 * incorrect value for coninhcount.
	 */
	if (newcons == NIL)
		return address;

	/*
	 * If adding a NO INHERIT constraint, no need to find our children.
	 */
	if (constr->is_no_inherit)
		return address;

	/*
	 * Propagate to children as appropriate.  Unlike most other ALTER
	 * routines, we have to do this one level of recursion at a time; we can't
	 * use find_all_inheritors to do it in one pass.
	 */
	children =
		find_inheritance_children(RelationGetRelid(rel), lockmode);

	/*
	 * Check if ONLY was specified with ALTER TABLE.  If so, allow the
	 * constraint creation only if there are no children currently.  Error out
	 * otherwise.
	 */
	if (!recurse && children != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("constraint must be added to child tables too")));

	foreach(child, children)
	{
		Oid			childrelid = lfirst_oid(child);
		Relation	childrel;
		AlteredTableInfo *childtab;

		/* find_inheritance_children already got lock */
		childrel = table_open(childrelid, NoLock);
		CheckTableNotInUse(childrel, "ALTER TABLE");

		/* Find or create work queue entry for this table */
		childtab = ATGetQueueEntry(wqueue, childrel);

		/* Recurse to child */
		ATAddCheckConstraint(wqueue, childtab, childrel,
							 constr, recurse, true, is_readd, lockmode);

		table_close(childrel, NoLock);
	}

	return address;
}

/*
 * Add a foreign-key constraint to a single table; return the new constraint's
 * address.
 *
 * Subroutine for ATExecAddConstraint.  Must already hold exclusive
 * lock on the rel, and have done appropriate validity checks for it.
 * We do permissions checks here, however.
 *
 * When the referenced or referencing tables (or both) are partitioned,
 * multiple pg_constraint rows are required -- one for each partitioned table
 * and each partition on each side (fortunately, not one for every combination
 * thereof).  We also need action triggers on each leaf partition on the
 * referenced side, and check triggers on each leaf partition on the
 * referencing side.
 */
static ObjectAddress
ATAddForeignKeyConstraint(List **wqueue, AlteredTableInfo *tab, Relation rel,
						  Constraint *fkconstraint,
						  bool recurse, bool recursing, LOCKMODE lockmode)
{
	Relation	pkrel;
	int16		pkattnum[INDEX_MAX_KEYS];
	int16		fkattnum[INDEX_MAX_KEYS];
	Oid			pktypoid[INDEX_MAX_KEYS];
	Oid			fktypoid[INDEX_MAX_KEYS];
	Oid			opclasses[INDEX_MAX_KEYS];
	Oid			pfeqoperators[INDEX_MAX_KEYS];
	Oid			ppeqoperators[INDEX_MAX_KEYS];
	Oid			ffeqoperators[INDEX_MAX_KEYS];
	int16		fkdelsetcols[INDEX_MAX_KEYS];
	int			i;
	int			numfks,
				numpks,
				numfkdelsetcols;
	Oid			indexOid;
	bool		old_check_ok;
	ObjectAddress address;
	ListCell   *old_pfeqop_item = list_head(fkconstraint->old_conpfeqop);

	/*
	 * Grab ShareRowExclusiveLock on the pk table, so that someone doesn't
	 * delete rows out from under us.
	 */
	if (OidIsValid(fkconstraint->old_pktable_oid))
		pkrel = table_open(fkconstraint->old_pktable_oid, ShareRowExclusiveLock);
	else
		pkrel = table_openrv(fkconstraint->pktable, ShareRowExclusiveLock);

	/*
	 * Validity checks (permission checks wait till we have the column
	 * numbers)
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		if (!recurse)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot use ONLY for foreign key on partitioned table \"%s\" referencing relation \"%s\"",
							RelationGetRelationName(rel),
							RelationGetRelationName(pkrel))));
		if (fkconstraint->skip_validation && !fkconstraint->initially_valid)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot add NOT VALID foreign key on partitioned table \"%s\" referencing relation \"%s\"",
							RelationGetRelationName(rel),
							RelationGetRelationName(pkrel)),
					 errdetail("This feature is not yet supported on partitioned tables.")));
	}

	if (pkrel->rd_rel->relkind != RELKIND_RELATION &&
		pkrel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("referenced relation \"%s\" is not a table",
						RelationGetRelationName(pkrel))));

	if (!allowSystemTableMods && IsSystemRelation(pkrel))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						RelationGetRelationName(pkrel))));

	/*
	 * References from permanent or unlogged tables to temp tables, and from
	 * permanent tables to unlogged tables, are disallowed because the
	 * referenced data can vanish out from under us.  References from temp
	 * tables to any other table type are also disallowed, because other
	 * backends might need to run the RI triggers on the perm table, but they
	 * can't reliably see tuples in the local buffers of other backends.
	 */
	switch (rel->rd_rel->relpersistence)
	{
		case RELPERSISTENCE_PERMANENT:
			if (!RelationIsPermanent(pkrel))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("constraints on permanent tables may reference only permanent tables")));
			break;
		case RELPERSISTENCE_UNLOGGED:
			if (!RelationIsPermanent(pkrel)
				&& pkrel->rd_rel->relpersistence != RELPERSISTENCE_UNLOGGED)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("constraints on unlogged tables may reference only permanent or unlogged tables")));
			break;
		case RELPERSISTENCE_TEMP:
			if (pkrel->rd_rel->relpersistence != RELPERSISTENCE_TEMP)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("constraints on temporary tables may reference only temporary tables")));
			if (!pkrel->rd_islocaltemp || !rel->rd_islocaltemp)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("constraints on temporary tables must involve temporary tables of this session")));
			break;
	}

	/*
	 * Look up the referencing attributes to make sure they exist, and record
	 * their attnums and type OIDs.
	 */
	MemSet(pkattnum, 0, sizeof(pkattnum));
	MemSet(fkattnum, 0, sizeof(fkattnum));
	MemSet(pktypoid, 0, sizeof(pktypoid));
	MemSet(fktypoid, 0, sizeof(fktypoid));
	MemSet(opclasses, 0, sizeof(opclasses));
	MemSet(pfeqoperators, 0, sizeof(pfeqoperators));
	MemSet(ppeqoperators, 0, sizeof(ppeqoperators));
	MemSet(ffeqoperators, 0, sizeof(ffeqoperators));
	MemSet(fkdelsetcols, 0, sizeof(fkdelsetcols));

	numfks = transformColumnNameList(RelationGetRelid(rel),
									 fkconstraint->fk_attrs,
									 fkattnum, fktypoid);

	numfkdelsetcols = transformColumnNameList(RelationGetRelid(rel),
											  fkconstraint->fk_del_set_cols,
											  fkdelsetcols, NULL);
	validateFkOnDeleteSetColumns(numfks, fkattnum,
								 numfkdelsetcols, fkdelsetcols,
								 fkconstraint->fk_del_set_cols);

	/*
	 * If the attribute list for the referenced table was omitted, lookup the
	 * definition of the primary key and use it.  Otherwise, validate the
	 * supplied attribute list.  In either case, discover the index OID and
	 * index opclasses, and the attnums and type OIDs of the attributes.
	 */
	if (fkconstraint->pk_attrs == NIL)
	{
		numpks = transformFkeyGetPrimaryKey(pkrel, &indexOid,
											&fkconstraint->pk_attrs,
											pkattnum, pktypoid,
											opclasses);
	}
	else
	{
		numpks = transformColumnNameList(RelationGetRelid(pkrel),
										 fkconstraint->pk_attrs,
										 pkattnum, pktypoid);
		/* Look for an index matching the column list */
		indexOid = transformFkeyCheckAttrs(pkrel, numpks, pkattnum,
										   opclasses);
	}

	/*
	 * Now we can check permissions.
	 */
	checkFkeyPermissions(pkrel, pkattnum, numpks);

	/*
	 * Check some things for generated columns.
	 */
	for (i = 0; i < numfks; i++)
	{
		char		attgenerated = TupleDescAttr(RelationGetDescr(rel), fkattnum[i] - 1)->attgenerated;

		if (attgenerated)
		{
			/*
			 * Check restrictions on UPDATE/DELETE actions, per SQL standard
			 */
			if (fkconstraint->fk_upd_action == FKCONSTR_ACTION_SETNULL ||
				fkconstraint->fk_upd_action == FKCONSTR_ACTION_SETDEFAULT ||
				fkconstraint->fk_upd_action == FKCONSTR_ACTION_CASCADE)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("invalid %s action for foreign key constraint containing generated column",
								"ON UPDATE")));
			if (fkconstraint->fk_del_action == FKCONSTR_ACTION_SETNULL ||
				fkconstraint->fk_del_action == FKCONSTR_ACTION_SETDEFAULT)
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("invalid %s action for foreign key constraint containing generated column",
								"ON DELETE")));
		}
	}

	/*
	 * Look up the equality operators to use in the constraint.
	 *
	 * Note that we have to be careful about the difference between the actual
	 * PK column type and the opclass' declared input type, which might be
	 * only binary-compatible with it.  The declared opcintype is the right
	 * thing to probe pg_amop with.
	 */
	if (numfks != numpks)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_FOREIGN_KEY),
				 errmsg("number of referencing and referenced columns for foreign key disagree")));

	/*
	 * On the strength of a previous constraint, we might avoid scanning
	 * tables to validate this one.  See below.
	 */
	old_check_ok = (fkconstraint->old_conpfeqop != NIL);
	Assert(!old_check_ok || numfks == list_length(fkconstraint->old_conpfeqop));

	for (i = 0; i < numpks; i++)
	{
		Oid			pktype = pktypoid[i];
		Oid			fktype = fktypoid[i];
		Oid			fktyped;
		HeapTuple	cla_ht;
		Form_pg_opclass cla_tup;
		Oid			amid;
		Oid			opfamily;
		Oid			opcintype;
		Oid			pfeqop;
		Oid			ppeqop;
		Oid			ffeqop;
		int16		eqstrategy;
		Oid			pfeqop_right;

		/* We need several fields out of the pg_opclass entry */
		cla_ht = SearchSysCache1(CLAOID, ObjectIdGetDatum(opclasses[i]));
		if (!HeapTupleIsValid(cla_ht))
			elog(ERROR, "cache lookup failed for opclass %u", opclasses[i]);
		cla_tup = (Form_pg_opclass) GETSTRUCT(cla_ht);
		amid = cla_tup->opcmethod;
		opfamily = cla_tup->opcfamily;
		opcintype = cla_tup->opcintype;
		ReleaseSysCache(cla_ht);

		/*
		 * Check it's a btree; currently this can never fail since no other
		 * index AMs support unique indexes.  If we ever did have other types
		 * of unique indexes, we'd need a way to determine which operator
		 * strategy number is equality.  (Is it reasonable to insist that
		 * every such index AM use btree's number for equality?)
		 */
		if (amid != BTREE_AM_OID && amid != LSM_AM_OID)
			elog(ERROR, "only b-tree and lsm indexes are supported for foreign keys");
		eqstrategy = BTEqualStrategyNumber;

		/*
		 * There had better be a primary equality operator for the index.
		 * We'll use it for PK = PK comparisons.
		 */
		ppeqop = get_opfamily_member(opfamily, opcintype, opcintype,
									 eqstrategy);

		if (!OidIsValid(ppeqop))
			elog(ERROR, "missing operator %d(%u,%u) in opfamily %u",
				 eqstrategy, opcintype, opcintype, opfamily);

		/*
		 * Are there equality operators that take exactly the FK type? Assume
		 * we should look through any domain here.
		 */
		fktyped = getBaseType(fktype);

		pfeqop = get_opfamily_member(opfamily, opcintype, fktyped,
									 eqstrategy);
		if (OidIsValid(pfeqop))
		{
			pfeqop_right = fktyped;
			ffeqop = get_opfamily_member(opfamily, fktyped, fktyped,
										 eqstrategy);
		}
		else
		{
			/* keep compiler quiet */
			pfeqop_right = InvalidOid;
			ffeqop = InvalidOid;
		}

		if (!(OidIsValid(pfeqop) && OidIsValid(ffeqop)))
		{
			/*
			 * Otherwise, look for an implicit cast from the FK type to the
			 * opcintype, and if found, use the primary equality operator.
			 * This is a bit tricky because opcintype might be a polymorphic
			 * type such as ANYARRAY or ANYENUM; so what we have to test is
			 * whether the two actual column types can be concurrently cast to
			 * that type.  (Otherwise, we'd fail to reject combinations such
			 * as int[] and point[].)
			 */
			Oid			input_typeids[2];
			Oid			target_typeids[2];

			input_typeids[0] = pktype;
			input_typeids[1] = fktype;
			target_typeids[0] = opcintype;
			target_typeids[1] = opcintype;
			if (can_coerce_type(2, input_typeids, target_typeids,
								COERCION_IMPLICIT))
			{
				pfeqop = ffeqop = ppeqop;
				pfeqop_right = opcintype;
			}
		}

		if (!(OidIsValid(pfeqop) && OidIsValid(ffeqop)))
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("foreign key constraint \"%s\" cannot be implemented",
							fkconstraint->conname),
					 errdetail("Key columns \"%s\" and \"%s\" "
							   "are of incompatible types: %s and %s.",
							   strVal(list_nth(fkconstraint->fk_attrs, i)),
							   strVal(list_nth(fkconstraint->pk_attrs, i)),
							   format_type_be(fktype),
							   format_type_be(pktype))));

		if (old_check_ok)
		{
			/*
			 * When a pfeqop changes, revalidate the constraint.  We could
			 * permit intra-opfamily changes, but that adds subtle complexity
			 * without any concrete benefit for core types.  We need not
			 * assess ppeqop or ffeqop, which RI_Initial_Check() does not use.
			 */
			old_check_ok = (pfeqop == lfirst_oid(old_pfeqop_item));
			old_pfeqop_item = lnext(fkconstraint->old_conpfeqop,
									old_pfeqop_item);
		}
		if (old_check_ok)
		{
			Oid			old_fktype;
			Oid			new_fktype;
			CoercionPathType old_pathtype;
			CoercionPathType new_pathtype;
			Oid			old_castfunc;
			Oid			new_castfunc;
			Form_pg_attribute attr = TupleDescAttr(tab->oldDesc,
												   fkattnum[i] - 1);

			/*
			 * Identify coercion pathways from each of the old and new FK-side
			 * column types to the right (foreign) operand type of the pfeqop.
			 * We may assume that pg_constraint.conkey is not changing.
			 */
			old_fktype = attr->atttypid;
			new_fktype = fktype;
			old_pathtype = findFkeyCast(pfeqop_right, old_fktype,
										&old_castfunc);
			new_pathtype = findFkeyCast(pfeqop_right, new_fktype,
										&new_castfunc);

			/*
			 * Upon a change to the cast from the FK column to its pfeqop
			 * operand, revalidate the constraint.  For this evaluation, a
			 * binary coercion cast is equivalent to no cast at all.  While
			 * type implementors should design implicit casts with an eye
			 * toward consistency of operations like equality, we cannot
			 * assume here that they have done so.
			 *
			 * A function with a polymorphic argument could change behavior
			 * arbitrarily in response to get_fn_expr_argtype().  Therefore,
			 * when the cast destination is polymorphic, we only avoid
			 * revalidation if the input type has not changed at all.  Given
			 * just the core data types and operator classes, this requirement
			 * prevents no would-be optimizations.
			 *
			 * If the cast converts from a base type to a domain thereon, then
			 * that domain type must be the opcintype of the unique index.
			 * Necessarily, the primary key column must then be of the domain
			 * type.  Since the constraint was previously valid, all values on
			 * the foreign side necessarily exist on the primary side and in
			 * turn conform to the domain.  Consequently, we need not treat
			 * domains specially here.
			 *
			 * Since we require that all collations share the same notion of
			 * equality (which they do, because texteq reduces to bitwise
			 * equality), we don't compare collation here.
			 *
			 * We need not directly consider the PK type.  It's necessarily
			 * binary coercible to the opcintype of the unique index column,
			 * and ri_triggers.c will only deal with PK datums in terms of
			 * that opcintype.  Changing the opcintype also changes pfeqop.
			 */
			old_check_ok = (new_pathtype == old_pathtype &&
							new_castfunc == old_castfunc &&
							(!IsPolymorphicType(pfeqop_right) ||
							 new_fktype == old_fktype));
		}

		pfeqoperators[i] = pfeqop;
		ppeqoperators[i] = ppeqop;
		ffeqoperators[i] = ffeqop;
	}

	/*
	 * Create all the constraint and trigger objects, recursing to partitions
	 * as necessary.  First handle the referenced side.
	 */
	address = addFkRecurseReferenced(wqueue, fkconstraint, rel, pkrel,
									 indexOid,
									 InvalidOid,	/* no parent constraint */
									 numfks,
									 pkattnum,
									 fkattnum,
									 pfeqoperators,
									 ppeqoperators,
									 ffeqoperators,
									 numfkdelsetcols,
									 fkdelsetcols,
									 old_check_ok,
									 InvalidOid, InvalidOid);

	/* Now handle the referencing side. */
	addFkRecurseReferencing(wqueue, fkconstraint, rel, pkrel,
							indexOid,
							address.objectId,
							numfks,
							pkattnum,
							fkattnum,
							pfeqoperators,
							ppeqoperators,
							ffeqoperators,
							numfkdelsetcols,
							fkdelsetcols,
							old_check_ok,
							lockmode,
							InvalidOid, InvalidOid);

	/*
	 * Done.  Close pk table, but keep lock until we've committed.
	 */
	table_close(pkrel, NoLock);

	return address;
}

/*
 * validateFkOnDeleteSetColumns
 *		Verifies that columns used in ON DELETE SET NULL/DEFAULT (...)
 *		column lists are valid.
 */
void
validateFkOnDeleteSetColumns(int numfks, const int16 *fkattnums,
							 int numfksetcols, const int16 *fksetcolsattnums,
							 List *fksetcols)
{
	for (int i = 0; i < numfksetcols; i++)
	{
		int16		setcol_attnum = fksetcolsattnums[i];
		bool		seen = false;

		for (int j = 0; j < numfks; j++)
		{
			if (fkattnums[j] == setcol_attnum)
			{
				seen = true;
				break;
			}
		}

		if (!seen)
		{
			char	   *col = strVal(list_nth(fksetcols, i));

			ereport(ERROR,
					(errcode(ERRCODE_INVALID_COLUMN_REFERENCE),
					 errmsg("column \"%s\" referenced in ON DELETE SET action must be part of foreign key", col)));
		}
	}
}

/*
 * addFkRecurseReferenced
 *		subroutine for ATAddForeignKeyConstraint; recurses on the referenced
 *		side of the constraint
 *
 * Create pg_constraint rows for the referenced side of the constraint,
 * referencing the parent of the referencing side; also create action triggers
 * on leaf partitions.  If the table is partitioned, recurse to handle each
 * partition.
 *
 * wqueue is the ALTER TABLE work queue; can be NULL when not running as part
 * of an ALTER TABLE sequence.
 * fkconstraint is the constraint being added.
 * rel is the root referencing relation.
 * pkrel is the referenced relation; might be a partition, if recursing.
 * indexOid is the OID of the index (on pkrel) implementing this constraint.
 * parentConstr is the OID of a parent constraint; InvalidOid if this is a
 * top-level constraint.
 * numfks is the number of columns in the foreign key
 * pkattnum is the attnum array of referenced attributes.
 * fkattnum is the attnum array of referencing attributes.
 * numfkdelsetcols is the number of columns in the ON DELETE SET NULL/DEFAULT
 *      (...) clause
 * fkdelsetcols is the attnum array of the columns in the ON DELETE SET
 *      NULL/DEFAULT clause
 * pf/pp/ffeqoperators are OID array of operators between columns.
 * old_check_ok signals that this constraint replaces an existing one that
 * was already validated (thus this one doesn't need validation).
 * parentDelTrigger and parentUpdTrigger, when being recursively called on
 * a partition, are the OIDs of the parent action triggers for DELETE and
 * UPDATE respectively.
 */
static ObjectAddress
addFkRecurseReferenced(List **wqueue, Constraint *fkconstraint, Relation rel,
					   Relation pkrel, Oid indexOid, Oid parentConstr,
					   int numfks,
					   int16 *pkattnum, int16 *fkattnum, Oid *pfeqoperators,
					   Oid *ppeqoperators, Oid *ffeqoperators,
					   int numfkdelsetcols, int16 *fkdelsetcols,
					   bool old_check_ok,
					   Oid parentDelTrigger, Oid parentUpdTrigger)
{
	ObjectAddress address;
	Oid			constrOid;
	char	   *conname;
	bool		conislocal;
	int			coninhcount;
	bool		connoinherit;
	Oid			deleteTriggerOid,
				updateTriggerOid;

	/*
	 * Verify relkind for each referenced partition.  At the top level, this
	 * is redundant with a previous check, but we need it when recursing.
	 */
	if (pkrel->rd_rel->relkind != RELKIND_RELATION &&
		pkrel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("referenced relation \"%s\" is not a table",
						RelationGetRelationName(pkrel))));

	/*
	 * Caller supplies us with a constraint name; however, it may be used in
	 * this partition, so come up with a different one in that case.
	 */
	if (ConstraintNameIsUsed(CONSTRAINT_RELATION,
							 RelationGetRelid(rel),
							 fkconstraint->conname))
		conname = ChooseConstraintName(RelationGetRelationName(rel),
									   ChooseForeignKeyConstraintNameAddition(fkconstraint->fk_attrs),
									   "fkey",
									   RelationGetNamespace(rel), NIL);
	else
		conname = fkconstraint->conname;

	if (OidIsValid(parentConstr))
	{
		conislocal = false;
		coninhcount = 1;
		connoinherit = false;
	}
	else
	{
		conislocal = true;
		coninhcount = 0;

		/*
		 * always inherit for partitioned tables, never for legacy inheritance
		 */
		connoinherit = rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE;
	}

	/*
	 * Record the FK constraint in pg_constraint.
	 */
	constrOid = CreateConstraintEntry(conname,
									  RelationGetNamespace(rel),
									  CONSTRAINT_FOREIGN,
									  fkconstraint->deferrable,
									  fkconstraint->initdeferred,
									  fkconstraint->initially_valid,
									  parentConstr,
									  RelationGetRelid(rel),
									  fkattnum,
									  numfks,
									  numfks,
									  InvalidOid,	/* not a domain constraint */
									  indexOid,
									  RelationGetRelid(pkrel),
									  pkattnum,
									  pfeqoperators,
									  ppeqoperators,
									  ffeqoperators,
									  numfks,
									  fkconstraint->fk_upd_action,
									  fkconstraint->fk_del_action,
									  fkdelsetcols,
									  numfkdelsetcols,
									  fkconstraint->fk_matchtype,
									  NULL, /* no exclusion constraint */
									  NULL, /* no check constraint */
									  NULL,
									  conislocal,	/* islocal */
									  coninhcount,	/* inhcount */
									  connoinherit, /* conNoInherit */
									  false);	/* is_internal */

	ObjectAddressSet(address, ConstraintRelationId, constrOid);

	/*
	 * Mark the child constraint as part of the parent constraint; it must not
	 * be dropped on its own.  (This constraint is deleted when the partition
	 * is detached, but a special check needs to occur that the partition
	 * contains no referenced values.)
	 */
	if (OidIsValid(parentConstr))
	{
		ObjectAddress referenced;

		ObjectAddressSet(referenced, ConstraintRelationId, parentConstr);
		recordDependencyOn(&address, &referenced, DEPENDENCY_INTERNAL);
	}

	/* make new constraint visible, in case we add more */
	CommandCounterIncrement();

	/*
	 * Create the action triggers that enforce the constraint.
	 */
	createForeignKeyActionTriggers(rel, RelationGetRelid(pkrel),
								   fkconstraint,
								   constrOid, indexOid,
								   parentDelTrigger, parentUpdTrigger,
								   &deleteTriggerOid, &updateTriggerOid);

	/*
	 * If the referenced table is partitioned, recurse on ourselves to handle
	 * each partition.  We need one pg_constraint row created for each
	 * partition in addition to the pg_constraint row for the parent table.
	 */
	if (pkrel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		PartitionDesc pd = RelationGetPartitionDesc(pkrel, true);

		for (int i = 0; i < pd->nparts; i++)
		{
			Relation	partRel;
			AttrMap    *map;
			AttrNumber *mapped_pkattnum;
			Oid			partIndexId;

			partRel = table_open(pd->oids[i], ShareRowExclusiveLock);

			/*
			 * Map the attribute numbers in the referenced side of the FK
			 * definition to match the partition's column layout.
			 */
			map = build_attrmap_by_name_if_req(RelationGetDescr(partRel),
											   RelationGetDescr(pkrel));
			if (map)
			{
				mapped_pkattnum = palloc(sizeof(AttrNumber) * numfks);
				for (int j = 0; j < numfks; j++)
					mapped_pkattnum[j] = map->attnums[pkattnum[j] - 1];
			}
			else
				mapped_pkattnum = pkattnum;

			/* do the deed */
			partIndexId = index_get_partition(partRel, indexOid);
			if (!OidIsValid(partIndexId))
				elog(ERROR, "index for %u not found in partition %s",
					 indexOid, RelationGetRelationName(partRel));
			addFkRecurseReferenced(wqueue, fkconstraint, rel, partRel,
								   partIndexId, constrOid, numfks,
								   mapped_pkattnum, fkattnum,
								   pfeqoperators, ppeqoperators, ffeqoperators,
								   numfkdelsetcols, fkdelsetcols,
								   old_check_ok,
								   deleteTriggerOid, updateTriggerOid);

			/* Done -- clean up (but keep the lock) */
			table_close(partRel, NoLock);
			if (map)
			{
				pfree(mapped_pkattnum);
				free_attrmap(map);
			}
		}
	}

	return address;
}

/*
 * addFkRecurseReferencing
 *		subroutine for ATAddForeignKeyConstraint and CloneFkReferencing
 *
 * If the referencing relation is a plain relation, create the necessary check
 * triggers that implement the constraint, and set up for Phase 3 constraint
 * verification.  If the referencing relation is a partitioned table, then
 * we create a pg_constraint row for it and recurse on this routine for each
 * partition.
 *
 * We assume that the referenced relation is locked against concurrent
 * deletions.  If it's a partitioned relation, every partition must be so
 * locked.
 *
 * wqueue is the ALTER TABLE work queue; can be NULL when not running as part
 * of an ALTER TABLE sequence.
 * fkconstraint is the constraint being added.
 * rel is the referencing relation; might be a partition, if recursing.
 * pkrel is the root referenced relation.
 * indexOid is the OID of the index (on pkrel) implementing this constraint.
 * parentConstr is the OID of the parent constraint (there is always one).
 * numfks is the number of columns in the foreign key
 * pkattnum is the attnum array of referenced attributes.
 * fkattnum is the attnum array of referencing attributes.
 * pf/pp/ffeqoperators are OID array of operators between columns.
 * numfkdelsetcols is the number of columns in the ON DELETE SET NULL/DEFAULT
 *      (...) clause
 * fkdelsetcols is the attnum array of the columns in the ON DELETE SET
 *      NULL/DEFAULT clause
 * old_check_ok signals that this constraint replaces an existing one that
 *		was already validated (thus this one doesn't need validation).
 * lockmode is the lockmode to acquire on partitions when recursing.
 * parentInsTrigger and parentUpdTrigger, when being recursively called on
 * a partition, are the OIDs of the parent check triggers for INSERT and
 * UPDATE respectively.
 */
static void
addFkRecurseReferencing(List **wqueue, Constraint *fkconstraint, Relation rel,
						Relation pkrel, Oid indexOid, Oid parentConstr,
						int numfks, int16 *pkattnum, int16 *fkattnum,
						Oid *pfeqoperators, Oid *ppeqoperators, Oid *ffeqoperators,
						int numfkdelsetcols, int16 *fkdelsetcols,
						bool old_check_ok, LOCKMODE lockmode,
						Oid parentInsTrigger, Oid parentUpdTrigger)
{
	Oid			insertTriggerOid,
				updateTriggerOid;

	AssertArg(OidIsValid(parentConstr));

	if (rel->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("foreign key constraints are not supported on foreign tables")));

	/*
	 * Add the check triggers to it and, if necessary, schedule it to be
	 * checked in Phase 3.
	 *
	 * If the relation is partitioned, drill down to do it to its partitions.
	 */
	createForeignKeyCheckTriggers(RelationGetRelid(rel),
								  RelationGetRelid(pkrel),
								  fkconstraint,
								  parentConstr,
								  indexOid,
								  parentInsTrigger, parentUpdTrigger,
								  &insertTriggerOid, &updateTriggerOid);

	if (rel->rd_rel->relkind == RELKIND_RELATION)
	{
		/*
		 * Tell Phase 3 to check that the constraint is satisfied by existing
		 * rows. We can skip this during table creation, when requested
		 * explicitly by specifying NOT VALID in an ADD FOREIGN KEY command,
		 * and when we're recreating a constraint following a SET DATA TYPE
		 * operation that did not impugn its validity.
		 */
		if (wqueue && !old_check_ok && !fkconstraint->skip_validation)
		{
			NewConstraint *newcon;
			AlteredTableInfo *tab;

			tab = ATGetQueueEntry(wqueue, rel);

			newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
			newcon->name = get_constraint_name(parentConstr);
			newcon->contype = CONSTR_FOREIGN;
			newcon->refrelid = RelationGetRelid(pkrel);
			newcon->refindid = indexOid;
			newcon->conid = parentConstr;
			newcon->qual = (Node *) fkconstraint;

			tab->constraints = lappend(tab->constraints, newcon);
		}
	}
	else if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		PartitionDesc pd = RelationGetPartitionDesc(rel, true);
		Relation	trigrel;

		/*
		 * Triggers of the foreign keys will be manipulated a bunch of times
		 * in the loop below.  To avoid repeatedly opening/closing the trigger
		 * catalog relation, we open it here and pass it to the subroutines
		 * called below.
		 */
		trigrel = table_open(TriggerRelationId, RowExclusiveLock);

		/*
		 * Recurse to take appropriate action on each partition; either we
		 * find an existing constraint to reparent to ours, or we create a new
		 * one.
		 */
		for (int i = 0; i < pd->nparts; i++)
		{
			Oid			partitionId = pd->oids[i];
			Relation	partition = table_open(partitionId, lockmode);
			List	   *partFKs;
			AttrMap    *attmap;
			AttrNumber	mapped_fkattnum[INDEX_MAX_KEYS];
			bool		attached;
			char	   *conname;
			Oid			constrOid;
			ObjectAddress address,
						referenced;
			ListCell   *cell;

			CheckTableNotInUse(partition, "ALTER TABLE");

			attmap = build_attrmap_by_name(RelationGetDescr(partition),
										   RelationGetDescr(rel),
										   false /* yb_ignore_type_mismatch */);
			for (int j = 0; j < numfks; j++)
				mapped_fkattnum[j] = attmap->attnums[fkattnum[j] - 1];

			/* Check whether an existing constraint can be repurposed */
			partFKs = copyObject(RelationGetFKeyList(partition));
			attached = false;
			foreach(cell, partFKs)
			{
				ForeignKeyCacheInfo *fk;

				fk = lfirst_node(ForeignKeyCacheInfo, cell);
				if (tryAttachPartitionForeignKey(fk,
												 partitionId,
												 parentConstr,
												 numfks,
												 mapped_fkattnum,
												 pkattnum,
												 pfeqoperators,
												 insertTriggerOid,
												 updateTriggerOid,
												 trigrel))
				{
					attached = true;
					break;
				}
			}
			if (attached)
			{
				table_close(partition, NoLock);
				continue;
			}

			/*
			 * No luck finding a good constraint to reuse; create our own.
			 */
			if (ConstraintNameIsUsed(CONSTRAINT_RELATION,
									 RelationGetRelid(partition),
									 fkconstraint->conname))
				conname = ChooseConstraintName(RelationGetRelationName(partition),
											   ChooseForeignKeyConstraintNameAddition(fkconstraint->fk_attrs),
											   "fkey",
											   RelationGetNamespace(partition), NIL);
			else
				conname = fkconstraint->conname;
			constrOid =
				CreateConstraintEntry(conname,
									  RelationGetNamespace(partition),
									  CONSTRAINT_FOREIGN,
									  fkconstraint->deferrable,
									  fkconstraint->initdeferred,
									  fkconstraint->initially_valid,
									  parentConstr,
									  partitionId,
									  mapped_fkattnum,
									  numfks,
									  numfks,
									  InvalidOid,
									  indexOid,
									  RelationGetRelid(pkrel),
									  pkattnum,
									  pfeqoperators,
									  ppeqoperators,
									  ffeqoperators,
									  numfks,
									  fkconstraint->fk_upd_action,
									  fkconstraint->fk_del_action,
									  fkdelsetcols,
									  numfkdelsetcols,
									  fkconstraint->fk_matchtype,
									  NULL,
									  NULL,
									  NULL,
									  false,
									  1,
									  false,
									  false);

			/*
			 * Give this constraint partition-type dependencies on the parent
			 * constraint as well as the table.
			 */
			ObjectAddressSet(address, ConstraintRelationId, constrOid);
			ObjectAddressSet(referenced, ConstraintRelationId, parentConstr);
			recordDependencyOn(&address, &referenced, DEPENDENCY_PARTITION_PRI);
			ObjectAddressSet(referenced, RelationRelationId, partitionId);
			recordDependencyOn(&address, &referenced, DEPENDENCY_PARTITION_SEC);

			/* Make all this visible before recursing */
			CommandCounterIncrement();

			/* call ourselves to finalize the creation and we're done */
			addFkRecurseReferencing(wqueue, fkconstraint, partition, pkrel,
									indexOid,
									constrOid,
									numfks,
									pkattnum,
									mapped_fkattnum,
									pfeqoperators,
									ppeqoperators,
									ffeqoperators,
									numfkdelsetcols,
									fkdelsetcols,
									old_check_ok,
									lockmode,
									insertTriggerOid,
									updateTriggerOid);

			table_close(partition, NoLock);
		}

		table_close(trigrel, RowExclusiveLock);
	}
}

/*
 * CloneForeignKeyConstraints
 *		Clone foreign keys from a partitioned table to a newly acquired
 *		partition.
 *
 * partitionRel is a partition of parentRel, so we can be certain that it has
 * the same columns with the same datatypes.  The columns may be in different
 * order, though.
 *
 * wqueue must be passed to set up phase 3 constraint checking, unless the
 * referencing-side partition is known to be empty (such as in CREATE TABLE /
 * PARTITION OF).
 */
static void
CloneForeignKeyConstraints(List **wqueue, Relation parentRel,
						   Relation partitionRel)
{
	/* This only works for declarative partitioning */
	Assert(parentRel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE);

	/*
	 * Clone constraints for which the parent is on the referenced side.
	 */
	CloneFkReferenced(parentRel, partitionRel);

	/*
	 * Now clone constraints where the parent is on the referencing side.
	 */
	CloneFkReferencing(wqueue, parentRel, partitionRel);
}

/*
 * CloneFkReferenced
 *		Subroutine for CloneForeignKeyConstraints
 *
 * Find all the FKs that have the parent relation on the referenced side;
 * clone those constraints to the given partition.  This is to be called
 * when the partition is being created or attached.
 *
 * This ignores self-referencing FKs; those are handled by CloneFkReferencing.
 *
 * This recurses to partitions, if the relation being attached is partitioned.
 * Recursion is done by calling addFkRecurseReferenced.
 */
static void
CloneFkReferenced(Relation parentRel, Relation partitionRel)
{
	Relation	pg_constraint;
	AttrMap    *attmap;
	ListCell   *cell;
	SysScanDesc scan;
	ScanKeyData key[2];
	HeapTuple	tuple;
	List	   *clone = NIL;
	Relation	trigrel;

	/*
	 * Search for any constraints where this partition's parent is in the
	 * referenced side.  However, we must not clone any constraint whose
	 * parent constraint is also going to be cloned, to avoid duplicates.  So
	 * do it in two steps: first construct the list of constraints to clone,
	 * then go over that list cloning those whose parents are not in the list.
	 * (We must not rely on the parent being seen first, since the catalog
	 * scan could return children first.)
	 */
	pg_constraint = table_open(ConstraintRelationId, RowShareLock);
	ScanKeyInit(&key[0],
				Anum_pg_constraint_confrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(parentRel)));
	ScanKeyInit(&key[1],
				Anum_pg_constraint_contype, BTEqualStrategyNumber,
				F_CHAREQ, CharGetDatum(CONSTRAINT_FOREIGN));
	/* This is a seqscan, as we don't have a usable index ... */
	scan = systable_beginscan(pg_constraint, InvalidOid, true,
							  NULL, 2, key);
	while ((tuple = systable_getnext(scan)) != NULL)
	{
		Form_pg_constraint constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		clone = lappend_oid(clone, constrForm->oid);
	}
	systable_endscan(scan);
	table_close(pg_constraint, RowShareLock);

	/*
	 * Triggers of the foreign keys will be manipulated a bunch of times in
	 * the loop below.  To avoid repeatedly opening/closing the trigger
	 * catalog relation, we open it here and pass it to the subroutines called
	 * below.
	 */
	trigrel = table_open(TriggerRelationId, RowExclusiveLock);

	attmap = build_attrmap_by_name(RelationGetDescr(partitionRel),
								   RelationGetDescr(parentRel),
								   false /* yb_ignore_type_mismatch */);
	foreach(cell, clone)
	{
		Oid			constrOid = lfirst_oid(cell);
		Form_pg_constraint constrForm;
		Relation	fkRel;
		Oid			indexOid;
		Oid			partIndexId;
		int			numfks;
		AttrNumber	conkey[INDEX_MAX_KEYS];
		AttrNumber	mapped_confkey[INDEX_MAX_KEYS];
		AttrNumber	confkey[INDEX_MAX_KEYS];
		Oid			conpfeqop[INDEX_MAX_KEYS];
		Oid			conppeqop[INDEX_MAX_KEYS];
		Oid			conffeqop[INDEX_MAX_KEYS];
		int			numfkdelsetcols;
		AttrNumber	confdelsetcols[INDEX_MAX_KEYS];
		Constraint *fkconstraint;
		Oid			deleteTriggerOid,
					updateTriggerOid;

		tuple = SearchSysCache1(CONSTROID, constrOid);
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for constraint %u", constrOid);
		constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		/*
		 * As explained above: don't try to clone a constraint for which we're
		 * going to clone the parent.
		 */
		if (list_member_oid(clone, constrForm->conparentid))
		{
			ReleaseSysCache(tuple);
			continue;
		}

		/*
		 * Don't clone self-referencing foreign keys, which can be in the
		 * partitioned table or in the partition-to-be.
		 */
		if (constrForm->conrelid == RelationGetRelid(parentRel) ||
			constrForm->conrelid == RelationGetRelid(partitionRel))
		{
			ReleaseSysCache(tuple);
			continue;
		}

		/*
		 * Because we're only expanding the key space at the referenced side,
		 * we don't need to prevent any operation in the referencing table, so
		 * AccessShareLock suffices (assumes that dropping the constraint
		 * acquires AEL).
		 */
		fkRel = table_open(constrForm->conrelid, AccessShareLock);

		indexOid = constrForm->conindid;
		DeconstructFkConstraintRow(tuple,
								   &numfks,
								   conkey,
								   confkey,
								   conpfeqop,
								   conppeqop,
								   conffeqop,
								   &numfkdelsetcols,
								   confdelsetcols);

		for (int i = 0; i < numfks; i++)
			mapped_confkey[i] = attmap->attnums[confkey[i] - 1];

		fkconstraint = makeNode(Constraint);
		fkconstraint->contype = CONSTRAINT_FOREIGN;
		fkconstraint->conname = NameStr(constrForm->conname);
		fkconstraint->deferrable = constrForm->condeferrable;
		fkconstraint->initdeferred = constrForm->condeferred;
		fkconstraint->location = -1;
		fkconstraint->pktable = NULL;
		/* ->fk_attrs determined below */
		fkconstraint->pk_attrs = NIL;
		fkconstraint->fk_matchtype = constrForm->confmatchtype;
		fkconstraint->fk_upd_action = constrForm->confupdtype;
		fkconstraint->fk_del_action = constrForm->confdeltype;
		fkconstraint->fk_del_set_cols = NIL;
		fkconstraint->old_conpfeqop = NIL;
		fkconstraint->old_pktable_oid = InvalidOid;
		fkconstraint->skip_validation = false;
		fkconstraint->initially_valid = true;

		/* set up colnames that are used to generate the constraint name */
		for (int i = 0; i < numfks; i++)
		{
			Form_pg_attribute att;

			att = TupleDescAttr(RelationGetDescr(fkRel),
								conkey[i] - 1);
			fkconstraint->fk_attrs = lappend(fkconstraint->fk_attrs,
											 makeString(NameStr(att->attname)));
		}

		/*
		 * Add the new foreign key constraint pointing to the new partition.
		 * Because this new partition appears in the referenced side of the
		 * constraint, we don't need to set up for Phase 3 check.
		 */
		partIndexId = index_get_partition(partitionRel, indexOid);
		if (!OidIsValid(partIndexId))
			elog(ERROR, "index for %u not found in partition %s",
				 indexOid, RelationGetRelationName(partitionRel));

		/*
		 * Get the "action" triggers belonging to the constraint to pass as
		 * parent OIDs for similar triggers that will be created on the
		 * partition in addFkRecurseReferenced().
		 */
		GetForeignKeyActionTriggers(trigrel, constrOid,
									constrForm->confrelid, constrForm->conrelid,
									&deleteTriggerOid, &updateTriggerOid);

		addFkRecurseReferenced(NULL,
							   fkconstraint,
							   fkRel,
							   partitionRel,
							   partIndexId,
							   constrOid,
							   numfks,
							   mapped_confkey,
							   conkey,
							   conpfeqop,
							   conppeqop,
							   conffeqop,
							   numfkdelsetcols,
							   confdelsetcols,
							   true,
							   deleteTriggerOid,
							   updateTriggerOid);

		table_close(fkRel, NoLock);
		ReleaseSysCache(tuple);
	}

	table_close(trigrel, RowExclusiveLock);
}

/*
 * CloneFkReferencing
 *		Subroutine for CloneForeignKeyConstraints
 *
 * For each FK constraint of the parent relation in the given list, find an
 * equivalent constraint in its partition relation that can be reparented;
 * if one cannot be found, create a new constraint in the partition as its
 * child.
 *
 * If wqueue is given, it is used to set up phase-3 verification for each
 * cloned constraint; if omitted, we assume that such verification is not
 * needed (example: the partition is being created anew).
 */
static void
CloneFkReferencing(List **wqueue, Relation parentRel, Relation partRel)
{
	AttrMap    *attmap;
	List	   *partFKs;
	List	   *clone = NIL;
	ListCell   *cell;
	Relation	trigrel;

	/* obtain a list of constraints that we need to clone */
	foreach(cell, RelationGetFKeyList(parentRel))
	{
		ForeignKeyCacheInfo *fk = lfirst(cell);

		clone = lappend_oid(clone, fk->conoid);
	}

	/*
	 * Silently do nothing if there's nothing to do.  In particular, this
	 * avoids throwing a spurious error for foreign tables.
	 */
	if (clone == NIL)
		return;

	if (partRel->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("foreign key constraints are not supported on foreign tables")));

	/*
	 * Triggers of the foreign keys will be manipulated a bunch of times in
	 * the loop below.  To avoid repeatedly opening/closing the trigger
	 * catalog relation, we open it here and pass it to the subroutines called
	 * below.
	 */
	trigrel = table_open(TriggerRelationId, RowExclusiveLock);

	/*
	 * The constraint key may differ, if the columns in the partition are
	 * different.  This map is used to convert them.
	 */
	attmap = build_attrmap_by_name(RelationGetDescr(partRel),
								   RelationGetDescr(parentRel),
								   false /* yb_ignore_type_mismatch */);

	partFKs = copyObject(RelationGetFKeyList(partRel));

	foreach(cell, clone)
	{
		Oid			parentConstrOid = lfirst_oid(cell);
		Form_pg_constraint constrForm;
		Relation	pkrel;
		HeapTuple	tuple;
		int			numfks;
		AttrNumber	conkey[INDEX_MAX_KEYS];
		AttrNumber	mapped_conkey[INDEX_MAX_KEYS];
		AttrNumber	confkey[INDEX_MAX_KEYS];
		Oid			conpfeqop[INDEX_MAX_KEYS];
		Oid			conppeqop[INDEX_MAX_KEYS];
		Oid			conffeqop[INDEX_MAX_KEYS];
		int			numfkdelsetcols;
		AttrNumber	confdelsetcols[INDEX_MAX_KEYS];
		Constraint *fkconstraint;
		bool		attached;
		Oid			indexOid;
		Oid			constrOid;
		ObjectAddress address,
					referenced;
		ListCell   *cell;
		Oid			insertTriggerOid,
					updateTriggerOid;

		tuple = SearchSysCache1(CONSTROID, parentConstrOid);
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for constraint %u",
				 parentConstrOid);
		constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		/* Don't clone constraints whose parents are being cloned */
		if (list_member_oid(clone, constrForm->conparentid))
		{
			ReleaseSysCache(tuple);
			continue;
		}

		/*
		 * Need to prevent concurrent deletions.  If pkrel is a partitioned
		 * relation, that means to lock all partitions.
		 */
		pkrel = table_open(constrForm->confrelid, ShareRowExclusiveLock);
		if (pkrel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
			(void) find_all_inheritors(RelationGetRelid(pkrel),
									   ShareRowExclusiveLock, NULL);

		DeconstructFkConstraintRow(tuple, &numfks, conkey, confkey,
								   conpfeqop, conppeqop, conffeqop,
								   &numfkdelsetcols, confdelsetcols);
		for (int i = 0; i < numfks; i++)
			mapped_conkey[i] = attmap->attnums[conkey[i] - 1];

		/*
		 * Get the "check" triggers belonging to the constraint to pass as
		 * parent OIDs for similar triggers that will be created on the
		 * partition in addFkRecurseReferencing().  They are also passed to
		 * tryAttachPartitionForeignKey() below to simply assign as parents to
		 * the partition's existing "check" triggers, that is, if the
		 * corresponding constraints is deemed attachable to the parent
		 * constraint.
		 */
		GetForeignKeyCheckTriggers(trigrel, constrForm->oid,
								   constrForm->confrelid, constrForm->conrelid,
								   &insertTriggerOid, &updateTriggerOid);

		/*
		 * Before creating a new constraint, see whether any existing FKs are
		 * fit for the purpose.  If one is, attach the parent constraint to
		 * it, and don't clone anything.  This way we avoid the expensive
		 * verification step and don't end up with a duplicate FK, and we
		 * don't need to recurse to partitions for this constraint.
		 */
		attached = false;
		foreach(cell, partFKs)
		{
			ForeignKeyCacheInfo *fk = lfirst_node(ForeignKeyCacheInfo, cell);

			if (tryAttachPartitionForeignKey(fk,
											 RelationGetRelid(partRel),
											 parentConstrOid,
											 numfks,
											 mapped_conkey,
											 confkey,
											 conpfeqop,
											 insertTriggerOid,
											 updateTriggerOid,
											 trigrel))
			{
				attached = true;
				table_close(pkrel, NoLock);
				break;
			}
		}
		if (attached)
		{
			ReleaseSysCache(tuple);
			continue;
		}

		/* No dice.  Set up to create our own constraint */
		fkconstraint = makeNode(Constraint);
		fkconstraint->contype = CONSTRAINT_FOREIGN;
		/* ->conname determined below */
		fkconstraint->deferrable = constrForm->condeferrable;
		fkconstraint->initdeferred = constrForm->condeferred;
		fkconstraint->location = -1;
		fkconstraint->pktable = NULL;
		/* ->fk_attrs determined below */
		fkconstraint->pk_attrs = NIL;
		fkconstraint->fk_matchtype = constrForm->confmatchtype;
		fkconstraint->fk_upd_action = constrForm->confupdtype;
		fkconstraint->fk_del_action = constrForm->confdeltype;
		fkconstraint->fk_del_set_cols = NIL;
		fkconstraint->old_conpfeqop = NIL;
		fkconstraint->old_pktable_oid = InvalidOid;
		fkconstraint->skip_validation = false;
		fkconstraint->initially_valid = true;
		for (int i = 0; i < numfks; i++)
		{
			Form_pg_attribute att;

			att = TupleDescAttr(RelationGetDescr(partRel),
								mapped_conkey[i] - 1);
			fkconstraint->fk_attrs = lappend(fkconstraint->fk_attrs,
											 makeString(NameStr(att->attname)));
		}
		if (ConstraintNameIsUsed(CONSTRAINT_RELATION,
								 RelationGetRelid(partRel),
								 NameStr(constrForm->conname)))
			fkconstraint->conname =
				ChooseConstraintName(RelationGetRelationName(partRel),
									 ChooseForeignKeyConstraintNameAddition(fkconstraint->fk_attrs),
									 "fkey",
									 RelationGetNamespace(partRel), NIL);
		else
			fkconstraint->conname = pstrdup(NameStr(constrForm->conname));

		indexOid = constrForm->conindid;
		constrOid =
			CreateConstraintEntry(fkconstraint->conname,
								  constrForm->connamespace,
								  CONSTRAINT_FOREIGN,
								  fkconstraint->deferrable,
								  fkconstraint->initdeferred,
								  constrForm->convalidated,
								  parentConstrOid,
								  RelationGetRelid(partRel),
								  mapped_conkey,
								  numfks,
								  numfks,
								  InvalidOid,	/* not a domain constraint */
								  indexOid,
								  constrForm->confrelid,	/* same foreign rel */
								  confkey,
								  conpfeqop,
								  conppeqop,
								  conffeqop,
								  numfks,
								  fkconstraint->fk_upd_action,
								  fkconstraint->fk_del_action,
								  confdelsetcols,
								  numfkdelsetcols,
								  fkconstraint->fk_matchtype,
								  NULL,
								  NULL,
								  NULL,
								  false,	/* islocal */
								  1,	/* inhcount */
								  false,	/* conNoInherit */
								  true);

		/* Set up partition dependencies for the new constraint */
		ObjectAddressSet(address, ConstraintRelationId, constrOid);
		ObjectAddressSet(referenced, ConstraintRelationId, parentConstrOid);
		recordDependencyOn(&address, &referenced, DEPENDENCY_PARTITION_PRI);
		ObjectAddressSet(referenced, RelationRelationId,
						 RelationGetRelid(partRel));
		recordDependencyOn(&address, &referenced, DEPENDENCY_PARTITION_SEC);

		/* Done with the cloned constraint's tuple */
		ReleaseSysCache(tuple);

		/* Make all this visible before recursing */
		CommandCounterIncrement();

		addFkRecurseReferencing(wqueue,
								fkconstraint,
								partRel,
								pkrel,
								indexOid,
								constrOid,
								numfks,
								confkey,
								mapped_conkey,
								conpfeqop,
								conppeqop,
								conffeqop,
								numfkdelsetcols,
								confdelsetcols,
								false,	/* no old check exists */
								AccessExclusiveLock,
								insertTriggerOid,
								updateTriggerOid);
		table_close(pkrel, NoLock);
	}

	table_close(trigrel, RowExclusiveLock);
}

/*
 * When the parent of a partition receives [the referencing side of] a foreign
 * key, we must propagate that foreign key to the partition.  However, the
 * partition might already have an equivalent foreign key; this routine
 * compares the given ForeignKeyCacheInfo (in the partition) to the FK defined
 * by the other parameters.  If they are equivalent, create the link between
 * the two constraints and return true.
 *
 * If the given FK does not match the one defined by rest of the params,
 * return false.
 */
static bool
tryAttachPartitionForeignKey(ForeignKeyCacheInfo *fk,
							 Oid partRelid,
							 Oid parentConstrOid,
							 int numfks,
							 AttrNumber *mapped_conkey,
							 AttrNumber *confkey,
							 Oid *conpfeqop,
							 Oid parentInsTrigger,
							 Oid parentUpdTrigger,
							 Relation trigrel)
{
	HeapTuple	parentConstrTup;
	Form_pg_constraint parentConstr;
	HeapTuple	partcontup;
	Form_pg_constraint partConstr;
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	trigtup;
	Oid			insertTriggerOid,
				updateTriggerOid;

	parentConstrTup = SearchSysCache1(CONSTROID,
									  ObjectIdGetDatum(parentConstrOid));
	if (!HeapTupleIsValid(parentConstrTup))
		elog(ERROR, "cache lookup failed for constraint %u", parentConstrOid);
	parentConstr = (Form_pg_constraint) GETSTRUCT(parentConstrTup);

	/*
	 * Do some quick & easy initial checks.  If any of these fail, we cannot
	 * use this constraint.
	 */
	if (fk->confrelid != parentConstr->confrelid || fk->nkeys != numfks)
	{
		ReleaseSysCache(parentConstrTup);
		return false;
	}
	for (int i = 0; i < numfks; i++)
	{
		if (fk->conkey[i] != mapped_conkey[i] ||
			fk->confkey[i] != confkey[i] ||
			fk->conpfeqop[i] != conpfeqop[i])
		{
			ReleaseSysCache(parentConstrTup);
			return false;
		}
	}

	/*
	 * Looks good so far; do some more extensive checks.  Presumably the check
	 * for 'convalidated' could be dropped, since we don't really care about
	 * that, but let's be careful for now.
	 */
	partcontup = SearchSysCache1(CONSTROID,
								 ObjectIdGetDatum(fk->conoid));
	if (!HeapTupleIsValid(partcontup))
		elog(ERROR, "cache lookup failed for constraint %u", fk->conoid);
	partConstr = (Form_pg_constraint) GETSTRUCT(partcontup);
	if (OidIsValid(partConstr->conparentid) ||
		!partConstr->convalidated ||
		partConstr->condeferrable != parentConstr->condeferrable ||
		partConstr->condeferred != parentConstr->condeferred ||
		partConstr->confupdtype != parentConstr->confupdtype ||
		partConstr->confdeltype != parentConstr->confdeltype ||
		partConstr->confmatchtype != parentConstr->confmatchtype)
	{
		ReleaseSysCache(parentConstrTup);
		ReleaseSysCache(partcontup);
		return false;
	}

	ReleaseSysCache(partcontup);
	ReleaseSysCache(parentConstrTup);

	/*
	 * Looks good!  Attach this constraint.  The action triggers in the new
	 * partition become redundant -- the parent table already has equivalent
	 * ones, and those will be able to reach the partition.  Remove the ones
	 * in the partition.  We identify them because they have our constraint
	 * OID, as well as being on the referenced rel.
	 */
	ScanKeyInit(&key,
				Anum_pg_trigger_tgconstraint,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(fk->conoid));
	scan = systable_beginscan(trigrel, TriggerConstraintIndexId, true,
							  NULL, 1, &key);
	while ((trigtup = systable_getnext(scan)) != NULL)
	{
		Form_pg_trigger trgform = (Form_pg_trigger) GETSTRUCT(trigtup);
		ObjectAddress trigger;

		if (trgform->tgconstrrelid != fk->conrelid)
			continue;
		if (trgform->tgrelid != fk->confrelid)
			continue;

		/*
		 * The constraint is originally set up to contain this trigger as an
		 * implementation object, so there's a dependency record that links
		 * the two; however, since the trigger is no longer needed, we remove
		 * the dependency link in order to be able to drop the trigger while
		 * keeping the constraint intact.
		 */
		deleteDependencyRecordsFor(TriggerRelationId,
								   trgform->oid,
								   false);
		/* make dependency deletion visible to performDeletion */
		CommandCounterIncrement();
		ObjectAddressSet(trigger, TriggerRelationId,
						 trgform->oid);
		performDeletion(&trigger, DROP_RESTRICT, 0);
		/* make trigger drop visible, in case the loop iterates */
		CommandCounterIncrement();
	}

	systable_endscan(scan);

	ConstraintSetParentConstraint(fk->conoid, parentConstrOid, partRelid);

	/*
	 * Like the constraint, attach partition's "check" triggers to the
	 * corresponding parent triggers.
	 */
	GetForeignKeyCheckTriggers(trigrel,
							   fk->conoid, fk->confrelid, fk->conrelid,
							   &insertTriggerOid, &updateTriggerOid);
	Assert(OidIsValid(insertTriggerOid) && OidIsValid(parentInsTrigger));
	TriggerSetParentTrigger(trigrel, insertTriggerOid, parentInsTrigger,
							partRelid);
	Assert(OidIsValid(updateTriggerOid) && OidIsValid(parentUpdTrigger));
	TriggerSetParentTrigger(trigrel, updateTriggerOid, parentUpdTrigger,
							partRelid);

	CommandCounterIncrement();
	return true;
}

/*
 * GetForeignKeyActionTriggers
 * 		Returns delete and update "action" triggers of the given relation
 * 		belonging to the given constraint
 */
static void
GetForeignKeyActionTriggers(Relation trigrel,
							Oid conoid, Oid confrelid, Oid conrelid,
							Oid *deleteTriggerOid,
							Oid *updateTriggerOid)
{
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	trigtup;

	*deleteTriggerOid = *updateTriggerOid = InvalidOid;
	ScanKeyInit(&key,
				Anum_pg_trigger_tgconstraint,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(conoid));

	scan = systable_beginscan(trigrel, TriggerConstraintIndexId, true,
							  NULL, 1, &key);
	while ((trigtup = systable_getnext(scan)) != NULL)
	{
		Form_pg_trigger trgform = (Form_pg_trigger) GETSTRUCT(trigtup);

		if (trgform->tgconstrrelid != conrelid)
			continue;
		if (trgform->tgrelid != confrelid)
			continue;
		/* Only ever look at "action" triggers on the PK side. */
		if (RI_FKey_trigger_type(trgform->tgfoid) != RI_TRIGGER_PK)
			continue;
		if (TRIGGER_FOR_DELETE(trgform->tgtype))
		{
			Assert(*deleteTriggerOid == InvalidOid);
			*deleteTriggerOid = trgform->oid;
		}
		else if (TRIGGER_FOR_UPDATE(trgform->tgtype))
		{
			Assert(*updateTriggerOid == InvalidOid);
			*updateTriggerOid = trgform->oid;
		}
#ifndef USE_ASSERT_CHECKING
		/* In an assert-enabled build, continue looking to find duplicates */
		if (OidIsValid(*deleteTriggerOid) && OidIsValid(*updateTriggerOid))
			break;
#endif
	}

	if (!OidIsValid(*deleteTriggerOid))
		elog(ERROR, "could not find ON DELETE action trigger of foreign key constraint %u",
			 conoid);
	if (!OidIsValid(*updateTriggerOid))
		elog(ERROR, "could not find ON UPDATE action trigger of foreign key constraint %u",
			 conoid);

	systable_endscan(scan);
}

/*
 * GetForeignKeyCheckTriggers
 * 		Returns insert and update "check" triggers of the given relation
 * 		belonging to the given constraint
 */
static void
GetForeignKeyCheckTriggers(Relation trigrel,
						   Oid conoid, Oid confrelid, Oid conrelid,
						   Oid *insertTriggerOid,
						   Oid *updateTriggerOid)
{
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	trigtup;

	*insertTriggerOid = *updateTriggerOid = InvalidOid;
	ScanKeyInit(&key,
				Anum_pg_trigger_tgconstraint,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(conoid));

	scan = systable_beginscan(trigrel, TriggerConstraintIndexId, true,
							  NULL, 1, &key);
	while ((trigtup = systable_getnext(scan)) != NULL)
	{
		Form_pg_trigger trgform = (Form_pg_trigger) GETSTRUCT(trigtup);

		if (trgform->tgconstrrelid != confrelid)
			continue;
		if (trgform->tgrelid != conrelid)
			continue;
		/* Only ever look at "check" triggers on the FK side. */
		if (RI_FKey_trigger_type(trgform->tgfoid) != RI_TRIGGER_FK)
			continue;
		if (TRIGGER_FOR_INSERT(trgform->tgtype))
		{
			Assert(*insertTriggerOid == InvalidOid);
			*insertTriggerOid = trgform->oid;
		}
		else if (TRIGGER_FOR_UPDATE(trgform->tgtype))
		{
			Assert(*updateTriggerOid == InvalidOid);
			*updateTriggerOid = trgform->oid;
		}
#ifndef USE_ASSERT_CHECKING
		/* In an assert-enabled build, continue looking to find duplicates. */
		if (OidIsValid(*insertTriggerOid) && OidIsValid(*updateTriggerOid))
			break;
#endif
	}

	if (!OidIsValid(*insertTriggerOid))
		elog(ERROR, "could not find ON INSERT check triggers of foreign key constraint %u",
			 conoid);
	if (!OidIsValid(*updateTriggerOid))
		elog(ERROR, "could not find ON UPDATE check triggers of foreign key constraint %u",
			 conoid);

	systable_endscan(scan);
}

/*
 * ALTER TABLE ALTER CONSTRAINT
 *
 * Update the attributes of a constraint.
 *
 * Currently only works for Foreign Key constraints.
 *
 * If the constraint is modified, returns its address; otherwise, return
 * InvalidObjectAddress.
 */
static ObjectAddress
ATExecAlterConstraint(Relation rel, AlterTableCmd *cmd, bool recurse,
					  bool recursing, LOCKMODE lockmode)
{
	Constraint *cmdcon;
	Relation	conrel;
	Relation	tgrel;
	SysScanDesc scan;
	ScanKeyData skey[3];
	HeapTuple	contuple;
	Form_pg_constraint currcon;
	ObjectAddress address;
	List	   *otherrelids = NIL;
	ListCell   *lc;

	cmdcon = castNode(Constraint, cmd->def);

	conrel = table_open(ConstraintRelationId, RowExclusiveLock);
	tgrel = table_open(TriggerRelationId, RowExclusiveLock);

	/*
	 * Find and check the target constraint
	 */
	ScanKeyInit(&skey[0],
				Anum_pg_constraint_conrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(rel)));
	ScanKeyInit(&skey[1],
				Anum_pg_constraint_contypid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(InvalidOid));
	ScanKeyInit(&skey[2],
				Anum_pg_constraint_conname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(cmdcon->conname));
	scan = systable_beginscan(conrel, ConstraintRelidTypidNameIndexId,
							  true, NULL, 3, skey);

	/* There can be at most one matching row */
	if (!HeapTupleIsValid(contuple = systable_getnext(scan)))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("constraint \"%s\" of relation \"%s\" does not exist",
						cmdcon->conname, RelationGetRelationName(rel))));

	currcon = (Form_pg_constraint) GETSTRUCT(contuple);
	if (currcon->contype != CONSTRAINT_FOREIGN)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("constraint \"%s\" of relation \"%s\" is not a foreign key constraint",
						cmdcon->conname, RelationGetRelationName(rel))));

	/*
	 * If it's not the topmost constraint, raise an error.
	 *
	 * Altering a non-topmost constraint leaves some triggers untouched, since
	 * they are not directly connected to this constraint; also, pg_dump would
	 * ignore the deferrability status of the individual constraint, since it
	 * only dumps topmost constraints.  Avoid these problems by refusing this
	 * operation and telling the user to alter the parent constraint instead.
	 */
	if (OidIsValid(currcon->conparentid))
	{
		HeapTuple	tp;
		Oid			parent = currcon->conparentid;
		char	   *ancestorname = NULL;
		char	   *ancestortable = NULL;

		/* Loop to find the topmost constraint */
		while (HeapTupleIsValid(tp = SearchSysCache1(CONSTROID, ObjectIdGetDatum(parent))))
		{
			Form_pg_constraint contup = (Form_pg_constraint) GETSTRUCT(tp);

			/* If no parent, this is the constraint we want */
			if (!OidIsValid(contup->conparentid))
			{
				ancestorname = pstrdup(NameStr(contup->conname));
				ancestortable = get_rel_name(contup->conrelid);
				ReleaseSysCache(tp);
				break;
			}

			parent = contup->conparentid;
			ReleaseSysCache(tp);
		}

		ereport(ERROR,
				(errmsg("cannot alter constraint \"%s\" on relation \"%s\"",
						cmdcon->conname, RelationGetRelationName(rel)),
				 ancestorname && ancestortable ?
				 errdetail("Constraint \"%s\" is derived from constraint \"%s\" of relation \"%s\".",
						   cmdcon->conname, ancestorname, ancestortable) : 0,
				 errhint("You may alter the constraint it derives from, instead.")));
	}

	/*
	 * Do the actual catalog work.  We can skip changing if already in the
	 * desired state, but not if a partitioned table: partitions need to be
	 * processed regardless, in case they had the constraint locally changed.
	 */
	address = InvalidObjectAddress;
	if (currcon->condeferrable != cmdcon->deferrable ||
		currcon->condeferred != cmdcon->initdeferred ||
		rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		if (ATExecAlterConstrRecurse(cmdcon, conrel, tgrel, rel, contuple,
									 &otherrelids, lockmode))
			ObjectAddressSet(address, ConstraintRelationId, currcon->oid);
	}

	/*
	 * ATExecConstrRecurse already invalidated relcache for the relations
	 * having the constraint itself; here we also invalidate for relations
	 * that have any triggers that are part of the constraint.
	 */
	foreach(lc, otherrelids)
		CacheInvalidateRelcacheByRelid(lfirst_oid(lc));

	systable_endscan(scan);

	table_close(tgrel, RowExclusiveLock);
	table_close(conrel, RowExclusiveLock);

	return address;
}

/*
 * Recursive subroutine of ATExecAlterConstraint.  Returns true if the
 * constraint is altered.
 *
 * *otherrelids is appended OIDs of relations containing affected triggers.
 *
 * Note that we must recurse even when the values are correct, in case
 * indirect descendants have had their constraints altered locally.
 * (This could be avoided if we forbade altering constraints in partitions
 * but existing releases don't do that.)
 */
static bool
ATExecAlterConstrRecurse(Constraint *cmdcon, Relation conrel, Relation tgrel,
						 Relation rel, HeapTuple contuple, List **otherrelids,
						 LOCKMODE lockmode)
{
	Form_pg_constraint currcon;
	Oid			conoid;
	Oid			refrelid;
	bool		changed = false;

	currcon = (Form_pg_constraint) GETSTRUCT(contuple);
	conoid = currcon->oid;
	refrelid = currcon->confrelid;

	/*
	 * Update pg_constraint with the flags from cmdcon.
	 *
	 * If called to modify a constraint that's already in the desired state,
	 * silently do nothing.
	 */
	if (currcon->condeferrable != cmdcon->deferrable ||
		currcon->condeferred != cmdcon->initdeferred)
	{
		HeapTuple	copyTuple;
		Form_pg_constraint copy_con;
		HeapTuple	tgtuple;
		ScanKeyData tgkey;
		SysScanDesc tgscan;

		copyTuple = heap_copytuple(contuple);
		copy_con = (Form_pg_constraint) GETSTRUCT(copyTuple);
		copy_con->condeferrable = cmdcon->deferrable;
		copy_con->condeferred = cmdcon->initdeferred;
		CatalogTupleUpdate(conrel, &copyTuple->t_self, copyTuple);

		InvokeObjectPostAlterHook(ConstraintRelationId,
								  conoid, 0);

		heap_freetuple(copyTuple);
		changed = true;

		/* Make new constraint flags visible to others */
		CacheInvalidateRelcache(rel);

		/*
		 * Now we need to update the multiple entries in pg_trigger that
		 * implement the constraint.
		 */
		ScanKeyInit(&tgkey,
					Anum_pg_trigger_tgconstraint,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(conoid));
		tgscan = systable_beginscan(tgrel, TriggerConstraintIndexId, true,
									NULL, 1, &tgkey);
		while (HeapTupleIsValid(tgtuple = systable_getnext(tgscan)))
		{
			Form_pg_trigger tgform = (Form_pg_trigger) GETSTRUCT(tgtuple);
			Form_pg_trigger copy_tg;
			HeapTuple	copyTuple;

			/*
			 * Remember OIDs of other relation(s) involved in FK constraint.
			 * (Note: it's likely that we could skip forcing a relcache inval
			 * for other rels that don't have a trigger whose properties
			 * change, but let's be conservative.)
			 */
			if (tgform->tgrelid != RelationGetRelid(rel))
				*otherrelids = list_append_unique_oid(*otherrelids,
													  tgform->tgrelid);

			/*
			 * Update deferrability of RI_FKey_noaction_del,
			 * RI_FKey_noaction_upd, RI_FKey_check_ins and RI_FKey_check_upd
			 * triggers, but not others; see createForeignKeyActionTriggers
			 * and CreateFKCheckTrigger.
			 */
			if (tgform->tgfoid != F_RI_FKEY_NOACTION_DEL &&
				tgform->tgfoid != F_RI_FKEY_NOACTION_UPD &&
				tgform->tgfoid != F_RI_FKEY_CHECK_INS &&
				tgform->tgfoid != F_RI_FKEY_CHECK_UPD)
				continue;

			copyTuple = heap_copytuple(tgtuple);
			copy_tg = (Form_pg_trigger) GETSTRUCT(copyTuple);

			copy_tg->tgdeferrable = cmdcon->deferrable;
			copy_tg->tginitdeferred = cmdcon->initdeferred;
			CatalogTupleUpdate(tgrel, &copyTuple->t_self, copyTuple);

			InvokeObjectPostAlterHook(TriggerRelationId, tgform->oid, 0);

			heap_freetuple(copyTuple);
		}

		systable_endscan(tgscan);
	}

	/*
	 * If the table at either end of the constraint is partitioned, we need to
	 * recurse and handle every constraint that is a child of this one.
	 *
	 * (This assumes that the recurse flag is forcibly set for partitioned
	 * tables, and not set for legacy inheritance, though we don't check for
	 * that here.)
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE ||
		get_rel_relkind(refrelid) == RELKIND_PARTITIONED_TABLE)
	{
		ScanKeyData pkey;
		SysScanDesc pscan;
		HeapTuple	childtup;

		ScanKeyInit(&pkey,
					Anum_pg_constraint_conparentid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(conoid));

		pscan = systable_beginscan(conrel, ConstraintParentIndexId,
								   true, NULL, 1, &pkey);

		while (HeapTupleIsValid(childtup = systable_getnext(pscan)))
		{
			Form_pg_constraint childcon = (Form_pg_constraint) GETSTRUCT(childtup);
			Relation	childrel;

			childrel = table_open(childcon->conrelid, lockmode);
			ATExecAlterConstrRecurse(cmdcon, conrel, tgrel, childrel, childtup,
									 otherrelids, lockmode);
			table_close(childrel, NoLock);
		}

		systable_endscan(pscan);
	}

	return changed;
}

/*
 * ALTER TABLE VALIDATE CONSTRAINT
 *
 * XXX The reason we handle recursion here rather than at Phase 1 is because
 * there's no good way to skip recursing when handling foreign keys: there is
 * no need to lock children in that case, yet we wouldn't be able to avoid
 * doing so at that level.
 *
 * Return value is the address of the validated constraint.  If the constraint
 * was already validated, InvalidObjectAddress is returned.
 */
static ObjectAddress
ATExecValidateConstraint(List **wqueue, Relation rel, char *constrName,
						 bool recurse, bool recursing, LOCKMODE lockmode)
{
	Relation	conrel;
	SysScanDesc scan;
	ScanKeyData skey[3];
	HeapTuple	tuple;
	Form_pg_constraint con;
	ObjectAddress address;

	conrel = table_open(ConstraintRelationId, RowExclusiveLock);

	/*
	 * Find and check the target constraint
	 */
	ScanKeyInit(&skey[0],
				Anum_pg_constraint_conrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(rel)));
	ScanKeyInit(&skey[1],
				Anum_pg_constraint_contypid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(InvalidOid));
	ScanKeyInit(&skey[2],
				Anum_pg_constraint_conname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(constrName));
	scan = systable_beginscan(conrel, ConstraintRelidTypidNameIndexId,
							  true, NULL, 3, skey);

	/* There can be at most one matching row */
	if (!HeapTupleIsValid(tuple = systable_getnext(scan)))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("constraint \"%s\" of relation \"%s\" does not exist",
						constrName, RelationGetRelationName(rel))));

	con = (Form_pg_constraint) GETSTRUCT(tuple);
	if (con->contype != CONSTRAINT_FOREIGN &&
		con->contype != CONSTRAINT_CHECK)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("constraint \"%s\" of relation \"%s\" is not a foreign key or check constraint",
						constrName, RelationGetRelationName(rel))));

	if (!con->convalidated)
	{
		AlteredTableInfo *tab;
		HeapTuple	copyTuple;
		Form_pg_constraint copy_con;

		if (con->contype == CONSTRAINT_FOREIGN)
		{
			NewConstraint *newcon;
			Constraint *fkconstraint;

			/* Queue validation for phase 3 */
			fkconstraint = makeNode(Constraint);
			/* for now this is all we need */
			fkconstraint->conname = constrName;

			newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
			newcon->name = constrName;
			newcon->contype = CONSTR_FOREIGN;
			newcon->refrelid = con->confrelid;
			newcon->refindid = con->conindid;
			newcon->conid = con->oid;
			newcon->qual = (Node *) fkconstraint;

			/* Find or create work queue entry for this table */
			tab = ATGetQueueEntry(wqueue, rel);
			tab->constraints = lappend(tab->constraints, newcon);

			/*
			 * We disallow creating invalid foreign keys to or from
			 * partitioned tables, so ignoring the recursion bit is okay.
			 */
		}
		else if (con->contype == CONSTRAINT_CHECK)
		{
			List	   *children = NIL;
			ListCell   *child;
			NewConstraint *newcon;
			bool		isnull;
			Datum		val;
			char	   *conbin;

			/*
			 * If we're recursing, the parent has already done this, so skip
			 * it.  Also, if the constraint is a NO INHERIT constraint, we
			 * shouldn't try to look for it in the children.
			 */
			if (!recursing && !con->connoinherit)
				children = find_all_inheritors(RelationGetRelid(rel),
											   lockmode, NULL);

			/*
			 * For CHECK constraints, we must ensure that we only mark the
			 * constraint as validated on the parent if it's already validated
			 * on the children.
			 *
			 * We recurse before validating on the parent, to reduce risk of
			 * deadlocks.
			 */
			foreach(child, children)
			{
				Oid			childoid = lfirst_oid(child);
				Relation	childrel;

				if (childoid == RelationGetRelid(rel))
					continue;

				/*
				 * If we are told not to recurse, there had better not be any
				 * child tables, because we can't mark the constraint on the
				 * parent valid unless it is valid for all child tables.
				 */
				if (!recurse)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("constraint must be validated on child tables too")));

				/* find_all_inheritors already got lock */
				childrel = table_open(childoid, NoLock);

				ATExecValidateConstraint(wqueue, childrel, constrName, false,
										 true, lockmode);
				table_close(childrel, NoLock);
			}

			/* Queue validation for phase 3 */
			newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
			newcon->name = constrName;
			newcon->contype = CONSTR_CHECK;
			newcon->refrelid = InvalidOid;
			newcon->refindid = InvalidOid;
			newcon->conid = con->oid;

			val = SysCacheGetAttr(CONSTROID, tuple,
								  Anum_pg_constraint_conbin, &isnull);
			if (isnull)
				elog(ERROR, "null conbin for constraint %u", con->oid);

			conbin = TextDatumGetCString(val);
			newcon->qual = (Node *) stringToNode(conbin);

			/* Find or create work queue entry for this table */
			tab = ATGetQueueEntry(wqueue, rel);
			tab->constraints = lappend(tab->constraints, newcon);

			/*
			 * Invalidate relcache so that others see the new validated
			 * constraint.
			 */
			CacheInvalidateRelcache(rel);
		}

		/*
		 * Now update the catalog, while we have the door open.
		 */
		copyTuple = heap_copytuple(tuple);
		copy_con = (Form_pg_constraint) GETSTRUCT(copyTuple);
		copy_con->convalidated = true;
		CatalogTupleUpdate(conrel, &copyTuple->t_self, copyTuple);

		InvokeObjectPostAlterHook(ConstraintRelationId, con->oid, 0);

		heap_freetuple(copyTuple);

		ObjectAddressSet(address, ConstraintRelationId, con->oid);
	}
	else
		address = InvalidObjectAddress; /* already validated */

	systable_endscan(scan);

	table_close(conrel, RowExclusiveLock);

	return address;
}


/*
 * transformColumnNameList - transform list of column names
 *
 * Lookup each name and return its attnum and, optionally, type OID
 */
static int
transformColumnNameList(Oid relId, List *colList,
						int16 *attnums, Oid *atttypids)
{
	ListCell   *l;
	int			attnum;

	attnum = 0;
	foreach(l, colList)
	{
		char	   *attname = strVal(lfirst(l));
		HeapTuple	atttuple;

		atttuple = SearchSysCacheAttName(relId, attname);
		if (!HeapTupleIsValid(atttuple))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_COLUMN),
					 errmsg("column \"%s\" referenced in foreign key constraint does not exist",
							attname)));
		if (attnum >= INDEX_MAX_KEYS)
			ereport(ERROR,
					(errcode(ERRCODE_TOO_MANY_COLUMNS),
					 errmsg("cannot have more than %d keys in a foreign key",
							INDEX_MAX_KEYS)));
		attnums[attnum] = ((Form_pg_attribute) GETSTRUCT(atttuple))->attnum;
		if (atttypids != NULL)
			atttypids[attnum] = ((Form_pg_attribute) GETSTRUCT(atttuple))->atttypid;
		ReleaseSysCache(atttuple);
		attnum++;
	}

	return attnum;
}

/*
 * transformFkeyGetPrimaryKey -
 *
 *	Look up the names, attnums, and types of the primary key attributes
 *	for the pkrel.  Also return the index OID and index opclasses of the
 *	index supporting the primary key.
 *
 *	All parameters except pkrel are output parameters.  Also, the function
 *	return value is the number of attributes in the primary key.
 *
 *	Used when the column list in the REFERENCES specification is omitted.
 */
static int
transformFkeyGetPrimaryKey(Relation pkrel, Oid *indexOid,
						   List **attnamelist,
						   int16 *attnums, Oid *atttypids,
						   Oid *opclasses)
{
	List	   *indexoidlist;
	ListCell   *indexoidscan;
	HeapTuple	indexTuple = NULL;
	Form_pg_index indexStruct = NULL;
	Datum		indclassDatum;
	bool		isnull;
	oidvector  *indclass;
	int			i;

	/*
	 * Get the list of index OIDs for the table from the relcache, and look up
	 * each one in the pg_index syscache until we find one marked primary key
	 * (hopefully there isn't more than one such).  Insist it's valid, too.
	 */
	*indexOid = InvalidOid;

	indexoidlist = RelationGetIndexList(pkrel);

	foreach(indexoidscan, indexoidlist)
	{
		Oid			indexoid = lfirst_oid(indexoidscan);

		indexTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexoid));
		if (!HeapTupleIsValid(indexTuple))
			elog(ERROR, "cache lookup failed for index %u", indexoid);
		indexStruct = (Form_pg_index) GETSTRUCT(indexTuple);
		if (indexStruct->indisprimary && indexStruct->indisvalid)
		{
			/*
			 * Refuse to use a deferrable primary key.  This is per SQL spec,
			 * and there would be a lot of interesting semantic problems if we
			 * tried to allow it.
			 */
			if (!indexStruct->indimmediate)
				ereport(ERROR,
						(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
						 errmsg("cannot use a deferrable primary key for referenced table \"%s\"",
								RelationGetRelationName(pkrel))));

			*indexOid = indexoid;
			break;
		}
		ReleaseSysCache(indexTuple);
	}

	list_free(indexoidlist);

	/*
	 * Check that we found it
	 */
	if (!OidIsValid(*indexOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("there is no primary key for referenced table \"%s\"",
						RelationGetRelationName(pkrel))));

	/* Must get indclass the hard way */
	indclassDatum = SysCacheGetAttr(INDEXRELID, indexTuple,
									Anum_pg_index_indclass, &isnull);
	Assert(!isnull);
	indclass = (oidvector *) DatumGetPointer(indclassDatum);

	/*
	 * Now build the list of PK attributes from the indkey definition (we
	 * assume a primary key cannot have expressional elements)
	 */
	*attnamelist = NIL;
	for (i = 0; i < indexStruct->indnkeyatts; i++)
	{
		int			pkattno = indexStruct->indkey.values[i];

		attnums[i] = pkattno;
		atttypids[i] = attnumTypeId(pkrel, pkattno);
		opclasses[i] = indclass->values[i];
		*attnamelist = lappend(*attnamelist,
							   makeString(pstrdup(NameStr(*attnumAttName(pkrel, pkattno)))));
	}

	ReleaseSysCache(indexTuple);

	return i;
}

/*
 * transformFkeyCheckAttrs -
 *
 *	Make sure that the attributes of a referenced table belong to a unique
 *	(or primary key) constraint.  Return the OID of the index supporting
 *	the constraint, as well as the opclasses associated with the index
 *	columns.
 */
static Oid
transformFkeyCheckAttrs(Relation pkrel,
						int numattrs, int16 *attnums,
						Oid *opclasses) /* output parameter */
{
	Oid			indexoid = InvalidOid;
	bool		found = false;
	bool		found_deferrable = false;
	List	   *indexoidlist;
	ListCell   *indexoidscan;
	int			i,
				j;

	/*
	 * Reject duplicate appearances of columns in the referenced-columns list.
	 * Such a case is forbidden by the SQL standard, and even if we thought it
	 * useful to allow it, there would be ambiguity about how to match the
	 * list to unique indexes (in particular, it'd be unclear which index
	 * opclass goes with which FK column).
	 */
	for (i = 0; i < numattrs; i++)
	{
		for (j = i + 1; j < numattrs; j++)
		{
			if (attnums[i] == attnums[j])
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_FOREIGN_KEY),
						 errmsg("foreign key referenced-columns list must not contain duplicates")));
		}
	}

	/*
	 * Get the list of index OIDs for the table from the relcache, and look up
	 * each one in the pg_index syscache, and match unique indexes to the list
	 * of attnums we are given.
	 */
	indexoidlist = RelationGetIndexList(pkrel);

	foreach(indexoidscan, indexoidlist)
	{
		HeapTuple	indexTuple;
		Form_pg_index indexStruct;

		indexoid = lfirst_oid(indexoidscan);
		indexTuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexoid));
		if (!HeapTupleIsValid(indexTuple))
			elog(ERROR, "cache lookup failed for index %u", indexoid);
		indexStruct = (Form_pg_index) GETSTRUCT(indexTuple);

		/*
		 * Must have the right number of columns; must be unique and not a
		 * partial index; forget it if there are any expressions, too. Invalid
		 * indexes are out as well.
		 */
		if (indexStruct->indnkeyatts == numattrs &&
			indexStruct->indisunique &&
			indexStruct->indisvalid &&
			heap_attisnull(indexTuple, Anum_pg_index_indpred, NULL) &&
			heap_attisnull(indexTuple, Anum_pg_index_indexprs, NULL))
		{
			Datum		indclassDatum;
			bool		isnull;
			oidvector  *indclass;

			/* Must get indclass the hard way */
			indclassDatum = SysCacheGetAttr(INDEXRELID, indexTuple,
											Anum_pg_index_indclass, &isnull);
			Assert(!isnull);
			indclass = (oidvector *) DatumGetPointer(indclassDatum);

			/*
			 * The given attnum list may match the index columns in any order.
			 * Check for a match, and extract the appropriate opclasses while
			 * we're at it.
			 *
			 * We know that attnums[] is duplicate-free per the test at the
			 * start of this function, and we checked above that the number of
			 * index columns agrees, so if we find a match for each attnums[]
			 * entry then we must have a one-to-one match in some order.
			 */
			for (i = 0; i < numattrs; i++)
			{
				found = false;
				for (j = 0; j < numattrs; j++)
				{
					if (attnums[i] == indexStruct->indkey.values[j])
					{
						opclasses[i] = indclass->values[j];
						found = true;
						break;
					}
				}
				if (!found)
					break;
			}

			/*
			 * Refuse to use a deferrable unique/primary key.  This is per SQL
			 * spec, and there would be a lot of interesting semantic problems
			 * if we tried to allow it.
			 */
			if (found && !indexStruct->indimmediate)
			{
				/*
				 * Remember that we found an otherwise matching index, so that
				 * we can generate a more appropriate error message.
				 */
				found_deferrable = true;
				found = false;
			}
		}
		ReleaseSysCache(indexTuple);
		if (found)
			break;
	}

	if (!found)
	{
		if (found_deferrable)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot use a deferrable unique constraint for referenced table \"%s\"",
							RelationGetRelationName(pkrel))));
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_FOREIGN_KEY),
					 errmsg("there is no unique constraint matching given keys for referenced table \"%s\"",
							RelationGetRelationName(pkrel))));
	}

	list_free(indexoidlist);

	return indexoid;
}

/*
 * findFkeyCast -
 *
 *	Wrapper around find_coercion_pathway() for ATAddForeignKeyConstraint().
 *	Caller has equal regard for binary coercibility and for an exact match.
*/
static CoercionPathType
findFkeyCast(Oid targetTypeId, Oid sourceTypeId, Oid *funcid)
{
	CoercionPathType ret;

	if (targetTypeId == sourceTypeId)
	{
		ret = COERCION_PATH_RELABELTYPE;
		*funcid = InvalidOid;
	}
	else
	{
		ret = find_coercion_pathway(targetTypeId, sourceTypeId,
									COERCION_IMPLICIT, funcid);
		if (ret == COERCION_PATH_NONE)
			/* A previously-relied-upon cast is now gone. */
			elog(ERROR, "could not find cast from %u to %u",
				 sourceTypeId, targetTypeId);
	}

	return ret;
}

/*
 * Permissions checks on the referenced table for ADD FOREIGN KEY
 *
 * Note: we have already checked that the user owns the referencing table,
 * else we'd have failed much earlier; no additional checks are needed for it.
 */
static void
checkFkeyPermissions(Relation rel, int16 *attnums, int natts)
{
	Oid			roleid = GetUserId();
	AclResult	aclresult;
	int			i;

	/* Okay if we have relation-level REFERENCES permission */
	aclresult = pg_class_aclcheck(RelationGetRelid(rel), roleid,
								  ACL_REFERENCES);
	if (aclresult == ACLCHECK_OK)
		return;
	/* Else we must have REFERENCES on each column */
	for (i = 0; i < natts; i++)
	{
		aclresult = pg_attribute_aclcheck(RelationGetRelid(rel), attnums[i],
										  roleid, ACL_REFERENCES);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, get_relkind_objtype(rel->rd_rel->relkind),
						   RelationGetRelationName(rel));
	}
}

typedef struct YbFKTriggerVTable YbFKTriggerVTable;

/*
 * YbFKTriggerScanDescData holds the state of the YbFKTriggerScan which reads multiple
 * tuples and preload FK for them. After preloading FK in a batch it returns each tuple one
 * after another like regular heap_scan.
 */
typedef struct YbFKTriggerScanDescData
{
	TableScanDesc scan;
	ScanDirection scan_direction;
	MemoryContext per_batch_cxt;
	YbFKTriggerVTable* vptr;
	Trigger* trigger;
	Relation fk_rel;
	int buffered_tuples_capacity;
	int buffered_tuples_size;
	int current_tuple_idx;
	bool all_tuples_processed;
	EState *estate;
	TupleTableSlot* buffered_tuples[];
} YbFKTriggerScanDescData;

typedef struct YbFKTriggerScanDescData *YbFKTriggerScanDesc;

/*
 * Helper structure to emulate virtual functions for YbFKTriggerScan.
 * This scan works as regular heap_scan in non-YB mode and has some extra
 * functionality in YB mode.
 */
typedef struct YbFKTriggerVTable
{
	TupleTableSlot *(*get_next)(YbFKTriggerScanDesc descr,
								TupleTableSlot *slot);
} YbFKTriggerVTable;

static TupleTableSlot *
YbPgGetNext(YbFKTriggerScanDesc desc, TupleTableSlot *slot)
{
	return table_scan_getnextslot(desc->scan, desc->scan_direction, slot) ?
			   slot :
			   NULL;
}

static TupleTableSlot *
YbGetNext(YbFKTriggerScanDesc desc, TupleTableSlot *slot)
{
	/* Note: slot argument is NULL and should not be used. */
	Assert(slot == NULL);
	if (desc->current_tuple_idx >= desc->buffered_tuples_size && !desc->all_tuples_processed)
	{
		/* Clear context of previously buffered tuples */
		MemoryContextReset(desc->per_batch_cxt);
		desc->current_tuple_idx = 0;
		desc->buffered_tuples_size = 0;
		while (desc->buffered_tuples_size < desc->buffered_tuples_capacity)
		{
			TupleTableSlot *new_slot = MakeSingleTupleTableSlot(
				RelationGetDescr(desc->scan->rs_rd),
				table_slot_callbacks(desc->scan->rs_rd));
			if (!heap_getnextslot(desc->scan, desc->scan_direction, new_slot))
			{
				desc->all_tuples_processed = true;
				ExecDropSingleTupleTableSlot(new_slot);
				break;
			}
			YbAddTriggerFKReferenceIntent(desc->trigger, desc->fk_rel, new_slot, desc->estate);
			desc->buffered_tuples[desc->buffered_tuples_size++] = new_slot;
		}
	}
	return desc->current_tuple_idx < desc->buffered_tuples_size ?
			   desc->buffered_tuples[desc->current_tuple_idx++] :
			   NULL;
}

static YbFKTriggerVTable YbFKTriggerScanVTableNotYugaByteEnabled =
	{
		.get_next = &YbPgGetNext,
	};

static YbFKTriggerVTable YbFKTriggerScanVTableIsYugaByteEnabled =
	{
		.get_next = &YbGetNext,
	};

static YbFKTriggerScanDesc
YbFKTriggerScanBegin(TableScanDesc scan,
					 ScanDirection direction,
					 Trigger *trigger,
					 Relation fk_rel,
					 int buffer_capacity,
					 MemoryContext per_batch_cxt)
{
	YbFKTriggerScanDesc descr = (YbFKTriggerScanDesc) palloc(
		sizeof(YbFKTriggerScanDescData) + buffer_capacity * sizeof(HeapTuple));
	MemSet(descr, 0, sizeof(YbFKTriggerScanDescData));
	descr->scan = scan;
	descr->scan_direction = direction;
	descr->trigger = trigger;
	descr->fk_rel = fk_rel;
	descr->buffered_tuples_capacity = buffer_capacity;
	descr->vptr = IsYBRelation(scan->rs_rd) ?
					  &YbFKTriggerScanVTableIsYugaByteEnabled :
					  &YbFKTriggerScanVTableNotYugaByteEnabled;
	descr->per_batch_cxt = per_batch_cxt;

	/* TODO(GH#25126): Postpone creating executor state until required. */
	descr->estate = CreateExecutorState();
	return descr;
}

static void
YbFKTriggerScanEnd(YbFKTriggerScanDesc descr)
{
	Assert(descr);
	if (descr->estate)
		FreeExecutorState(descr->estate);
	pfree(descr);
}

static TupleTableSlot *
YbFKTriggerScanGetNext(YbFKTriggerScanDesc descr, TupleTableSlot *slot)
{
	return descr->vptr->get_next(descr, slot);
}

/*
 * Scan the existing rows in a table to verify they meet a proposed FK
 * constraint.
 *
 * Caller must have opened and locked both relations appropriately.
 */
static void
validateForeignKeyConstraint(char *conname,
							 Relation rel,
							 Relation pkrel,
							 Oid pkindOid,
							 Oid constraintOid)
{
	TupleTableSlot *slot;
	TableScanDesc scan;
	Trigger		trig;
	Snapshot	snapshot;
	MemoryContext oldcxt;
	MemoryContext perTupCxt;
	TupleTableSlot *ybSlot;

	ereport(DEBUG1,
			(errmsg_internal("validating foreign key constraint \"%s\"", conname)));

	/*
	 * Build a trigger call structure; we'll need it either way.
	 */
	MemSet(&trig, 0, sizeof(trig));
	trig.tgoid = InvalidOid;
	trig.tgname = conname;
	trig.tgenabled = TRIGGER_FIRES_ON_ORIGIN;
	trig.tgisinternal = true;
	trig.tgconstrrelid = RelationGetRelid(pkrel);
	trig.tgconstrindid = pkindOid;
	trig.tgconstraint = constraintOid;
	trig.tgdeferrable = false;
	trig.tginitdeferred = false;
	/* we needn't fill in remaining fields */

	/*
	 * See if we can do it with a single LEFT JOIN query.  A false result
	 * indicates we must proceed with the fire-the-trigger method.
	 * Note: YB handles LEFT JOIN inefficiently. So skip this approach and
	 * call trigger on each row instead. As triggers can buffer the FK check.
	 */
	if (!IsYBRelation(rel) && RI_Initial_Check(&trig, rel, pkrel))
		return;

	/*
	 * Scan through each tuple, calling RI_FKey_check_ins (insert trigger) as
	 * if that tuple had just been inserted.  If any of those fail, it should
	 * ereport(ERROR) and that's that.
	 */
	snapshot = RegisterSnapshot(GetLatestSnapshot());
	/* YB note: slot is not used for YB relations */
	slot = !IsYBRelation(rel) ? table_slot_create(rel, NULL) : NULL;
	scan = table_beginscan(rel, snapshot, 0, NULL);

	/* YB note: perTupCxt is used as per-batch (and not per-tuple) context */
	if (IsYBRelation(rel))
		perTupCxt = AllocSetContextCreate(GetCurrentMemoryContext(),
										  "validateForeignKeyConstraint",
										  ALLOCSET_DEFAULT_SIZES);
	else
		perTupCxt = AllocSetContextCreate(GetCurrentMemoryContext(),
										  "validateForeignKeyConstraint",
										  ALLOCSET_SMALL_SIZES);

	YbFKTriggerScanDesc fk_scan = YbFKTriggerScanBegin(
		scan,
		ForwardScanDirection,
		&trig,
		rel,
		*YBCGetGFlags()->ysql_session_max_batch_size,
		perTupCxt);
	oldcxt = MemoryContextSwitchTo(perTupCxt);

	while ((ybSlot = YbFKTriggerScanGetNext(fk_scan, slot)) != NULL)
	{
		LOCAL_FCINFO(fcinfo, 0);
		TriggerData trigdata = {0};

		CHECK_FOR_INTERRUPTS();

		/*
		 * Make a call to the trigger function
		 *
		 * No parameters are passed, but we do set a context
		 */
		MemSet(fcinfo, 0, SizeForFunctionCallInfo(0));

		/*
		 * We assume RI_FKey_check_ins won't look at flinfo...
		 */
		trigdata.type = T_TriggerData;
		trigdata.tg_event = TRIGGER_EVENT_INSERT | TRIGGER_EVENT_ROW;
		trigdata.tg_relation = rel;
		trigdata.tg_trigtuple = ExecFetchSlotHeapTuple(ybSlot, false, NULL);
		trigdata.tg_trigslot = ybSlot;
		trigdata.tg_trigger = &trig;
		trigdata.estate = fk_scan->estate;

		fcinfo->context = (Node *) &trigdata;

		RI_FKey_check_ins(fcinfo);

		if (IsYBRelation(rel))
			ExecDropSingleTupleTableSlot(ybSlot);
		else
			MemoryContextReset(perTupCxt);
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(perTupCxt);
	table_endscan(scan);
	YbFKTriggerScanEnd(fk_scan);
	UnregisterSnapshot(snapshot);
	if (!IsYBRelation(rel))
		ExecDropSingleTupleTableSlot(slot);
}

/*
 * CreateFKCheckTrigger
 *		Creates the insert (on_insert=true) or update "check" trigger that
 *		implements a given foreign key
 *
 * Returns the OID of the so created trigger.
 */
static Oid
CreateFKCheckTrigger(Oid myRelOid, Oid refRelOid, Constraint *fkconstraint,
					 Oid constraintOid, Oid indexOid, Oid parentTrigOid,
					 bool on_insert)
{
	ObjectAddress trigAddress;
	CreateTrigStmt *fk_trigger;

	/*
	 * Note: for a self-referential FK (referencing and referenced tables are
	 * the same), it is important that the ON UPDATE action fires before the
	 * CHECK action, since both triggers will fire on the same row during an
	 * UPDATE event; otherwise the CHECK trigger will be checking a non-final
	 * state of the row.  Triggers fire in name order, so we ensure this by
	 * using names like "RI_ConstraintTrigger_a_NNNN" for the action triggers
	 * and "RI_ConstraintTrigger_c_NNNN" for the check triggers.
	 */
	fk_trigger = makeNode(CreateTrigStmt);
	fk_trigger->replace = false;
	fk_trigger->isconstraint = true;
	fk_trigger->trigname = "RI_ConstraintTrigger_c";
	fk_trigger->relation = NULL;

	/* Either ON INSERT or ON UPDATE */
	if (on_insert)
	{
		fk_trigger->funcname = SystemFuncName("RI_FKey_check_ins");
		fk_trigger->events = TRIGGER_TYPE_INSERT;
	}
	else
	{
		fk_trigger->funcname = SystemFuncName("RI_FKey_check_upd");
		fk_trigger->events = TRIGGER_TYPE_UPDATE;
	}

	fk_trigger->args = NIL;
	fk_trigger->row = true;
	fk_trigger->timing = TRIGGER_TYPE_AFTER;
	fk_trigger->columns = NIL;
	fk_trigger->whenClause = NULL;
	fk_trigger->transitionRels = NIL;
	fk_trigger->deferrable = fkconstraint->deferrable;
	fk_trigger->initdeferred = fkconstraint->initdeferred;
	fk_trigger->constrrel = NULL;

	trigAddress = CreateTrigger(fk_trigger, NULL, myRelOid, refRelOid,
								constraintOid, indexOid, InvalidOid,
								parentTrigOid, NULL, true, false);

	/* Make changes-so-far visible */
	CommandCounterIncrement();

	return trigAddress.objectId;
}

/*
 * createForeignKeyActionTriggers
 *		Create the referenced-side "action" triggers that implement a foreign
 *		key.
 *
 * Returns the OIDs of the so created triggers in *deleteTrigOid and
 * *updateTrigOid.
 */
static void
createForeignKeyActionTriggers(Relation rel, Oid refRelOid, Constraint *fkconstraint,
							   Oid constraintOid, Oid indexOid,
							   Oid parentDelTrigger, Oid parentUpdTrigger,
							   Oid *deleteTrigOid, Oid *updateTrigOid)
{
	CreateTrigStmt *fk_trigger;
	ObjectAddress trigAddress;

	/*
	 * Build and execute a CREATE CONSTRAINT TRIGGER statement for the ON
	 * DELETE action on the referenced table.
	 */
	fk_trigger = makeNode(CreateTrigStmt);
	fk_trigger->replace = false;
	fk_trigger->isconstraint = true;
	fk_trigger->trigname = "RI_ConstraintTrigger_a";
	fk_trigger->relation = NULL;
	fk_trigger->args = NIL;
	fk_trigger->row = true;
	fk_trigger->timing = TRIGGER_TYPE_AFTER;
	fk_trigger->events = TRIGGER_TYPE_DELETE;
	fk_trigger->columns = NIL;
	fk_trigger->whenClause = NULL;
	fk_trigger->transitionRels = NIL;
	fk_trigger->constrrel = NULL;
	switch (fkconstraint->fk_del_action)
	{
		case FKCONSTR_ACTION_NOACTION:
			fk_trigger->deferrable = fkconstraint->deferrable;
			fk_trigger->initdeferred = fkconstraint->initdeferred;
			fk_trigger->funcname = SystemFuncName("RI_FKey_noaction_del");
			break;
		case FKCONSTR_ACTION_RESTRICT:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_restrict_del");
			break;
		case FKCONSTR_ACTION_CASCADE:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_cascade_del");
			break;
		case FKCONSTR_ACTION_SETNULL:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_setnull_del");
			break;
		case FKCONSTR_ACTION_SETDEFAULT:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_setdefault_del");
			break;
		default:
			elog(ERROR, "unrecognized FK action type: %d",
				 (int) fkconstraint->fk_del_action);
			break;
	}

	trigAddress = CreateTrigger(fk_trigger, NULL, refRelOid,
								RelationGetRelid(rel),
								constraintOid, indexOid, InvalidOid,
								parentDelTrigger, NULL, true, false);
	if (deleteTrigOid)
		*deleteTrigOid = trigAddress.objectId;

	/* Make changes-so-far visible */
	CommandCounterIncrement();

	/*
	 * Build and execute a CREATE CONSTRAINT TRIGGER statement for the ON
	 * UPDATE action on the referenced table.
	 */
	fk_trigger = makeNode(CreateTrigStmt);
	fk_trigger->replace = false;
	fk_trigger->isconstraint = true;
	fk_trigger->trigname = "RI_ConstraintTrigger_a";
	fk_trigger->relation = NULL;
	fk_trigger->args = NIL;
	fk_trigger->row = true;
	fk_trigger->timing = TRIGGER_TYPE_AFTER;
	fk_trigger->events = TRIGGER_TYPE_UPDATE;
	fk_trigger->columns = NIL;
	fk_trigger->whenClause = NULL;
	fk_trigger->transitionRels = NIL;
	fk_trigger->constrrel = NULL;
	switch (fkconstraint->fk_upd_action)
	{
		case FKCONSTR_ACTION_NOACTION:
			fk_trigger->deferrable = fkconstraint->deferrable;
			fk_trigger->initdeferred = fkconstraint->initdeferred;
			fk_trigger->funcname = SystemFuncName("RI_FKey_noaction_upd");
			break;
		case FKCONSTR_ACTION_RESTRICT:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_restrict_upd");
			break;
		case FKCONSTR_ACTION_CASCADE:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_cascade_upd");
			break;
		case FKCONSTR_ACTION_SETNULL:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_setnull_upd");
			break;
		case FKCONSTR_ACTION_SETDEFAULT:
			fk_trigger->deferrable = false;
			fk_trigger->initdeferred = false;
			fk_trigger->funcname = SystemFuncName("RI_FKey_setdefault_upd");
			break;
		default:
			elog(ERROR, "unrecognized FK action type: %d",
				 (int) fkconstraint->fk_upd_action);
			break;
	}

	trigAddress = CreateTrigger(fk_trigger, NULL, refRelOid,
								RelationGetRelid(rel),
								constraintOid, indexOid, InvalidOid,
								parentUpdTrigger, NULL, true, false);
	if (updateTrigOid)
		*updateTrigOid = trigAddress.objectId;
}

/*
 * createForeignKeyCheckTriggers
 *		Create the referencing-side "check" triggers that implement a foreign
 *		key.
 *
 * Returns the OIDs of the so created triggers in *insertTrigOid and
 * *updateTrigOid.
 */
static void
createForeignKeyCheckTriggers(Oid myRelOid, Oid refRelOid,
							  Constraint *fkconstraint, Oid constraintOid,
							  Oid indexOid,
							  Oid parentInsTrigger, Oid parentUpdTrigger,
							  Oid *insertTrigOid, Oid *updateTrigOid)
{
	*insertTrigOid = CreateFKCheckTrigger(myRelOid, refRelOid, fkconstraint,
										  constraintOid, indexOid,
										  parentInsTrigger, true);
	*updateTrigOid = CreateFKCheckTrigger(myRelOid, refRelOid, fkconstraint,
										  constraintOid, indexOid,
										  parentUpdTrigger, false);
}

/*
 * ALTER TABLE DROP CONSTRAINT
 *
 * Like DROP COLUMN, we can't use the normal ALTER TABLE recursion mechanism.
 */
static void
ATExecDropConstraint(List **yb_wqueue, AlteredTableInfo *tab,
					 Relation *mutable_rel,
					 const char *constrName, DropBehavior behavior,
					 bool recurse, bool recursing,
					 bool missing_ok, LOCKMODE lockmode)
{
	List	   *children;
	ListCell   *child;
	Relation	conrel;
	Form_pg_constraint con;
	SysScanDesc scan;
	ScanKeyData skey[3];
	HeapTuple	tuple;
	bool		found = false;
	bool		is_no_inherit_constraint = false;
	char		contype;

	/* At top level, permission check was done in ATPrepCmd, else do it */
	if (recursing)
		ATSimplePermissions(AT_DropConstraint, *mutable_rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	conrel = table_open(ConstraintRelationId, RowExclusiveLock);

	/*
	 * Find and drop the target constraint
	 */
	ScanKeyInit(&skey[0],
				Anum_pg_constraint_conrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(*mutable_rel)));
	ScanKeyInit(&skey[1],
				Anum_pg_constraint_contypid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(InvalidOid));
	ScanKeyInit(&skey[2],
				Anum_pg_constraint_conname,
				BTEqualStrategyNumber, F_NAMEEQ,
				CStringGetDatum(constrName));
	scan = systable_beginscan(conrel, ConstraintRelidTypidNameIndexId,
							  true, NULL, 3, skey);

	/* There can be at most one matching row */
	if (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		ObjectAddress conobj;

		con = (Form_pg_constraint) GETSTRUCT(tuple);

		/* Don't drop inherited constraints */
		if (con->coninhcount > 0 && !recursing)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot drop inherited constraint \"%s\" of relation \"%s\"",
							constrName, RelationGetRelationName(*mutable_rel))));

		is_no_inherit_constraint = con->connoinherit;
		contype = con->contype;

		/*
		 * If it's a foreign-key constraint, we'd better lock the referenced
		 * table and check that that's not in use, just as we've already done
		 * for the constrained table (else we might, eg, be dropping a trigger
		 * that has unfired events).  But we can/must skip that in the
		 * self-referential case.
		 */
		if (contype == CONSTRAINT_FOREIGN &&
			con->confrelid != RelationGetRelid(*mutable_rel))
		{
			Relation	frel;

			/* Must match lock taken by RemoveTriggerById: */
			frel = table_open(con->confrelid, AccessExclusiveLock);
			CheckTableNotInUse(frel, "ALTER TABLE");
			table_close(frel, NoLock);
		}

		/*
		 * Perform the actual constraint deletion
		 */
		conobj.classId = ConstraintRelationId;
		conobj.objectId = con->oid;
		conobj.objectSubId = 0;

		performDeletion(&conobj, behavior, 0);

		found = true;
	}

	systable_endscan(scan);

	if (!found)
	{
		if (!missing_ok)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("constraint \"%s\" of relation \"%s\" does not exist",
							constrName, RelationGetRelationName(*mutable_rel))));
		}
		else
		{
			ereport(NOTICE,
					(errmsg("constraint \"%s\" of relation \"%s\" does not exist, skipping",
							constrName, RelationGetRelationName(*mutable_rel))));
			table_close(conrel, RowExclusiveLock);
			return;
		}
	}

	if (IsYBRelation(*mutable_rel) && contype == CONSTRAINT_PRIMARY)
	{
		if (!yb_enable_alter_table_rewrite)
		{
			/* Table will be re-created without a dummy PK index. */
			*mutable_rel = YbATCloneRelationSetPrimaryKey(
				*mutable_rel, NULL /* stmt */, NULL /* result */);

			/*
			 * Update the table relid so that further passes will operate on
			 * the new table.
			 */
			if (tab)
				tab->relid = RelationGetRelid(*mutable_rel);
		}
		else
		{
			/*
			 * In YB, the ADD/DROP primary key operation involves a table
			 * rewrite. So if this is partitioned table, we need to add
			 * its children to the work queue as well.
			 */
			if ((*mutable_rel)->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
				YbATSetPKRewriteChildPartitions(yb_wqueue,
					tab, false /* skip_copy_split_options */);
			tab->rewrite |= YB_AT_REWRITE_ALTER_PRIMARY_KEY;
		}
	}

	/*
	 * For partitioned tables, non-CHECK inherited constraints are dropped via
	 * the dependency mechanism, so we're done here.
	 */
	if (contype != CONSTRAINT_CHECK &&
		(*mutable_rel)->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		table_close(conrel, RowExclusiveLock);
		return;
	}

	/*
	 * Propagate to children as appropriate.  Unlike most other ALTER
	 * routines, we have to do this one level of recursion at a time; we can't
	 * use find_all_inheritors to do it in one pass.
	 */
	if (!is_no_inherit_constraint)
		children = find_inheritance_children(RelationGetRelid(*mutable_rel), lockmode);
	else
		children = NIL;

	/*
	 * For a partitioned table, if partitions exist and we are told not to
	 * recurse, it's a user error.  It doesn't make sense to have a constraint
	 * be defined only on the parent, especially if it's a partitioned table.
	 */
	if ((*mutable_rel)->rd_rel->relkind == RELKIND_PARTITIONED_TABLE &&
		children != NIL && !recurse)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot remove constraint from only the partitioned table when partitions exist"),
				 errhint("Do not specify the ONLY keyword.")));

	foreach(child, children)
	{
		Oid			childrelid = lfirst_oid(child);
		Relation	childrel;
		HeapTuple	copy_tuple;

		/* find_inheritance_children already got lock */
		childrel = table_open(childrelid, NoLock);
		CheckTableNotInUse(childrel, "ALTER TABLE");

		ScanKeyInit(&skey[0],
					Anum_pg_constraint_conrelid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(childrelid));
		ScanKeyInit(&skey[1],
					Anum_pg_constraint_contypid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(InvalidOid));
		ScanKeyInit(&skey[2],
					Anum_pg_constraint_conname,
					BTEqualStrategyNumber, F_NAMEEQ,
					CStringGetDatum(constrName));
		scan = systable_beginscan(conrel, ConstraintRelidTypidNameIndexId,
								  true, NULL, 3, skey);

		/* There can be at most one matching row */
		if (!HeapTupleIsValid(tuple = systable_getnext(scan)))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("constraint \"%s\" of relation \"%s\" does not exist",
							constrName,
							RelationGetRelationName(childrel))));

		copy_tuple = heap_copytuple(tuple);

		systable_endscan(scan);

		con = (Form_pg_constraint) GETSTRUCT(copy_tuple);

		/* Right now only CHECK constraints can be inherited */
		if (con->contype != CONSTRAINT_CHECK)
			elog(ERROR, "inherited constraint is not a CHECK constraint");

		if (con->coninhcount <= 0)	/* shouldn't happen */
			elog(ERROR, "relation %u has non-inherited constraint \"%s\"",
				 childrelid, constrName);

		if (recurse)
		{
			/*
			 * If the child constraint has other definition sources, just
			 * decrement its inheritance count; if not, recurse to delete it.
			 */
			if (con->coninhcount == 1 && !con->conislocal)
			{
				/* Time to delete this child constraint, too */
				/*
				 * YB note:
				 * We don't want to update AlteredTableInfo for child constraints.
				 */
				ATExecDropConstraint(NULL /* yb_wqueue */, NULL /* tab */, &childrel, constrName,
									 behavior, true, true,
									 false, lockmode);
			}
			else
			{
				/* Child constraint must survive my deletion */
				con->coninhcount--;
				CatalogTupleUpdate(conrel, &copy_tuple->t_self, copy_tuple);

				/* Make update visible */
				CommandCounterIncrement();
			}
		}
		else
		{
			/*
			 * If we were told to drop ONLY in this table (no recursion), we
			 * need to mark the inheritors' constraints as locally defined
			 * rather than inherited.
			 */
			con->coninhcount--;
			con->conislocal = true;

			CatalogTupleUpdate(conrel, &copy_tuple->t_self, copy_tuple);

			/* Make update visible */
			CommandCounterIncrement();
		}

		heap_freetuple(copy_tuple);

		table_close(childrel, NoLock);
	}

	table_close(conrel, RowExclusiveLock);
}

/*
 * ALTER COLUMN TYPE
 *
 * Unlike other subcommand types, we do parse transformation for ALTER COLUMN
 * TYPE during phase 1 --- the AlterTableCmd passed in here is already
 * transformed (and must be, because we rely on some transformed fields).
 *
 * The point of this is that the execution of all ALTER COLUMN TYPEs for a
 * table will be done "in parallel" during phase 3, so all the USING
 * expressions should be parsed assuming the original column types.  Also,
 * this allows a USING expression to refer to a field that will be dropped.
 *
 * To make this work safely, AT_PASS_DROP then AT_PASS_ALTER_TYPE must be
 * the first two execution steps in phase 2; they must not see the effects
 * of any other subcommand types, since the USING expressions are parsed
 * against the unmodified table's state.
 */
static void
ATPrepAlterColumnType(List **wqueue,
					  AlteredTableInfo *tab, Relation rel,
					  bool recurse, bool recursing,
					  AlterTableCmd *cmd, LOCKMODE lockmode,
					  AlterTableUtilityContext *context)
{
	char	   *colName = cmd->name;
	ColumnDef  *def = (ColumnDef *) cmd->def;
	TypeName   *typeName = def->typeName;
	Node	   *transform = def->cooked_default;
	HeapTuple	tuple;
	Form_pg_attribute attTup;
	AttrNumber	attnum;
	Oid			targettype;
	int32		targettypmod;
	Oid			targetcollid;
	NewColumnValue *newval;
	ParseState *pstate = make_parsestate(NULL);
	AclResult	aclresult;
	bool		is_expr;

	if (rel->rd_rel->reloftype && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot alter column type of typed table")));

	/* lookup the attribute so we can check inheritance status */
	tuple = SearchSysCacheAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));
	attTup = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = attTup->attnum;

	/* Can't alter a system attribute */
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"",
						colName)));

	/*
	 * Don't alter inherited columns.  At outer level, there had better not be
	 * any inherited definition; when recursing, we assume this was checked at
	 * the parent level (see below).
	 */
	if (attTup->attinhcount > 0 && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot alter inherited column \"%s\"",
						colName)));

	/* Don't alter columns used in the partition key */
	if (has_partition_attrs(rel,
							bms_make_singleton(attnum - YBGetFirstLowInvalidAttributeNumber(rel)),
							&is_expr))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot alter column \"%s\" because it is part of the partition key of relation \"%s\"",
						colName, RelationGetRelationName(rel))));

	/* Look up the target type */
	typenameTypeIdAndMod(NULL, typeName, &targettype, &targettypmod);

	aclresult = pg_type_aclcheck(targettype, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, targettype);

	/* And the collation */
	targetcollid = GetColumnDefCollation(NULL, def, targettype);

	/* make sure datatype is legal for a column */
	CheckAttributeType(colName, targettype, targetcollid,
					   list_make1_oid(rel->rd_rel->reltype),
					   0);

	if (tab->relkind == RELKIND_RELATION ||
		tab->relkind == RELKIND_PARTITIONED_TABLE)
	{
		/*
		 * Set up an expression to transform the old data value to the new
		 * type. If a USING option was given, use the expression as
		 * transformed by transformAlterTableStmt, else just take the old
		 * value and try to coerce it.  We do this first so that type
		 * incompatibility can be detected before we waste effort, and because
		 * we need the expression to be parsed against the original table row
		 * type.
		 */
		if (!transform)
		{
			transform = (Node *) makeVar(1, attnum,
										 attTup->atttypid, attTup->atttypmod,
										 attTup->attcollation,
										 0);
		}

		transform = coerce_to_target_type(pstate,
										  transform, exprType(transform),
										  targettype, targettypmod,
										  COERCION_ASSIGNMENT,
										  COERCE_IMPLICIT_CAST,
										  -1);
		if (transform == NULL)
		{
			/* error text depends on whether USING was specified or not */
			if (def->cooked_default != NULL)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("result of USING clause for column \"%s\""
								" cannot be cast automatically to type %s",
								colName, format_type_be(targettype)),
						 errhint("You might need to add an explicit cast.")));
			else
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("column \"%s\" cannot be cast automatically to type %s",
								colName, format_type_be(targettype)),
				/* translator: USING is SQL, don't translate it */
						 errhint("You might need to specify \"USING %s::%s\".",
								 quote_identifier(colName),
								 format_type_with_typemod(targettype,
														  targettypmod))));
		}

		/* Fix collations after all else */
		assign_expr_collations(pstate, transform);

		/* Plan the expr now so we can accurately assess the need to rewrite. */
		transform = (Node *) expression_planner((Expr *) transform);

		/*
		 * Add a work queue item to make ATRewriteTable update the column
		 * contents.
		 */
		newval = (NewColumnValue *) palloc0(sizeof(NewColumnValue));
		newval->attnum = attnum;
		newval->expr = (Expr *) transform;
		newval->is_generated = false;

		tab->newvals = lappend(tab->newvals, newval);
		if (ATColumnChangeRequiresRewrite(transform, attnum))
		{
			tab->rewrite |= AT_REWRITE_COLUMN_REWRITE;
			if (IsYBRelation(rel) && yb_enable_alter_table_rewrite)
			{
				YbGetTableProperties(rel);
				/*
				 * Skip copying split options if the altered column is a
				 * part of the primary key
				 */
				if (IsYBRelation(rel) &&
					rel->yb_table_properties->num_range_key_columns > 0 &&
					YbIsColumnPartOfKey(rel, colName))
					tab->yb_skip_copy_split_options = true;
			}
		}
	}
	else if (transform)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a table",
						RelationGetRelationName(rel))));

	if (!RELKIND_HAS_STORAGE(tab->relkind))
	{
		/*
		 * For relations without storage, do this check now.  Regular tables
		 * will check it later when the table is being rewritten.
		 */
		find_composite_type_dependencies(rel->rd_rel->reltype, rel, NULL);
	}

	ReleaseSysCache(tuple);

	/*
	 * Recurse manually by queueing a new command for each child, if
	 * necessary. We cannot apply ATSimpleRecursion here because we need to
	 * remap attribute numbers in the USING expression, if any.
	 *
	 * If we are told not to recurse, there had better not be any child
	 * tables; else the alter would put them out of step.
	 */
	if (recurse)
	{
		Oid			relid = RelationGetRelid(rel);
		List	   *child_oids,
				   *child_numparents;
		ListCell   *lo,
				   *li;

		child_oids = find_all_inheritors(relid, lockmode,
										 &child_numparents);

		/*
		 * find_all_inheritors does the recursive search of the inheritance
		 * hierarchy, so all we have to do is process all of the relids in the
		 * list that it returns.
		 */
		forboth(lo, child_oids, li, child_numparents)
		{
			Oid			childrelid = lfirst_oid(lo);
			int			numparents = lfirst_int(li);
			Relation	childrel;
			HeapTuple	childtuple;
			Form_pg_attribute childattTup;

			if (childrelid == relid)
				continue;

			/* find_all_inheritors already got lock */
			childrel = relation_open(childrelid, NoLock);
			CheckTableNotInUse(childrel, "ALTER TABLE");

			/*
			 * Verify that the child doesn't have any inherited definitions of
			 * this column that came from outside this inheritance hierarchy.
			 * (renameatt makes a similar test, though in a different way
			 * because of its different recursion mechanism.)
			 */
			childtuple = SearchSysCacheAttName(RelationGetRelid(childrel),
											   colName);
			if (!HeapTupleIsValid(childtuple))
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_COLUMN),
						 errmsg("column \"%s\" of relation \"%s\" does not exist",
								colName, RelationGetRelationName(childrel))));
			childattTup = (Form_pg_attribute) GETSTRUCT(childtuple);

			if (childattTup->attinhcount > numparents)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("cannot alter inherited column \"%s\" of relation \"%s\"",
								colName, RelationGetRelationName(childrel))));

			ReleaseSysCache(childtuple);

			/*
			 * Remap the attribute numbers.  If no USING expression was
			 * specified, there is no need for this step.
			 */
			if (def->cooked_default)
			{
				AttrMap    *attmap;
				bool		found_whole_row;

				/* create a copy to scribble on */
				cmd = copyObject(cmd);

				attmap = build_attrmap_by_name(RelationGetDescr(childrel),
											   RelationGetDescr(rel),
											   false /* yb_ignore_type_mismatch */);
				((ColumnDef *) cmd->def)->cooked_default =
					map_variable_attnos(def->cooked_default,
										1, 0,
										attmap,
										InvalidOid, &found_whole_row);
				if (found_whole_row)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot convert whole-row table reference"),
							 errdetail("USING expression contains a whole-row table reference.")));
				pfree(attmap);
			}
			ATPrepCmd(wqueue, childrel, cmd, false, true, lockmode, context);
			relation_close(childrel, NoLock);
		}
	}
	else if (!recursing &&
			 find_inheritance_children(RelationGetRelid(rel), NoLock) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("type of inherited column \"%s\" must be changed in child tables too",
						colName)));

	if (tab->relkind == RELKIND_COMPOSITE_TYPE)
		ATTypedTableRecursion(wqueue, rel, cmd, lockmode, context);
}

/*
 * When the data type of a column is changed, a rewrite might not be required
 * if the new type is sufficiently identical to the old one, and the USING
 * clause isn't trying to insert some other value.  It's safe to skip the
 * rewrite in these cases:
 *
 * - the old type is binary coercible to the new type
 * - the new type is an unconstrained domain over the old type
 * - {NEW,OLD} or {OLD,NEW} is {timestamptz,timestamp} and the timezone is UTC
 *
 * In the case of a constrained domain, we could get by with scanning the
 * table and checking the constraint rather than actually rewriting it, but we
 * don't currently try to do that.
 */
static bool
ATColumnChangeRequiresRewrite(Node *expr, AttrNumber varattno)
{
	Assert(expr != NULL);

	for (;;)
	{
		/* only one varno, so no need to check that */
		if (IsA(expr, Var) && ((Var *) expr)->varattno == varattno)
			return false;
		else if (IsA(expr, RelabelType))
			expr = (Node *) ((RelabelType *) expr)->arg;
		else if (IsA(expr, CoerceToDomain))
		{
			CoerceToDomain *d = (CoerceToDomain *) expr;

			if (DomainHasConstraints(d->resulttype))
				return true;
			expr = (Node *) d->arg;
		}
		else if (IsA(expr, FuncExpr))
		{
			FuncExpr   *f = (FuncExpr *) expr;

			switch (f->funcid)
			{
				case F_TIMESTAMPTZ_TIMESTAMP:
				case F_TIMESTAMP_TIMESTAMPTZ:
					if (TimestampTimestampTzRequiresRewrite())
						return true;
					else
						expr = linitial(f->args);
					break;
				default:
					return true;
			}
		}
		else
			return true;
	}
}

/*
 * ALTER COLUMN .. SET DATA TYPE
 *
 * Return the address of the modified column.
 */
static ObjectAddress
ATExecAlterColumnType(AlteredTableInfo *tab, Relation *yb_mutable_rel,
					  AlterTableCmd *cmd, LOCKMODE lockmode)
{
	char	   *colName = cmd->name;
	ColumnDef  *def = (ColumnDef *) cmd->def;
	TypeName   *typeName = def->typeName;
	HeapTuple	heapTup;
	Form_pg_attribute attTup,
				attOldTup;
	AttrNumber	attnum;
	HeapTuple	typeTuple;
	Form_pg_type tform;
	Oid			targettype;
	int32		targettypmod;
	Oid			targetcollid;
	Node	   *defaultexpr;
	Relation	attrelation;
	Relation	depRel;
	ScanKeyData key[3];
	SysScanDesc scan;
	HeapTuple	depTup;
	ObjectAddress address;

	Relation rel = *yb_mutable_rel;

	/*
	 * Clear all the missing values if we're rewriting the table, since this
	 * renders them pointless.
	 */
	if (tab->rewrite)
	{
		Relation	newrel;

		newrel = table_open(RelationGetRelid(rel), NoLock);
		RelationClearMissing(newrel);
		relation_close(newrel, NoLock);
		/* make sure we don't conflict with later attribute modifications */
		CommandCounterIncrement();
	}

	bool yb_clone_table = IsYBRelation(rel) && !yb_enable_alter_table_rewrite
		&& tab->rewrite > 0;

	if (!yb_clone_table)
		attrelation = table_open(AttributeRelationId, RowExclusiveLock);

	/* Look up the target column */
	heapTup = SearchSysCacheCopyAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(heapTup)) /* shouldn't happen */
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));
	attTup = (Form_pg_attribute) GETSTRUCT(heapTup);
	attnum = attTup->attnum;
	attOldTup = TupleDescAttr(tab->oldDesc, attnum - 1);

	/* Check for multiple ALTER TYPE on same column --- can't cope */
	if (attTup->atttypid != attOldTup->atttypid ||
		attTup->atttypmod != attOldTup->atttypmod)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter type of column \"%s\" twice",
						colName)));

	/* Look up the target type (should not fail, since prep found it) */
	typeTuple = typenameType(NULL, typeName, &targettypmod);
	tform = (Form_pg_type) GETSTRUCT(typeTuple);
	targettype = tform->oid;
	/* And the collation */
	targetcollid = GetColumnDefCollation(NULL, def, targettype);

	/*
	 * If there is a default expression for the column, get it and ensure we
	 * can coerce it to the new datatype.  (We must do this before changing
	 * the column type, because build_column_default itself will try to
	 * coerce, and will not issue the error message we want if it fails.)
	 *
	 * We remove any implicit coercion steps at the top level of the old
	 * default expression; this has been agreed to satisfy the principle of
	 * least surprise.  (The conversion to the new column type should act like
	 * it started from what the user sees as the stored expression, and the
	 * implicit coercions aren't going to be shown.)
	 */
	if (attTup->atthasdef)
	{
		defaultexpr = build_column_default(rel, attnum);
		Assert(defaultexpr);
		defaultexpr = strip_implicit_coercions(defaultexpr);
		defaultexpr = coerce_to_target_type(NULL,	/* no UNKNOWN params */
											defaultexpr, exprType(defaultexpr),
											targettype, targettypmod,
											COERCION_ASSIGNMENT,
											COERCE_IMPLICIT_CAST,
											-1);
		if (defaultexpr == NULL)
		{
			if (attTup->attgenerated)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("generation expression for column \"%s\" cannot be cast automatically to type %s",
								colName, format_type_be(targettype))));
			else
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("default for column \"%s\" cannot be cast automatically to type %s",
								colName, format_type_be(targettype))));
		}
	}
	else
		defaultexpr = NULL;

	/*
	 * Find everything that depends on the column (constraints, indexes, etc),
	 * and record enough information to let us recreate the objects.
	 *
	 * The actual recreation does not happen here, but only after we have
	 * performed all the individual ALTER TYPE operations.  We have to save
	 * the info before executing ALTER TYPE, though, else the deparser will
	 * get confused.
	 */
	depRel = table_open(DependRelationId, RowExclusiveLock);

	ScanKeyInit(&key[0],
				Anum_pg_depend_refclassid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_refobjid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(rel)));
	ScanKeyInit(&key[2],
				Anum_pg_depend_refobjsubid,
				BTEqualStrategyNumber, F_INT4EQ,
				Int32GetDatum((int32) attnum));

	scan = systable_beginscan(depRel, DependReferenceIndexId, true,
							  NULL, 3, key);

	while (HeapTupleIsValid(depTup = systable_getnext(scan)))
	{
		Form_pg_depend foundDep = (Form_pg_depend) GETSTRUCT(depTup);
		ObjectAddress foundObject;

		foundObject.classId = foundDep->classid;
		foundObject.objectId = foundDep->objid;
		foundObject.objectSubId = foundDep->objsubid;

		switch (getObjectClass(&foundObject))
		{
			case OCLASS_CLASS:
				{
					char		relKind = get_rel_relkind(foundObject.objectId);

					if (relKind == RELKIND_INDEX ||
						relKind == RELKIND_PARTITIONED_INDEX)
					{
						Assert(foundObject.objectSubId == 0);
						RememberIndexForRebuilding(foundObject.objectId, tab);
					}
					else if (relKind == RELKIND_SEQUENCE)
					{
						/*
						 * This must be a SERIAL column's sequence.  We need
						 * not do anything to it.
						 */
						Assert(foundObject.objectSubId == 0);
					}
					else
					{
						/* Not expecting any other direct dependencies... */
						elog(ERROR, "unexpected object depending on column: %s",
							 getObjectDescription(&foundObject, false));
					}
					break;
				}

			case OCLASS_CONSTRAINT:
				Assert(foundObject.objectSubId == 0);
				RememberConstraintForRebuilding(foundObject.objectId, tab);
				break;

			case OCLASS_REWRITE:
				/* XXX someday see if we can cope with revising views */
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type of a column used by a view or rule"),
						 errdetail("%s depends on column \"%s\"",
								   getObjectDescription(&foundObject, false),
								   colName)));
				break;

			case OCLASS_TRIGGER:

				/*
				 * A trigger can depend on a column because the column is
				 * specified as an update target, or because the column is
				 * used in the trigger's WHEN condition.  The first case would
				 * not require any extra work, but the second case would
				 * require updating the WHEN expression, which will take a
				 * significant amount of new code.  Since we can't easily tell
				 * which case applies, we punt for both.  FIXME someday.
				 */
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type of a column used in a trigger definition"),
						 errdetail("%s depends on column \"%s\"",
								   getObjectDescription(&foundObject, false),
								   colName)));
				break;

			case OCLASS_POLICY:

				/*
				 * A policy can depend on a column because the column is
				 * specified in the policy's USING or WITH CHECK qual
				 * expressions.  It might be possible to rewrite and recheck
				 * the policy expression, but punt for now.  It's certainly
				 * easy enough to remove and recreate the policy; still, FIXME
				 * someday.
				 */
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type of a column used in a policy definition"),
						 errdetail("%s depends on column \"%s\"",
								   getObjectDescription(&foundObject, false),
								   colName)));
				break;

			case OCLASS_DEFAULT:
				{
					ObjectAddress col = GetAttrDefaultColumnAddress(foundObject.objectId);

					if (col.objectId == RelationGetRelid(rel) &&
						col.objectSubId == attnum)
					{
						/*
						 * Ignore the column's own default expression, which
						 * we will deal with below.
						 */
						Assert(defaultexpr);
					}
					else
					{
						/*
						 * This must be a reference from the expression of a
						 * generated column elsewhere in the same table.
						 * Changing the type of a column that is used by a
						 * generated column is not allowed by SQL standard, so
						 * just punt for now.  It might be doable with some
						 * thinking and effort.
						 */
						ereport(ERROR,
								(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
								 errmsg("cannot alter type of a column used by a generated column"),
								 errdetail("Column \"%s\" is used by generated column \"%s\".",
										   colName,
										   get_attname(col.objectId,
													   col.objectSubId,
													   false))));
					}
					break;
				}

			case OCLASS_STATISTIC_EXT:

				/*
				 * Give the extended-stats machinery a chance to fix anything
				 * that this column type change would break.
				 */
				RememberStatisticsForRebuilding(foundObject.objectId, tab);
				break;

			case OCLASS_PROC:
			case OCLASS_TYPE:
			case OCLASS_CAST:
			case OCLASS_COLLATION:
			case OCLASS_CONVERSION:
			case OCLASS_LANGUAGE:
			case OCLASS_LARGEOBJECT:
			case OCLASS_OPERATOR:
			case OCLASS_OPCLASS:
			case OCLASS_OPFAMILY:
			case OCLASS_AM:
			case OCLASS_AMOP:
			case OCLASS_AMPROC:
			case OCLASS_SCHEMA:
			case OCLASS_TSPARSER:
			case OCLASS_TSDICT:
			case OCLASS_TSTEMPLATE:
			case OCLASS_TSCONFIG:
			case OCLASS_ROLE:
			case OCLASS_DATABASE:
			case OCLASS_TBLGROUP:
			case OCLASS_TBLSPACE:
			case OCLASS_FDW:
			case OCLASS_FOREIGN_SERVER:
			case OCLASS_USER_MAPPING:
			case OCLASS_DEFACL:
			case OCLASS_EXTENSION:
			case OCLASS_EVENT_TRIGGER:
			case OCLASS_PARAMETER_ACL:
			case OCLASS_PUBLICATION:
			case OCLASS_PUBLICATION_NAMESPACE:
			case OCLASS_PUBLICATION_REL:
			case OCLASS_SUBSCRIPTION:
			case OCLASS_TRANSFORM:
			case OCLASS_YBPROFILE:
			case OCLASS_YBROLE_PROFILE:

				/*
				 * We don't expect any of these sorts of objects to depend on
				 * a column.
				 */
				elog(ERROR, "unexpected object depending on column: %s",
					 getObjectDescription(&foundObject, false));
				break;

				/*
				 * There's intentionally no default: case here; we want the
				 * compiler to warn if a new OCLASS hasn't been handled above.
				 */
		}
	}

	systable_endscan(scan);

	/*
	 * Now scan for dependencies of this column on other things.  The only
	 * things we should find are the dependency on the column datatype and
	 * possibly a collation dependency.  Those can be removed.
	 */
	ScanKeyInit(&key[0],
				Anum_pg_depend_classid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_objid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(rel)));
	ScanKeyInit(&key[2],
				Anum_pg_depend_objsubid,
				BTEqualStrategyNumber, F_INT4EQ,
				Int32GetDatum((int32) attnum));

	scan = systable_beginscan(depRel, DependDependerIndexId, true,
							  NULL, 3, key);

	while (HeapTupleIsValid(depTup = systable_getnext(scan)))
	{
		Form_pg_depend foundDep = (Form_pg_depend) GETSTRUCT(depTup);
		ObjectAddress foundObject;

		foundObject.classId = foundDep->refclassid;
		foundObject.objectId = foundDep->refobjid;
		foundObject.objectSubId = foundDep->refobjsubid;

		if (foundDep->deptype != DEPENDENCY_NORMAL)
			elog(ERROR, "found unexpected dependency type '%c'",
				 foundDep->deptype);
		if (!(foundDep->refclassid == TypeRelationId &&
			  foundDep->refobjid == attTup->atttypid) &&
			!(foundDep->refclassid == CollationRelationId &&
			  foundDep->refobjid == attTup->attcollation))
			elog(ERROR, "found unexpected dependency for column: %s",
				 getObjectDescription(&foundObject, false));

		CatalogTupleDelete(depRel, depTup);
	}

	systable_endscan(scan);

	table_close(depRel, RowExclusiveLock);

	/*
	 * If a YB relation and this table requires a rewrite, we will clone the
	 * table onto an entirely new table with an updated schema reflecting the
	 * new column type.
	 */
	if (yb_clone_table)
	{
		*yb_mutable_rel = YbATCloneRelationSetColumnType(
			rel, colName, targetcollid, typeName, tab->newvals);

		/*
		 * Update the table relid so that further passes will operate on the new
		 * table.
		 */
		tab->relid = (*yb_mutable_rel)->rd_id;

		/* Update the altered column's attribute number. */
		HeapTuple attTup =
			SearchSysCacheAttName(RelationGetRelid(*yb_mutable_rel), colName);
		Assert(HeapTupleIsValid(attTup));
		attnum = ((Form_pg_attribute) GETSTRUCT(attTup))->attnum;
		ReleaseSysCache(attTup);
	}

	/*
	 * Here we go --- change the recorded column type and collation.  (Note
	 * heapTup is a copy of the syscache entry, so okay to scribble on.) First
	 * fix up the missing value if any.
	 */
	if (!yb_clone_table && attTup->atthasmissing)
	{
		Datum		missingval;
		bool		missingNull;

		/* if rewrite is true the missing value should already be cleared */
		Assert(tab->rewrite == 0);

		/* Get the missing value datum */
		missingval = heap_getattr(heapTup,
								  Anum_pg_attribute_attmissingval,
								  attrelation->rd_att,
								  &missingNull);

		/* if it's a null array there is nothing to do */

		if (!missingNull)
		{
			/*
			 * Get the datum out of the array and repack it in a new array
			 * built with the new type data. We assume that since the table
			 * doesn't need rewriting, the actual Datum doesn't need to be
			 * changed, only the array metadata.
			 */

			int			one = 1;
			bool		isNull;
			Datum		valuesAtt[Natts_pg_attribute];
			bool		nullsAtt[Natts_pg_attribute];
			bool		replacesAtt[Natts_pg_attribute];
			HeapTuple	newTup;

			MemSet(valuesAtt, 0, sizeof(valuesAtt));
			MemSet(nullsAtt, false, sizeof(nullsAtt));
			MemSet(replacesAtt, false, sizeof(replacesAtt));

			missingval = array_get_element(missingval,
										   1,
										   &one,
										   0,
										   attTup->attlen,
										   attTup->attbyval,
										   attTup->attalign,
										   &isNull);
			missingval = PointerGetDatum(construct_array(&missingval,
														 1,
														 targettype,
														 tform->typlen,
														 tform->typbyval,
														 tform->typalign));

			valuesAtt[Anum_pg_attribute_attmissingval - 1] = missingval;
			replacesAtt[Anum_pg_attribute_attmissingval - 1] = true;
			nullsAtt[Anum_pg_attribute_attmissingval - 1] = false;

			newTup = heap_modify_tuple(heapTup, RelationGetDescr(attrelation),
									   valuesAtt, nullsAtt, replacesAtt);
			heap_freetuple(heapTup);
			heapTup = newTup;
			attTup = (Form_pg_attribute) GETSTRUCT(heapTup);
		}
	}

	if (!yb_clone_table)
	{
		attTup->atttypid = targettype;
		attTup->atttypmod = targettypmod;
		attTup->attcollation = targetcollid;
		attTup->attndims = list_length(typeName->arrayBounds);
		attTup->attlen = tform->typlen;
		attTup->attbyval = tform->typbyval;
		attTup->attalign = tform->typalign;
		attTup->attstorage = tform->typstorage;
		attTup->attcompression = InvalidCompressionMethod;
	}

	ReleaseSysCache(typeTuple);

	/*
	 * YB Note: Skip the steps below because datatype and collation
	 * dependencies for the table have already been created as part of the
	 * rewrite flow, so we do not need to install them here.
	 * Also, the pg_statistic entries were not cloned for the altered column,
	 * so we don't have any statistics to remove.
	 */
	if (!yb_clone_table)
	{
		CatalogTupleUpdate(attrelation, &heapTup->t_self, heapTup);

		table_close(attrelation, RowExclusiveLock);

		/* Install dependencies on new datatype and collation */
		add_column_datatype_dependency(RelationGetRelid(rel), attnum, targettype);
		add_column_collation_dependency(RelationGetRelid(rel), attnum, targetcollid);

		/*
		 * Drop any pg_statistic entry for the column, since it's now wrong type
		 */
		RemoveStatistics(RelationGetRelid(rel), attnum);
	}

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(*yb_mutable_rel), attnum);

	/*
	 * Update the default, if present, by brute force --- remove and re-add
	 * the default.  Probably unsafe to take shortcuts, since the new version
	 * may well have additional dependencies.  (It's okay to do this now,
	 * rather than after other ALTER TYPE commands, since the default won't
	 * depend on other column types.)
	 */
	if (defaultexpr)
	{
		/*
		 * If it's a GENERATED default, drop its dependency records, in
		 * particular its INTERNAL dependency on the column, which would
		 * otherwise cause dependency.c to refuse to perform the deletion.
		 */
		if (attTup->attgenerated)
		{
			Oid			attrdefoid = GetAttrDefaultOid(RelationGetRelid(rel), attnum);

			if (!OidIsValid(attrdefoid))
				elog(ERROR, "could not find attrdef tuple for relation %u attnum %d",
					 RelationGetRelid(rel), attnum);
			(void) deleteDependencyRecordsFor(AttrDefaultRelationId, attrdefoid, false);
		}

		/*
		 * Make updates-so-far visible, particularly the new pg_attribute row
		 * which will be updated again.
		 */
		CommandCounterIncrement();

		/*
		 * We use RESTRICT here for safety, but at present we do not expect
		 * anything to depend on the default.
		 */
		RemoveAttrDefault(RelationGetRelid(*yb_mutable_rel), attnum,
						  DROP_RESTRICT, true, true);

		StoreAttrDefault(*yb_mutable_rel, attnum, defaultexpr, true, false);
	}

	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(*yb_mutable_rel), attnum);

	/* Cleanup */
	heap_freetuple(heapTup);

	return address;
}

/*
 * Subroutine for ATExecAlterColumnType: remember that a replica identity
 * needs to be reset.
 */
static void
RememberReplicaIdentityForRebuilding(Oid indoid, AlteredTableInfo *tab)
{
	if (!get_index_isreplident(indoid))
		return;

	if (tab->replicaIdentityIndex)
		elog(ERROR, "relation %u has multiple indexes marked as replica identity", tab->relid);

	tab->replicaIdentityIndex = get_rel_name(indoid);
}

/*
 * Subroutine for ATExecAlterColumnType: remember any clustered index.
 */
static void
RememberClusterOnForRebuilding(Oid indoid, AlteredTableInfo *tab)
{
	if (!get_index_isclustered(indoid))
		return;

	if (tab->clusterOnIndex)
		elog(ERROR, "relation %u has multiple clustered indexes", tab->relid);

	tab->clusterOnIndex = get_rel_name(indoid);
}

/*
 * Subroutine for ATExecAlterColumnType: remember that a constraint needs
 * to be rebuilt (which we might already know).
 */
static void
RememberConstraintForRebuilding(Oid conoid, AlteredTableInfo *tab)
{
	/*
	 * This de-duplication check is critical for two independent reasons: we
	 * mustn't try to recreate the same constraint twice, and if a constraint
	 * depends on more than one column whose type is to be altered, we must
	 * capture its definition string before applying any of the column type
	 * changes.  ruleutils.c will get confused if we ask again later.
	 */
	if (!list_member_oid(tab->changedConstraintOids, conoid))
	{
		/* OK, capture the constraint's existing definition string */
		char	   *defstring = pg_get_constraintdef_command(conoid);
		Oid			indoid;

		tab->changedConstraintOids = lappend_oid(tab->changedConstraintOids,
												 conoid);
		tab->changedConstraintDefs = lappend(tab->changedConstraintDefs,
											 defstring);

		/*
		 * For the index of a constraint, if any, remember if it is used for
		 * the table's replica identity or if it is a clustered index, so that
		 * ATPostAlterTypeCleanup() can queue up commands necessary to restore
		 * those properties.
		 */
		indoid = get_constraint_index(conoid);
		if (OidIsValid(indoid))
		{
			RememberReplicaIdentityForRebuilding(indoid, tab);
			RememberClusterOnForRebuilding(indoid, tab);
		}
	}
}

/*
 * Subroutine for ATExecAlterColumnType: remember that an index needs
 * to be rebuilt (which we might already know).
 */
static void
RememberIndexForRebuilding(Oid indoid, AlteredTableInfo *tab)
{
	/*
	 * This de-duplication check is critical for two independent reasons: we
	 * mustn't try to recreate the same index twice, and if an index depends
	 * on more than one column whose type is to be altered, we must capture
	 * its definition string before applying any of the column type changes.
	 * ruleutils.c will get confused if we ask again later.
	 */
	if (!list_member_oid(tab->changedIndexOids, indoid))
	{
		/*
		 * Before adding it as an index-to-rebuild, we'd better see if it
		 * belongs to a constraint, and if so rebuild the constraint instead.
		 * Typically this check fails, because constraint indexes normally
		 * have only dependencies on their constraint.  But it's possible for
		 * such an index to also have direct dependencies on table columns,
		 * for example with a partial exclusion constraint.
		 */
		Oid			conoid = get_index_constraint(indoid);

		if (OidIsValid(conoid))
		{
			RememberConstraintForRebuilding(conoid, tab);
		}
		else
		{
			/* OK, capture the index's existing definition string */
			char	   *defstring = pg_get_indexdef_string(indoid);

			tab->changedIndexOids = lappend_oid(tab->changedIndexOids,
												indoid);
			tab->changedIndexDefs = lappend(tab->changedIndexDefs,
											defstring);

			/*
			 * Remember if this index is used for the table's replica identity
			 * or if it is a clustered index, so that ATPostAlterTypeCleanup()
			 * can queue up commands necessary to restore those properties.
			 */
			RememberReplicaIdentityForRebuilding(indoid, tab);
			RememberClusterOnForRebuilding(indoid, tab);
		}
	}
}

/*
 * Subroutine for ATExecAlterColumnType: remember that a statistics object
 * needs to be rebuilt (which we might already know).
 */
static void
RememberStatisticsForRebuilding(Oid stxoid, AlteredTableInfo *tab)
{
	/*
	 * This de-duplication check is critical for two independent reasons: we
	 * mustn't try to recreate the same statistics object twice, and if the
	 * statistics object depends on more than one column whose type is to be
	 * altered, we must capture its definition string before applying any of
	 * the type changes. ruleutils.c will get confused if we ask again later.
	 */
	if (!list_member_oid(tab->changedStatisticsOids, stxoid))
	{
		/* OK, capture the statistics object's existing definition string */
		char	   *defstring = pg_get_statisticsobjdef_string(stxoid);

		tab->changedStatisticsOids = lappend_oid(tab->changedStatisticsOids,
												 stxoid);
		tab->changedStatisticsDefs = lappend(tab->changedStatisticsDefs,
											 defstring);
	}
}

/*
 * Cleanup after we've finished all the ALTER TYPE operations for a
 * particular relation.  We have to drop and recreate all the indexes
 * and constraints that depend on the altered columns.  We do the
 * actual dropping here, but re-creation is managed by adding work
 * queue entries to do those steps later.
 */
static void
ATPostAlterTypeCleanup(List **wqueue, AlteredTableInfo *tab, LOCKMODE lockmode)
{
	ObjectAddress obj;
	ObjectAddresses *objects;
	ListCell   *def_item;
	ListCell   *oid_item;

	/*
	 * Collect all the constraints and indexes to drop so we can process them
	 * in a single call.  That way we don't have to worry about dependencies
	 * among them.
	 */
	objects = new_object_addresses();

	/*
	 * Re-parse the index and constraint definitions, and attach them to the
	 * appropriate work queue entries.  We do this before dropping because in
	 * the case of a FOREIGN KEY constraint, we might not yet have exclusive
	 * lock on the table the constraint is attached to, and we need to get
	 * that before reparsing/dropping.
	 *
	 * We can't rely on the output of deparsing to tell us which relation to
	 * operate on, because concurrent activity might have made the name
	 * resolve differently.  Instead, we've got to use the OID of the
	 * constraint or index we're processing to figure out which relation to
	 * operate on.
	 */
	forboth(oid_item, tab->changedConstraintOids,
			def_item, tab->changedConstraintDefs)
	{
		Oid			oldId = lfirst_oid(oid_item);
		HeapTuple	tup;
		Form_pg_constraint con;
		Oid			relid;
		Oid			confrelid;
		char		contype;
		bool		conislocal;

		tup = SearchSysCache1(CONSTROID, ObjectIdGetDatum(oldId));
		if (!HeapTupleIsValid(tup)) /* should not happen */
			elog(ERROR, "cache lookup failed for constraint %u", oldId);
		con = (Form_pg_constraint) GETSTRUCT(tup);
		if (OidIsValid(con->conrelid))
			relid = con->conrelid;
		else
		{
			/* must be a domain constraint */
			relid = get_typ_typrelid(getBaseType(con->contypid));
			if (!OidIsValid(relid))
				elog(ERROR, "could not identify relation associated with constraint %u", oldId);
		}
		confrelid = con->confrelid;
		contype = con->contype;
		conislocal = con->conislocal;
		ReleaseSysCache(tup);

		ObjectAddressSet(obj, ConstraintRelationId, oldId);
		add_exact_object_address(&obj, objects);

		/*
		 * If the constraint is inherited (only), we don't want to inject a
		 * new definition here; it'll get recreated when ATAddCheckConstraint
		 * recurses from adding the parent table's constraint.  But we had to
		 * carry the info this far so that we can drop the constraint below.
		 */
		if (!conislocal)
			continue;

		/*
		 * When rebuilding an FK constraint that references the table we're
		 * modifying, we might not yet have any lock on the FK's table, so get
		 * one now.  We'll need AccessExclusiveLock for the DROP CONSTRAINT
		 * step, so there's no value in asking for anything weaker.
		 */
		if (relid != tab->relid && contype == CONSTRAINT_FOREIGN)
			LockRelationOid(relid, AccessExclusiveLock);

		ATPostAlterTypeParse(oldId, relid, confrelid,
							 (char *) lfirst(def_item),
							 wqueue, lockmode, tab->rewrite);
	}
	forboth(oid_item, tab->changedIndexOids,
			def_item, tab->changedIndexDefs)
	{
		Oid			oldId = lfirst_oid(oid_item);
		Oid			relid;

		relid = IndexGetRelation(oldId, false);
		ATPostAlterTypeParse(oldId, relid, InvalidOid,
							 (char *) lfirst(def_item),
							 wqueue, lockmode, tab->rewrite);

		ObjectAddressSet(obj, RelationRelationId, oldId);
		add_exact_object_address(&obj, objects);
	}

	/* add dependencies for new statistics */
	forboth(oid_item, tab->changedStatisticsOids,
			def_item, tab->changedStatisticsDefs)
	{
		Oid			oldId = lfirst_oid(oid_item);
		Oid			relid;

		relid = StatisticsGetRelation(oldId, false);
		ATPostAlterTypeParse(oldId, relid, InvalidOid,
							 (char *) lfirst(def_item),
							 wqueue, lockmode, tab->rewrite);

		ObjectAddressSet(obj, StatisticExtRelationId, oldId);
		add_exact_object_address(&obj, objects);
	}

	/*
	 * Queue up command to restore replica identity index marking
	 */
	if (tab->replicaIdentityIndex)
	{
		AlterTableCmd *cmd = makeNode(AlterTableCmd);
		ReplicaIdentityStmt *subcmd = makeNode(ReplicaIdentityStmt);

		subcmd->identity_type = REPLICA_IDENTITY_INDEX;
		subcmd->name = tab->replicaIdentityIndex;
		cmd->subtype = AT_ReplicaIdentity;
		cmd->def = (Node *) subcmd;

		/* do it after indexes and constraints */
		tab->subcmds[AT_PASS_OLD_CONSTR] =
			lappend(tab->subcmds[AT_PASS_OLD_CONSTR], cmd);
	}

	/*
	 * Queue up command to restore marking of index used for cluster.
	 */
	if (tab->clusterOnIndex)
	{
		AlterTableCmd *cmd = makeNode(AlterTableCmd);

		cmd->subtype = AT_ClusterOn;
		cmd->name = tab->clusterOnIndex;

		/* do it after indexes and constraints */
		tab->subcmds[AT_PASS_OLD_CONSTR] =
			lappend(tab->subcmds[AT_PASS_OLD_CONSTR], cmd);
	}

	/*
	 * It should be okay to use DROP_RESTRICT here, since nothing else should
	 * be depending on these objects.
	 */
	performMultipleDeletions(objects, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);

	free_object_addresses(objects);

	/*
	 * The objects will get recreated during subsequent passes over the work
	 * queue.
	 */
}

/*
 * Parse the previously-saved definition string for a constraint, index or
 * statistics object against the newly-established column data type(s), and
 * queue up the resulting command parsetrees for execution.
 *
 * This might fail if, for example, you have a WHERE clause that uses an
 * operator that's not available for the new column type.
 */
static void
ATPostAlterTypeParse(Oid oldId, Oid oldRelId, Oid refRelId, char *cmd,
					 List **wqueue, LOCKMODE lockmode, bool rewrite)
{
	List	   *raw_parsetree_list;
	List	   *querytree_list;
	ListCell   *list_item;
	Relation	rel;

	/*
	 * We expect that we will get only ALTER TABLE and CREATE INDEX
	 * statements. Hence, there is no need to pass them through
	 * parse_analyze_*() or the rewriter, but instead we need to pass them
	 * through parse_utilcmd.c to make them ready for execution.
	 */
	raw_parsetree_list = raw_parser(cmd, RAW_PARSE_DEFAULT);
	querytree_list = NIL;
	foreach(list_item, raw_parsetree_list)
	{
		RawStmt    *rs = lfirst_node(RawStmt, list_item);
		Node	   *stmt = rs->stmt;

		if (IsA(stmt, IndexStmt))
			querytree_list = lappend(querytree_list,
									 transformIndexStmt(oldRelId,
														(IndexStmt *) stmt,
														cmd));
		else if (IsA(stmt, AlterTableStmt))
		{
			List	   *beforeStmts;
			List	   *afterStmts;

			stmt = (Node *) transformAlterTableStmt(oldRelId,
													(AlterTableStmt *) stmt,
													cmd,
													&beforeStmts,
													&afterStmts);
			querytree_list = list_concat(querytree_list, beforeStmts);
			querytree_list = lappend(querytree_list, stmt);
			querytree_list = list_concat(querytree_list, afterStmts);
		}
		else if (IsA(stmt, CreateStatsStmt))
			querytree_list = lappend(querytree_list,
									 transformStatsStmt(oldRelId,
														(CreateStatsStmt *) stmt,
														cmd));
		else
			querytree_list = lappend(querytree_list, stmt);
	}

	/* Caller should already have acquired whatever lock we need. */
	rel = relation_open(oldRelId, NoLock);

	/*
	 * Attach each generated command to the proper place in the work queue.
	 * Note this could result in creation of entirely new work-queue entries.
	 *
	 * Also note that we have to tweak the command subtypes, because it turns
	 * out that re-creation of indexes and constraints has to act a bit
	 * differently from initial creation.
	 */
	foreach(list_item, querytree_list)
	{
		Node	   *stm = (Node *) lfirst(list_item);
		AlteredTableInfo *tab;

		tab = ATGetQueueEntry(wqueue, rel);

		if (IsA(stm, IndexStmt))
		{
			IndexStmt  *stmt = (IndexStmt *) stm;
			AlterTableCmd *newcmd;

			if (!rewrite)
				TryReuseIndex(oldId, stmt);

			if (IsYugaByteEnabled())
				YbATCopyIndexSplitOptions(oldId, stmt, tab);

			stmt->reset_default_tblspc = true;
			/* keep the index's comment */
			stmt->idxcomment = GetComment(oldId, RelationRelationId, 0);

			newcmd = makeNode(AlterTableCmd);
			newcmd->subtype = AT_ReAddIndex;
			newcmd->def = (Node *) stmt;
			tab->subcmds[AT_PASS_OLD_INDEX] =
				lappend(tab->subcmds[AT_PASS_OLD_INDEX], newcmd);
		}
		else if (IsA(stm, AlterTableStmt))
		{
			AlterTableStmt *stmt = (AlterTableStmt *) stm;
			ListCell   *lcmd;

			foreach(lcmd, stmt->cmds)
			{
				AlterTableCmd *cmd = lfirst_node(AlterTableCmd, lcmd);

				if (cmd->subtype == AT_AddIndex)
				{
					IndexStmt  *indstmt;
					Oid			indoid;

					indstmt = castNode(IndexStmt, cmd->def);
					indoid = get_constraint_index(oldId);

					if (!rewrite)
						TryReuseIndex(indoid, indstmt);
					/* keep any comment on the index */
					indstmt->idxcomment = GetComment(indoid,
													 RelationRelationId, 0);
					indstmt->reset_default_tblspc = true;

					cmd->subtype = AT_ReAddIndex;
					tab->subcmds[AT_PASS_OLD_INDEX] =
						lappend(tab->subcmds[AT_PASS_OLD_INDEX], cmd);

					/* recreate any comment on the constraint */
					RebuildConstraintComment(tab,
											 AT_PASS_OLD_INDEX,
											 oldId,
											 rel,
											 NIL,
											 indstmt->idxname);
				}
				else if (cmd->subtype == AT_AddConstraint)
				{
					Constraint *con = castNode(Constraint, cmd->def);

					con->old_pktable_oid = refRelId;
					/* rewriting neither side of a FK */
					if (con->contype == CONSTR_FOREIGN &&
						!rewrite && tab->rewrite == 0)
						TryReuseForeignKey(oldId, con);
					con->reset_default_tblspc = true;
					cmd->subtype = AT_ReAddConstraint;
					tab->subcmds[AT_PASS_OLD_CONSTR] =
						lappend(tab->subcmds[AT_PASS_OLD_CONSTR], cmd);

					/* recreate any comment on the constraint */
					RebuildConstraintComment(tab,
											 AT_PASS_OLD_CONSTR,
											 oldId,
											 rel,
											 NIL,
											 con->conname);
				}
				else if (cmd->subtype == AT_SetNotNull)
				{
					/*
					 * The parser will create AT_SetNotNull subcommands for
					 * columns of PRIMARY KEY indexes/constraints, but we need
					 * not do anything with them here, because the columns'
					 * NOT NULL marks will already have been propagated into
					 * the new table definition.
					 */
				}
				else
					elog(ERROR, "unexpected statement subtype: %d",
						 (int) cmd->subtype);
			}
		}
		else if (IsA(stm, AlterDomainStmt))
		{
			AlterDomainStmt *stmt = (AlterDomainStmt *) stm;

			if (stmt->subtype == 'C')	/* ADD CONSTRAINT */
			{
				Constraint *con = castNode(Constraint, stmt->def);
				AlterTableCmd *cmd = makeNode(AlterTableCmd);

				cmd->subtype = AT_ReAddDomainConstraint;
				cmd->def = (Node *) stmt;
				tab->subcmds[AT_PASS_OLD_CONSTR] =
					lappend(tab->subcmds[AT_PASS_OLD_CONSTR], cmd);

				/* recreate any comment on the constraint */
				RebuildConstraintComment(tab,
										 AT_PASS_OLD_CONSTR,
										 oldId,
										 NULL,
										 stmt->typeName,
										 con->conname);
			}
			else
				elog(ERROR, "unexpected statement subtype: %d",
					 (int) stmt->subtype);
		}
		else if (IsA(stm, CreateStatsStmt))
		{
			CreateStatsStmt *stmt = (CreateStatsStmt *) stm;
			AlterTableCmd *newcmd;

			/* keep the statistics object's comment */
			stmt->stxcomment = GetComment(oldId, StatisticExtRelationId, 0);

			newcmd = makeNode(AlterTableCmd);
			newcmd->subtype = AT_ReAddStatistics;
			newcmd->def = (Node *) stmt;
			tab->subcmds[AT_PASS_MISC] =
				lappend(tab->subcmds[AT_PASS_MISC], newcmd);
		}
		else
			elog(ERROR, "unexpected statement type: %d",
				 (int) nodeTag(stm));
	}

	relation_close(rel, NoLock);
}

/*
 * Subroutine for ATPostAlterTypeParse() to recreate any existing comment
 * for a table or domain constraint that is being rebuilt.
 *
 * objid is the OID of the constraint.
 * Pass "rel" for a table constraint, or "domname" (domain's qualified name
 * as a string list) for a domain constraint.
 * (We could dig that info, as well as the conname, out of the pg_constraint
 * entry; but callers already have them so might as well pass them.)
 */
static void
RebuildConstraintComment(AlteredTableInfo *tab, int pass, Oid objid,
						 Relation rel, List *domname,
						 const char *conname)
{
	CommentStmt *cmd;
	char	   *comment_str;
	AlterTableCmd *newcmd;

	/* Look for comment for object wanted, and leave if none */
	comment_str = GetComment(objid, ConstraintRelationId, 0);
	if (comment_str == NULL)
		return;

	/* Build CommentStmt node, copying all input data for safety */
	cmd = makeNode(CommentStmt);
	if (rel)
	{
		cmd->objtype = OBJECT_TABCONSTRAINT;
		cmd->object = (Node *)
			list_make3(makeString(get_namespace_name(RelationGetNamespace(rel))),
					   makeString(pstrdup(RelationGetRelationName(rel))),
					   makeString(pstrdup(conname)));
	}
	else
	{
		cmd->objtype = OBJECT_DOMCONSTRAINT;
		cmd->object = (Node *)
			list_make2(makeTypeNameFromNameList(copyObject(domname)),
					   makeString(pstrdup(conname)));
	}
	cmd->comment = comment_str;

	/* Append it to list of commands */
	newcmd = makeNode(AlterTableCmd);
	newcmd->subtype = AT_ReAddComment;
	newcmd->def = (Node *) cmd;
	tab->subcmds[pass] = lappend(tab->subcmds[pass], newcmd);
}

/*
 * Subroutine for ATPostAlterTypeParse().  Calls out to CheckIndexCompatible()
 * for the real analysis, then mutates the IndexStmt based on that verdict.
 */
static void
TryReuseIndex(Oid oldId, IndexStmt *stmt)
{
	if (CheckIndexCompatible(oldId,
							 stmt->accessMethod,
							 stmt->indexParams,
							 stmt->excludeOpNames))
	{
		Relation	irel = index_open(oldId, NoLock);

		/* If it's a partitioned index, there is no storage to share. */
		if (irel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX)
		{
			stmt->oldNode = irel->rd_node.relNode;
			stmt->oldCreateSubid = irel->rd_createSubid;
			stmt->oldFirstRelfilenodeSubid = irel->rd_firstRelfilenodeSubid;
		}
		index_close(irel, NoLock);
	}
}

/*
 * Subroutine for ATPostAlterTypeParse().
 *
 * Stash the old P-F equality operator into the Constraint node, for possible
 * use by ATAddForeignKeyConstraint() in determining whether revalidation of
 * this constraint can be skipped.
 */
static void
TryReuseForeignKey(Oid oldId, Constraint *con)
{
	HeapTuple	tup;
	Datum		adatum;
	bool		isNull;
	ArrayType  *arr;
	Oid		   *rawarr;
	int			numkeys;
	int			i;

	Assert(con->contype == CONSTR_FOREIGN);
	Assert(con->old_conpfeqop == NIL);	/* already prepared this node */

	tup = SearchSysCache1(CONSTROID, ObjectIdGetDatum(oldId));
	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for constraint %u", oldId);

	adatum = SysCacheGetAttr(CONSTROID, tup,
							 Anum_pg_constraint_conpfeqop, &isNull);
	if (isNull)
		elog(ERROR, "null conpfeqop for constraint %u", oldId);
	arr = DatumGetArrayTypeP(adatum);	/* ensure not toasted */
	numkeys = ARR_DIMS(arr)[0];
	/* test follows the one in ri_FetchConstraintInfo() */
	if (ARR_NDIM(arr) != 1 ||
		ARR_HASNULL(arr) ||
		ARR_ELEMTYPE(arr) != OIDOID)
		elog(ERROR, "conpfeqop is not a 1-D Oid array");
	rawarr = (Oid *) ARR_DATA_PTR(arr);

	/* stash a List of the operator Oids in our Constraint node */
	for (i = 0; i < numkeys; i++)
		con->old_conpfeqop = lappend_oid(con->old_conpfeqop, rawarr[i]);

	ReleaseSysCache(tup);
}

/*
 * ALTER COLUMN .. OPTIONS ( ... )
 *
 * Returns the address of the modified column
 */
static ObjectAddress
ATExecAlterColumnGenericOptions(Relation rel,
								const char *colName,
								List *options,
								LOCKMODE lockmode)
{
	Relation	ftrel;
	Relation	attrel;
	ForeignServer *server;
	ForeignDataWrapper *fdw;
	HeapTuple	tuple;
	HeapTuple	newtuple;
	bool		isnull;
	Datum		repl_val[Natts_pg_attribute];
	bool		repl_null[Natts_pg_attribute];
	bool		repl_repl[Natts_pg_attribute];
	Datum		datum;
	Form_pg_foreign_table fttableform;
	Form_pg_attribute atttableform;
	AttrNumber	attnum;
	ObjectAddress address;

	if (options == NIL)
		return InvalidObjectAddress;

	/* First, determine FDW validator associated to the foreign table. */
	ftrel = table_open(ForeignTableRelationId, AccessShareLock);
	tuple = SearchSysCache1(FOREIGNTABLEREL, rel->rd_id);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("foreign table \"%s\" does not exist",
						RelationGetRelationName(rel))));
	fttableform = (Form_pg_foreign_table) GETSTRUCT(tuple);
	server = GetForeignServer(fttableform->ftserver);
	fdw = GetForeignDataWrapper(server->fdwid);

	table_close(ftrel, AccessShareLock);
	ReleaseSysCache(tuple);

	attrel = table_open(AttributeRelationId, RowExclusiveLock);
	tuple = SearchSysCacheAttName(RelationGetRelid(rel), colName);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						colName, RelationGetRelationName(rel))));

	/* Prevent them from altering a system attribute */
	atttableform = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = atttableform->attnum;
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"", colName)));


	/* Initialize buffers for new tuple values */
	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	/* Extract the current options */
	datum = SysCacheGetAttr(ATTNAME,
							tuple,
							Anum_pg_attribute_attfdwoptions,
							&isnull);
	if (isnull)
		datum = PointerGetDatum(NULL);

	/* Transform the options */
	datum = transformGenericOptions(AttributeRelationId,
									datum,
									options,
									fdw->fdwvalidator);

	if (PointerIsValid(DatumGetPointer(datum)))
		repl_val[Anum_pg_attribute_attfdwoptions - 1] = datum;
	else
		repl_null[Anum_pg_attribute_attfdwoptions - 1] = true;

	repl_repl[Anum_pg_attribute_attfdwoptions - 1] = true;

	/* Everything looks good - update the tuple */

	newtuple = heap_modify_tuple(tuple, RelationGetDescr(attrel),
								 repl_val, repl_null, repl_repl);

	CatalogTupleUpdate(attrel, &newtuple->t_self, newtuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  atttableform->attnum);
	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);

	ReleaseSysCache(tuple);

	table_close(attrel, RowExclusiveLock);

	heap_freetuple(newtuple);

	return address;
}

/*
 * ALTER TABLE OWNER
 *
 * recursing is true if we are recursing from a table to its indexes,
 * sequences, or toast table.  We don't allow the ownership of those things to
 * be changed separately from the parent table.  Also, we can skip permission
 * checks (this is necessary not just an optimization, else we'd fail to
 * handle toast tables properly).
 *
 * recursing is also true if ALTER TYPE OWNER is calling us to fix up a
 * free-standing composite type.
 */
void
ATExecChangeOwner(Oid relationOid, Oid newOwnerId, bool recursing, LOCKMODE lockmode)
{
	Relation	target_rel;
	Relation	class_rel;
	HeapTuple	tuple;
	Form_pg_class tuple_class;

	/*
	 * Get exclusive lock till end of transaction on the target table. Use
	 * relation_open so that we can work on indexes and sequences.
	 */
	target_rel = relation_open(relationOid, lockmode);

	/* Get its pg_class tuple, too */
	class_rel = table_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relationOid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relationOid);
	tuple_class = (Form_pg_class) GETSTRUCT(tuple);

	/* Can we change the ownership of this tuple? */
	switch (tuple_class->relkind)
	{
		case RELKIND_RELATION:
		case RELKIND_VIEW:
		case RELKIND_MATVIEW:
		case RELKIND_FOREIGN_TABLE:
		case RELKIND_PARTITIONED_TABLE:
			/* ok to change owner */
			break;
		case RELKIND_INDEX:
			if (!recursing)
			{
				/*
				 * Because ALTER INDEX OWNER used to be allowed, and in fact
				 * is generated by old versions of pg_dump, we give a warning
				 * and do nothing rather than erroring out.  Also, to avoid
				 * unnecessary chatter while restoring those old dumps, say
				 * nothing at all if the command would be a no-op anyway.
				 */
				if (tuple_class->relowner != newOwnerId)
					ereport(WARNING,
							(errcode(ERRCODE_WRONG_OBJECT_TYPE),
							 errmsg("cannot change owner of index \"%s\"",
									NameStr(tuple_class->relname)),
							 errhint("Change the ownership of the index's table, instead.")));
				/* quick hack to exit via the no-op path */
				newOwnerId = tuple_class->relowner;
			}
			break;
		case RELKIND_PARTITIONED_INDEX:
			if (recursing)
				break;
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot change owner of index \"%s\"",
							NameStr(tuple_class->relname)),
					 errhint("Change the ownership of the index's table, instead.")));
			break;
		case RELKIND_SEQUENCE:
			if (!recursing &&
				tuple_class->relowner != newOwnerId)
			{
				/* if it's an owned sequence, disallow changing it by itself */
				Oid			tableId;
				int32		colId;

				if (sequenceIsOwned(relationOid, DEPENDENCY_AUTO, &tableId, &colId) ||
					sequenceIsOwned(relationOid, DEPENDENCY_INTERNAL, &tableId, &colId))
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot change owner of sequence \"%s\"",
									NameStr(tuple_class->relname)),
							 errdetail("Sequence \"%s\" is linked to table \"%s\".",
									   NameStr(tuple_class->relname),
									   get_rel_name(tableId))));
			}
			break;
		case RELKIND_COMPOSITE_TYPE:
			if (recursing)
				break;
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("\"%s\" is a composite type",
							NameStr(tuple_class->relname)),
					 errhint("Use ALTER TYPE instead.")));
			break;
		case RELKIND_TOASTVALUE:
			if (recursing)
				break;
			switch_fallthrough();
		default:
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot change owner of relation \"%s\"",
							NameStr(tuple_class->relname)),
					 errdetail_relkind_not_supported(tuple_class->relkind)));
	}

	/*
	 * If the new owner is the same as the existing owner, consider the
	 * command to have succeeded.  This is for dump restoration purposes.
	 */
	if (tuple_class->relowner != newOwnerId)
	{
		Datum		repl_val[Natts_pg_class];
		bool		repl_null[Natts_pg_class];
		bool		repl_repl[Natts_pg_class];
		Acl		   *newAcl;
		Datum		aclDatum;
		bool		isNull;
		HeapTuple	newtuple;

		/* skip permission checks when recursing to index or toast table */
		if (!recursing)
		{
			/* Superusers can always do it */
			if (!superuser() && !IsYbDbAdminUser(GetUserId()))
			{
				Oid			namespaceOid = tuple_class->relnamespace;
				AclResult	aclresult;

				/* Otherwise, must be owner of the existing object */
				if (!pg_class_ownercheck(relationOid, GetUserId()))
					aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(relationOid)),
								   RelationGetRelationName(target_rel));

				/* Must be able to become new owner */
				check_is_member_of_role(GetUserId(), newOwnerId);

				/* New owner must have CREATE privilege on namespace */
				aclresult = pg_namespace_aclcheck(namespaceOid, newOwnerId,
												  ACL_CREATE);
				if (aclresult != ACLCHECK_OK)
					aclcheck_error(aclresult, OBJECT_SCHEMA,
								   get_namespace_name(namespaceOid));
			}
		}

		memset(repl_null, false, sizeof(repl_null));
		memset(repl_repl, false, sizeof(repl_repl));

		repl_repl[Anum_pg_class_relowner - 1] = true;
		repl_val[Anum_pg_class_relowner - 1] = ObjectIdGetDatum(newOwnerId);

		/*
		 * Determine the modified ACL for the new owner.  This is only
		 * necessary when the ACL is non-null.
		 */
		aclDatum = SysCacheGetAttr(RELOID, tuple,
								   Anum_pg_class_relacl,
								   &isNull);
		if (!isNull)
		{
			newAcl = aclnewowner(DatumGetAclP(aclDatum),
								 tuple_class->relowner, newOwnerId);
			repl_repl[Anum_pg_class_relacl - 1] = true;
			repl_val[Anum_pg_class_relacl - 1] = PointerGetDatum(newAcl);
		}

		newtuple = heap_modify_tuple(tuple, RelationGetDescr(class_rel), repl_val, repl_null, repl_repl);

		CatalogTupleUpdate(class_rel, &newtuple->t_self, newtuple);

		heap_freetuple(newtuple);

		/*
		 * We must similarly update any per-column ACLs to reflect the new
		 * owner; for neatness reasons that's split out as a subroutine.
		 */
		change_owner_fix_column_acls(relationOid,
									 tuple_class->relowner,
									 newOwnerId);

		/*
		 * Update owner dependency reference, if any.  A composite type has
		 * none, because it's tracked for the pg_type entry instead of here;
		 * indexes and TOAST tables don't have their own entries either.
		 */
		if (tuple_class->relkind != RELKIND_COMPOSITE_TYPE &&
			tuple_class->relkind != RELKIND_INDEX &&
			tuple_class->relkind != RELKIND_PARTITIONED_INDEX &&
			tuple_class->relkind != RELKIND_TOASTVALUE)
			changeDependencyOnOwner(RelationRelationId, relationOid,
									newOwnerId);

		/*
		 * Also change the ownership of the table's row type, if it has one
		 */
		if (OidIsValid(tuple_class->reltype))
			AlterTypeOwnerInternal(tuple_class->reltype, newOwnerId);

		/*
		 * If we are operating on a table or materialized view, also change
		 * the ownership of any indexes and sequences that belong to the
		 * relation, as well as its toast table (if it has one).
		 */
		if (tuple_class->relkind == RELKIND_RELATION ||
			tuple_class->relkind == RELKIND_PARTITIONED_TABLE ||
			tuple_class->relkind == RELKIND_MATVIEW ||
			tuple_class->relkind == RELKIND_TOASTVALUE)
		{
			List	   *index_oid_list;
			ListCell   *i;

			/* Find all the indexes belonging to this relation */
			index_oid_list = RelationGetIndexList(target_rel);

			/* For each index, recursively change its ownership */
			foreach(i, index_oid_list)
				ATExecChangeOwner(lfirst_oid(i), newOwnerId, true, lockmode);

			list_free(index_oid_list);
		}

		/* If it has a toast table, recurse to change its ownership */
		if (tuple_class->reltoastrelid != InvalidOid)
			ATExecChangeOwner(tuple_class->reltoastrelid, newOwnerId,
							  true, lockmode);

		/* If it has dependent sequences, recurse to change them too */
		change_owner_recurse_to_sequences(relationOid, newOwnerId, lockmode);
	}

	InvokeObjectPostAlterHook(RelationRelationId, relationOid, 0);

	ReleaseSysCache(tuple);
	table_close(class_rel, RowExclusiveLock);
	relation_close(target_rel, NoLock);
}

/*
 * change_owner_fix_column_acls
 *
 * Helper function for ATExecChangeOwner.  Scan the columns of the table
 * and fix any non-null column ACLs to reflect the new owner.
 */
static void
change_owner_fix_column_acls(Oid relationOid, Oid oldOwnerId, Oid newOwnerId)
{
	Relation	attRelation;
	SysScanDesc scan;
	ScanKeyData key[1];
	HeapTuple	attributeTuple;

	attRelation = table_open(AttributeRelationId, RowExclusiveLock);
	ScanKeyInit(&key[0],
				Anum_pg_attribute_attrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relationOid));
	scan = systable_beginscan(attRelation, AttributeRelidNumIndexId,
							  true, NULL, 1, key);
	while (HeapTupleIsValid(attributeTuple = systable_getnext(scan)))
	{
		Form_pg_attribute att = (Form_pg_attribute) GETSTRUCT(attributeTuple);
		Datum		repl_val[Natts_pg_attribute];
		bool		repl_null[Natts_pg_attribute];
		bool		repl_repl[Natts_pg_attribute];
		Acl		   *newAcl;
		Datum		aclDatum;
		bool		isNull;
		HeapTuple	newtuple;

		/* Ignore dropped columns */
		if (att->attisdropped)
			continue;

		aclDatum = heap_getattr(attributeTuple,
								Anum_pg_attribute_attacl,
								RelationGetDescr(attRelation),
								&isNull);
		/* Null ACLs do not require changes */
		if (isNull)
			continue;

		memset(repl_null, false, sizeof(repl_null));
		memset(repl_repl, false, sizeof(repl_repl));

		newAcl = aclnewowner(DatumGetAclP(aclDatum),
							 oldOwnerId, newOwnerId);
		repl_repl[Anum_pg_attribute_attacl - 1] = true;
		repl_val[Anum_pg_attribute_attacl - 1] = PointerGetDatum(newAcl);

		newtuple = heap_modify_tuple(attributeTuple,
									 RelationGetDescr(attRelation),
									 repl_val, repl_null, repl_repl);

		CatalogTupleUpdate(attRelation, &newtuple->t_self, newtuple);

		heap_freetuple(newtuple);
	}
	systable_endscan(scan);
	table_close(attRelation, RowExclusiveLock);
}

/*
 * change_owner_recurse_to_sequences
 *
 * Helper function for ATExecChangeOwner.  Examines pg_depend searching
 * for sequences that are dependent on serial columns, and changes their
 * ownership.
 */
static void
change_owner_recurse_to_sequences(Oid relationOid, Oid newOwnerId, LOCKMODE lockmode)
{
	Relation	depRel;
	SysScanDesc scan;
	ScanKeyData key[2];
	HeapTuple	tup;

	/*
	 * SERIAL sequences are those having an auto dependency on one of the
	 * table's columns (we don't care *which* column, exactly).
	 */
	depRel = table_open(DependRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_depend_refclassid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_refobjid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relationOid));
	/* we leave refobjsubid unspecified */

	scan = systable_beginscan(depRel, DependReferenceIndexId, true,
							  NULL, 2, key);

	while (HeapTupleIsValid(tup = systable_getnext(scan)))
	{
		Form_pg_depend depForm = (Form_pg_depend) GETSTRUCT(tup);
		Relation	seqRel;

		/* skip dependencies other than auto dependencies on columns */
		if (depForm->refobjsubid == 0 ||
			depForm->classid != RelationRelationId ||
			depForm->objsubid != 0 ||
			!(depForm->deptype == DEPENDENCY_AUTO || depForm->deptype == DEPENDENCY_INTERNAL))
			continue;

		/* Use relation_open just in case it's an index */
		seqRel = relation_open(depForm->objid, lockmode);

		/* skip non-sequence relations */
		if (RelationGetForm(seqRel)->relkind != RELKIND_SEQUENCE)
		{
			/* No need to keep the lock */
			relation_close(seqRel, lockmode);
			continue;
		}

		/* We don't need to close the sequence while we alter it. */
		ATExecChangeOwner(depForm->objid, newOwnerId, true, lockmode);

		/* Now we can close it.  Keep the lock till end of transaction. */
		relation_close(seqRel, NoLock);
	}

	systable_endscan(scan);

	relation_close(depRel, AccessShareLock);
}

/*
 * ALTER TABLE CLUSTER ON
 *
 * The only thing we have to do is to change the indisclustered bits.
 *
 * Return the address of the new clustering index.
 */
static ObjectAddress
ATExecClusterOn(Relation rel, const char *indexName, LOCKMODE lockmode)
{
	Oid			indexOid;
	ObjectAddress address;

	indexOid = get_relname_relid(indexName, rel->rd_rel->relnamespace);

	if (!OidIsValid(indexOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("index \"%s\" for table \"%s\" does not exist",
						indexName, RelationGetRelationName(rel))));

	/* Check index is valid to cluster on */
	check_index_is_clusterable(rel, indexOid, lockmode);

	/* And do the work */
	mark_index_clustered(rel, indexOid, false);

	ObjectAddressSet(address,
					 RelationRelationId, indexOid);

	return address;
}

/*
 * ALTER TABLE SET WITHOUT CLUSTER
 *
 * We have to find any indexes on the table that have indisclustered bit
 * set and turn it off.
 */
static void
ATExecDropCluster(Relation rel, LOCKMODE lockmode)
{
	mark_index_clustered(rel, InvalidOid, false);
}

/*
 * Preparation phase for SET ACCESS METHOD
 *
 * Check that access method exists.  If it is the same as the table's current
 * access method, it is a no-op.  Otherwise, a table rewrite is necessary.
 */
static void
ATPrepSetAccessMethod(AlteredTableInfo *tab, Relation rel, const char *amname)
{
	Oid			amoid;

	/* Check that the table access method exists */
	amoid = get_table_am_oid(amname, false);

	if (rel->rd_rel->relam == amoid)
		return;

	/* Save info for Phase 3 to do the real work */
	tab->rewrite |= AT_REWRITE_ACCESS_METHOD;
	tab->newAccessMethod = amoid;
}

/*
 * ALTER TABLE SET TABLESPACE
 */
static void
ATPrepSetTableSpace(AlteredTableInfo *tab, Relation rel,
					const char *tablespacename, LOCKMODE lockmode,
					bool yb_cascade)
{
	Oid			tablespaceId;

	if (IsYugaByteEnabled() && tablespacename &&
		rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP)
	{
		/*
		 * Disable setting tablespaces for temporary tables in Yugabyte
		 * clusters.
		 */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot set tablespaces for temporary tables")));
	}

	if (IsYugaByteEnabled() && tablespacename && 
		rel->rd_index &&
		rel->rd_index->indisprimary) {
		/*
		 * Disable setting tablespaces for primary key indexes in Yugabyte
		 * clusters.
		 */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot set tablespace for primary key index")));
	}

	if (IsYugaByteEnabled() && !yb_cascade && MyDatabaseColocated &&
		YbGetTableProperties(rel)->is_colocated)
	{
		/*
		 * Cannot move one colocated relation alone
		 * Use Alter TABLE ALL IN TABLESPACE <tsp> SET TABLESPACE <new tsp> CASCADE;
		 */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot move one colocated relation alone"),
				 errhint("Use ALTER ... ALL ... CASCADE to move all colocated relations.")));
	}

	/* Check that the tablespace exists */
	tablespaceId = get_tablespace_oid(tablespacename, false);

	/* Check permissions except when moving to database's default */
	if (OidIsValid(tablespaceId) && tablespaceId != MyDatabaseTableSpace)
	{
		AclResult	aclresult;

		aclresult = pg_tablespace_aclcheck(tablespaceId, GetUserId(), ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_TABLESPACE, tablespacename);
	}

	/* Save info for Phase 3 to do the real work */
	if (OidIsValid(tab->newTableSpace))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("cannot have multiple SET TABLESPACE subcommands")));

	tab->newTableSpace = tablespaceId;
}

/*
 * Set, reset, or replace reloptions.
 */
static void
ATExecSetRelOptions(Relation rel, List *defList, AlterTableType operation,
					LOCKMODE lockmode)
{
	Oid			relid;
	Relation	pgclass;
	HeapTuple	tuple;
	HeapTuple	newtuple;
	Datum		datum;
	bool		isnull;
	Datum		newOptions;
	Datum		repl_val[Natts_pg_class];
	bool		repl_null[Natts_pg_class];
	bool		repl_repl[Natts_pg_class];
	static char *validnsps[] = HEAP_RELOPT_NAMESPACES;

	if (defList == NIL && operation != AT_ReplaceRelOptions)
		return;					/* nothing to do */

	pgclass = table_open(RelationRelationId, RowExclusiveLock);

	/* Fetch heap tuple */
	relid = RelationGetRelid(rel);
	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);

	if (operation == AT_ReplaceRelOptions)
	{
		/*
		 * If we're supposed to replace the reloptions list, we just pretend
		 * there were none before.
		 */
		datum = (Datum) 0;
		isnull = true;
	}
	else
	{
		/* Get the old reloptions */
		datum = SysCacheGetAttr(RELOID, tuple, Anum_pg_class_reloptions,
								&isnull);
	}

	/* Generate new proposed reloptions (text array) */
	newOptions = transformRelOptions(isnull ? (Datum) 0 : datum,
									 defList, NULL, validnsps, false,
									 operation == AT_ResetRelOptions);

	newOptions = ybExcludeNonPersistentReloptions(newOptions);

	/* Validate */
	switch (rel->rd_rel->relkind)
	{
		case RELKIND_RELATION:
		case RELKIND_TOASTVALUE:
		case RELKIND_MATVIEW:
			(void) heap_reloptions(rel->rd_rel->relkind, newOptions, true);
			break;
		case RELKIND_PARTITIONED_TABLE:
			(void) partitioned_table_reloptions(newOptions, true);
			break;
		case RELKIND_VIEW:
			(void) view_reloptions(newOptions, true);
			break;
		case RELKIND_INDEX:
		case RELKIND_PARTITIONED_INDEX:
			(void) index_reloptions(rel->rd_indam->amoptions, newOptions, true);
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot set options for relation \"%s\"",
							RelationGetRelationName(rel)),
					 errdetail_relkind_not_supported(rel->rd_rel->relkind)));
			break;
	}

	/* Special-case validation of view options */
	if (rel->rd_rel->relkind == RELKIND_VIEW)
	{
		Query	   *view_query = get_view_query(rel);
		List	   *view_options = untransformRelOptions(newOptions);
		ListCell   *cell;
		bool		check_option = false;

		foreach(cell, view_options)
		{
			DefElem    *defel = (DefElem *) lfirst(cell);

			if (strcmp(defel->defname, "check_option") == 0)
				check_option = true;
		}

		/*
		 * If the check option is specified, look to see if the view is
		 * actually auto-updatable or not.
		 */
		if (check_option)
		{
			const char *view_updatable_error =
			view_query_is_auto_updatable(view_query, true);

			if (view_updatable_error)
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("WITH CHECK OPTION is supported only on automatically updatable views"),
						 errhint("%s", _(view_updatable_error))));
		}
	}

	/*
	 * All we need do here is update the pg_class row; the new options will be
	 * propagated into relcaches during post-commit cache inval.
	 */
	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	if (newOptions != (Datum) 0)
		repl_val[Anum_pg_class_reloptions - 1] = newOptions;
	else
		repl_null[Anum_pg_class_reloptions - 1] = true;

	repl_repl[Anum_pg_class_reloptions - 1] = true;

	newtuple = heap_modify_tuple(tuple, RelationGetDescr(pgclass),
								 repl_val, repl_null, repl_repl);

	CatalogTupleUpdate(pgclass, &newtuple->t_self, newtuple);

	InvokeObjectPostAlterHook(RelationRelationId, RelationGetRelid(rel), 0);

	heap_freetuple(newtuple);

	ReleaseSysCache(tuple);

	/* repeat the whole exercise for the toast table, if there's one */
	if (OidIsValid(rel->rd_rel->reltoastrelid))
	{
		Relation	toastrel;
		Oid			toastid = rel->rd_rel->reltoastrelid;

		toastrel = table_open(toastid, lockmode);

		/* Fetch heap tuple */
		tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(toastid));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for relation %u", toastid);

		if (operation == AT_ReplaceRelOptions)
		{
			/*
			 * If we're supposed to replace the reloptions list, we just
			 * pretend there were none before.
			 */
			datum = (Datum) 0;
			isnull = true;
		}
		else
		{
			/* Get the old reloptions */
			datum = SysCacheGetAttr(RELOID, tuple, Anum_pg_class_reloptions,
									&isnull);
		}

		newOptions = transformRelOptions(isnull ? (Datum) 0 : datum,
										 defList, "toast", validnsps, false,
										 operation == AT_ResetRelOptions);

		(void) heap_reloptions(RELKIND_TOASTVALUE, newOptions, true);

		memset(repl_val, 0, sizeof(repl_val));
		memset(repl_null, false, sizeof(repl_null));
		memset(repl_repl, false, sizeof(repl_repl));

		if (newOptions != (Datum) 0)
			repl_val[Anum_pg_class_reloptions - 1] = newOptions;
		else
			repl_null[Anum_pg_class_reloptions - 1] = true;

		repl_repl[Anum_pg_class_reloptions - 1] = true;

		newtuple = heap_modify_tuple(tuple, RelationGetDescr(pgclass),
									 repl_val, repl_null, repl_repl);

		CatalogTupleUpdate(pgclass, &newtuple->t_self, newtuple);

		InvokeObjectPostAlterHookArg(RelationRelationId,
									 RelationGetRelid(toastrel), 0,
									 InvalidOid, true);

		heap_freetuple(newtuple);

		ReleaseSysCache(tuple);

		table_close(toastrel, NoLock);
	}

	table_close(pgclass, RowExclusiveLock);
}

/*
 * Execute ALTER TABLE SET TABLESPACE for cases where there is no tuple
 * rewriting to be done, so we just want to copy the data as fast as possible.
 */
static void
ATExecSetTableSpace(Oid tableOid, Oid newTableSpace, LOCKMODE lockmode)
{
	Relation	rel;
	Oid			reltoastrelid;
	Oid			newrelfilenode;
	RelFileNode newrnode;
	List	   *reltoastidxids = NIL;
	ListCell   *lc;

	/*
	 * Need lock here in case we are recursing to toast table or index
	 */
	rel = relation_open(tableOid, lockmode);

	/*
	 * Should only be called on non-Yugabyte relations.
	 */
	Assert(!IsYBRelation(rel));

	/* Check first if relation can be moved to new tablespace */
	if (!CheckRelationTableSpaceMove(rel, newTableSpace))
	{
		InvokeObjectPostAlterHook(RelationRelationId,
								  RelationGetRelid(rel), 0);
		relation_close(rel, NoLock);
		return;
	}

	reltoastrelid = rel->rd_rel->reltoastrelid;
	/* Fetch the list of indexes on toast relation if necessary */
	if (OidIsValid(reltoastrelid))
	{
		Relation	toastRel = relation_open(reltoastrelid, lockmode);

		reltoastidxids = RelationGetIndexList(toastRel);
		relation_close(toastRel, lockmode);
	}

	/*
	 * Relfilenodes are not unique in databases across tablespaces, so we need
	 * to allocate a new one in the new tablespace.
	 */
	newrelfilenode = GetNewRelFileNode(newTableSpace, NULL,
									   rel->rd_rel->relpersistence);

	/* Open old and new relation */
	newrnode = rel->rd_node;
	newrnode.relNode = newrelfilenode;
	newrnode.spcNode = newTableSpace;

	/* hand off to AM to actually create the new filenode and copy the data */
	if (rel->rd_rel->relkind == RELKIND_INDEX)
	{
		index_copy_data(rel, newrnode);
	}
	else
	{
		Assert(RELKIND_HAS_TABLE_AM(rel->rd_rel->relkind));
		table_relation_copy_data(rel, &newrnode);
	}

	/*
	 * Update the pg_class row.
	 *
	 * NB: This wouldn't work if ATExecSetTableSpace() were allowed to be
	 * executed on pg_class or its indexes (the above copy wouldn't contain
	 * the updated pg_class entry), but that's forbidden with
	 * CheckRelationTableSpaceMove().
	 */
	SetRelationTableSpace(rel, newTableSpace, newrelfilenode);

	InvokeObjectPostAlterHook(RelationRelationId, RelationGetRelid(rel), 0);

	RelationAssumeNewRelfilenode(rel);

	relation_close(rel, NoLock);

	/* Make sure the reltablespace change is visible */
	CommandCounterIncrement();

	/* Move associated toast relation and/or indexes, too */
	if (OidIsValid(reltoastrelid))
		ATExecSetTableSpace(reltoastrelid, newTableSpace, lockmode);
	foreach(lc, reltoastidxids)
		ATExecSetTableSpace(lfirst_oid(lc), newTableSpace, lockmode);

	/* Clean up */
	list_free(reltoastidxids);
}

/*
 * Special handling of ALTER TABLE SET TABLESPACE for relations with no
 * storage that have an interest in preserving tablespace.
 *
 * Since these have no storage the tablespace can be updated with a simple
 * metadata only operation to update the tablespace.
 */
static void
ATExecSetTableSpaceNoStorage(Relation rel, Oid newTableSpace)
{
	/*
	 * Shouldn't be called on relations having storage; these are processed
	 * in phase 3.  Yugabyte tables do not use the Postgres store so it appears to
	 * Postgres as if there is no associated storage.
	 */
	Assert(IsYBRelation(rel) || !RELKIND_HAS_STORAGE(rel->rd_rel->relkind));

	/* check if relation can be moved to its new tablespace */
	if (!CheckRelationTableSpaceMove(rel, newTableSpace))
	{
		InvokeObjectPostAlterHook(RelationRelationId,
								  RelationGetRelid(rel),
								  0);
		return;
	}

	if (IsYBRelation(rel)) {
		Datum *options;
		int num_options;
		yb_get_tablespace_options(&options, &num_options, newTableSpace);
		/*
		 * Validation should only happen on tablespaces that have a defined
		 * replica placement
		 */
		for (int i = 0; i < num_options; i++) {
			char *option = text_to_cstring(DatumGetTextP(options[i]));
			YBCValidatePlacement(option);
			pfree(option);
		}
	}

	/* Update can be done, so change reltablespace */
	SetRelationTableSpace(rel, newTableSpace, InvalidOid);

	InvokeObjectPostAlterHook(RelationRelationId, RelationGetRelid(rel), 0);

	/* Make sure the reltablespace change is visible */
	CommandCounterIncrement();

	/* Notify the user that this command is async */
	ereport(NOTICE,
			(errmsg("Data movement for table %s is successfully initiated.",
					RelationGetRelationName(rel)),
			 errdetail("Data movement is a long running asynchronous process "
					   "and can be monitored by checking the tablet placement "
					   "in http://<YB-Master-host>:7000/tables")));
}

/*
 * Alter Table ALL ... SET TABLESPACE
 *
 * Allows a user to move all objects of some type in a given tablespace in the
 * current database to another tablespace.  Objects can be chosen based on the
 * owner of the object also, to allow users to move only their objects.
 * The user must have CREATE rights on the new tablespace, as usual.   The main
 * permissions handling is done by the lower-level table move function.
 *
 * All to-be-moved objects are locked first. If NOWAIT is specified and the
 * lock can't be acquired then we ereport(ERROR).
 */
Oid
AlterTableMoveAll(AlterTableMoveAllStmt *stmt)
{
	List	   *relations = NIL;
	ListCell   *l;
	ScanKeyData key[1];
	Relation	rel;
	TableScanDesc scan;
	HeapTuple	tuple;
	Oid			orig_tablespaceoid;
	Oid			new_tablespaceoid;
	List	   *role_oids = roleSpecsToIds(stmt->roles);
	Form_pg_class yb_rd_rel;
	Relation	yb_index_rel;
	Relation	yb_table_rel;
	Relation	yb_pg_class;
	Oid			yb_table_oid = InvalidOid;
	Oid			yb_colocated_with_tablegroup_oid = InvalidOid;
	Oid			yb_orig_tablegroup_oid = InvalidOid;
	Oid			yb_new_tablegroup_oid = InvalidOid;
	char	   *yb_orig_tablegroup_name;
	char	   *yb_new_tablegroup_name;
	bool		yb_cascade = stmt->yb_cascade;

	/* Ensure we were not asked to move something we can't */
	if (stmt->objtype != OBJECT_TABLE && stmt->objtype != OBJECT_INDEX &&
		stmt->objtype != OBJECT_MATVIEW)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("only tables, indexes, and materialized views exist in tablespaces")));

	/* Get the orig and new tablespace OIDs */
	orig_tablespaceoid = get_tablespace_oid(stmt->orig_tablespacename, false);
	new_tablespaceoid = get_tablespace_oid(stmt->new_tablespacename, false);
	yb_orig_tablegroup_name = get_implicit_tablegroup_name(orig_tablespaceoid);
	yb_new_tablegroup_name = get_implicit_tablegroup_name(new_tablespaceoid);
	yb_orig_tablegroup_oid = get_tablegroup_oid(yb_orig_tablegroup_name, true);
	yb_new_tablegroup_oid = get_tablegroup_oid(yb_new_tablegroup_name, true);
	/*
	 * If a relation name is passed with the ALTER TABLE ALL ... COLOCATED WITH
	 * ... SET TABLESPACE ... CASCADE command then we get the relation being
	 * passed.
	 */
	if (stmt->yb_relation != NULL)
	{
		yb_table_oid = RangeVarGetRelid(stmt->yb_relation, NoLock, false);
		yb_table_rel = RelationIdGetRelation(yb_table_oid);
		yb_colocated_with_tablegroup_oid =
			YbGetTableProperties(yb_table_rel)->tablegroup_oid;
		RelationClose(yb_table_rel);
		yb_orig_tablegroup_oid = yb_colocated_with_tablegroup_oid;
		yb_orig_tablegroup_name = get_tablegroup_name(yb_orig_tablegroup_oid);
	}

	/*
	 * The new tablespace must not have any colocated relations present in
	 * it. As we don't support decolocation of colocated tablets.
	 */
	if (MyDatabaseColocated && OidIsValid(yb_new_tablegroup_oid) &&
		OidIsValid(yb_orig_tablegroup_oid))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot move colocated relations to tablespace %s,"
						" as it contains existing colocated relation",
						stmt->new_tablespacename)));

	/*
	 * If CASCADE is not specified and the original tablespace contains
	 * colocated tables then we don't support moving it unless cascade is
	 * specified.
	 */
	if (!yb_cascade && OidIsValid(yb_orig_tablegroup_oid))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot move colocated relations present in"
						" tablespace %s", stmt->orig_tablespacename),
				 errhint("Use ALTER ... CASCADE to move colcated relations.")));

	if (OidIsValid(yb_table_oid) && !MyDatabaseColocated)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("this command is not supported in a non-colocated"
						" database"),
				 errdetail("Use ALTER ... SET TABLESPACE to move non-colocated"
						   " relations.")));

	if (OidIsValid(yb_table_oid) &&
		!(OidIsValid(yb_colocated_with_tablegroup_oid)))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("the specified relation is non-colocated"
						" which can't be moved using this command")));

	/* Can't move shared relations in to or out of pg_global */
	/* This is also checked by ATExecSetTableSpace, but nice to stop earlier */
	if (orig_tablespaceoid == GLOBALTABLESPACE_OID ||
		new_tablespaceoid == GLOBALTABLESPACE_OID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("cannot move relations in to or out of pg_global tablespace")));

	/*
	 * Must have CREATE rights on the new tablespace, unless it is the
	 * database default tablespace (which all users implicitly have CREATE
	 * rights on).
	 */
	if (OidIsValid(new_tablespaceoid) && new_tablespaceoid != MyDatabaseTableSpace)
	{
		AclResult	aclresult;

		aclresult = pg_tablespace_aclcheck(new_tablespaceoid, GetUserId(),
										   ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_TABLESPACE,
						   get_tablespace_name(new_tablespaceoid));
	}

	/*
	 * Now that the checks are done, check if we should set either to
	 * InvalidOid because it is our database's default tablespace.
	 */
	if (orig_tablespaceoid == MyDatabaseTableSpace)
		orig_tablespaceoid = InvalidOid;

	if (new_tablespaceoid == MyDatabaseTableSpace)
		new_tablespaceoid = InvalidOid;

	/* no-op */
	if (orig_tablespaceoid == new_tablespaceoid)
		return new_tablespaceoid;

	/*
	 * Walk the list of objects in the tablespace and move them. This will
	 * only find objects in our database, of course.
	 */
	ScanKeyInit(&key[0],
				Anum_pg_class_reltablespace,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(orig_tablespaceoid));

	rel = table_open(RelationRelationId, AccessShareLock);
	scan = table_beginscan_catalog(rel, 1, key);
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Form_pg_class relForm = (Form_pg_class) GETSTRUCT(tuple);
		Oid			relOid = relForm->oid;

		/*
		 * Do not move objects in pg_catalog as part of this, if an admin
		 * really wishes to do so, they can issue the individual ALTER
		 * commands directly.
		 *
		 * Also, explicitly avoid any shared tables, temp tables, or TOAST
		 * (TOAST will be moved with the main table).
		 */
		if (IsCatalogNamespace(relForm->relnamespace) ||
			relForm->relisshared ||
			isAnyTempNamespace(relForm->relnamespace) ||
			IsToastNamespace(relForm->relnamespace))
			continue;

		if (OidIsValid(yb_colocated_with_tablegroup_oid) &&
			!ybIsTablegroupDependent(relOid, yb_colocated_with_tablegroup_oid))
			continue;

		/*
		 * In YB, a primary key index is an intrinsic part of its base table.
		 * For a primary key index, we only need to update the
		 * new_tablespaceoid field in pg_class.
		 */
		if (relForm->relkind == RELKIND_INDEX ||
			relForm->relkind == RELKIND_PARTITIONED_INDEX)
		{
			yb_index_rel = RelationIdGetRelation(relOid);
			bool isPrimaryIndex = (yb_index_rel != NULL &&
								   yb_index_rel->rd_index->indisprimary);

			RelationClose(yb_index_rel);

			if (isPrimaryIndex)
			{
				/*
				 * We move the primary key indexes along with the tables that
				 * they are associated with when using the following commands
				 * ALTER TABLE/INDEX/MATERIALIZED VIEW ... SET TABLESPACE ...
				 */
				if (yb_cascade || (!yb_cascade && stmt->objtype == OBJECT_TABLE))
				{
					yb_pg_class = table_open(RelationRelationId,
											RowExclusiveLock);

					tuple = SearchSysCacheCopy1(RELOID,
												ObjectIdGetDatum(relOid));
					if (!HeapTupleIsValid(tuple))
						elog(ERROR, "cache lookup failed for relation %u",
							 relOid);
					yb_rd_rel = (Form_pg_class) GETSTRUCT(tuple);

					/* Update the pg_class row */
					yb_rd_rel->reltablespace = new_tablespaceoid;
					CatalogTupleUpdate(yb_pg_class, &tuple->t_self, tuple);

					InvokeObjectPostAlterHook(RelationRelationId, relOid, 0);

					heap_freetuple(tuple);

					table_close(yb_pg_class, RowExclusiveLock);

					/* Update the pg_shdepend entries. */
					changeDependencyOnTablespace(RelationRelationId, relOid,
												 new_tablespaceoid);
				}
				continue;
			}
		}

		/*
		 * If CASCADE is not specified, only move the object type requested.
		 */
		if (!yb_cascade &&
			((stmt->objtype == OBJECT_TABLE &&
			  relForm->relkind != RELKIND_RELATION &&
			  relForm->relkind != RELKIND_PARTITIONED_TABLE) ||
			 (stmt->objtype == OBJECT_INDEX &&
			  relForm->relkind != RELKIND_INDEX &&
			  relForm->relkind != RELKIND_PARTITIONED_INDEX) ||
			 (stmt->objtype == OBJECT_MATVIEW &&
			  relForm->relkind != RELKIND_MATVIEW)))
			continue;

		/* Check if we are only moving objects owned by certain roles */
		if (role_oids != NIL && !list_member_oid(role_oids, relForm->relowner))
			continue;

		/*
		 * Handle permissions-checking here since we are locking the tables
		 * and also to avoid doing a bunch of work only to fail part-way. Note
		 * that permissions will also be checked by AlterTableInternal().
		 *
		 * Caller must be considered an owner on the table to move it.
		 */
		if (!pg_class_ownercheck(relOid, GetUserId()))
			aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(relOid)),
						   NameStr(relForm->relname));

		if (stmt->nowait &&
			!ConditionalLockRelationOid(relOid, AccessExclusiveLock))
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_IN_USE),
					 errmsg("aborting because lock on relation \"%s.%s\" is not available",
							get_namespace_name(relForm->relnamespace),
							NameStr(relForm->relname))));
		else
			LockRelationOid(relOid, AccessExclusiveLock);

		/* Update the pg_shdepend tables */
		changeDependencyOnTablespace(RelationRelationId, relOid,
									 new_tablespaceoid);

		/* Add to our list of objects to move */
		relations = lappend_oid(relations, relOid);
	}

	table_endscan(scan);
	table_close(rel, AccessShareLock);

	if (relations == NIL)
	{
		ereport(NOTICE,
				(errcode(ERRCODE_NO_DATA_FOUND),
				 errmsg("no matching relations in tablespace \"%s\" found",
						orig_tablespaceoid == InvalidOid ? "(database default)" :
						get_tablespace_name(orig_tablespaceoid))));
		return new_tablespaceoid;
	}

	/* Everything is locked, loop through and move all of the relations. */
	foreach(l, relations)
	{
		List	   *cmds = NIL;
		AlterTableCmd *cmd = makeNode(AlterTableCmd);

		cmd->subtype = AT_SetTableSpace;
		cmd->name = stmt->new_tablespacename;
		cmd->yb_cascade = true;

		cmds = lappend(cmds, cmd);

		EventTriggerAlterTableStart((Node *) stmt);
		/* OID is set by AlterTableInternal */
		AlterTableInternal(lfirst_oid(l), cmds, false);
		EventTriggerAlterTableEnd();
	}

	/*
	 * Update the dependencies present in pg_shdepend for tablegroup to
	 * tablespace dependencies if CASCADE command is used in a colocated
	 * database.
	 */
	if (yb_cascade && OidIsValid(new_tablespaceoid) &&
		MyDatabaseColocated &&
		!OidIsValid(yb_new_tablegroup_oid) &&
		OidIsValid(yb_orig_tablegroup_oid))
	{
		/* Update pg_shdepend values with the new Tablespace. */
		changeDependencyOnTablespace(YbTablegroupRelationId,
									 yb_orig_tablegroup_oid, new_tablespaceoid);
		/* Update entry in pg_yb_tablegroup */
		ybAlterTablespaceForTablegroup(yb_orig_tablegroup_name,
									   new_tablespaceoid,
									   yb_new_tablegroup_name);
	}

	return new_tablespaceoid;
}

static void
index_copy_data(Relation rel, RelFileNode newrnode)
{
	SMgrRelation dstrel;

	dstrel = smgropen(newrnode, rel->rd_backend);

	/*
	 * Since we copy the file directly without looking at the shared buffers,
	 * we'd better first flush out any pages of the source relation that are
	 * in shared buffers.  We assume no new changes will be made while we are
	 * holding exclusive lock on the rel.
	 */
	FlushRelationBuffers(rel);

	/*
	 * Create and copy all forks of the relation, and schedule unlinking of
	 * old physical files.
	 *
	 * NOTE: any conflict in relfilenode value will be caught in
	 * RelationCreateStorage().
	 */
	RelationCreateStorage(newrnode, rel->rd_rel->relpersistence, true);

	/* copy main fork */
	RelationCopyStorage(RelationGetSmgr(rel), dstrel, MAIN_FORKNUM,
						rel->rd_rel->relpersistence);

	/* copy those extra forks that exist */
	for (ForkNumber forkNum = MAIN_FORKNUM + 1;
		 forkNum <= MAX_FORKNUM; forkNum++)
	{
		if (smgrexists(RelationGetSmgr(rel), forkNum))
		{
			smgrcreate(dstrel, forkNum, false);

			/*
			 * WAL log creation if the relation is persistent, or this is the
			 * init fork of an unlogged relation.
			 */
			if (RelationIsPermanent(rel) ||
				(rel->rd_rel->relpersistence == RELPERSISTENCE_UNLOGGED &&
				 forkNum == INIT_FORKNUM))
				log_smgrcreate(&newrnode, forkNum);
			RelationCopyStorage(RelationGetSmgr(rel), dstrel, forkNum,
								rel->rd_rel->relpersistence);
		}
	}

	/* drop old relation, and close new one */
	RelationDropStorage(rel);
	smgrclose(dstrel);
}

/*
 * ALTER TABLE ENABLE/DISABLE TRIGGER
 *
 * We just pass this off to trigger.c.
 */
static void
ATExecEnableDisableTrigger(Relation rel, const char *trigname,
						   char fires_when, bool skip_system, bool recurse,
						   LOCKMODE lockmode)
{
	EnableDisableTriggerNew(rel, trigname, fires_when, skip_system, recurse,
							lockmode);
}

/*
 * ALTER TABLE ENABLE/DISABLE RULE
 *
 * We just pass this off to rewriteDefine.c.
 */
static void
ATExecEnableDisableRule(Relation rel, const char *rulename,
						char fires_when, LOCKMODE lockmode)
{
	EnableDisableRule(rel, rulename, fires_when);
}

/*
 * ALTER TABLE INHERIT
 *
 * Add a parent to the child's parents. This verifies that all the columns and
 * check constraints of the parent appear in the child and that they have the
 * same data types and expressions.
 */
static void
ATPrepAddInherit(Relation child_rel)
{
	if (child_rel->rd_rel->reloftype)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot change inheritance of typed table")));

	if (child_rel->rd_rel->relispartition)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot change inheritance of a partition")));

	if (child_rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot change inheritance of partitioned table")));
}

/*
 * Return the address of the new parent relation.
 */
static ObjectAddress
ATExecAddInherit(Relation child_rel, RangeVar *parent, LOCKMODE lockmode)
{
	Relation	parent_rel;
	List	   *children;
	ObjectAddress address;
	const char *trigger_name;

	/*
	 * A self-exclusive lock is needed here.  See the similar case in
	 * MergeAttributes() for a full explanation.
	 */
	parent_rel = table_openrv(parent, ShareUpdateExclusiveLock);

	/*
	 * Must be owner of both parent and child -- child was checked by
	 * ATSimplePermissions call in ATPrepCmd
	 */
	ATSimplePermissions(AT_AddInherit, parent_rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	/* Permanent rels cannot inherit from temporary ones */
	if (parent_rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
		child_rel->rd_rel->relpersistence != RELPERSISTENCE_TEMP)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot inherit from temporary relation \"%s\"",
						RelationGetRelationName(parent_rel))));

	/* If parent rel is temp, it must belong to this session */
	if (parent_rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
		!parent_rel->rd_islocaltemp)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot inherit from temporary relation of another session")));

	/* Ditto for the child */
	if (child_rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
		!child_rel->rd_islocaltemp)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot inherit to temporary relation of another session")));

	/* Prevent partitioned tables from becoming inheritance parents */
	if (parent_rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot inherit from partitioned table \"%s\"",
						parent->relname)));

	/* Likewise for partitions */
	if (parent_rel->rd_rel->relispartition)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot inherit from a partition")));

	/*
	 * Prevent circularity by seeing if proposed parent inherits from child.
	 * (In particular, this disallows making a rel inherit from itself.)
	 *
	 * This is not completely bulletproof because of race conditions: in
	 * multi-level inheritance trees, someone else could concurrently be
	 * making another inheritance link that closes the loop but does not join
	 * either of the rels we have locked.  Preventing that seems to require
	 * exclusive locks on the entire inheritance tree, which is a cure worse
	 * than the disease.  find_all_inheritors() will cope with circularity
	 * anyway, so don't sweat it too much.
	 *
	 * We use weakest lock we can on child's children, namely AccessShareLock.
	 */
	children = find_all_inheritors(RelationGetRelid(child_rel),
								   AccessShareLock, NULL);

	if (list_member_oid(children, RelationGetRelid(parent_rel)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_TABLE),
				 errmsg("circular inheritance not allowed"),
				 errdetail("\"%s\" is already a child of \"%s\".",
						   parent->relname,
						   RelationGetRelationName(child_rel))));

	/*
	 * If child_rel has row-level triggers with transition tables, we
	 * currently don't allow it to become an inheritance child.  See also
	 * prohibitions in ATExecAttachPartition() and CreateTrigger().
	 */
	trigger_name = FindTriggerIncompatibleWithInheritance(child_rel->trigdesc);
	if (trigger_name != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("trigger \"%s\" prevents table \"%s\" from becoming an inheritance child",
						trigger_name, RelationGetRelationName(child_rel)),
				 errdetail("ROW triggers with transition tables are not supported in inheritance hierarchies.")));

	/* OK to create inheritance */
	CreateInheritance(child_rel, parent_rel);

	ObjectAddressSet(address, RelationRelationId,
					 RelationGetRelid(parent_rel));

	/* keep our lock on the parent relation until commit */
	table_close(parent_rel, NoLock);

	return address;
}

/*
 * CreateInheritance
 *		Catalog manipulation portion of creating inheritance between a child
 *		table and a parent table.
 *
 * Common to ATExecAddInherit() and ATExecAttachPartition().
 */
static void
CreateInheritance(Relation child_rel, Relation parent_rel)
{
	Relation	catalogRelation;
	SysScanDesc scan;
	ScanKeyData key;
	HeapTuple	inheritsTuple;
	int32		inhseqno;

	/* Note: get RowExclusiveLock because we will write pg_inherits below. */
	catalogRelation = table_open(InheritsRelationId, RowExclusiveLock);

	/*
	 * Check for duplicates in the list of parents, and determine the highest
	 * inhseqno already present; we'll use the next one for the new parent.
	 * Also, if proposed child is a partition, it cannot already be
	 * inheriting.
	 *
	 * Note: we do not reject the case where the child already inherits from
	 * the parent indirectly; CREATE TABLE doesn't reject comparable cases.
	 */
	ScanKeyInit(&key,
				Anum_pg_inherits_inhrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(child_rel)));
	scan = systable_beginscan(catalogRelation, InheritsRelidSeqnoIndexId,
							  true, NULL, 1, &key);

	/* inhseqno sequences start at 1 */
	inhseqno = 0;
	while (HeapTupleIsValid(inheritsTuple = systable_getnext(scan)))
	{
		Form_pg_inherits inh = (Form_pg_inherits) GETSTRUCT(inheritsTuple);

		if (inh->inhparent == RelationGetRelid(parent_rel))
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_TABLE),
					 errmsg("relation \"%s\" would be inherited from more than once",
							RelationGetRelationName(parent_rel))));

		if (inh->inhseqno > inhseqno)
			inhseqno = inh->inhseqno;
	}
	systable_endscan(scan);

	/* Match up the columns and bump attinhcount as needed */
	MergeAttributesIntoExisting(child_rel, parent_rel);

	/* Match up the constraints and bump coninhcount as needed */
	MergeConstraintsIntoExisting(child_rel, parent_rel);

	/*
	 * OK, it looks valid.  Make the catalog entries that show inheritance.
	 */
	StoreCatalogInheritance1(RelationGetRelid(child_rel),
							 RelationGetRelid(parent_rel),
							 inhseqno + 1,
							 catalogRelation,
							 parent_rel->rd_rel->relkind ==
							 RELKIND_PARTITIONED_TABLE);

	/* Now we're done with pg_inherits */
	table_close(catalogRelation, RowExclusiveLock);
}

/*
 * Obtain the source-text form of the constraint expression for a check
 * constraint, given its pg_constraint tuple
 */
static char *
decompile_conbin(HeapTuple contup, TupleDesc tupdesc)
{
	Form_pg_constraint con;
	bool		isnull;
	Datum		attr;
	Datum		expr;

	con = (Form_pg_constraint) GETSTRUCT(contup);
	attr = heap_getattr(contup, Anum_pg_constraint_conbin, tupdesc, &isnull);
	if (isnull)
		elog(ERROR, "null conbin for constraint %u", con->oid);

	expr = DirectFunctionCall2(pg_get_expr, attr,
							   ObjectIdGetDatum(con->conrelid));
	return TextDatumGetCString(expr);
}

/*
 * Determine whether two check constraints are functionally equivalent
 *
 * The test we apply is to see whether they reverse-compile to the same
 * source string.  This insulates us from issues like whether attributes
 * have the same physical column numbers in parent and child relations.
 */
static bool
constraints_equivalent(HeapTuple a, HeapTuple b, TupleDesc tupleDesc)
{
	Form_pg_constraint acon = (Form_pg_constraint) GETSTRUCT(a);
	Form_pg_constraint bcon = (Form_pg_constraint) GETSTRUCT(b);

	if (acon->condeferrable != bcon->condeferrable ||
		acon->condeferred != bcon->condeferred ||
		strcmp(decompile_conbin(a, tupleDesc),
			   decompile_conbin(b, tupleDesc)) != 0)
		return false;
	else
		return true;
}

/*
 * Check columns in child table match up with columns in parent, and increment
 * their attinhcount.
 *
 * Called by CreateInheritance
 *
 * Currently all parent columns must be found in child. Missing columns are an
 * error.  One day we might consider creating new columns like CREATE TABLE
 * does.  However, that is widely unpopular --- in the common use case of
 * partitioned tables it's a foot-gun.
 *
 * The data type must match exactly. If the parent column is NOT NULL then
 * the child must be as well. Defaults are not compared, however.
 */
static void
MergeAttributesIntoExisting(Relation child_rel, Relation parent_rel)
{
	Relation	attrrel;
	AttrNumber	parent_attno;
	int			parent_natts;
	TupleDesc	tupleDesc;
	HeapTuple	tuple;
	bool		child_is_partition = false;

	attrrel = table_open(AttributeRelationId, RowExclusiveLock);

	tupleDesc = RelationGetDescr(parent_rel);
	parent_natts = tupleDesc->natts;

	/* If parent_rel is a partitioned table, child_rel must be a partition */
	if (parent_rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		child_is_partition = true;

	for (parent_attno = 1; parent_attno <= parent_natts; parent_attno++)
	{
		Form_pg_attribute attribute = TupleDescAttr(tupleDesc,
													parent_attno - 1);
		char	   *attributeName = NameStr(attribute->attname);

		/* Ignore dropped columns in the parent. */
		if (attribute->attisdropped)
			continue;

		/* Find same column in child (matching on column name). */
		tuple = SearchSysCacheCopyAttName(RelationGetRelid(child_rel),
										  attributeName);
		if (HeapTupleIsValid(tuple))
		{
			/* Check they are same type, typmod, and collation */
			Form_pg_attribute childatt = (Form_pg_attribute) GETSTRUCT(tuple);

			if (attribute->atttypid != childatt->atttypid ||
				attribute->atttypmod != childatt->atttypmod)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("child table \"%s\" has different type for column \"%s\"",
								RelationGetRelationName(child_rel),
								attributeName)));

			if (attribute->attcollation != childatt->attcollation)
				ereport(ERROR,
						(errcode(ERRCODE_COLLATION_MISMATCH),
						 errmsg("child table \"%s\" has different collation for column \"%s\"",
								RelationGetRelationName(child_rel),
								attributeName)));

			/*
			 * Check child doesn't discard NOT NULL property.  (Other
			 * constraints are checked elsewhere.)
			 */
			if (attribute->attnotnull && !childatt->attnotnull)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("column \"%s\" in child table must be marked NOT NULL",
								attributeName)));

			/*
			 * If parent column is generated, child column must be, too.
			 */
			if (attribute->attgenerated && !childatt->attgenerated)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("column \"%s\" in child table must be a generated column",
								attributeName)));

			/*
			 * Check that both generation expressions match.
			 *
			 * The test we apply is to see whether they reverse-compile to the
			 * same source string.  This insulates us from issues like whether
			 * attributes have the same physical column numbers in parent and
			 * child relations.  (See also constraints_equivalent().)
			 */
			if (attribute->attgenerated && childatt->attgenerated)
			{
				TupleConstr *child_constr = child_rel->rd_att->constr;
				TupleConstr *parent_constr = parent_rel->rd_att->constr;
				char	   *child_expr = NULL;
				char	   *parent_expr = NULL;

				Assert(child_constr != NULL);
				Assert(parent_constr != NULL);

				for (int i = 0; i < child_constr->num_defval; i++)
				{
					if (child_constr->defval[i].adnum == childatt->attnum)
					{
						child_expr =
							TextDatumGetCString(DirectFunctionCall2(pg_get_expr,
																	CStringGetTextDatum(child_constr->defval[i].adbin),
																	ObjectIdGetDatum(child_rel->rd_id)));
						break;
					}
				}
				Assert(child_expr != NULL);

				for (int i = 0; i < parent_constr->num_defval; i++)
				{
					if (parent_constr->defval[i].adnum == attribute->attnum)
					{
						parent_expr =
							TextDatumGetCString(DirectFunctionCall2(pg_get_expr,
																	CStringGetTextDatum(parent_constr->defval[i].adbin),
																	ObjectIdGetDatum(parent_rel->rd_id)));
						break;
					}
				}
				Assert(parent_expr != NULL);

				if (strcmp(child_expr, parent_expr) != 0)
					ereport(ERROR,
							(errcode(ERRCODE_DATATYPE_MISMATCH),
							 errmsg("column \"%s\" in child table has a conflicting generation expression",
									attributeName)));
			}

			/*
			 * OK, bump the child column's inheritance count.  (If we fail
			 * later on, this change will just roll back.)
			 */
			childatt->attinhcount++;

			/*
			 * In case of partitions, we must enforce that value of attislocal
			 * is same in all partitions. (Note: there are only inherited
			 * attributes in partitions)
			 */
			if (child_is_partition)
			{
				Assert(childatt->attinhcount == 1);
				childatt->attislocal = false;
			}

			CatalogTupleUpdate(attrrel, &tuple->t_self, tuple);
			heap_freetuple(tuple);
		}
		else
		{
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("child table is missing column \"%s\"",
							attributeName)));
		}
	}

	table_close(attrrel, RowExclusiveLock);
}

/*
 * Check constraints in child table match up with constraints in parent,
 * and increment their coninhcount.
 *
 * Constraints that are marked ONLY in the parent are ignored.
 *
 * Called by CreateInheritance
 *
 * Currently all constraints in parent must be present in the child. One day we
 * may consider adding new constraints like CREATE TABLE does.
 *
 * XXX This is O(N^2) which may be an issue with tables with hundreds of
 * constraints. As long as tables have more like 10 constraints it shouldn't be
 * a problem though. Even 100 constraints ought not be the end of the world.
 *
 * XXX See MergeWithExistingConstraint too if you change this code.
 */
static void
MergeConstraintsIntoExisting(Relation child_rel, Relation parent_rel)
{
	Relation	catalog_relation;
	TupleDesc	tuple_desc;
	SysScanDesc parent_scan;
	ScanKeyData parent_key;
	HeapTuple	parent_tuple;
	bool		child_is_partition = false;

	catalog_relation = table_open(ConstraintRelationId, RowExclusiveLock);
	tuple_desc = RelationGetDescr(catalog_relation);

	/* If parent_rel is a partitioned table, child_rel must be a partition */
	if (parent_rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		child_is_partition = true;

	/* Outer loop scans through the parent's constraint definitions */
	ScanKeyInit(&parent_key,
				Anum_pg_constraint_conrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(parent_rel)));
	parent_scan = systable_beginscan(catalog_relation, ConstraintRelidTypidNameIndexId,
									 true, NULL, 1, &parent_key);

	while (HeapTupleIsValid(parent_tuple = systable_getnext(parent_scan)))
	{
		Form_pg_constraint parent_con = (Form_pg_constraint) GETSTRUCT(parent_tuple);
		SysScanDesc child_scan;
		ScanKeyData child_key;
		HeapTuple	child_tuple;
		bool		found = false;

		if (parent_con->contype != CONSTRAINT_CHECK)
			continue;

		/* if the parent's constraint is marked NO INHERIT, it's not inherited */
		if (parent_con->connoinherit)
			continue;

		/* Search for a child constraint matching this one */
		ScanKeyInit(&child_key,
					Anum_pg_constraint_conrelid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(RelationGetRelid(child_rel)));
		child_scan = systable_beginscan(catalog_relation, ConstraintRelidTypidNameIndexId,
										true, NULL, 1, &child_key);

		while (HeapTupleIsValid(child_tuple = systable_getnext(child_scan)))
		{
			Form_pg_constraint child_con = (Form_pg_constraint) GETSTRUCT(child_tuple);
			HeapTuple	child_copy;

			if (child_con->contype != CONSTRAINT_CHECK)
				continue;

			if (strcmp(NameStr(parent_con->conname),
					   NameStr(child_con->conname)) != 0)
				continue;

			if (!constraints_equivalent(parent_tuple, child_tuple, tuple_desc))
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("child table \"%s\" has different definition for check constraint \"%s\"",
								RelationGetRelationName(child_rel),
								NameStr(parent_con->conname))));

			/* If the child constraint is "no inherit" then cannot merge */
			if (child_con->connoinherit)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("constraint \"%s\" conflicts with non-inherited constraint on child table \"%s\"",
								NameStr(child_con->conname),
								RelationGetRelationName(child_rel))));

			/*
			 * If the child constraint is "not valid" then cannot merge with a
			 * valid parent constraint
			 */
			if (parent_con->convalidated && !child_con->convalidated)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("constraint \"%s\" conflicts with NOT VALID constraint on child table \"%s\"",
								NameStr(child_con->conname),
								RelationGetRelationName(child_rel))));

			/*
			 * OK, bump the child constraint's inheritance count.  (If we fail
			 * later on, this change will just roll back.)
			 */
			child_copy = heap_copytuple(child_tuple);
			child_con = (Form_pg_constraint) GETSTRUCT(child_copy);
			child_con->coninhcount++;

			/*
			 * In case of partitions, an inherited constraint must be
			 * inherited only once since it cannot have multiple parents and
			 * it is never considered local.
			 */
			if (child_is_partition)
			{
				Assert(child_con->coninhcount == 1);
				child_con->conislocal = false;
			}

			CatalogTupleUpdate(catalog_relation, &child_copy->t_self, child_copy);
			heap_freetuple(child_copy);

			found = true;
			break;
		}

		systable_endscan(child_scan);

		if (!found)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("child table is missing constraint \"%s\"",
							NameStr(parent_con->conname))));
	}

	systable_endscan(parent_scan);
	table_close(catalog_relation, RowExclusiveLock);
}

/*
 * ALTER TABLE NO INHERIT
 *
 * Return value is the address of the relation that is no longer parent.
 */
static ObjectAddress
ATExecDropInherit(Relation rel, RangeVar *parent, LOCKMODE lockmode)
{
	ObjectAddress address;
	Relation	parent_rel;

	if (rel->rd_rel->relispartition)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot change inheritance of a partition")));

	/*
	 * AccessShareLock on the parent is probably enough, seeing that DROP
	 * TABLE doesn't lock parent tables at all.  We need some lock since we'll
	 * be inspecting the parent's schema.
	 */
	parent_rel = table_openrv(parent, AccessShareLock);

	/*
	 * We don't bother to check ownership of the parent table --- ownership of
	 * the child is presumed enough rights.
	 */

	/* Off to RemoveInheritance() where most of the work happens */
	RemoveInheritance(rel, parent_rel, false);

	ObjectAddressSet(address, RelationRelationId,
					 RelationGetRelid(parent_rel));

	/* keep our lock on the parent relation until commit */
	table_close(parent_rel, NoLock);

	return address;
}

/*
 * MarkInheritDetached
 *
 * Set inhdetachpending for a partition, for ATExecDetachPartition
 * in concurrent mode.  While at it, verify that no other partition is
 * already pending detach.
 */
static void
MarkInheritDetached(Relation child_rel, Relation parent_rel)
{
	Relation	catalogRelation;
	SysScanDesc scan;
	ScanKeyData key;
	HeapTuple	inheritsTuple;
	bool		found = false;

	Assert(parent_rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE);

	/*
	 * Find pg_inherits entries by inhparent.  (We need to scan them all in
	 * order to verify that no other partition is pending detach.)
	 */
	catalogRelation = table_open(InheritsRelationId, RowExclusiveLock);
	ScanKeyInit(&key,
				Anum_pg_inherits_inhparent,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(parent_rel)));
	scan = systable_beginscan(catalogRelation, InheritsParentIndexId,
							  true, NULL, 1, &key);

	while (HeapTupleIsValid(inheritsTuple = systable_getnext(scan)))
	{
		Form_pg_inherits inhForm;

		inhForm = (Form_pg_inherits) GETSTRUCT(inheritsTuple);
		if (inhForm->inhdetachpending)
			ereport(ERROR,
					errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					errmsg("partition \"%s\" already pending detach in partitioned table \"%s.%s\"",
						   get_rel_name(inhForm->inhrelid),
						   get_namespace_name(parent_rel->rd_rel->relnamespace),
						   RelationGetRelationName(parent_rel)),
					errhint("Use ALTER TABLE ... DETACH PARTITION ... FINALIZE to complete the pending detach operation."));

		if (inhForm->inhrelid == RelationGetRelid(child_rel))
		{
			HeapTuple	newtup;

			newtup = heap_copytuple(inheritsTuple);
			((Form_pg_inherits) GETSTRUCT(newtup))->inhdetachpending = true;

			CatalogTupleUpdate(catalogRelation,
							   &inheritsTuple->t_self,
							   newtup);
			found = true;
			heap_freetuple(newtup);
			/* keep looking, to ensure we catch others pending detach */
		}
	}

	/* Done */
	systable_endscan(scan);
	table_close(catalogRelation, RowExclusiveLock);

	if (!found)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_TABLE),
				 errmsg("relation \"%s\" is not a partition of relation \"%s\"",
						RelationGetRelationName(child_rel),
						RelationGetRelationName(parent_rel))));
}

/*
 * RemoveInheritance
 *
 * Drop a parent from the child's parents. This just adjusts the attinhcount
 * and attislocal of the columns and removes the pg_inherit and pg_depend
 * entries.  expect_detached is passed down to DeleteInheritsTuple, q.v..
 *
 * If attinhcount goes to 0 then attislocal gets set to true. If it goes back
 * up attislocal stays true, which means if a child is ever removed from a
 * parent then its columns will never be automatically dropped which may
 * surprise. But at least we'll never surprise by dropping columns someone
 * isn't expecting to be dropped which would actually mean data loss.
 *
 * coninhcount and conislocal for inherited constraints are adjusted in
 * exactly the same way.
 *
 * Common to ATExecDropInherit() and ATExecDetachPartition().
 */
static void
RemoveInheritance(Relation child_rel, Relation parent_rel, bool expect_detached)
{
	Relation	catalogRelation;
	SysScanDesc scan;
	ScanKeyData key[3];
	HeapTuple	attributeTuple,
				constraintTuple;
	List	   *connames;
	bool		found;
	bool		child_is_partition = false;

	/* If parent_rel is a partitioned table, child_rel must be a partition */
	if (parent_rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		child_is_partition = true;

	found = DeleteInheritsTuple(RelationGetRelid(child_rel),
								RelationGetRelid(parent_rel),
								expect_detached,
								RelationGetRelationName(child_rel));
	if (!found)
	{
		if (child_is_partition)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_TABLE),
					 errmsg("relation \"%s\" is not a partition of relation \"%s\"",
							RelationGetRelationName(child_rel),
							RelationGetRelationName(parent_rel))));
		else
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_TABLE),
					 errmsg("relation \"%s\" is not a parent of relation \"%s\"",
							RelationGetRelationName(parent_rel),
							RelationGetRelationName(child_rel))));
	}

	/*
	 * Search through child columns looking for ones matching parent rel
	 */
	catalogRelation = table_open(AttributeRelationId, RowExclusiveLock);
	ScanKeyInit(&key[0],
				Anum_pg_attribute_attrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(child_rel)));
	scan = systable_beginscan(catalogRelation, AttributeRelidNumIndexId,
							  true, NULL, 1, key);
	while (HeapTupleIsValid(attributeTuple = systable_getnext(scan)))
	{
		Form_pg_attribute att = (Form_pg_attribute) GETSTRUCT(attributeTuple);

		/* Ignore if dropped or not inherited */
		if (att->attisdropped)
			continue;
		if (att->attinhcount <= 0)
			continue;

		if (SearchSysCacheExistsAttName(RelationGetRelid(parent_rel),
										NameStr(att->attname)))
		{
			/* Decrement inhcount and possibly set islocal to true */
			HeapTuple	copyTuple = heap_copytuple(attributeTuple);
			Form_pg_attribute copy_att = (Form_pg_attribute) GETSTRUCT(copyTuple);

			copy_att->attinhcount--;
			if (copy_att->attinhcount == 0)
				copy_att->attislocal = true;

			CatalogTupleUpdate(catalogRelation, &copyTuple->t_self, copyTuple);
			heap_freetuple(copyTuple);
		}
	}
	systable_endscan(scan);
	table_close(catalogRelation, RowExclusiveLock);

	/*
	 * Likewise, find inherited check constraints and disinherit them. To do
	 * this, we first need a list of the names of the parent's check
	 * constraints.  (We cheat a bit by only checking for name matches,
	 * assuming that the expressions will match.)
	 */
	catalogRelation = table_open(ConstraintRelationId, RowExclusiveLock);
	ScanKeyInit(&key[0],
				Anum_pg_constraint_conrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(parent_rel)));
	scan = systable_beginscan(catalogRelation, ConstraintRelidTypidNameIndexId,
							  true, NULL, 1, key);

	connames = NIL;

	while (HeapTupleIsValid(constraintTuple = systable_getnext(scan)))
	{
		Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(constraintTuple);

		if (con->contype == CONSTRAINT_CHECK)
			connames = lappend(connames, pstrdup(NameStr(con->conname)));
	}

	systable_endscan(scan);

	/* Now scan the child's constraints */
	ScanKeyInit(&key[0],
				Anum_pg_constraint_conrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(child_rel)));
	scan = systable_beginscan(catalogRelation, ConstraintRelidTypidNameIndexId,
							  true, NULL, 1, key);

	while (HeapTupleIsValid(constraintTuple = systable_getnext(scan)))
	{
		Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(constraintTuple);
		bool		match;
		ListCell   *lc;

		if (con->contype != CONSTRAINT_CHECK)
			continue;

		match = false;
		foreach(lc, connames)
		{
			if (strcmp(NameStr(con->conname), (char *) lfirst(lc)) == 0)
			{
				match = true;
				break;
			}
		}

		if (match)
		{
			/* Decrement inhcount and possibly set islocal to true */
			HeapTuple	copyTuple = heap_copytuple(constraintTuple);
			Form_pg_constraint copy_con = (Form_pg_constraint) GETSTRUCT(copyTuple);

			if (copy_con->coninhcount <= 0) /* shouldn't happen */
				elog(ERROR, "relation %u has non-inherited constraint \"%s\"",
					 RelationGetRelid(child_rel), NameStr(copy_con->conname));

			copy_con->coninhcount--;
			if (copy_con->coninhcount == 0)
				copy_con->conislocal = true;

			CatalogTupleUpdate(catalogRelation, &copyTuple->t_self, copyTuple);
			heap_freetuple(copyTuple);
		}
	}

	systable_endscan(scan);
	table_close(catalogRelation, RowExclusiveLock);

	drop_parent_dependency(RelationGetRelid(child_rel),
						   RelationRelationId,
						   RelationGetRelid(parent_rel),
						   child_dependency_type(child_is_partition));

	/*
	 * Post alter hook of this inherits. Since object_access_hook doesn't take
	 * multiple object identifiers, we relay oid of parent relation using
	 * auxiliary_id argument.
	 */
	InvokeObjectPostAlterHookArg(InheritsRelationId,
								 RelationGetRelid(child_rel), 0,
								 RelationGetRelid(parent_rel), false);
}

/*
 * Drop the dependency created by StoreCatalogInheritance1 (CREATE TABLE
 * INHERITS/ALTER TABLE INHERIT -- refclassid will be RelationRelationId) or
 * heap_create_with_catalog (CREATE TABLE OF/ALTER TABLE OF -- refclassid will
 * be TypeRelationId).  There's no convenient way to do this, so go trawling
 * through pg_depend.
 */
static void
drop_parent_dependency(Oid relid, Oid refclassid, Oid refobjid,
					   DependencyType deptype)
{
	Relation	catalogRelation;
	SysScanDesc scan;
	ScanKeyData key[3];
	HeapTuple	depTuple;

	catalogRelation = table_open(DependRelationId, RowExclusiveLock);

	ScanKeyInit(&key[0],
				Anum_pg_depend_classid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_objid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relid));
	ScanKeyInit(&key[2],
				Anum_pg_depend_objsubid,
				BTEqualStrategyNumber, F_INT4EQ,
				Int32GetDatum(0));

	scan = systable_beginscan(catalogRelation, DependDependerIndexId, true,
							  NULL, 3, key);

	while (HeapTupleIsValid(depTuple = systable_getnext(scan)))
	{
		Form_pg_depend dep = (Form_pg_depend) GETSTRUCT(depTuple);

		if (dep->refclassid == refclassid &&
			dep->refobjid == refobjid &&
			dep->refobjsubid == 0 &&
			dep->deptype == deptype)
			CatalogTupleDelete(catalogRelation, depTuple);
	}

	systable_endscan(scan);
	table_close(catalogRelation, RowExclusiveLock);
}

/*
 * ALTER TABLE OF
 *
 * Attach a table to a composite type, as though it had been created with CREATE
 * TABLE OF.  All attname, atttypid, atttypmod and attcollation must match.  The
 * subject table must not have inheritance parents.  These restrictions ensure
 * that you cannot create a configuration impossible with CREATE TABLE OF alone.
 *
 * The address of the type is returned.
 */
static ObjectAddress
ATExecAddOf(Relation rel, const TypeName *ofTypename, LOCKMODE lockmode)
{
	Oid			relid = RelationGetRelid(rel);
	Type		typetuple;
	Form_pg_type typeform;
	Oid			typeid;
	Relation	inheritsRelation,
				relationRelation;
	SysScanDesc scan;
	ScanKeyData key;
	AttrNumber	table_attno,
				type_attno;
	TupleDesc	typeTupleDesc,
				tableTupleDesc;
	ObjectAddress tableobj,
				typeobj;
	HeapTuple	classtuple;

	/* Validate the type. */
	typetuple = typenameType(NULL, ofTypename, NULL);
	check_of_type(typetuple);
	typeform = (Form_pg_type) GETSTRUCT(typetuple);
	typeid = typeform->oid;

	/* Fail if the table has any inheritance parents. */
	inheritsRelation = table_open(InheritsRelationId, AccessShareLock);
	ScanKeyInit(&key,
				Anum_pg_inherits_inhrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relid));
	scan = systable_beginscan(inheritsRelation, InheritsRelidSeqnoIndexId,
							  true, NULL, 1, &key);
	if (HeapTupleIsValid(systable_getnext(scan)))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("typed tables cannot inherit")));
	systable_endscan(scan);
	table_close(inheritsRelation, AccessShareLock);

	/*
	 * Check the tuple descriptors for compatibility.  Unlike inheritance, we
	 * require that the order also match.  However, attnotnull need not match.
	 */
	typeTupleDesc = lookup_rowtype_tupdesc(typeid, -1);
	tableTupleDesc = RelationGetDescr(rel);
	table_attno = 1;
	for (type_attno = 1; type_attno <= typeTupleDesc->natts; type_attno++)
	{
		Form_pg_attribute type_attr,
					table_attr;
		const char *type_attname,
				   *table_attname;

		/* Get the next non-dropped type attribute. */
		type_attr = TupleDescAttr(typeTupleDesc, type_attno - 1);
		if (type_attr->attisdropped)
			continue;
		type_attname = NameStr(type_attr->attname);

		/* Get the next non-dropped table attribute. */
		do
		{
			if (table_attno > tableTupleDesc->natts)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("table is missing column \"%s\"",
								type_attname)));
			table_attr = TupleDescAttr(tableTupleDesc, table_attno - 1);
			table_attno++;
		} while (table_attr->attisdropped);
		table_attname = NameStr(table_attr->attname);

		/* Compare name. */
		if (strncmp(table_attname, type_attname, NAMEDATALEN) != 0)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("table has column \"%s\" where type requires \"%s\"",
							table_attname, type_attname)));

		/* Compare type. */
		if (table_attr->atttypid != type_attr->atttypid ||
			table_attr->atttypmod != type_attr->atttypmod ||
			table_attr->attcollation != type_attr->attcollation)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("table \"%s\" has different type for column \"%s\"",
							RelationGetRelationName(rel), type_attname)));
	}
	ReleaseTupleDesc(typeTupleDesc);

	/* Any remaining columns at the end of the table had better be dropped. */
	for (; table_attno <= tableTupleDesc->natts; table_attno++)
	{
		Form_pg_attribute table_attr = TupleDescAttr(tableTupleDesc,
													 table_attno - 1);

		if (!table_attr->attisdropped)
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("table has extra column \"%s\"",
							NameStr(table_attr->attname))));
	}

	/* If the table was already typed, drop the existing dependency. */
	if (rel->rd_rel->reloftype)
		drop_parent_dependency(relid, TypeRelationId, rel->rd_rel->reloftype,
							   DEPENDENCY_NORMAL);

	/* Record a dependency on the new type. */
	tableobj.classId = RelationRelationId;
	tableobj.objectId = relid;
	tableobj.objectSubId = 0;
	typeobj.classId = TypeRelationId;
	typeobj.objectId = typeid;
	typeobj.objectSubId = 0;
	recordDependencyOn(&tableobj, &typeobj, DEPENDENCY_NORMAL);

	/* Update pg_class.reloftype */
	relationRelation = table_open(RelationRelationId, RowExclusiveLock);
	classtuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(classtuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	((Form_pg_class) GETSTRUCT(classtuple))->reloftype = typeid;
	CatalogTupleUpdate(relationRelation, &classtuple->t_self, classtuple);

	InvokeObjectPostAlterHook(RelationRelationId, relid, 0);

	heap_freetuple(classtuple);
	table_close(relationRelation, RowExclusiveLock);

	ReleaseSysCache(typetuple);

	return typeobj;
}

/*
 * ALTER TABLE NOT OF
 *
 * Detach a typed table from its originating type.  Just clear reloftype and
 * remove the dependency.
 */
static void
ATExecDropOf(Relation rel, LOCKMODE lockmode)
{
	Oid			relid = RelationGetRelid(rel);
	Relation	relationRelation;
	HeapTuple	tuple;

	if (!OidIsValid(rel->rd_rel->reloftype))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a typed table",
						RelationGetRelationName(rel))));

	/*
	 * We don't bother to check ownership of the type --- ownership of the
	 * table is presumed enough rights.  No lock required on the type, either.
	 */

	drop_parent_dependency(relid, TypeRelationId, rel->rd_rel->reloftype,
						   DEPENDENCY_NORMAL);

	/* Clear pg_class.reloftype */
	relationRelation = table_open(RelationRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	((Form_pg_class) GETSTRUCT(tuple))->reloftype = InvalidOid;
	CatalogTupleUpdate(relationRelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId, relid, 0);

	heap_freetuple(tuple);
	table_close(relationRelation, RowExclusiveLock);
}

/*
 * relation_mark_replica_identity: Update a table's replica identity
 *
 * Iff ri_type = REPLICA_IDENTITY_INDEX, indexOid must be the Oid of a suitable
 * index. Otherwise, it must be InvalidOid.
 *
 * Caller had better hold an exclusive lock on the relation, as the results
 * of running two of these concurrently wouldn't be pretty.
 */
static void
relation_mark_replica_identity(Relation rel, char ri_type, Oid indexOid,
							   bool is_internal)
{
	Relation	pg_index;
	Relation	pg_class;
	HeapTuple	pg_class_tuple;
	HeapTuple	pg_index_tuple;
	Form_pg_class pg_class_form;
	Form_pg_index pg_index_form;
	ListCell   *index;

	/*
	 * Check whether relreplident has changed, and update it if so.
	 */
	pg_class = table_open(RelationRelationId, RowExclusiveLock);
	pg_class_tuple = SearchSysCacheCopy1(RELOID,
										 ObjectIdGetDatum(RelationGetRelid(rel)));
	if (!HeapTupleIsValid(pg_class_tuple))
		elog(ERROR, "cache lookup failed for relation \"%s\"",
			 RelationGetRelationName(rel));
	pg_class_form = (Form_pg_class) GETSTRUCT(pg_class_tuple);
	if (pg_class_form->relreplident != ri_type)
	{
		pg_class_form->relreplident = ri_type;
		CatalogTupleUpdate(pg_class, &pg_class_tuple->t_self, pg_class_tuple);
	}
	table_close(pg_class, RowExclusiveLock);
	heap_freetuple(pg_class_tuple);

	/*
	 * Update the per-index indisreplident flags correctly.
	 */
	pg_index = table_open(IndexRelationId, RowExclusiveLock);
	foreach(index, RelationGetIndexList(rel))
	{
		Oid			thisIndexOid = lfirst_oid(index);
		bool		dirty = false;

		pg_index_tuple = SearchSysCacheCopy1(INDEXRELID,
											 ObjectIdGetDatum(thisIndexOid));
		if (!HeapTupleIsValid(pg_index_tuple))
			elog(ERROR, "cache lookup failed for index %u", thisIndexOid);
		pg_index_form = (Form_pg_index) GETSTRUCT(pg_index_tuple);

		if (thisIndexOid == indexOid)
		{
			/* Set the bit if not already set. */
			if (!pg_index_form->indisreplident)
			{
				dirty = true;
				pg_index_form->indisreplident = true;
			}
		}
		else
		{
			/* Unset the bit if set. */
			if (pg_index_form->indisreplident)
			{
				dirty = true;
				pg_index_form->indisreplident = false;
			}
		}

		if (dirty)
		{
			CatalogTupleUpdate(pg_index, &pg_index_tuple->t_self, pg_index_tuple);
			InvokeObjectPostAlterHookArg(IndexRelationId, thisIndexOid, 0,
										 InvalidOid, is_internal);

			/*
			 * Invalidate the relcache for the table, so that after we commit
			 * all sessions will refresh the table's replica identity index
			 * before attempting any UPDATE or DELETE on the table.  (If we
			 * changed the table's pg_class row above, then a relcache inval
			 * is already queued due to that; but we might not have.)
			 */
			CacheInvalidateRelcache(rel);
		}
		heap_freetuple(pg_index_tuple);
	}

	table_close(pg_index, RowExclusiveLock);
}

/*
 * ALTER TABLE <name> REPLICA IDENTITY ...
 */
static void
ATExecReplicaIdentity(Relation rel, ReplicaIdentityStmt *stmt, LOCKMODE lockmode)
{
	Oid			indexOid;
	Relation	indexRel;
	int			key;

	if (stmt->identity_type == REPLICA_IDENTITY_DEFAULT)
	{
		relation_mark_replica_identity(rel, stmt->identity_type, InvalidOid, true);
		return;
	}
	else if (stmt->identity_type == REPLICA_IDENTITY_FULL)
	{
		relation_mark_replica_identity(rel, stmt->identity_type, InvalidOid, true);
		return;
	}
	else if (stmt->identity_type == REPLICA_IDENTITY_NOTHING)
	{
		relation_mark_replica_identity(rel, stmt->identity_type, InvalidOid, true);
		return;
	}
	else if (IsYugaByteEnabled() && stmt->identity_type == YB_REPLICA_IDENTITY_CHANGE)
	{
		relation_mark_replica_identity(rel, stmt->identity_type, InvalidOid, true);
		return;
	}
	else if (stmt->identity_type == REPLICA_IDENTITY_INDEX)
	{
		 /* fallthrough */ ;
	}
	else
		elog(ERROR, "unexpected identity type %u", stmt->identity_type);

	/* Check that the index exists */
	indexOid = get_relname_relid(stmt->name, rel->rd_rel->relnamespace);
	if (!OidIsValid(indexOid))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("index \"%s\" for table \"%s\" does not exist",
						stmt->name, RelationGetRelationName(rel))));

	indexRel = index_open(indexOid, ShareLock);

	/* Check that the index is on the relation we're altering. */
	if (indexRel->rd_index == NULL ||
		indexRel->rd_index->indrelid != RelationGetRelid(rel))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not an index for table \"%s\"",
						RelationGetRelationName(indexRel),
						RelationGetRelationName(rel))));
	/* The AM must support uniqueness, and the index must in fact be unique. */
	if (!indexRel->rd_indam->amcanunique ||
		!indexRel->rd_index->indisunique)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot use non-unique index \"%s\" as replica identity",
						RelationGetRelationName(indexRel))));
	/* Deferred indexes are not guaranteed to be always unique. */
	if (!indexRel->rd_index->indimmediate)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use non-immediate index \"%s\" as replica identity",
						RelationGetRelationName(indexRel))));
	/* Expression indexes aren't supported. */
	if (RelationGetIndexExpressions(indexRel) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use expression index \"%s\" as replica identity",
						RelationGetRelationName(indexRel))));
	/* Predicate indexes aren't supported. */
	if (RelationGetIndexPredicate(indexRel) != NIL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use partial index \"%s\" as replica identity",
						RelationGetRelationName(indexRel))));

	/* Check index for nullable columns. */
	for (key = 0; key < IndexRelationGetNumberOfKeyAttributes(indexRel); key++)
	{
		int16		attno = indexRel->rd_index->indkey.values[key];
		Form_pg_attribute attr;

		/*
		 * Reject any other system columns.  (Going forward, we'll disallow
		 * indexes containing such columns in the first place, but they might
		 * exist in older branches.)
		 */
		if (attno <= 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_COLUMN_REFERENCE),
					 errmsg("index \"%s\" cannot be used as replica identity because column %d is a system column",
							RelationGetRelationName(indexRel), attno)));

		attr = TupleDescAttr(rel->rd_att, attno - 1);
		if (!attr->attnotnull)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("index \"%s\" cannot be used as replica identity because column \"%s\" is nullable",
							RelationGetRelationName(indexRel),
							NameStr(attr->attname))));
	}

	/* This index is suitable for use as a replica identity. Mark it. */
	relation_mark_replica_identity(rel, stmt->identity_type, indexOid, true);

	index_close(indexRel, NoLock);
}

/*
 * ALTER TABLE ENABLE/DISABLE ROW LEVEL SECURITY
 */
static void
ATExecSetRowSecurity(Relation rel, bool rls)
{
	Relation	pg_class;
	Oid			relid;
	HeapTuple	tuple;

	relid = RelationGetRelid(rel);

	/* Pull the record for this relation and update it */
	pg_class = table_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);

	((Form_pg_class) GETSTRUCT(tuple))->relrowsecurity = rls;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	table_close(pg_class, RowExclusiveLock);
	heap_freetuple(tuple);
}

/*
 * ALTER TABLE FORCE/NO FORCE ROW LEVEL SECURITY
 */
static void
ATExecForceNoForceRowSecurity(Relation rel, bool force_rls)
{
	Relation	pg_class;
	Oid			relid;
	HeapTuple	tuple;

	relid = RelationGetRelid(rel);

	pg_class = table_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);

	((Form_pg_class) GETSTRUCT(tuple))->relforcerowsecurity = force_rls;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	table_close(pg_class, RowExclusiveLock);
	heap_freetuple(tuple);
}

/*
 * ALTER FOREIGN TABLE <name> OPTIONS (...)
 */
static void
ATExecGenericOptions(Relation rel, List *options)
{
	Relation	ftrel;
	ForeignServer *server;
	ForeignDataWrapper *fdw;
	HeapTuple	tuple;
	bool		isnull;
	Datum		repl_val[Natts_pg_foreign_table];
	bool		repl_null[Natts_pg_foreign_table];
	bool		repl_repl[Natts_pg_foreign_table];
	Datum		datum;
	Form_pg_foreign_table tableform;

	if (options == NIL)
		return;

	ftrel = table_open(ForeignTableRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(FOREIGNTABLEREL, rel->rd_id);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("foreign table \"%s\" does not exist",
						RelationGetRelationName(rel))));
	tableform = (Form_pg_foreign_table) GETSTRUCT(tuple);
	server = GetForeignServer(tableform->ftserver);
	fdw = GetForeignDataWrapper(server->fdwid);

	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	/* Extract the current options */
	datum = SysCacheGetAttr(FOREIGNTABLEREL,
							tuple,
							Anum_pg_foreign_table_ftoptions,
							&isnull);
	if (isnull)
		datum = PointerGetDatum(NULL);

	/* Transform the options */
	datum = transformGenericOptions(ForeignTableRelationId,
									datum,
									options,
									fdw->fdwvalidator);

	if (PointerIsValid(DatumGetPointer(datum)))
		repl_val[Anum_pg_foreign_table_ftoptions - 1] = datum;
	else
		repl_null[Anum_pg_foreign_table_ftoptions - 1] = true;

	repl_repl[Anum_pg_foreign_table_ftoptions - 1] = true;

	/* Everything looks good - update the tuple */

	tuple = heap_modify_tuple(tuple, RelationGetDescr(ftrel),
							  repl_val, repl_null, repl_repl);

	CatalogTupleUpdate(ftrel, &tuple->t_self, tuple);

	/*
	 * Invalidate relcache so that all sessions will refresh any cached plans
	 * that might depend on the old options.
	 */
	CacheInvalidateRelcache(rel);

	InvokeObjectPostAlterHook(ForeignTableRelationId,
							  RelationGetRelid(rel), 0);

	table_close(ftrel, RowExclusiveLock);

	heap_freetuple(tuple);
}

/*
 * ALTER TABLE ALTER COLUMN SET COMPRESSION
 *
 * Return value is the address of the modified column
 */
static ObjectAddress
ATExecSetCompression(AlteredTableInfo *tab,
					 Relation rel,
					 const char *column,
					 Node *newValue,
					 LOCKMODE lockmode)
{
	Relation	attrel;
	HeapTuple	tuple;
	Form_pg_attribute atttableform;
	AttrNumber	attnum;
	char	   *compression;
	char		cmethod;
	ObjectAddress address;

	Assert(IsA(newValue, String));
	compression = strVal(newValue);

	attrel = table_open(AttributeRelationId, RowExclusiveLock);

	/* copy the cache entry so we can scribble on it below */
	tuple = SearchSysCacheCopyAttName(RelationGetRelid(rel), column);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_COLUMN),
				 errmsg("column \"%s\" of relation \"%s\" does not exist",
						column, RelationGetRelationName(rel))));

	/* prevent them from altering a system attribute */
	atttableform = (Form_pg_attribute) GETSTRUCT(tuple);
	attnum = atttableform->attnum;
	if (attnum <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot alter system column \"%s\"", column)));

	/*
	 * Check that column type is compressible, then get the attribute
	 * compression method code
	 */
	cmethod = GetAttributeCompression(atttableform->atttypid, compression);

	/* update pg_attribute entry */
	atttableform->attcompression = cmethod;
	CatalogTupleUpdate(attrel, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel),
							  attnum);

	/*
	 * Apply the change to indexes as well (only for simple index columns,
	 * matching behavior of index.c ConstructTupleDescriptor()).
	 */
	SetIndexStorageProperties(rel, attrel, attnum,
							  false, 0,
							  true, cmethod,
							  lockmode);

	heap_freetuple(tuple);

	table_close(attrel, RowExclusiveLock);

	/* make changes visible */
	CommandCounterIncrement();

	ObjectAddressSubSet(address, RelationRelationId,
						RelationGetRelid(rel), attnum);
	return address;
}


/*
 * Preparation phase for SET LOGGED/UNLOGGED
 *
 * This verifies that we're not trying to change a temp table.  Also,
 * existing foreign key constraints are checked to avoid ending up with
 * permanent tables referencing unlogged tables.
 *
 * Return value is false if the operation is a no-op (in which case the
 * checks are skipped), otherwise true.
 */
static bool
ATPrepChangePersistence(Relation rel, bool toLogged)
{
	Relation	pg_constraint;
	HeapTuple	tuple;
	SysScanDesc scan;
	ScanKeyData skey[1];

	/*
	 * Disallow changing status for a temp table.  Also verify whether we can
	 * get away with doing nothing; in such cases we don't need to run the
	 * checks below, either.
	 */
	switch (rel->rd_rel->relpersistence)
	{
		case RELPERSISTENCE_TEMP:
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot change logged status of table \"%s\" because it is temporary",
							RelationGetRelationName(rel)),
					 errtable(rel)));
			break;
		case RELPERSISTENCE_PERMANENT:
			if (toLogged)
				/* nothing to do */
				return false;
			break;
		case RELPERSISTENCE_UNLOGGED:
			if (!toLogged)
				/* nothing to do */
				return false;
			break;
	}

	/*
	 * Check that the table is not part any publication when changing to
	 * UNLOGGED as UNLOGGED tables can't be published.
	 */
	if (!toLogged &&
		list_length(GetRelationPublications(RelationGetRelid(rel))) > 0)
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot change table \"%s\" to unlogged because it is part of a publication",
						RelationGetRelationName(rel)),
				 errdetail("Unlogged relations cannot be replicated.")));

	/*
	 * Check existing foreign key constraints to preserve the invariant that
	 * permanent tables cannot reference unlogged ones.  Self-referencing
	 * foreign keys can safely be ignored.
	 */
	pg_constraint = table_open(ConstraintRelationId, AccessShareLock);

	/*
	 * Scan conrelid if changing to permanent, else confrelid.  This also
	 * determines whether a useful index exists.
	 */
	ScanKeyInit(&skey[0],
				toLogged ? Anum_pg_constraint_conrelid :
				Anum_pg_constraint_confrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(rel)));
	scan = systable_beginscan(pg_constraint,
							  toLogged ? ConstraintRelidTypidNameIndexId : InvalidOid,
							  true, NULL, 1, skey);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_constraint con = (Form_pg_constraint) GETSTRUCT(tuple);

		if (con->contype == CONSTRAINT_FOREIGN)
		{
			Oid			foreignrelid;
			Relation	foreignrel;

			/* the opposite end of what we used as scankey */
			foreignrelid = toLogged ? con->confrelid : con->conrelid;

			/* ignore if self-referencing */
			if (RelationGetRelid(rel) == foreignrelid)
				continue;

			foreignrel = relation_open(foreignrelid, AccessShareLock);

			if (toLogged)
			{
				if (!RelationIsPermanent(foreignrel))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("could not change table \"%s\" to logged because it references unlogged table \"%s\"",
									RelationGetRelationName(rel),
									RelationGetRelationName(foreignrel)),
							 errtableconstraint(rel, NameStr(con->conname))));
			}
			else
			{
				if (RelationIsPermanent(foreignrel))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("could not change table \"%s\" to unlogged because it references logged table \"%s\"",
									RelationGetRelationName(rel),
									RelationGetRelationName(foreignrel)),
							 errtableconstraint(rel, NameStr(con->conname))));
			}

			relation_close(foreignrel, AccessShareLock);
		}
	}

	systable_endscan(scan);

	table_close(pg_constraint, AccessShareLock);

	return true;
}

/*
 * Execute ALTER TABLE SET SCHEMA
 */
ObjectAddress
AlterTableNamespace(AlterObjectSchemaStmt *stmt, Oid *oldschema)
{
	Relation	rel;
	Oid			relid;
	Oid			oldNspOid;
	Oid			nspOid;
	RangeVar   *newrv;
	ObjectAddresses *objsMoved;
	ObjectAddress myself;

	relid = RangeVarGetRelidExtended(stmt->relation, AccessExclusiveLock,
									 stmt->missing_ok ? RVR_MISSING_OK : 0,
									 RangeVarCallbackForAlterRelation,
									 (void *) stmt);

	if (!OidIsValid(relid))
	{
		ereport(NOTICE,
				(errmsg("relation \"%s\" does not exist, skipping",
						stmt->relation->relname)));
		return InvalidObjectAddress;
	}

	rel = relation_open(relid, NoLock);

	oldNspOid = RelationGetNamespace(rel);

	/* If it's an owned sequence, disallow moving it by itself. */
	if (rel->rd_rel->relkind == RELKIND_SEQUENCE)
	{
		Oid			tableId;
		int32		colId;

		if (sequenceIsOwned(relid, DEPENDENCY_AUTO, &tableId, &colId) ||
			sequenceIsOwned(relid, DEPENDENCY_INTERNAL, &tableId, &colId))
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("cannot move an owned sequence into another schema"),
					 errdetail("Sequence \"%s\" is linked to table \"%s\".",
							   RelationGetRelationName(rel),
							   get_rel_name(tableId))));
	}

	/* Get and lock schema OID and check its permissions. */
	newrv = makeRangeVar(stmt->newschema, RelationGetRelationName(rel), -1);
	nspOid = RangeVarGetAndCheckCreationNamespace(newrv, NoLock, NULL);

	/* common checks on switching namespaces */
	CheckSetNamespace(oldNspOid, nspOid);

	objsMoved = new_object_addresses();
	AlterTableNamespaceInternal(rel, oldNspOid, nspOid, objsMoved);
	free_object_addresses(objsMoved);

	ObjectAddressSet(myself, RelationRelationId, relid);

	if (oldschema)
		*oldschema = oldNspOid;

	/* close rel, but keep lock until commit */
	relation_close(rel, NoLock);

	return myself;
}

/*
 * The guts of relocating a table or materialized view to another namespace:
 * besides moving the relation itself, its dependent objects are relocated to
 * the new schema.
 */
void
AlterTableNamespaceInternal(Relation rel, Oid oldNspOid, Oid nspOid,
							ObjectAddresses *objsMoved)
{
	Relation	classRel;

	Assert(objsMoved != NULL);

	/* OK, modify the pg_class row and pg_depend entry */
	classRel = table_open(RelationRelationId, RowExclusiveLock);

	AlterRelationNamespaceInternal(classRel, RelationGetRelid(rel), oldNspOid,
								   nspOid, true, objsMoved);

	/* Fix the table's row type too, if it has one */
	if (OidIsValid(rel->rd_rel->reltype))
		AlterTypeNamespaceInternal(rel->rd_rel->reltype,
								   nspOid, false, false, objsMoved);

	/* Fix other dependent stuff */
	if (rel->rd_rel->relkind == RELKIND_RELATION ||
		rel->rd_rel->relkind == RELKIND_MATVIEW ||
		rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		AlterIndexNamespaces(classRel, rel, oldNspOid, nspOid, objsMoved);
		AlterSeqNamespaces(classRel, rel, oldNspOid, nspOid,
						   objsMoved, AccessExclusiveLock);
		AlterConstraintNamespaces(RelationGetRelid(rel), oldNspOid, nspOid,
								  false, objsMoved);
	}

	table_close(classRel, RowExclusiveLock);
}

/*
 * The guts of relocating a relation to another namespace: fix the pg_class
 * entry, and the pg_depend entry if any.  Caller must already have
 * opened and write-locked pg_class.
 */
void
AlterRelationNamespaceInternal(Relation classRel, Oid relOid,
							   Oid oldNspOid, Oid newNspOid,
							   bool hasDependEntry,
							   ObjectAddresses *objsMoved)
{
	HeapTuple	classTup;
	Form_pg_class classForm;
	ObjectAddress thisobj;
	bool		already_done = false;

	classTup = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relOid));
	if (!HeapTupleIsValid(classTup))
		elog(ERROR, "cache lookup failed for relation %u", relOid);
	classForm = (Form_pg_class) GETSTRUCT(classTup);

	Assert(classForm->relnamespace == oldNspOid);

	thisobj.classId = RelationRelationId;
	thisobj.objectId = relOid;
	thisobj.objectSubId = 0;

	/*
	 * If the object has already been moved, don't move it again.  If it's
	 * already in the right place, don't move it, but still fire the object
	 * access hook.
	 */
	already_done = object_address_present(&thisobj, objsMoved);
	if (!already_done && oldNspOid != newNspOid)
	{
		/* check for duplicate name (more friendly than unique-index failure) */
		if (get_relname_relid(NameStr(classForm->relname),
							  newNspOid) != InvalidOid)
			ereport(ERROR,
					(errcode(ERRCODE_DUPLICATE_TABLE),
					 errmsg("relation \"%s\" already exists in schema \"%s\"",
							NameStr(classForm->relname),
							get_namespace_name(newNspOid))));

		/* classTup is a copy, so OK to scribble on */
		classForm->relnamespace = newNspOid;

		CatalogTupleUpdate(classRel, &classTup->t_self, classTup);

		/*
		 * Call SetSchema handler for the related internal YB DocDB table.
		 * No YB DocDB table for a primary key dummy index.
		 */
		const Relation rel = RelationIdGetRelation(relOid);
		if (IsYBRelation(rel) && !(rel->rd_index && rel->rd_index->indisprimary))
			YBCAlterTableNamespace(classForm, relOid);

		RelationClose(rel);

		/* Update dependency on schema if caller said so */
		if (hasDependEntry &&
			changeDependencyFor(RelationRelationId,
								relOid,
								NamespaceRelationId,
								oldNspOid,
								newNspOid) != 1)
			elog(ERROR, "failed to change schema dependency for relation \"%s\"",
				 NameStr(classForm->relname));
	}
	if (!already_done)
	{
		add_exact_object_address(&thisobj, objsMoved);

		InvokeObjectPostAlterHook(RelationRelationId, relOid, 0);
	}

	heap_freetuple(classTup);
}

/*
 * Move all indexes for the specified relation to another namespace.
 *
 * Note: we assume adequate permission checking was done by the caller,
 * and that the caller has a suitable lock on the owning relation.
 */
static void
AlterIndexNamespaces(Relation classRel, Relation rel,
					 Oid oldNspOid, Oid newNspOid, ObjectAddresses *objsMoved)
{
	List	   *indexList;
	ListCell   *l;

	indexList = RelationGetIndexList(rel);

	foreach(l, indexList)
	{
		Oid			indexOid = lfirst_oid(l);
		ObjectAddress thisobj;

		thisobj.classId = RelationRelationId;
		thisobj.objectId = indexOid;
		thisobj.objectSubId = 0;

		/*
		 * Note: currently, the index will not have its own dependency on the
		 * namespace, so we don't need to do changeDependencyFor(). There's no
		 * row type in pg_type, either.
		 *
		 * XXX this objsMoved test may be pointless -- surely we have a single
		 * dependency link from a relation to each index?
		 */
		if (!object_address_present(&thisobj, objsMoved))
		{
			AlterRelationNamespaceInternal(classRel, indexOid,
										   oldNspOid, newNspOid,
										   false, objsMoved);
			add_exact_object_address(&thisobj, objsMoved);
		}
	}

	list_free(indexList);
}

/*
 * Move all identity and SERIAL-column sequences of the specified relation to another
 * namespace.
 *
 * Note: we assume adequate permission checking was done by the caller,
 * and that the caller has a suitable lock on the owning relation.
 */
static void
AlterSeqNamespaces(Relation classRel, Relation rel,
				   Oid oldNspOid, Oid newNspOid, ObjectAddresses *objsMoved,
				   LOCKMODE lockmode)
{
	Relation	depRel;
	SysScanDesc scan;
	ScanKeyData key[2];
	HeapTuple	tup;

	/*
	 * SERIAL sequences are those having an auto dependency on one of the
	 * table's columns (we don't care *which* column, exactly).
	 */
	depRel = table_open(DependRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_depend_refclassid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1],
				Anum_pg_depend_refobjid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(rel)));
	/* we leave refobjsubid unspecified */

	scan = systable_beginscan(depRel, DependReferenceIndexId, true,
							  NULL, 2, key);

	while (HeapTupleIsValid(tup = systable_getnext(scan)))
	{
		Form_pg_depend depForm = (Form_pg_depend) GETSTRUCT(tup);
		Relation	seqRel;

		/* skip dependencies other than auto dependencies on columns */
		if (depForm->refobjsubid == 0 ||
			depForm->classid != RelationRelationId ||
			depForm->objsubid != 0 ||
			!(depForm->deptype == DEPENDENCY_AUTO || depForm->deptype == DEPENDENCY_INTERNAL))
			continue;

		/* Use relation_open just in case it's an index */
		seqRel = relation_open(depForm->objid, lockmode);

		/* skip non-sequence relations */
		if (RelationGetForm(seqRel)->relkind != RELKIND_SEQUENCE)
		{
			/* No need to keep the lock */
			relation_close(seqRel, lockmode);
			continue;
		}

		/* Fix the pg_class and pg_depend entries */
		AlterRelationNamespaceInternal(classRel, depForm->objid,
									   oldNspOid, newNspOid,
									   true, objsMoved);

		/*
		 * Sequences used to have entries in pg_type, but no longer do.  If we
		 * ever re-instate that, we'll need to move the pg_type entry to the
		 * new namespace, too (using AlterTypeNamespaceInternal).
		 */
		Assert(RelationGetForm(seqRel)->reltype == InvalidOid);

		/* Now we can close it.  Keep the lock till end of transaction. */
		relation_close(seqRel, NoLock);
	}

	systable_endscan(scan);

	relation_close(depRel, AccessShareLock);
}


/*
 * This code supports
 *	CREATE TEMP TABLE ... ON COMMIT { DROP | PRESERVE ROWS | DELETE ROWS }
 *
 * Because we only support this for TEMP tables, it's sufficient to remember
 * the state in a backend-local data structure.
 */

/*
 * Register a newly-created relation's ON COMMIT action.
 */
void
register_on_commit_action(Oid relid, OnCommitAction action)
{
	OnCommitItem *oc;
	MemoryContext oldcxt;

	/*
	 * We needn't bother registering the relation unless there is an ON COMMIT
	 * action we need to take.
	 */
	if (action == ONCOMMIT_NOOP || action == ONCOMMIT_PRESERVE_ROWS)
		return;

	oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

	oc = (OnCommitItem *) palloc(sizeof(OnCommitItem));
	oc->relid = relid;
	oc->oncommit = action;
	oc->creating_subid = GetCurrentSubTransactionId();
	oc->deleting_subid = InvalidSubTransactionId;

	/*
	 * We use lcons() here so that ON COMMIT actions are processed in reverse
	 * order of registration.  That might not be essential but it seems
	 * reasonable.
	 */
	on_commits = lcons(oc, on_commits);

	MemoryContextSwitchTo(oldcxt);
}

/*
 * Unregister any ON COMMIT action when a relation is deleted.
 *
 * Actually, we only mark the OnCommitItem entry as to be deleted after commit.
 */
void
remove_on_commit_action(Oid relid)
{
	ListCell   *l;

	foreach(l, on_commits)
	{
		OnCommitItem *oc = (OnCommitItem *) lfirst(l);

		if (oc->relid == relid)
		{
			oc->deleting_subid = GetCurrentSubTransactionId();
			break;
		}
	}
}

/*
 * Perform ON COMMIT actions.
 *
 * This is invoked just before actually committing, since it's possible
 * to encounter errors.
 */
void
PreCommit_on_commit_actions(void)
{
	ListCell   *l;
	List	   *oids_to_truncate = NIL;
	List	   *oids_to_drop = NIL;

	foreach(l, on_commits)
	{
		OnCommitItem *oc = (OnCommitItem *) lfirst(l);

		/* Ignore entry if already dropped in this xact */
		if (oc->deleting_subid != InvalidSubTransactionId)
			continue;

		switch (oc->oncommit)
		{
			case ONCOMMIT_NOOP:
			case ONCOMMIT_PRESERVE_ROWS:
				/* Do nothing (there shouldn't be such entries, actually) */
				break;
			case ONCOMMIT_DELETE_ROWS:

				/*
				 * If this transaction hasn't accessed any temporary
				 * relations, we can skip truncating ON COMMIT DELETE ROWS
				 * tables, as they must still be empty.
				 */
				if ((MyXactFlags & XACT_FLAGS_ACCESSEDTEMPNAMESPACE))
					oids_to_truncate = lappend_oid(oids_to_truncate, oc->relid);
				break;
			case ONCOMMIT_DROP:
				oids_to_drop = lappend_oid(oids_to_drop, oc->relid);
				break;
		}
	}

	/*
	 * Truncate relations before dropping so that all dependencies between
	 * relations are removed after they are worked on.  Doing it like this
	 * might be a waste as it is possible that a relation being truncated will
	 * be dropped anyway due to its parent being dropped, but this makes the
	 * code more robust because of not having to re-check that the relation
	 * exists at truncation time.
	 */
	if (oids_to_truncate != NIL)
		heap_truncate(oids_to_truncate);

	if (oids_to_drop != NIL)
	{
		ObjectAddresses *targetObjects = new_object_addresses();
		ListCell   *l;

		foreach(l, oids_to_drop)
		{
			ObjectAddress object;

			object.classId = RelationRelationId;
			object.objectId = lfirst_oid(l);
			object.objectSubId = 0;

			Assert(!object_address_present(&object, targetObjects));

			add_exact_object_address(&object, targetObjects);
		}

		/*
		 * Since this is an automatic drop, rather than one directly initiated
		 * by the user, we pass the PERFORM_DELETION_INTERNAL flag.
		 */
		performMultipleDeletions(targetObjects, DROP_CASCADE,
								 PERFORM_DELETION_INTERNAL | PERFORM_DELETION_QUIETLY);

#ifdef USE_ASSERT_CHECKING

		/*
		 * Note that table deletion will call remove_on_commit_action, so the
		 * entry should get marked as deleted.
		 */
		foreach(l, on_commits)
		{
			OnCommitItem *oc = (OnCommitItem *) lfirst(l);

			if (oc->oncommit != ONCOMMIT_DROP)
				continue;

			Assert(oc->deleting_subid != InvalidSubTransactionId);
		}
#endif
	}
}

/*
 * Post-commit or post-abort cleanup for ON COMMIT management.
 *
 * All we do here is remove no-longer-needed OnCommitItem entries.
 *
 * During commit, remove entries that were deleted during this transaction;
 * during abort, remove those created during this transaction.
 */
void
AtEOXact_on_commit_actions(bool isCommit)
{
	ListCell   *cur_item;

	foreach(cur_item, on_commits)
	{
		OnCommitItem *oc = (OnCommitItem *) lfirst(cur_item);

		if (isCommit ? oc->deleting_subid != InvalidSubTransactionId :
			oc->creating_subid != InvalidSubTransactionId)
		{
			/* cur_item must be removed */
			on_commits = foreach_delete_current(on_commits, cur_item);
			pfree(oc);
		}
		else
		{
			/* cur_item must be preserved */
			oc->creating_subid = InvalidSubTransactionId;
			oc->deleting_subid = InvalidSubTransactionId;
		}
	}
}

/*
 * Post-subcommit or post-subabort cleanup for ON COMMIT management.
 *
 * During subabort, we can immediately remove entries created during this
 * subtransaction.  During subcommit, just relabel entries marked during
 * this subtransaction as being the parent's responsibility.
 */
void
AtEOSubXact_on_commit_actions(bool isCommit, SubTransactionId mySubid,
							  SubTransactionId parentSubid)
{
	ListCell   *cur_item;

	foreach(cur_item, on_commits)
	{
		OnCommitItem *oc = (OnCommitItem *) lfirst(cur_item);

		if (!isCommit && oc->creating_subid == mySubid)
		{
			/* cur_item must be removed */
			on_commits = foreach_delete_current(on_commits, cur_item);
			pfree(oc);
		}
		else
		{
			/* cur_item must be preserved */
			if (oc->creating_subid == mySubid)
				oc->creating_subid = parentSubid;
			if (oc->deleting_subid == mySubid)
				oc->deleting_subid = isCommit ? parentSubid : InvalidSubTransactionId;
		}
	}
}

/*
 * This is intended as a callback for RangeVarGetRelidExtended().  It allows
 * the relation to be locked only if (1) it's a plain or partitioned table,
 * materialized view, or TOAST table and (2) the current user is the owner (or
 * the superuser).  This meets the permission-checking needs of CLUSTER,
 * REINDEX TABLE, and REFRESH MATERIALIZED VIEW; we expose it here so that it
 * can be used by all.
 */
void
RangeVarCallbackOwnsTable(const RangeVar *relation,
						  Oid relId, Oid oldRelId, void *arg)
{
	char		relkind;

	/* Nothing to do if the relation was not found. */
	if (!OidIsValid(relId))
		return;

	/*
	 * If the relation does exist, check whether it's an index.  But note that
	 * the relation might have been dropped between the time we did the name
	 * lookup and now.  In that case, there's nothing to do.
	 */
	relkind = get_rel_relkind(relId);
	if (!relkind)
		return;
	if (relkind != RELKIND_RELATION && relkind != RELKIND_TOASTVALUE &&
		relkind != RELKIND_MATVIEW && relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a table or materialized view", relation->relname)));

	/* Check permissions */
	if (!pg_class_ownercheck(relId, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(relId)), relation->relname);
}

/*
 * Callback to RangeVarGetRelidExtended() for TRUNCATE processing.
 */
static void
RangeVarCallbackForTruncate(const RangeVar *relation,
							Oid relId, Oid oldRelId, void *arg)
{
	HeapTuple	tuple;

	/* Nothing to do if the relation was not found. */
	if (!OidIsValid(relId))
		return;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relId));
	if (!HeapTupleIsValid(tuple))	/* should not happen */
		elog(ERROR, "cache lookup failed for relation %u", relId);

	truncate_check_rel(relId, (Form_pg_class) GETSTRUCT(tuple));
	truncate_check_perms(relId, (Form_pg_class) GETSTRUCT(tuple));

	ReleaseSysCache(tuple);
}

/*
 * Callback to RangeVarGetRelidExtended(), similar to
 * RangeVarCallbackOwnsTable() but without checks on the type of the relation.
 */
void
RangeVarCallbackOwnsRelation(const RangeVar *relation,
							 Oid relId, Oid oldRelId, void *arg)
{
	HeapTuple	tuple;

	/* Nothing to do if the relation was not found. */
	if (!OidIsValid(relId))
		return;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relId));
	if (!HeapTupleIsValid(tuple))	/* should not happen */
		elog(ERROR, "cache lookup failed for relation %u", relId);

	if (!pg_class_ownercheck(relId, GetUserId()) && !IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(relId)),
					   relation->relname);

	if (!allowSystemTableMods &&
		IsSystemClass(relId, (Form_pg_class) GETSTRUCT(tuple)))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						relation->relname)));

	ReleaseSysCache(tuple);
}

/*
 * Common RangeVarGetRelid callback for rename, set schema, and alter table
 * processing.
 */
static void
RangeVarCallbackForAlterRelation(const RangeVar *rv, Oid relid, Oid oldrelid,
								 void *arg)
{
	Node	   *stmt = (Node *) arg;
	ObjectType	reltype;
	HeapTuple	tuple;
	Form_pg_class classform;
	AclResult	aclresult;
	char		relkind;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tuple))
		return;					/* concurrently dropped */
	classform = (Form_pg_class) GETSTRUCT(tuple);
	relkind = classform->relkind;

	/* Must own relation or be yb_db_admin user. */
	if (!pg_class_ownercheck(relid, GetUserId()) && !IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(relid)), rv->relname);

	/* No system table modifications unless explicitly allowed. */
	if (!allowSystemTableMods && IsSystemClass(relid, classform))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						rv->relname)));

	/*
	 * Extract the specified relation type from the statement parse tree.
	 *
	 * Also, for ALTER .. RENAME, check permissions: the user must (still)
	 * have CREATE rights on the containing namespace.
	 */
	if (IsA(stmt, RenameStmt))
	{
		aclresult = pg_namespace_aclcheck(classform->relnamespace,
										  GetUserId(), ACL_CREATE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, OBJECT_SCHEMA,
						   get_namespace_name(classform->relnamespace));
		reltype = ((RenameStmt *) stmt)->renameType;
	}
	else if (IsA(stmt, AlterObjectSchemaStmt))
		reltype = ((AlterObjectSchemaStmt *) stmt)->objectType;

	else if (IsA(stmt, AlterTableStmt))
		reltype = ((AlterTableStmt *) stmt)->objtype;
	else
	{
		elog(ERROR, "unrecognized node type: %d", (int) nodeTag(stmt));
		reltype = OBJECT_TABLE; /* placate compiler */
	}

	/*
	 * For compatibility with prior releases, we allow ALTER TABLE to be used
	 * with most other types of relations (but not composite types). We allow
	 * similar flexibility for ALTER INDEX in the case of RENAME, but not
	 * otherwise.  Otherwise, the user must select the correct form of the
	 * command for the relation at issue.
	 */
	if (reltype == OBJECT_SEQUENCE && relkind != RELKIND_SEQUENCE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a sequence", rv->relname)));

	if (reltype == OBJECT_VIEW && relkind != RELKIND_VIEW)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a view", rv->relname)));

	if (reltype == OBJECT_MATVIEW && relkind != RELKIND_MATVIEW)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a materialized view", rv->relname)));

	if (reltype == OBJECT_FOREIGN_TABLE && relkind != RELKIND_FOREIGN_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a foreign table", rv->relname)));

	if (reltype == OBJECT_TYPE && relkind != RELKIND_COMPOSITE_TYPE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a composite type", rv->relname)));

	if (reltype == OBJECT_INDEX && relkind != RELKIND_INDEX &&
		relkind != RELKIND_PARTITIONED_INDEX
		&& !IsA(stmt, RenameStmt))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not an index", rv->relname)));

	/*
	 * Don't allow ALTER TABLE on composite types. We want people to use ALTER
	 * TYPE for that.
	 */
	if (reltype != OBJECT_TYPE && relkind == RELKIND_COMPOSITE_TYPE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is a composite type", rv->relname),
				 errhint("Use ALTER TYPE instead.")));

	/*
	 * Don't allow ALTER TABLE .. SET SCHEMA on relations that can't be moved
	 * to a different schema, such as indexes and TOAST tables.
	 */
	if (IsA(stmt, AlterObjectSchemaStmt))
	{
		if (relkind == RELKIND_INDEX || relkind == RELKIND_PARTITIONED_INDEX)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot change schema of index \"%s\"",
							rv->relname),
					 errhint("Change the schema of the table instead.")));
		else if (relkind == RELKIND_COMPOSITE_TYPE)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot change schema of composite type \"%s\"",
							rv->relname),
					 errhint("Use ALTER TYPE instead.")));
		else if (relkind == RELKIND_TOASTVALUE)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("cannot change schema of TOAST table \"%s\"",
							rv->relname),
					 errhint("Change the schema of the table instead.")));
	}

	ReleaseSysCache(tuple);
}

/*
 * Transform any expressions present in the partition key
 *
 * Returns a transformed PartitionSpec, as well as the strategy code
 */
static PartitionSpec *
transformPartitionSpec(Relation rel, PartitionSpec *partspec, char *strategy)
{
	PartitionSpec *newspec;
	ParseState *pstate;
	ParseNamespaceItem *nsitem;
	ListCell   *l;

	newspec = makeNode(PartitionSpec);

	newspec->strategy = partspec->strategy;
	newspec->partParams = NIL;
	newspec->location = partspec->location;

	/* Parse partitioning strategy name */
	if (pg_strcasecmp(partspec->strategy, "hash") == 0)
		*strategy = PARTITION_STRATEGY_HASH;
	else if (pg_strcasecmp(partspec->strategy, "list") == 0)
		*strategy = PARTITION_STRATEGY_LIST;
	else if (pg_strcasecmp(partspec->strategy, "range") == 0)
		*strategy = PARTITION_STRATEGY_RANGE;
	else
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("unrecognized partitioning strategy \"%s\"",
						partspec->strategy)));

	/* Check valid number of columns for strategy */
	if (*strategy == PARTITION_STRATEGY_LIST &&
		list_length(partspec->partParams) != 1)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("cannot use \"list\" partition strategy with more than one column")));

	/*
	 * Create a dummy ParseState and insert the target relation as its sole
	 * rangetable entry.  We need a ParseState for transformExpr.
	 */
	pstate = make_parsestate(NULL);
	nsitem = addRangeTableEntryForRelation(pstate, rel, AccessShareLock,
										   NULL, false, true);
	addNSItemToQuery(pstate, nsitem, true, true, true);

	/* take care of any partition expressions */
	foreach(l, partspec->partParams)
	{
		PartitionElem *pelem = lfirst_node(PartitionElem, l);

		if (pelem->expr)
		{
			/* Copy, to avoid scribbling on the input */
			pelem = copyObject(pelem);

			/* Now do parse transformation of the expression */
			pelem->expr = transformExpr(pstate, pelem->expr,
										EXPR_KIND_PARTITION_EXPRESSION);

			/* we have to fix its collations too */
			assign_expr_collations(pstate, pelem->expr);
		}

		newspec->partParams = lappend(newspec->partParams, pelem);
	}

	return newspec;
}

/*
 * Compute per-partition-column information from a list of PartitionElems.
 * Expressions in the PartitionElems must be parse-analyzed already.
 */
static void
ComputePartitionAttrs(ParseState *pstate, Relation rel, List *partParams, AttrNumber *partattrs,
					  List **partexprs, Oid *partopclass, Oid *partcollation,
					  char strategy)
{
	int			attn;
	ListCell   *lc;
	Oid			am_oid;

	attn = 0;
	foreach(lc, partParams)
	{
		PartitionElem *pelem = lfirst_node(PartitionElem, lc);
		Oid			atttype;
		Oid			attcollation;

		if (pelem->name != NULL)
		{
			/* Simple attribute reference */
			HeapTuple	atttuple;
			Form_pg_attribute attform;

			atttuple = SearchSysCacheAttName(RelationGetRelid(rel),
											 pelem->name);
			if (!HeapTupleIsValid(atttuple))
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_COLUMN),
						 errmsg("column \"%s\" named in partition key does not exist",
								pelem->name),
						 parser_errposition(pstate, pelem->location)));
			attform = (Form_pg_attribute) GETSTRUCT(atttuple);

			if (attform->attnum <= 0)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("cannot use system column \"%s\" in partition key",
								pelem->name),
						 parser_errposition(pstate, pelem->location)));

			/*
			 * Generated columns cannot work: They are computed after BEFORE
			 * triggers, but partition routing is done before all triggers.
			 */
			if (attform->attgenerated)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("cannot use generated column in partition key"),
						 errdetail("Column \"%s\" is a generated column.",
								   pelem->name),
						 parser_errposition(pstate, pelem->location)));

			partattrs[attn] = attform->attnum;
			atttype = attform->atttypid;
			attcollation = attform->attcollation;
			ReleaseSysCache(atttuple);
		}
		else
		{
			/* Expression */
			Node	   *expr = pelem->expr;
			char		partattname[16];

			Assert(expr != NULL);
			atttype = exprType(expr);
			attcollation = exprCollation(expr);

			/*
			 * The expression must be of a storable type (e.g., not RECORD).
			 * The test is the same as for whether a table column is of a safe
			 * type (which is why we needn't check for the non-expression
			 * case).
			 */
			snprintf(partattname, sizeof(partattname), "%d", attn + 1);
			CheckAttributeType(partattname,
							   atttype, attcollation,
							   NIL, CHKATYPE_IS_PARTKEY);

			/*
			 * Strip any top-level COLLATE clause.  This ensures that we treat
			 * "x COLLATE y" and "(x COLLATE y)" alike.
			 */
			while (IsA(expr, CollateExpr))
				expr = (Node *) ((CollateExpr *) expr)->arg;

			if (IsA(expr, Var) &&
				((Var *) expr)->varattno > 0)
			{
				/*
				 * User wrote "(column)" or "(column COLLATE something)".
				 * Treat it like simple attribute anyway.
				 */
				partattrs[attn] = ((Var *) expr)->varattno;
			}
			else
			{
				Bitmapset  *expr_attrs = NULL;
				int			i;

				partattrs[attn] = 0;	/* marks the column as expression */
				*partexprs = lappend(*partexprs, expr);

				/*
				 * Try to simplify the expression before checking for
				 * mutability.  The main practical value of doing it in this
				 * order is that an inline-able SQL-language function will be
				 * accepted if its expansion is immutable, whether or not the
				 * function itself is marked immutable.
				 *
				 * Note that expression_planner does not change the passed in
				 * expression destructively and we have already saved the
				 * expression to be stored into the catalog above.
				 */
				expr = (Node *) expression_planner((Expr *) expr);

				/*
				 * Partition expression cannot contain mutable functions,
				 * because a given row must always map to the same partition
				 * as long as there is no change in the partition boundary
				 * structure.
				 */
				if (contain_mutable_functions(expr))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("functions in partition key expression must be marked IMMUTABLE")));

				/*
				 * transformPartitionSpec() should have already rejected
				 * subqueries, aggregates, window functions, and SRFs, based
				 * on the EXPR_KIND_ for partition expressions.
				 */

				/*
				 * Cannot allow system column references, since that would
				 * make partition routing impossible: their values won't be
				 * known yet when we need to do that.
				 */
				pull_varattnos(expr, 1, &expr_attrs);
				for (i = FirstLowInvalidHeapAttributeNumber; i < 0; i++)
				{
					if (bms_is_member(i - FirstLowInvalidHeapAttributeNumber,
									  expr_attrs))
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
								 errmsg("partition key expressions cannot contain system column references")));
				}

				/*
				 * Generated columns cannot work: They are computed after
				 * BEFORE triggers, but partition routing is done before all
				 * triggers.
				 */
				i = -1;
				while ((i = bms_next_member(expr_attrs, i)) >= 0)
				{
					AttrNumber	attno = i + FirstLowInvalidHeapAttributeNumber;

					if (attno > 0 &&
						TupleDescAttr(RelationGetDescr(rel), attno - 1)->attgenerated)
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
								 errmsg("cannot use generated column in partition key"),
								 errdetail("Column \"%s\" is a generated column.",
										   get_attname(RelationGetRelid(rel), attno, false)),
								 parser_errposition(pstate, pelem->location)));
				}

				/*
				 * While it is not exactly *wrong* for a partition expression
				 * to be a constant, it seems better to reject such keys.
				 */
				if (IsA(expr, Const))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("cannot use constant expression as partition key")));
			}
		}

		/*
		 * Apply collation override if any
		 */
		if (pelem->collation)
			attcollation = get_collation_oid(pelem->collation, false);

		/*
		 * Check we have a collation iff it's a collatable type.  The only
		 * expected failures here are (1) COLLATE applied to a noncollatable
		 * type, or (2) partition expression had an unresolved collation. But
		 * we might as well code this to be a complete consistency check.
		 */
		if (type_is_collatable(atttype))
		{
			if (!OidIsValid(attcollation))
				ereport(ERROR,
						(errcode(ERRCODE_INDETERMINATE_COLLATION),
						 errmsg("could not determine which collation to use for partition expression"),
						 errhint("Use the COLLATE clause to set the collation explicitly.")));
		}
		else
		{
			if (OidIsValid(attcollation))
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("collations are not supported by type %s",
								format_type_be(atttype))));
		}

		partcollation[attn] = attcollation;

		/*
		 * Identify the appropriate operator class.  For list and range
		 * partitioning, we use a btree operator class; hash partitioning uses
		 * a hash operator class.
		 */
		if (strategy == PARTITION_STRATEGY_HASH)
			am_oid = HASH_AM_OID;
		else
			am_oid = IsYugaByteEnabled() ? LSM_AM_OID : BTREE_AM_OID;

		if (!pelem->opclass)
		{
			partopclass[attn] = GetDefaultOpClass(atttype, am_oid);

			if (!OidIsValid(partopclass[attn]))
			{
				if (strategy == PARTITION_STRATEGY_HASH)
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_OBJECT),
							 errmsg("data type %s has no default operator class for access method \"%s\"",
									format_type_be(atttype), "hash"),
							 errhint("You must specify a hash operator class or define a default hash operator class for the data type.")));
				else
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_OBJECT),
							 errmsg("data type %s has no default operator class for access method \"%s\"",
									format_type_be(atttype), "btree"),
							 errhint("You must specify a btree operator class or define a default btree operator class for the data type.")));
			}
		}
		else
			partopclass[attn] = ResolveOpClass(pelem->opclass,
											   atttype,
											   am_oid == HASH_AM_OID ? "hash" : "btree",
											   am_oid);

		attn++;
	}
}

/*
 * PartConstraintImpliedByRelConstraint
 *		Do scanrel's existing constraints imply the partition constraint?
 *
 * "Existing constraints" include its check constraints and column-level
 * NOT NULL constraints.  partConstraint describes the partition constraint,
 * in implicit-AND form.
 */
bool
PartConstraintImpliedByRelConstraint(Relation scanrel,
									 List *partConstraint)
{
	List	   *existConstraint = NIL;
	TupleConstr *constr = RelationGetDescr(scanrel)->constr;
	int			i;

	if (constr && constr->has_not_null)
	{
		int			natts = scanrel->rd_att->natts;

		for (i = 1; i <= natts; i++)
		{
			Form_pg_attribute att = TupleDescAttr(scanrel->rd_att, i - 1);

			if (att->attnotnull && !att->attisdropped)
			{
				NullTest   *ntest = makeNode(NullTest);

				ntest->arg = (Expr *) makeVar(1,
											  i,
											  att->atttypid,
											  att->atttypmod,
											  att->attcollation,
											  0);
				ntest->nulltesttype = IS_NOT_NULL;

				/*
				 * argisrow=false is correct even for a composite column,
				 * because attnotnull does not represent a SQL-spec IS NOT
				 * NULL test in such a case, just IS DISTINCT FROM NULL.
				 */
				ntest->argisrow = false;
				ntest->location = -1;
				existConstraint = lappend(existConstraint, ntest);
			}
		}
	}

	return ConstraintImpliedByRelConstraint(scanrel, partConstraint, existConstraint);
}

/*
 * ConstraintImpliedByRelConstraint
 *		Do scanrel's existing constraints imply the given constraint?
 *
 * testConstraint is the constraint to validate. provenConstraint is a
 * caller-provided list of conditions which this function may assume
 * to be true. Both provenConstraint and testConstraint must be in
 * implicit-AND form, must only contain immutable clauses, and must
 * contain only Vars with varno = 1.
 */
bool
ConstraintImpliedByRelConstraint(Relation scanrel, List *testConstraint, List *provenConstraint)
{
	List	   *existConstraint = list_copy(provenConstraint);
	TupleConstr *constr = RelationGetDescr(scanrel)->constr;
	int			num_check,
				i;

	num_check = (constr != NULL) ? constr->num_check : 0;
	for (i = 0; i < num_check; i++)
	{
		Node	   *cexpr;

		/*
		 * If this constraint hasn't been fully validated yet, we must ignore
		 * it here.
		 */
		if (!constr->check[i].ccvalid)
			continue;

		cexpr = stringToNode(constr->check[i].ccbin);

		/*
		 * Run each expression through const-simplification and
		 * canonicalization.  It is necessary, because we will be comparing it
		 * to similarly-processed partition constraint expressions, and may
		 * fail to detect valid matches without this.
		 */
		cexpr = eval_const_expressions(NULL, cexpr);
		cexpr = (Node *) canonicalize_qual((Expr *) cexpr, true);

		existConstraint = list_concat(existConstraint,
									  make_ands_implicit((Expr *) cexpr));
	}

	/*
	 * Try to make the proof.  Since we are comparing CHECK constraints, we
	 * need to use weak implication, i.e., we assume existConstraint is
	 * not-false and try to prove the same for testConstraint.
	 *
	 * Note that predicate_implied_by assumes its first argument is known
	 * immutable.  That should always be true for both NOT NULL and partition
	 * constraints, so we don't test it here.
	 */
	return predicate_implied_by(testConstraint, existConstraint, true);
}

/*
 * QueuePartitionConstraintValidation
 *
 * Add an entry to wqueue to have the given partition constraint validated by
 * Phase 3, for the given relation, and all its children.
 *
 * We first verify whether the given constraint is implied by pre-existing
 * relation constraints; if it is, there's no need to scan the table to
 * validate, so don't queue in that case.
 */
static void
QueuePartitionConstraintValidation(List **wqueue, Relation scanrel,
								   List *partConstraint,
								   bool validate_default)
{
	/*
	 * Based on the table's existing constraints, determine whether or not we
	 * may skip scanning the table.
	 */
	if (PartConstraintImpliedByRelConstraint(scanrel, partConstraint))
	{
		if (!validate_default)
			ereport(DEBUG1,
					(errmsg_internal("partition constraint for table \"%s\" is implied by existing constraints",
									 RelationGetRelationName(scanrel))));
		else
			ereport(DEBUG1,
					(errmsg_internal("updated partition constraint for default partition \"%s\" is implied by existing constraints",
									 RelationGetRelationName(scanrel))));
		return;
	}

	/*
	 * Constraints proved insufficient. For plain relations, queue a
	 * validation item now; for partitioned tables, recurse to process each
	 * partition.
	 */
	if (scanrel->rd_rel->relkind == RELKIND_RELATION)
	{
		AlteredTableInfo *tab;

		/* Grab a work queue entry. */
		tab = ATGetQueueEntry(wqueue, scanrel);
		Assert(tab->partition_constraint == NULL);
		tab->partition_constraint = (Expr *) linitial(partConstraint);
		tab->validate_default = validate_default;
	}
	else if (scanrel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		PartitionDesc partdesc = RelationGetPartitionDesc(scanrel, true);
		int			i;

		for (i = 0; i < partdesc->nparts; i++)
		{
			Relation	part_rel;
			List	   *thisPartConstraint;

			/*
			 * This is the minimum lock we need to prevent deadlocks.
			 */
			part_rel = table_open(partdesc->oids[i], AccessExclusiveLock);

			/*
			 * Adjust the constraint for scanrel so that it matches this
			 * partition's attribute numbers.
			 */
			thisPartConstraint =
				map_partition_varattnos(partConstraint, 1,
										part_rel, scanrel);

			QueuePartitionConstraintValidation(wqueue, part_rel,
											   thisPartConstraint,
											   validate_default);
			table_close(part_rel, NoLock);	/* keep lock till commit */
		}
	}
}

/*
 * ALTER TABLE <name> ATTACH PARTITION <partition-name> FOR VALUES
 *
 * Return the address of the newly attached partition.
 */
static ObjectAddress
ATExecAttachPartition(List **wqueue, Relation rel, PartitionCmd *cmd,
					  AlterTableUtilityContext *context)
{
	Relation	attachrel,
				catalog;
	List	   *attachrel_children;
	List	   *partConstraint;
	SysScanDesc scan;
	ScanKeyData skey;
	AttrNumber	attno;
	int			natts;
	TupleDesc	tupleDesc;
	ObjectAddress address;
	const char *trigger_name;
	Oid			defaultPartOid;
	List	   *partBoundConstraint;
	ParseState *pstate = make_parsestate(NULL);

	pstate->p_sourcetext = context->queryString;

	/*
	 * We must lock the default partition if one exists, because attaching a
	 * new partition will change its partition constraint.
	 */
	defaultPartOid =
		get_default_oid_from_partdesc(RelationGetPartitionDesc(rel, true));
	if (OidIsValid(defaultPartOid))
		LockRelationOid(defaultPartOid, AccessExclusiveLock);

	attachrel = table_openrv(cmd->name, AccessExclusiveLock);

	/*
	 * XXX I think it'd be a good idea to grab locks on all tables referenced
	 * by FKs at this point also.
	 */

	/*
	 * Must be owner of both parent and source table -- parent was checked by
	 * ATSimplePermissions call in ATPrepCmd
	 */
	ATSimplePermissions(AT_AttachPartition, attachrel, ATT_TABLE | ATT_FOREIGN_TABLE);

	/* A partition can only have one parent */
	if (attachrel->rd_rel->relispartition)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is already a partition",
						RelationGetRelationName(attachrel))));

	if (OidIsValid(attachrel->rd_rel->reloftype))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach a typed table as partition")));

	/*
	 * Table being attached should not already be part of inheritance; either
	 * as a child table...
	 */
	catalog = table_open(InheritsRelationId, AccessShareLock);
	ScanKeyInit(&skey,
				Anum_pg_inherits_inhrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(attachrel)));
	scan = systable_beginscan(catalog, InheritsRelidSeqnoIndexId, true,
							  NULL, 1, &skey);
	if (HeapTupleIsValid(systable_getnext(scan)))
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach inheritance child as partition")));
	systable_endscan(scan);

	/* ...or as a parent table (except the case when it is partitioned) */
	ScanKeyInit(&skey,
				Anum_pg_inherits_inhparent,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(attachrel)));
	scan = systable_beginscan(catalog, InheritsParentIndexId, true, NULL,
							  1, &skey);
	if (HeapTupleIsValid(systable_getnext(scan)) &&
		attachrel->rd_rel->relkind == RELKIND_RELATION)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach inheritance parent as partition")));
	systable_endscan(scan);
	table_close(catalog, AccessShareLock);

	/*
	 * Prevent circularity by seeing if rel is a partition of attachrel. (In
	 * particular, this disallows making a rel a partition of itself.)
	 *
	 * We do that by checking if rel is a member of the list of attachrel's
	 * partitions provided the latter is partitioned at all.  We want to avoid
	 * having to construct this list again, so we request the strongest lock
	 * on all partitions.  We need the strongest lock, because we may decide
	 * to scan them if we find out that the table being attached (or its leaf
	 * partitions) may contain rows that violate the partition constraint. If
	 * the table has a constraint that would prevent such rows, which by
	 * definition is present in all the partitions, we need not scan the
	 * table, nor its partitions.  But we cannot risk a deadlock by taking a
	 * weaker lock now and the stronger one only when needed.
	 */
	attachrel_children = find_all_inheritors(RelationGetRelid(attachrel),
											 AccessExclusiveLock, NULL);
	if (list_member_oid(attachrel_children, RelationGetRelid(rel)))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_TABLE),
				 errmsg("circular inheritance not allowed"),
				 errdetail("\"%s\" is already a child of \"%s\".",
						   RelationGetRelationName(rel),
						   RelationGetRelationName(attachrel))));

	/* If the parent is permanent, so must be all of its partitions. */
	if (rel->rd_rel->relpersistence != RELPERSISTENCE_TEMP &&
		attachrel->rd_rel->relpersistence == RELPERSISTENCE_TEMP)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach a temporary relation as partition of permanent relation \"%s\"",
						RelationGetRelationName(rel))));

	/* Temp parent cannot have a partition that is itself not a temp */
	if (rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
		attachrel->rd_rel->relpersistence != RELPERSISTENCE_TEMP)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach a permanent relation as partition of temporary relation \"%s\"",
						RelationGetRelationName(rel))));

	/* If the parent is temp, it must belong to this session */
	if (rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
		!rel->rd_islocaltemp)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach as partition of temporary relation of another session")));

	/* Ditto for the partition */
	if (attachrel->rd_rel->relpersistence == RELPERSISTENCE_TEMP &&
		!attachrel->rd_islocaltemp)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach temporary relation of another session as partition")));

	/* Check if there are any columns in attachrel that aren't in the parent */
	tupleDesc = RelationGetDescr(attachrel);
	natts = tupleDesc->natts;
	for (attno = 1; attno <= natts; attno++)
	{
		Form_pg_attribute attribute = TupleDescAttr(tupleDesc, attno - 1);
		char	   *attributeName = NameStr(attribute->attname);

		/* Ignore dropped */
		if (attribute->attisdropped)
			continue;

		/* Try to find the column in parent (matching on column name) */
		if (!SearchSysCacheExists2(ATTNAME,
								   ObjectIdGetDatum(RelationGetRelid(rel)),
								   CStringGetDatum(attributeName)))
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("table \"%s\" contains column \"%s\" not found in parent \"%s\"",
							RelationGetRelationName(attachrel), attributeName,
							RelationGetRelationName(rel)),
					 errdetail("The new partition may contain only the columns present in parent.")));
	}

	/*
	 * If child_rel has row-level triggers with transition tables, we
	 * currently don't allow it to become a partition.  See also prohibitions
	 * in ATExecAddInherit() and CreateTrigger().
	 */
	trigger_name = FindTriggerIncompatibleWithInheritance(attachrel->trigdesc);
	if (trigger_name != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("trigger \"%s\" prevents table \"%s\" from becoming a partition",
						trigger_name, RelationGetRelationName(attachrel)),
				 errdetail("ROW triggers with transition tables are not supported on partitions.")));

	/*
	 * Check that the new partition's bound is valid and does not overlap any
	 * of existing partitions of the parent - note that it does not return on
	 * error.
	 */
	check_new_partition_bound(RelationGetRelationName(attachrel), rel,
							  cmd->bound, pstate);

	/* OK to create inheritance.  Rest of the checks performed there */
	CreateInheritance(attachrel, rel);

	/* Update the pg_class entry. */
	StorePartitionBound(attachrel, rel, cmd->bound);

	/* Ensure there exists a correct set of indexes in the partition. */
	AttachPartitionEnsureIndexes(rel, attachrel, wqueue);

	/* and triggers */
	CloneRowTriggersToPartition(rel, attachrel);

	/*
	 * Clone foreign key constraints.  Callee is responsible for setting up
	 * for phase 3 constraint verification.
	 */
	CloneForeignKeyConstraints(wqueue, rel, attachrel);

	/*
	 * Generate partition constraint from the partition bound specification.
	 * If the parent itself is a partition, make sure to include its
	 * constraint as well.
	 */
	partBoundConstraint = get_qual_from_partbound(rel, cmd->bound);
	partConstraint = list_concat(partBoundConstraint,
								 RelationGetPartitionQual(rel));

	/* Skip validation if there are no constraints to validate. */
	if (partConstraint)
	{
		/*
		 * Run the partition quals through const-simplification similar to
		 * check constraints.  We skip canonicalize_qual, though, because
		 * partition quals should be in canonical form already.
		 */
		partConstraint =
			(List *) eval_const_expressions(NULL,
											(Node *) partConstraint);

		/* XXX this sure looks wrong */
		partConstraint = list_make1(make_ands_explicit(partConstraint));

		/*
		 * Adjust the generated constraint to match this partition's attribute
		 * numbers.
		 */
		partConstraint = map_partition_varattnos(partConstraint, 1, attachrel,
												 rel);

		/* Validate partition constraints against the table being attached. */
		QueuePartitionConstraintValidation(wqueue, attachrel, partConstraint,
										   false);
	}

	/*
	 * If we're attaching a partition other than the default partition and a
	 * default one exists, then that partition's partition constraint changes,
	 * so add an entry to the work queue to validate it, too.  (We must not do
	 * this when the partition being attached is the default one; we already
	 * did it above!)
	 */
	if (OidIsValid(defaultPartOid))
	{
		Relation	defaultrel;
		List	   *defPartConstraint;

		Assert(!cmd->bound->is_default);

		/* we already hold a lock on the default partition */
		defaultrel = table_open(defaultPartOid, NoLock);
		defPartConstraint =
			get_proposed_default_constraint(partBoundConstraint);

		/*
		 * Map the Vars in the constraint expression from rel's attnos to
		 * defaultrel's.
		 */
		defPartConstraint =
			map_partition_varattnos(defPartConstraint,
									1, defaultrel, rel);
		QueuePartitionConstraintValidation(wqueue, defaultrel,
										   defPartConstraint, true);

		/* keep our lock until commit. */
		table_close(defaultrel, NoLock);
	}

	ObjectAddressSet(address, RelationRelationId, RelationGetRelid(attachrel));

	/*
	 * If the partition we just attached is partitioned itself, invalidate
	 * relcache for all descendent partitions too to ensure that their
	 * rd_partcheck expression trees are rebuilt; partitions already locked at
	 * the beginning of this function.
	 */
	if (attachrel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		ListCell   *l;

		foreach(l, attachrel_children)
		{
			CacheInvalidateRelcacheByRelid(lfirst_oid(l));
		}
	}

	/* keep our lock until commit */
	table_close(attachrel, NoLock);

	return address;
}

/*
 * AttachPartitionEnsureIndexes
 *		subroutine for ATExecAttachPartition to create/match indexes
 *
 * Enforce the indexing rule for partitioned tables during ALTER TABLE / ATTACH
 * PARTITION: every partition must have an index attached to each index on the
 * partitioned table.
 */
static void
AttachPartitionEnsureIndexes(Relation rel, Relation attachrel, List **yb_wqueue)
{
	List	   *idxes;
	List	   *attachRelIdxs;
	Relation   *attachrelIdxRels;
	IndexInfo **attachInfos;
	int			i;
	ListCell   *cell;
	MemoryContext cxt;
	MemoryContext oldcxt;

	cxt = AllocSetContextCreate(GetCurrentMemoryContext(),
								"AttachPartitionEnsureIndexes",
								ALLOCSET_DEFAULT_SIZES);
	oldcxt = MemoryContextSwitchTo(cxt);

	idxes = RelationGetIndexList(rel);
	attachRelIdxs = RelationGetIndexList(attachrel);
	attachrelIdxRels = palloc(sizeof(Relation) * list_length(attachRelIdxs));
	attachInfos = palloc(sizeof(IndexInfo *) * list_length(attachRelIdxs));

	/* Build arrays of all existing indexes and their IndexInfos */
	i = 0;
	foreach(cell, attachRelIdxs)
	{
		Oid			cldIdxId = lfirst_oid(cell);

		attachrelIdxRels[i] = index_open(cldIdxId, AccessShareLock);
		attachInfos[i] = BuildIndexInfo(attachrelIdxRels[i]);
		i++;
	}

	/*
	 * If we're attaching a foreign table, we must fail if any of the indexes
	 * is a constraint index; otherwise, there's nothing to do here.  Do this
	 * before starting work, to avoid wasting the effort of building a few
	 * non-unique indexes before coming across a unique one.
	 */
	if (attachrel->rd_rel->relkind == RELKIND_FOREIGN_TABLE)
	{
		foreach(cell, idxes)
		{
			Oid			idx = lfirst_oid(cell);
			Relation	idxRel = index_open(idx, AccessShareLock);

			if (idxRel->rd_index->indisunique ||
				idxRel->rd_index->indisprimary)
				ereport(ERROR,
						(errcode(ERRCODE_WRONG_OBJECT_TYPE),
						 errmsg("cannot attach foreign table \"%s\" as partition of partitioned table \"%s\"",
								RelationGetRelationName(attachrel),
								RelationGetRelationName(rel)),
						 errdetail("Partitioned table \"%s\" contains unique indexes.",
								   RelationGetRelationName(rel))));
			index_close(idxRel, AccessShareLock);
		}

		goto out;
	}

	/*
	 * For each index on the partitioned table, find a matching one in the
	 * partition-to-be; if one is not found, create one.
	 */
	foreach(cell, idxes)
	{
		Oid			idx = lfirst_oid(cell);
		Relation	idxRel = index_open(idx, AccessShareLock);
		IndexInfo  *info;
		AttrMap    *attmap;
		bool		found = false;
		Oid			constraintOid;

		/*
		 * Ignore indexes in the partitioned table other than partitioned
		 * indexes.
		 */
		if (idxRel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX)
		{
			index_close(idxRel, AccessShareLock);
			continue;
		}

		/* construct an indexinfo to compare existing indexes against */
		info = BuildIndexInfo(idxRel);
		attmap = build_attrmap_by_name(RelationGetDescr(attachrel),
									   RelationGetDescr(rel),
									   false /* yb_ignore_type_mismatch */);
		constraintOid = get_relation_idx_constraint_oid(RelationGetRelid(rel), idx);

		/*
		 * Scan the list of existing indexes in the partition-to-be, and mark
		 * the first matching, unattached one we find, if any, as partition of
		 * the parent index.  If we find one, we're done.
		 */
		for (i = 0; i < list_length(attachRelIdxs); i++)
		{
			Oid			cldIdxId = RelationGetRelid(attachrelIdxRels[i]);
			Oid			cldConstrOid = InvalidOid;

			/* does this index have a parent?  if so, can't use it */
			if (attachrelIdxRels[i]->rd_rel->relispartition)
				continue;

			if (CompareIndexInfo(attachInfos[i], info,
								 attachrelIdxRels[i]->rd_indcollation,
								 idxRel->rd_indcollation,
								 attachrelIdxRels[i]->rd_opfamily,
								 idxRel->rd_opfamily,
								 attmap))
			{
				/*
				 * If this index is being created in the parent because of a
				 * constraint, then the child needs to have a constraint also,
				 * so look for one.  If there is no such constraint, this
				 * index is no good, so keep looking.
				 */
				if (OidIsValid(constraintOid))
				{
					cldConstrOid =
						get_relation_idx_constraint_oid(RelationGetRelid(attachrel),
														cldIdxId);
					/* no dice */
					if (!OidIsValid(cldConstrOid))
						continue;
				}

				/* bingo. */
				IndexSetParentIndex(attachrelIdxRels[i], idx);
				if (OidIsValid(constraintOid))
					ConstraintSetParentConstraint(cldConstrOid, constraintOid,
												  RelationGetRelid(attachrel));
				found = true;

				CommandCounterIncrement();
				break;
			}
		}

		/*
		 * If no suitable index was found in the partition-to-be, create one
		 * now.
		 */
		if (!found)
		{
			IndexStmt  *stmt;
			Oid			constraintOid;

			stmt = generateClonedIndexStmt(NULL,
										   idxRel, attmap,
										   &constraintOid);
			/*
			 * YB Note: If a matching pk index is not found for the child
			 * partition, then we must rewrite the child partition (and all
			 * of its children).
			 */
			if (IsYBRelation(idxRel) && idxRel->rd_index->indisprimary)
			{
				MemoryContextSwitchTo(oldcxt);
				AlteredTableInfo *tab;
				tab = ATGetQueueEntry(yb_wqueue, attachrel);
				tab->rewrite = YB_AT_REWRITE_ALTER_PRIMARY_KEY;
				YbGetTableProperties(attachrel);
				/* Don't copy split options if we are creating a range key. */
				bool skip_copy_split_options = YbATIsRangePk(
					linitial_node(IndexElem, stmt->indexParams)->ordering,
					attachrel->yb_table_properties->is_colocated,
					OidIsValid(
						attachrel->yb_table_properties->tablegroup_oid));
				YbATSetPKRewriteChildPartitions(yb_wqueue, tab,
					skip_copy_split_options);
				MemoryContextSwitchTo(cxt);
			}
			DefineIndex(RelationGetRelid(attachrel), stmt, InvalidOid,
						RelationGetRelid(idxRel),
						constraintOid,
						true, false, false, false, false);
		}

		index_close(idxRel, AccessShareLock);
	}

out:
	/* Clean up. */
	for (i = 0; i < list_length(attachRelIdxs); i++)
		index_close(attachrelIdxRels[i], AccessShareLock);
	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(cxt);
}

/*
 * CloneRowTriggersToPartition
 *		subroutine for ATExecAttachPartition/DefineRelation to create row
 *		triggers on partitions
 */
static void
CloneRowTriggersToPartition(Relation parent, Relation partition)
{
	Relation	pg_trigger;
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;
	MemoryContext perTupCxt;

	ScanKeyInit(&key, Anum_pg_trigger_tgrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(parent)));
	pg_trigger = table_open(TriggerRelationId, RowExclusiveLock);
	scan = systable_beginscan(pg_trigger, TriggerRelidNameIndexId,
							  true, NULL, 1, &key);

	perTupCxt = AllocSetContextCreate(GetCurrentMemoryContext(),
									  "clone trig", ALLOCSET_SMALL_SIZES);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_trigger trigForm = (Form_pg_trigger) GETSTRUCT(tuple);
		CreateTrigStmt *trigStmt;
		Node	   *qual = NULL;
		Datum		value;
		bool		isnull;
		List	   *cols = NIL;
		List	   *trigargs = NIL;
		MemoryContext oldcxt;

		/*
		 * Ignore statement-level triggers; those are not cloned.
		 */
		if (!TRIGGER_FOR_ROW(trigForm->tgtype))
			continue;

		/*
		 * Don't clone internal triggers, because the constraint cloning code
		 * will.
		 */
		if (trigForm->tgisinternal)
			continue;

		/*
		 * Complain if we find an unexpected trigger type.
		 */
		if (!TRIGGER_FOR_BEFORE(trigForm->tgtype) &&
			!TRIGGER_FOR_AFTER(trigForm->tgtype))
			elog(ERROR, "unexpected trigger \"%s\" found",
				 NameStr(trigForm->tgname));

		/* Use short-lived context for CREATE TRIGGER */
		oldcxt = MemoryContextSwitchTo(perTupCxt);

		/*
		 * If there is a WHEN clause, generate a 'cooked' version of it that's
		 * appropriate for the partition.
		 */
		value = heap_getattr(tuple, Anum_pg_trigger_tgqual,
							 RelationGetDescr(pg_trigger), &isnull);
		if (!isnull)
		{
			qual = stringToNode(TextDatumGetCString(value));
			qual = (Node *) map_partition_varattnos((List *) qual, PRS2_OLD_VARNO,
													partition, parent);
			qual = (Node *) map_partition_varattnos((List *) qual, PRS2_NEW_VARNO,
													partition, parent);
		}

		/*
		 * If there is a column list, transform it to a list of column names.
		 * Note we don't need to map this list in any way ...
		 */
		if (trigForm->tgattr.dim1 > 0)
		{
			int			i;

			for (i = 0; i < trigForm->tgattr.dim1; i++)
			{
				Form_pg_attribute col;

				col = TupleDescAttr(parent->rd_att,
									trigForm->tgattr.values[i] - 1);
				cols = lappend(cols,
							   makeString(pstrdup(NameStr(col->attname))));
			}
		}

		/* Reconstruct trigger arguments list. */
		if (trigForm->tgnargs > 0)
		{
			char	   *p;

			value = heap_getattr(tuple, Anum_pg_trigger_tgargs,
								 RelationGetDescr(pg_trigger), &isnull);
			if (isnull)
				elog(ERROR, "tgargs is null for trigger \"%s\" in partition \"%s\"",
					 NameStr(trigForm->tgname), RelationGetRelationName(partition));

			p = (char *) VARDATA_ANY(DatumGetByteaPP(value));

			for (int i = 0; i < trigForm->tgnargs; i++)
			{
				trigargs = lappend(trigargs, makeString(pstrdup(p)));
				p += strlen(p) + 1;
			}
		}

		trigStmt = makeNode(CreateTrigStmt);
		trigStmt->replace = false;
		trigStmt->isconstraint = OidIsValid(trigForm->tgconstraint);
		trigStmt->trigname = NameStr(trigForm->tgname);
		trigStmt->relation = NULL;
		trigStmt->funcname = NULL;	/* passed separately */
		trigStmt->args = trigargs;
		trigStmt->row = true;
		trigStmt->timing = trigForm->tgtype & TRIGGER_TYPE_TIMING_MASK;
		trigStmt->events = trigForm->tgtype & TRIGGER_TYPE_EVENT_MASK;
		trigStmt->columns = cols;
		trigStmt->whenClause = NULL;	/* passed separately */
		trigStmt->transitionRels = NIL; /* not supported at present */
		trigStmt->deferrable = trigForm->tgdeferrable;
		trigStmt->initdeferred = trigForm->tginitdeferred;
		trigStmt->constrrel = NULL; /* passed separately */

		CreateTriggerFiringOn(trigStmt, NULL, RelationGetRelid(partition),
							  trigForm->tgconstrrelid, InvalidOid, InvalidOid,
							  trigForm->tgfoid, trigForm->oid, qual,
							  false, true, trigForm->tgenabled);

		MemoryContextSwitchTo(oldcxt);
		MemoryContextReset(perTupCxt);
	}

	MemoryContextDelete(perTupCxt);

	systable_endscan(scan);
	table_close(pg_trigger, RowExclusiveLock);
}

/*
 * ALTER TABLE DETACH PARTITION
 *
 * Return the address of the relation that is no longer a partition of rel.
 *
 * If concurrent mode is requested, we run in two transactions.  A side-
 * effect is that this command cannot run in a multi-part ALTER TABLE.
 * Currently, that's enforced by the grammar.
 *
 * The strategy for concurrency is to first modify the partition's
 * pg_inherit catalog row to make it visible to everyone that the
 * partition is detached, lock the partition against writes, and commit
 * the transaction; anyone who requests the partition descriptor from
 * that point onwards has to ignore such a partition.  In a second
 * transaction, we wait until all transactions that could have seen the
 * partition as attached are gone, then we remove the rest of partition
 * metadata (pg_inherits and pg_class.relpartbounds).
 */
static ObjectAddress
ATExecDetachPartition(List **wqueue, AlteredTableInfo *tab, Relation rel,
					  RangeVar *name, bool concurrent)
{
	Relation	partRel;
	ObjectAddress address;
	Oid			defaultPartOid;

	/*
	 * We must lock the default partition, because detaching this partition
	 * will change its partition constraint.
	 */
	defaultPartOid =
		get_default_oid_from_partdesc(RelationGetPartitionDesc(rel, true));
	if (OidIsValid(defaultPartOid))
	{
		/*
		 * Concurrent detaching when a default partition exists is not
		 * supported. The main problem is that the default partition
		 * constraint would change.  And there's a definitional problem: what
		 * should happen to the tuples that are being inserted that belong to
		 * the partition being detached?  Putting them on the partition being
		 * detached would be wrong, since they'd become "lost" after the
		 * detaching completes but we cannot put them in the default partition
		 * either until we alter its partition constraint.
		 *
		 * I think we could solve this problem if we effected the constraint
		 * change before committing the first transaction.  But the lock would
		 * have to remain AEL and it would cause concurrent query planning to
		 * be blocked, so changing it that way would be even worse.
		 */
		if (concurrent)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot detach partitions concurrently when a default partition exists")));
		LockRelationOid(defaultPartOid, AccessExclusiveLock);
	}

	/*
	 * In concurrent mode, the partition is locked with share-update-exclusive
	 * in the first transaction.  This allows concurrent transactions to be
	 * doing DML to the partition.
	 */
	partRel = table_openrv(name, concurrent ? ShareUpdateExclusiveLock :
						   AccessExclusiveLock);

	/*
	 * Check inheritance conditions and either delete the pg_inherits row (in
	 * non-concurrent mode) or just set the inhdetachpending flag.
	 */
	if (!concurrent)
		RemoveInheritance(partRel, rel, false);
	else
		MarkInheritDetached(partRel, rel);

	/*
	 * Ensure that foreign keys still hold after this detach.  This keeps
	 * locks on the referencing tables, which prevents concurrent transactions
	 * from adding rows that we wouldn't see.  For this to work in concurrent
	 * mode, it is critical that the partition appears as no longer attached
	 * for the RI queries as soon as the first transaction commits.
	 */
	ATDetachCheckNoForeignKeyRefs(partRel);

	/*
	 * Concurrent mode has to work harder; first we add a new constraint to
	 * the partition that matches the partition constraint.  Then we close our
	 * existing transaction, and in a new one wait for all processes to catch
	 * up on the catalog updates we've done so far; at that point we can
	 * complete the operation.
	 */
	if (concurrent)
	{
		Oid			partrelid,
					parentrelid;
		LOCKTAG		tag;
		char	   *parentrelname;
		char	   *partrelname;

		/*
		 * Add a new constraint to the partition being detached, which
		 * supplants the partition constraint (unless there is one already).
		 */
		DetachAddConstraintIfNeeded(wqueue, partRel);

		/*
		 * We're almost done now; the only traces that remain are the
		 * pg_inherits tuple and the partition's relpartbounds.  Before we can
		 * remove those, we need to wait until all transactions that know that
		 * this is a partition are gone.
		 */

		/*
		 * Remember relation OIDs to re-acquire them later; and relation names
		 * too, for error messages if something is dropped in between.
		 */
		partrelid = RelationGetRelid(partRel);
		parentrelid = RelationGetRelid(rel);
		parentrelname = MemoryContextStrdup(PortalContext,
											RelationGetRelationName(rel));
		partrelname = MemoryContextStrdup(PortalContext,
										  RelationGetRelationName(partRel));

		/* Invalidate relcache entries for the parent -- must be before close */
		CacheInvalidateRelcache(rel);

		table_close(partRel, NoLock);
		table_close(rel, NoLock);
		tab->rel = NULL;

		/* Make updated catalog entry visible */
		PopActiveSnapshot();
		CommitTransactionCommand();

		StartTransactionCommand();

		/*
		 * Now wait.  This ensures that all queries that were planned
		 * including the partition are finished before we remove the rest of
		 * catalog entries.  We don't need or indeed want to acquire this
		 * lock, though -- that would block later queries.
		 *
		 * We don't need to concern ourselves with waiting for a lock on the
		 * partition itself, since we will acquire AccessExclusiveLock below.
		 */
		SET_LOCKTAG_RELATION(tag, MyDatabaseId, parentrelid);
		WaitForLockersMultiple(list_make1(&tag), AccessExclusiveLock, false);

		/*
		 * Now acquire locks in both relations again.  Note they may have been
		 * removed in the meantime, so care is required.
		 */
		rel = try_relation_open(parentrelid, ShareUpdateExclusiveLock);
		partRel = try_relation_open(partrelid, AccessExclusiveLock);

		/* If the relations aren't there, something bad happened; bail out */
		if (rel == NULL)
		{
			if (partRel != NULL)	/* shouldn't happen */
				elog(WARNING, "dangling partition \"%s\" remains, can't fix",
					 partrelname);
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("partitioned table \"%s\" was removed concurrently",
							parentrelname)));
		}
		if (partRel == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("partition \"%s\" was removed concurrently", partrelname)));

		tab->rel = rel;
	}

	/* Do the final part of detaching */
	DetachPartitionFinalize(rel, partRel, concurrent, defaultPartOid);

	ObjectAddressSet(address, RelationRelationId, RelationGetRelid(partRel));

	/* keep our lock until commit */
	table_close(partRel, NoLock);

	return address;
}

/*
 * Second part of ALTER TABLE .. DETACH.
 *
 * This is separate so that it can be run independently when the second
 * transaction of the concurrent algorithm fails (crash or abort).
 */
static void
DetachPartitionFinalize(Relation rel, Relation partRel, bool concurrent,
						Oid defaultPartOid)
{
	Relation	classRel;
	List	   *fks;
	ListCell   *cell;
	List	   *indexes;
	Datum		new_val[Natts_pg_class];
	bool		new_null[Natts_pg_class],
				new_repl[Natts_pg_class];
	HeapTuple	tuple,
				newtuple;
	Relation	trigrel = NULL;

	if (concurrent)
	{
		/*
		 * We can remove the pg_inherits row now. (In the non-concurrent case,
		 * this was already done).
		 */
		RemoveInheritance(partRel, rel, true);
	}

	/* Drop any triggers that were cloned on creation/attach. */
	DropClonedTriggersFromPartition(RelationGetRelid(partRel));

	/*
	 * Detach any foreign keys that are inherited.  This includes creating
	 * additional action triggers.
	 */
	fks = copyObject(RelationGetFKeyList(partRel));
	if (fks != NIL)
		trigrel = table_open(TriggerRelationId, RowExclusiveLock);
	foreach(cell, fks)
	{
		ForeignKeyCacheInfo *fk = lfirst(cell);
		HeapTuple	contup;
		Form_pg_constraint conform;
		Constraint *fkconstraint;
		Oid			insertTriggerOid,
					updateTriggerOid;

		contup = SearchSysCache1(CONSTROID, ObjectIdGetDatum(fk->conoid));
		if (!HeapTupleIsValid(contup))
			elog(ERROR, "cache lookup failed for constraint %u", fk->conoid);
		conform = (Form_pg_constraint) GETSTRUCT(contup);

		/* consider only the inherited foreign keys */
		if (conform->contype != CONSTRAINT_FOREIGN ||
			!OidIsValid(conform->conparentid))
		{
			ReleaseSysCache(contup);
			continue;
		}

		/* unset conparentid and adjust conislocal, coninhcount, etc. */
		ConstraintSetParentConstraint(fk->conoid, InvalidOid, InvalidOid);

		/*
		 * Also, look up the partition's "check" triggers corresponding to the
		 * constraint being detached and detach them from the parent triggers.
		 */
		GetForeignKeyCheckTriggers(trigrel,
								   fk->conoid, fk->confrelid, fk->conrelid,
								   &insertTriggerOid, &updateTriggerOid);
		Assert(OidIsValid(insertTriggerOid));
		TriggerSetParentTrigger(trigrel, insertTriggerOid, InvalidOid,
								RelationGetRelid(partRel));
		Assert(OidIsValid(updateTriggerOid));
		TriggerSetParentTrigger(trigrel, updateTriggerOid, InvalidOid,
								RelationGetRelid(partRel));

		/*
		 * Make the action triggers on the referenced relation.  When this was
		 * a partition the action triggers pointed to the parent rel (they
		 * still do), but now we need separate ones of our own.
		 */
		fkconstraint = makeNode(Constraint);
		fkconstraint->contype = CONSTRAINT_FOREIGN;
		fkconstraint->conname = pstrdup(NameStr(conform->conname));
		fkconstraint->deferrable = conform->condeferrable;
		fkconstraint->initdeferred = conform->condeferred;
		fkconstraint->location = -1;
		fkconstraint->pktable = NULL;
		fkconstraint->fk_attrs = NIL;
		fkconstraint->pk_attrs = NIL;
		fkconstraint->fk_matchtype = conform->confmatchtype;
		fkconstraint->fk_upd_action = conform->confupdtype;
		fkconstraint->fk_del_action = conform->confdeltype;
		fkconstraint->fk_del_set_cols = NIL;
		fkconstraint->old_conpfeqop = NIL;
		fkconstraint->old_pktable_oid = InvalidOid;
		fkconstraint->skip_validation = false;
		fkconstraint->initially_valid = true;

		createForeignKeyActionTriggers(partRel, conform->confrelid,
									   fkconstraint, fk->conoid,
									   conform->conindid,
									   InvalidOid, InvalidOid,
									   NULL, NULL);

		ReleaseSysCache(contup);
	}
	list_free_deep(fks);
	if (trigrel)
		table_close(trigrel, RowExclusiveLock);

	/*
	 * Any sub-constraints that are in the referenced-side of a larger
	 * constraint have to be removed.  This partition is no longer part of the
	 * key space of the constraint.
	 */
	foreach(cell, GetParentedForeignKeyRefs(partRel))
	{
		Oid			constrOid = lfirst_oid(cell);
		ObjectAddress constraint;

		ConstraintSetParentConstraint(constrOid, InvalidOid, InvalidOid);
		deleteDependencyRecordsForClass(ConstraintRelationId,
										constrOid,
										ConstraintRelationId,
										DEPENDENCY_INTERNAL);
		CommandCounterIncrement();

		ObjectAddressSet(constraint, ConstraintRelationId, constrOid);
		performDeletion(&constraint, DROP_RESTRICT, 0);
	}

	/* Now we can detach indexes */
	indexes = RelationGetIndexList(partRel);
	foreach(cell, indexes)
	{
		Oid			idxid = lfirst_oid(cell);
		Relation	idx;
		Oid			constrOid;

		if (!has_superclass(idxid))
			continue;

		Assert((IndexGetRelation(get_partition_parent(idxid, false), false) ==
				RelationGetRelid(rel)));

		idx = index_open(idxid, AccessExclusiveLock);
		IndexSetParentIndex(idx, InvalidOid);

		/* If there's a constraint associated with the index, detach it too */
		constrOid = get_relation_idx_constraint_oid(RelationGetRelid(partRel),
													idxid);
		if (OidIsValid(constrOid))
			ConstraintSetParentConstraint(constrOid, InvalidOid, InvalidOid);

		index_close(idx, NoLock);
	}

	/* Update pg_class tuple */
	classRel = table_open(RelationRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopy1(RELOID,
								ObjectIdGetDatum(RelationGetRelid(partRel)));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u",
			 RelationGetRelid(partRel));
	Assert(((Form_pg_class) GETSTRUCT(tuple))->relispartition);

	/* Clear relpartbound and reset relispartition */
	memset(new_val, 0, sizeof(new_val));
	memset(new_null, false, sizeof(new_null));
	memset(new_repl, false, sizeof(new_repl));
	new_val[Anum_pg_class_relpartbound - 1] = (Datum) 0;
	new_null[Anum_pg_class_relpartbound - 1] = true;
	new_repl[Anum_pg_class_relpartbound - 1] = true;
	newtuple = heap_modify_tuple(tuple, RelationGetDescr(classRel),
								 new_val, new_null, new_repl);

	((Form_pg_class) GETSTRUCT(newtuple))->relispartition = false;
	CatalogTupleUpdate(classRel, &newtuple->t_self, newtuple);
	heap_freetuple(newtuple);
	table_close(classRel, RowExclusiveLock);

	if (OidIsValid(defaultPartOid))
	{
		/*
		 * If the relation being detached is the default partition itself,
		 * remove it from the parent's pg_partitioned_table entry.
		 *
		 * If not, we must invalidate default partition's relcache entry, as
		 * in StorePartitionBound: its partition constraint depends on every
		 * other partition's partition constraint.
		 */
		if (RelationGetRelid(partRel) == defaultPartOid)
			update_default_partition_oid(RelationGetRelid(rel), InvalidOid);
		else
			CacheInvalidateRelcacheByRelid(defaultPartOid);
	}

	/*
	 * Invalidate the parent's relcache so that the partition is no longer
	 * included in its partition descriptor.
	 */
	CacheInvalidateRelcache(rel);

	/*
	 * If the partition we just detached is partitioned itself, invalidate
	 * relcache for all descendent partitions too to ensure that their
	 * rd_partcheck expression trees are rebuilt; must lock partitions before
	 * doing so, using the same lockmode as what partRel has been locked with
	 * by the caller.
	 */
	if (partRel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		List	   *children;

		children = find_all_inheritors(RelationGetRelid(partRel),
									   AccessExclusiveLock, NULL);
		foreach(cell, children)
		{
			CacheInvalidateRelcacheByRelid(lfirst_oid(cell));
		}
	}
}

/*
 * ALTER TABLE ... DETACH PARTITION ... FINALIZE
 *
 * To use when a DETACH PARTITION command previously did not run to
 * completion; this completes the detaching process.
 */
static ObjectAddress
ATExecDetachPartitionFinalize(Relation rel, RangeVar *name)
{
	Relation	partRel;
	ObjectAddress address;
	Snapshot	snap = GetActiveSnapshot();

	partRel = table_openrv(name, AccessExclusiveLock);

	/*
	 * Wait until existing snapshots are gone.  This is important if the
	 * second transaction of DETACH PARTITION CONCURRENTLY is canceled: the
	 * user could immediately run DETACH FINALIZE without actually waiting for
	 * existing transactions.  We must not complete the detach action until
	 * all such queries are complete (otherwise we would present them with an
	 * inconsistent view of catalogs).
	 */
	WaitForOlderSnapshots(snap->xmin, false);

	DetachPartitionFinalize(rel, partRel, true, InvalidOid);

	ObjectAddressSet(address, RelationRelationId, RelationGetRelid(partRel));

	table_close(partRel, NoLock);

	return address;
}

/*
 * DetachAddConstraintIfNeeded
 *		Subroutine for ATExecDetachPartition.  Create a constraint that
 *		takes the place of the partition constraint, but avoid creating
 *		a dupe if an constraint already exists which implies the needed
 *		constraint.
 */
static void
DetachAddConstraintIfNeeded(List **wqueue, Relation partRel)
{
	List	   *constraintExpr;

	constraintExpr = RelationGetPartitionQual(partRel);
	constraintExpr = (List *) eval_const_expressions(NULL, (Node *) constraintExpr);

	/* Drop any triggers that were cloned on creation/attach. */
	DropClonedTriggersFromPartition(RelationGetRelid(partRel));

	/*
	 * Avoid adding a new constraint if the needed constraint is implied by an
	 * existing constraint
	 */
	if (!PartConstraintImpliedByRelConstraint(partRel, constraintExpr))
	{
		AlteredTableInfo *tab;
		Constraint *n;

		tab = ATGetQueueEntry(wqueue, partRel);

		/* Add constraint on partition, equivalent to the partition constraint */
		n = makeNode(Constraint);
		n->contype = CONSTR_CHECK;
		n->conname = NULL;
		n->location = -1;
		n->is_no_inherit = false;
		n->raw_expr = NULL;
		n->cooked_expr = nodeToString(make_ands_explicit(constraintExpr));
		n->initially_valid = true;
		n->skip_validation = true;
		/* It's a re-add, since it nominally already exists */
		ATAddCheckConstraint(wqueue, tab, partRel, n,
							 true, false, true, ShareUpdateExclusiveLock);
	}
}

/*
 * DropClonedTriggersFromPartition
 *		subroutine for ATExecDetachPartition to remove any triggers that were
 *		cloned to the partition when it was created-as-partition or attached.
 *		This undoes what CloneRowTriggersToPartition did.
 */
static void
DropClonedTriggersFromPartition(Oid partitionId)
{
	ScanKeyData skey;
	SysScanDesc scan;
	HeapTuple	trigtup;
	Relation	tgrel;
	ObjectAddresses *objects;

	objects = new_object_addresses();

	/*
	 * Scan pg_trigger to search for all triggers on this rel.
	 */
	ScanKeyInit(&skey, Anum_pg_trigger_tgrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(partitionId));
	tgrel = table_open(TriggerRelationId, RowExclusiveLock);
	scan = systable_beginscan(tgrel, TriggerRelidNameIndexId,
							  true, NULL, 1, &skey);
	while (HeapTupleIsValid(trigtup = systable_getnext(scan)))
	{
		Form_pg_trigger pg_trigger = (Form_pg_trigger) GETSTRUCT(trigtup);
		ObjectAddress trig;

		/* Ignore triggers that weren't cloned */
		if (!OidIsValid(pg_trigger->tgparentid))
			continue;

		/*
		 * Ignore internal triggers that are implementation objects of foreign
		 * keys, because these will be detached when the foreign keys
		 * themselves are.
		 */
		if (OidIsValid(pg_trigger->tgconstrrelid))
			continue;

		/*
		 * This is ugly, but necessary: remove the dependency markings on the
		 * trigger so that it can be removed.
		 */
		deleteDependencyRecordsForClass(TriggerRelationId, pg_trigger->oid,
										TriggerRelationId,
										DEPENDENCY_PARTITION_PRI);
		deleteDependencyRecordsForClass(TriggerRelationId, pg_trigger->oid,
										RelationRelationId,
										DEPENDENCY_PARTITION_SEC);

		/* remember this trigger to remove it below */
		ObjectAddressSet(trig, TriggerRelationId, pg_trigger->oid);
		add_exact_object_address(&trig, objects);
	}

	/* make the dependency removal visible to the deletion below */
	CommandCounterIncrement();
	performMultipleDeletions(objects, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);

	/* done */
	free_object_addresses(objects);
	systable_endscan(scan);
	table_close(tgrel, RowExclusiveLock);
}

/*
 * Before acquiring lock on an index, acquire the same lock on the owning
 * table.
 */
struct AttachIndexCallbackState
{
	Oid			partitionOid;
	Oid			parentTblOid;
	bool		lockedParentTbl;
};

static void
RangeVarCallbackForAttachIndex(const RangeVar *rv, Oid relOid, Oid oldRelOid,
							   void *arg)
{
	struct AttachIndexCallbackState *state;
	Form_pg_class classform;
	HeapTuple	tuple;

	state = (struct AttachIndexCallbackState *) arg;

	if (!state->lockedParentTbl)
	{
		LockRelationOid(state->parentTblOid, AccessShareLock);
		state->lockedParentTbl = true;
	}

	/*
	 * If we previously locked some other heap, and the name we're looking up
	 * no longer refers to an index on that relation, release the now-useless
	 * lock.  XXX maybe we should do *after* we verify whether the index does
	 * not actually belong to the same relation ...
	 */
	if (relOid != oldRelOid && OidIsValid(state->partitionOid))
	{
		UnlockRelationOid(state->partitionOid, AccessShareLock);
		state->partitionOid = InvalidOid;
	}

	/* Didn't find a relation, so no need for locking or permission checks. */
	if (!OidIsValid(relOid))
		return;

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(relOid));
	if (!HeapTupleIsValid(tuple))
		return;					/* concurrently dropped, so nothing to do */
	classform = (Form_pg_class) GETSTRUCT(tuple);
	if (classform->relkind != RELKIND_PARTITIONED_INDEX &&
		classform->relkind != RELKIND_INDEX)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
				 errmsg("\"%s\" is not an index", rv->relname)));
	ReleaseSysCache(tuple);

	/*
	 * Since we need only examine the heap's tupledesc, an access share lock
	 * on it (preventing any DDL) is sufficient.
	 */
	state->partitionOid = IndexGetRelation(relOid, false);
	LockRelationOid(state->partitionOid, AccessShareLock);
}

/*
 * ALTER INDEX i1 ATTACH PARTITION i2
 */
static ObjectAddress
ATExecAttachPartitionIdx(List **wqueue, Relation parentIdx, RangeVar *name)
{
	Relation	partIdx;
	Relation	partTbl;
	Relation	parentTbl;
	ObjectAddress address;
	Oid			partIdxId;
	Oid			currParent;
	struct AttachIndexCallbackState state;

	/*
	 * We need to obtain lock on the index 'name' to modify it, but we also
	 * need to read its owning table's tuple descriptor -- so we need to lock
	 * both.  To avoid deadlocks, obtain lock on the table before doing so on
	 * the index.  Furthermore, we need to examine the parent table of the
	 * partition, so lock that one too.
	 */
	state.partitionOid = InvalidOid;
	state.parentTblOid = parentIdx->rd_index->indrelid;
	state.lockedParentTbl = false;
	partIdxId =
		RangeVarGetRelidExtended(name, AccessExclusiveLock, 0,
								 RangeVarCallbackForAttachIndex,
								 (void *) &state);
	/* Not there? */
	if (!OidIsValid(partIdxId))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("index \"%s\" does not exist", name->relname)));

	/* no deadlock risk: RangeVarGetRelidExtended already acquired the lock */
	partIdx = relation_open(partIdxId, AccessExclusiveLock);

	/* we already hold locks on both tables, so this is safe: */
	parentTbl = relation_open(parentIdx->rd_index->indrelid, AccessShareLock);
	partTbl = relation_open(partIdx->rd_index->indrelid, NoLock);

	ObjectAddressSet(address, RelationRelationId, RelationGetRelid(partIdx));

	/* Silently do nothing if already in the right state */
	currParent = partIdx->rd_rel->relispartition ?
		get_partition_parent(partIdxId, false) : InvalidOid;
	if (currParent != RelationGetRelid(parentIdx))
	{
		IndexInfo  *childInfo;
		IndexInfo  *parentInfo;
		AttrMap    *attmap;
		bool		found;
		int			i;
		PartitionDesc partDesc;
		Oid			constraintOid,
					cldConstrId = InvalidOid;

		/*
		 * If this partition already has an index attached, refuse the
		 * operation.
		 */
		refuseDupeIndexAttach(parentIdx, partIdx, partTbl);

		if (OidIsValid(currParent))
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
							RelationGetRelationName(partIdx),
							RelationGetRelationName(parentIdx)),
					 errdetail("Index \"%s\" is already attached to another index.",
							   RelationGetRelationName(partIdx))));

		/* Make sure it indexes a partition of the other index's table */
		partDesc = RelationGetPartitionDesc(parentTbl, true);
		found = false;
		for (i = 0; i < partDesc->nparts; i++)
		{
			if (partDesc->oids[i] == state.partitionOid)
			{
				found = true;
				break;
			}
		}
		if (!found)
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
							RelationGetRelationName(partIdx),
							RelationGetRelationName(parentIdx)),
					 errdetail("Index \"%s\" is not an index on any partition of table \"%s\".",
							   RelationGetRelationName(partIdx),
							   RelationGetRelationName(parentTbl))));

		/* Ensure the indexes are compatible */
		childInfo = BuildIndexInfo(partIdx);
		parentInfo = BuildIndexInfo(parentIdx);
		attmap = build_attrmap_by_name(RelationGetDescr(partTbl),
									   RelationGetDescr(parentTbl),
									   false /* yb_ignore_type_mismatch */);
		if (!CompareIndexInfo(childInfo, parentInfo,
							  partIdx->rd_indcollation,
							  parentIdx->rd_indcollation,
							  partIdx->rd_opfamily,
							  parentIdx->rd_opfamily,
							  attmap))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
					 errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
							RelationGetRelationName(partIdx),
							RelationGetRelationName(parentIdx)),
					 errdetail("The index definitions do not match.")));

		/*
		 * If there is a constraint in the parent, make sure there is one in
		 * the child too.
		 */
		constraintOid = get_relation_idx_constraint_oid(RelationGetRelid(parentTbl),
														RelationGetRelid(parentIdx));

		if (OidIsValid(constraintOid))
		{
			cldConstrId = get_relation_idx_constraint_oid(RelationGetRelid(partTbl),
														  partIdxId);
			if (!OidIsValid(cldConstrId))
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
								RelationGetRelationName(partIdx),
								RelationGetRelationName(parentIdx)),
						 errdetail("The index \"%s\" belongs to a constraint in table \"%s\" but no constraint exists for index \"%s\".",
								   RelationGetRelationName(parentIdx),
								   RelationGetRelationName(parentTbl),
								   RelationGetRelationName(partIdx))));
		}

		/* All good -- do it */
		IndexSetParentIndex(partIdx, RelationGetRelid(parentIdx));
		if (OidIsValid(constraintOid))
			ConstraintSetParentConstraint(cldConstrId, constraintOid,
										  RelationGetRelid(partTbl));

		free_attrmap(attmap);

		validatePartitionedIndex(parentIdx, parentTbl);
	}

	relation_close(parentTbl, AccessShareLock);
	/* keep these locks till commit */
	relation_close(partTbl, NoLock);
	relation_close(partIdx, NoLock);

	return address;
}

/*
 * Verify whether the given partition already contains an index attached
 * to the given partitioned index.  If so, raise an error.
 */
static void
refuseDupeIndexAttach(Relation parentIdx, Relation partIdx, Relation partitionTbl)
{
	Oid			existingIdx;

	existingIdx = index_get_partition(partitionTbl,
									  RelationGetRelid(parentIdx));
	if (OidIsValid(existingIdx))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
				 errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
						RelationGetRelationName(partIdx),
						RelationGetRelationName(parentIdx)),
				 errdetail("Another index is already attached for partition \"%s\".",
						   RelationGetRelationName(partitionTbl))));
}

/*
 * Verify whether the set of attached partition indexes to a parent index on
 * a partitioned table is complete.  If it is, mark the parent index valid.
 *
 * This should be called each time a partition index is attached.
 */
static void
validatePartitionedIndex(Relation partedIdx, Relation partedTbl)
{
	Relation	inheritsRel;
	SysScanDesc scan;
	ScanKeyData key;
	int			tuples = 0;
	HeapTuple	inhTup;
	bool		updated = false;

	Assert(partedIdx->rd_rel->relkind == RELKIND_PARTITIONED_INDEX);

	/*
	 * Scan pg_inherits for this parent index.  Count each valid index we find
	 * (verifying the pg_index entry for each), and if we reach the total
	 * amount we expect, we can mark this parent index as valid.
	 */
	inheritsRel = table_open(InheritsRelationId, AccessShareLock);
	ScanKeyInit(&key, Anum_pg_inherits_inhparent,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(partedIdx)));
	scan = systable_beginscan(inheritsRel, InheritsParentIndexId, true,
							  NULL, 1, &key);
	while ((inhTup = systable_getnext(scan)) != NULL)
	{
		Form_pg_inherits inhForm = (Form_pg_inherits) GETSTRUCT(inhTup);
		HeapTuple	indTup;
		Form_pg_index indexForm;

		indTup = SearchSysCache1(INDEXRELID,
								 ObjectIdGetDatum(inhForm->inhrelid));
		if (!HeapTupleIsValid(indTup))
			elog(ERROR, "cache lookup failed for index %u", inhForm->inhrelid);
		indexForm = (Form_pg_index) GETSTRUCT(indTup);
		if (indexForm->indisvalid)
			tuples += 1;
		ReleaseSysCache(indTup);
	}

	/* Done with pg_inherits */
	systable_endscan(scan);
	table_close(inheritsRel, AccessShareLock);

	/*
	 * If we found as many inherited indexes as the partitioned table has
	 * partitions, we're good; update pg_index to set indisvalid.
	 */
	if (tuples == RelationGetPartitionDesc(partedTbl, true)->nparts)
	{
		Relation	idxRel;
		HeapTuple	newtup;

		idxRel = table_open(IndexRelationId, RowExclusiveLock);

		newtup = heap_copytuple(partedIdx->rd_indextuple);
		((Form_pg_index) GETSTRUCT(newtup))->indisvalid = true;
		updated = true;

		CatalogTupleUpdate(idxRel, &partedIdx->rd_indextuple->t_self, newtup);

		table_close(idxRel, RowExclusiveLock);
	}

	/*
	 * If this index is in turn a partition of a larger index, validating it
	 * might cause the parent to become valid also.  Try that.
	 */
	if (updated && partedIdx->rd_rel->relispartition)
	{
		Oid			parentIdxId,
					parentTblId;
		Relation	parentIdx,
					parentTbl;

		/* make sure we see the validation we just did */
		CommandCounterIncrement();

		parentIdxId = get_partition_parent(RelationGetRelid(partedIdx), false);
		parentTblId = get_partition_parent(RelationGetRelid(partedTbl), false);
		parentIdx = relation_open(parentIdxId, AccessExclusiveLock);
		parentTbl = relation_open(parentTblId, AccessExclusiveLock);
		Assert(!parentIdx->rd_index->indisvalid);

		validatePartitionedIndex(parentIdx, parentTbl);

		relation_close(parentIdx, AccessExclusiveLock);
		relation_close(parentTbl, AccessExclusiveLock);
	}
}

/*
 * Return an OID list of constraints that reference the given relation
 * that are marked as having a parent constraints.
 */
static List *
GetParentedForeignKeyRefs(Relation partition)
{
	Relation	pg_constraint;
	HeapTuple	tuple;
	SysScanDesc scan;
	ScanKeyData key[2];
	List	   *constraints = NIL;

	/*
	 * If no indexes, or no columns are referenceable by FKs, we can avoid the
	 * scan.
	 */
	if (RelationGetIndexList(partition) == NIL ||
		bms_is_empty(RelationGetIndexAttrBitmap(partition,
												INDEX_ATTR_BITMAP_KEY)))
		return NIL;

	/* Search for constraints referencing this table */
	pg_constraint = table_open(ConstraintRelationId, AccessShareLock);
	ScanKeyInit(&key[0],
				Anum_pg_constraint_confrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(partition)));
	ScanKeyInit(&key[1],
				Anum_pg_constraint_contype, BTEqualStrategyNumber,
				F_CHAREQ, CharGetDatum(CONSTRAINT_FOREIGN));

	/* XXX This is a seqscan, as we don't have a usable index */
	scan = systable_beginscan(pg_constraint, InvalidOid, true, NULL, 2, key);
	while ((tuple = systable_getnext(scan)) != NULL)
	{
		Form_pg_constraint constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		/*
		 * We only need to process constraints that are part of larger ones.
		 */
		if (!OidIsValid(constrForm->conparentid))
			continue;

		constraints = lappend_oid(constraints, constrForm->oid);
	}

	systable_endscan(scan);
	table_close(pg_constraint, AccessShareLock);

	return constraints;
}

/*
 * During DETACH PARTITION, verify that any foreign keys pointing to the
 * partitioned table would not become invalid.  An error is raised if any
 * referenced values exist.
 */
static void
ATDetachCheckNoForeignKeyRefs(Relation partition)
{
	List	   *constraints;
	ListCell   *cell;

	constraints = GetParentedForeignKeyRefs(partition);

	foreach(cell, constraints)
	{
		Oid			constrOid = lfirst_oid(cell);
		HeapTuple	tuple;
		Form_pg_constraint constrForm;
		Relation	rel;
		Trigger		trig;

		tuple = SearchSysCache1(CONSTROID, ObjectIdGetDatum(constrOid));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for constraint %u", constrOid);
		constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		Assert(OidIsValid(constrForm->conparentid));
		Assert(constrForm->confrelid == RelationGetRelid(partition));

		/* prevent data changes into the referencing table until commit */
		rel = table_open(constrForm->conrelid, ShareLock);

		MemSet(&trig, 0, sizeof(trig));
		trig.tgoid = InvalidOid;
		trig.tgname = NameStr(constrForm->conname);
		trig.tgenabled = TRIGGER_FIRES_ON_ORIGIN;
		trig.tgisinternal = true;
		trig.tgconstrrelid = RelationGetRelid(partition);
		trig.tgconstrindid = constrForm->conindid;
		trig.tgconstraint = constrForm->oid;
		trig.tgdeferrable = false;
		trig.tginitdeferred = false;
		/* we needn't fill in remaining fields */

		RI_PartitionRemove_Check(&trig, rel, partition);

		ReleaseSysCache(tuple);

		table_close(rel, NoLock);
	}
}

/*
 * resolve column compression specification to compression method.
 */
static char
GetAttributeCompression(Oid atttypid, char *compression)
{
	char		cmethod;

	if (compression == NULL || strcmp(compression, "default") == 0)
		return InvalidCompressionMethod;

	/*
	 * To specify a nondefault method, the column data type must be toastable.
	 * Note this says nothing about whether the column's attstorage setting
	 * permits compression; we intentionally allow attstorage and
	 * attcompression to be independent.  But with a non-toastable type,
	 * attstorage could not be set to a value that would permit compression.
	 *
	 * We don't actually need to enforce this, since nothing bad would happen
	 * if attcompression were non-default; it would never be consulted.  But
	 * it seems more user-friendly to complain about a certainly-useless
	 * attempt to set the property.
	 */
	if (!TypeIsToastable(atttypid))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("column data type %s does not support compression",
						format_type_be(atttypid))));

	cmethod = CompressionNameToMethod(compression);
	if (!CompressionMethodIsValid(cmethod))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid compression method \"%s\"", compression)));

	return cmethod;
}

/*
 * Used when cloning a table for an alter statement to make copies of the old
 * relation's extended statistics objects and statistics for the new relation.
 */
static void
YbATCopyStats(Oid old_relid, RangeVar *new_rel, Oid new_relid,
			  const AttrMap *attmap, int altered_old_attnum)
{
	Relation		pg_statistic, pg_statistic_ext;
	HeapTuple		tuple;
	ScanKeyData		key;
	SysScanDesc		scan;

	/* Copy extended statistics objects. */
	pg_statistic_ext = table_open(StatisticExtRelationId, RowExclusiveLock);
	ScanKeyInit(&key, Anum_pg_statistic_ext_stxrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(old_relid));
	scan = systable_beginscan(pg_statistic_ext,
							  StatisticExtRelidIndexId,
							  true,
							  NULL,
							  1 ,
							  &key);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		CreateStatsStmt *stmt;
		HeapTuple		stat_ext_tuple;

		/*
		 * We need to rename the ext. stats object so that we can create
		 * a new one with the original name. Later, the old stats object
		 * will be dropped along with the old table.
		 */
		Form_pg_statistic_ext stat_ext_form =
			(Form_pg_statistic_ext) GETSTRUCT(tuple);
		const char *orig_stats_name =
			pstrdup(NameStr(stat_ext_form->stxname));
		const char *temp_old_stats_name =
			YbChooseExtendedStatisticName(orig_stats_name,
										  NULL /* name2 */,
										  "temp_old" /* label */,
										  stat_ext_form->stxnamespace);

		stat_ext_tuple = heap_copytuple(tuple);
		stat_ext_form = (Form_pg_statistic_ext) GETSTRUCT(stat_ext_tuple);
		namestrcpy(&(stat_ext_form->stxname), temp_old_stats_name);
		CatalogTupleUpdate(pg_statistic_ext,
						   &stat_ext_tuple->t_self, stat_ext_tuple);
		CommandCounterIncrement();

		/* Create the new ext. stats object. */
		stmt = YbGenerateClonedExtStatsStmt(new_rel, old_relid,
											stat_ext_form->oid);
		stmt->defnames = stringToQualifiedNameList(orig_stats_name);
		CreateStatistics(stmt);
	}
	systable_endscan(scan);
	table_close(pg_statistic_ext, RowExclusiveLock);

	/* Copy pg_statistic entries with updated starelid and staattnum values. */
	pg_statistic =  table_open(StatisticRelationId, RowExclusiveLock);
	ScanKeyInit(&key, Anum_pg_statistic_starelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(old_relid));
	scan = systable_beginscan(pg_statistic, StatisticRelidAttnumInhIndexId,
							  true, NULL, 1, &key);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_statistic stat_form = (Form_pg_statistic) GETSTRUCT(tuple);
		Datum		values[Natts_pg_statistic];
		bool		nulls[Natts_pg_statistic];
		bool		replaces[Natts_pg_statistic];
		HeapTuple	newtuple;

		/*
		 * If this attribute's type was changed, don't copy the pg_statistic
		 * entry because it is invalid.
		 */
		if (stat_form->staattnum == altered_old_attnum)
			continue;

		memset(values, 0, sizeof(values));
		memset(nulls, false, sizeof(nulls));
		memset(replaces, false, sizeof(replaces));

		/* Set starelid to new relation's OID. */
		values[Anum_pg_statistic_starelid - 1] = ObjectIdGetDatum(new_relid);
		replaces[Anum_pg_statistic_starelid - 1 ] = true;

		/* Set staattnum to reflect new relation's attribute numbering. */
		values[Anum_pg_statistic_staattnum - 1] =
			Int16GetDatum(attmap->attnums[stat_form->staattnum - 1]);
		replaces[Anum_pg_statistic_staattnum - 1] = true;

		newtuple = heap_modify_tuple(tuple, RelationGetDescr(pg_statistic),
									 values, nulls, replaces);

		/* Insert new pg_statistic entry. */
		CatalogTupleInsert(pg_statistic, newtuple);
		heap_freetuple(newtuple);
	}
	systable_endscan(scan);
	table_close(pg_statistic, RowExclusiveLock);
}

/*
 * Used in YB when cloning a table for an alter statement to make copies of the
 * old relation's policy objects for the new relation. This function is adapted
 * from CreatePolicy().
 */
static void
YbATCopyPolicyObjects(Relation old_rel, Relation new_rel, const AttrMap *attmap)
{
	Relation		pg_policy;
	ScanKeyData		key;
	HeapTuple		old_policy_tuple;
	SysScanDesc		scan;

	pg_policy = table_open(PolicyRelationId, RowExclusiveLock);
	ScanKeyInit(&key, Anum_pg_policy_polrelid, BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(old_rel)));
	scan = systable_beginscan(pg_policy, PolicyPolrelidPolnameIndexId,
							  true, NULL, 1, &key);

	while (HeapTupleIsValid(old_policy_tuple = systable_getnext(scan)))
	{
		Datum		values[Natts_pg_policy];
		bool		nulls[Natts_pg_policy];
		bool		replaces[Natts_pg_policy];
		bool		is_null = false;
		bool		found_whole_row  = false;
		Oid			new_policy_oid;
		HeapTuple	new_policy_tuple;
		ObjectAddress myself, target;
		Node		*qual = NULL, *with_check = NULL;
		Form_pg_policy pol_form = (Form_pg_policy) GETSTRUCT(old_policy_tuple);

		memset(values, 0, sizeof(values));
		memset(nulls, false, sizeof(nulls));
		memset(replaces, false, sizeof(replaces));

		new_policy_oid =
			GetNewOidWithIndex(pg_policy, PolicyOidIndexId, Anum_pg_policy_oid);
		values[Anum_pg_policy_oid - 1] = ObjectIdGetDatum(new_policy_oid);
		replaces[Anum_pg_policy_oid - 1] = true;

		values[Anum_pg_policy_polrelid - 1] =
			ObjectIdGetDatum(RelationGetRelid(new_rel));
		replaces[Anum_pg_policy_polrelid - 1] = true;

		Datum qual_datum = heap_getattr(old_policy_tuple,
										Anum_pg_policy_polqual,
										RelationGetDescr(pg_policy),
										&is_null);

		if (!is_null)
		{
			qual = map_variable_attnos(
				stringToNode(TextDatumGetCString(qual_datum)), 1, 0, attmap,
				RelationGetForm(new_rel)->reltype, &found_whole_row);

			/* There can never be a whole-row reference here. */
			if (found_whole_row)
				elog(ERROR, "unexpected whole-row reference found in USING"
							"clause of policy %s",
							NameStr(pol_form->polname));

			values[Anum_pg_policy_polqual - 1] =
				CStringGetTextDatum(nodeToString(qual));
			replaces[Anum_pg_policy_polqual - 1] = true;
		}
		else
			nulls[Anum_pg_policy_polqual - 1] = true;

		Datum with_check_datum = heap_getattr(old_policy_tuple,
											  Anum_pg_policy_polwithcheck,
											  RelationGetDescr(pg_policy),
											  &is_null);

		if (!is_null)
		{
			with_check = map_variable_attnos(
				stringToNode(TextDatumGetCString(with_check_datum)), 1, 0,
				attmap,
				RelationGetForm(new_rel)->reltype, &found_whole_row);

			/* There can never be a whole-row reference here. */
			if (found_whole_row)
				elog(ERROR, "unexpected whole-row reference found in "
							"WITH CHECK clause of policy %s",
							NameStr(pol_form->polname));

			values[Anum_pg_policy_polwithcheck - 1] =
				CStringGetTextDatum(nodeToString(with_check));
			replaces[Anum_pg_policy_polwithcheck - 1] = true;
		}
		else
			nulls[Anum_pg_policy_polwithcheck - 1] = true;

		new_policy_tuple = heap_modify_tuple(old_policy_tuple,
											 RelationGetDescr(pg_policy),
											 values, nulls, replaces);

		CatalogTupleInsert(pg_policy, new_policy_tuple);

		/* Record dependencies. */
		target.classId = RelationRelationId;
		target.objectId = RelationGetRelid(new_rel);
		target.objectSubId = 0;

		myself.classId = PolicyRelationId;
		myself.objectId = new_policy_oid;
		myself.objectSubId = 0;

		/*
		 * Record a dependency between the policy and the table the policy
		 * is on.
		 */
		recordDependencyOn(&myself, &target, DEPENDENCY_AUTO);

		/*
		 * Create dummy parse state and insert the target relation as its sole
		 * rangetable entry so that we can call recordDependencyOnExpr to
		 * record dependencies on the policy's qual and with_check.
		 */
		ParseState *pstate = make_parsestate(NULL);
		ParseNamespaceItem *nsitem =
			addRangeTableEntryForRelation(pstate, new_rel, AccessShareLock, NULL, false, true);
		addNSItemToQuery(pstate, nsitem, true, true, true);

		recordDependencyOnExpr(&myself, qual, pstate->p_rtable,
							   DEPENDENCY_NORMAL);
		recordDependencyOnExpr(&myself, with_check, pstate->p_rtable,
							   DEPENDENCY_NORMAL);

		Datum role_datum = heap_getattr(old_policy_tuple,
										Anum_pg_policy_polroles,
										RelationGetDescr(pg_policy),
										&is_null);
		if (!is_null)
		{
			/* Record role dependencies. */
			ArrayType *arr = DatumGetArrayTypeP(role_datum);
			Oid 	  *roles = (Oid *) ARR_DATA_PTR(arr);

			if (arr)
			{
				target.classId = AuthIdRelationId;
				target.objectSubId = 0;
				for (int i = 0; i < ARR_DIMS(arr)[0]; i++)
				{
					target.objectId = DatumGetObjectId(roles[i]);
					/* no dependency if public */
					if (target.objectId != ACL_ID_PUBLIC)
						recordSharedDependencyOn(&myself, &target,
												 SHARED_DEPENDENCY_POLICY);
				}
			}
		}
	}
	systable_endscan(scan);
	table_close(pg_policy, RowExclusiveLock);
}

/*
 * Used in YB to replace views' queries.
 */
static void
YbATReplaceViewQueries(const List *view_oids, const List *view_queries)
{
	ListCell *oid_cell, *def_cell;
	forboth(oid_cell, view_oids, def_cell, view_queries)
	{
		char *query_str = (char *) lfirst(def_cell);
		RawStmt *rawstmt =
			(RawStmt *) linitial(raw_parser(query_str, RAW_PARSE_DEFAULT));
		Query *viewParse = parse_analyze_fixedparams(rawstmt, query_str, NULL, 0, NULL);
		StoreViewQuery(lfirst_oid(oid_cell), viewParse, true);
	}
}

/*
 * Used in YB when cloning a table for an alter statement to copy some metadata
 * from the old relation to the new relation (specifically, pg_class.relacl,
 * pg_class.relrowsecurity, pg_class.reltuples, pg_attribute.attacl,
 * pg_attribute.attstattarget).
 */
static void
YbATCopyMiscMetadata(Relation old_rel, Relation new_rel, const AttrMap *attmap)
{
	Oid old_relid = RelationGetRelid(old_rel);
	Oid new_relid = RelationGetRelid(new_rel);

	/*
	 * Copy relacl, relrowsecurity and reltuples values from the old relation's
	 * pg_class tuple.
	 */
	Datum		  values[Natts_pg_class];
	bool		  nulls[Natts_pg_class];
	bool		  replaces[Natts_pg_class];
	bool		  is_null;
	Datum		  acl_datum;
	HeapTuple	  old_rel_tuple, new_rel_old_tuple, new_rel_new_tuple;
	Form_pg_class old_rel_form;
	Relation	  pg_class =
		table_open(RelationRelationId, RowExclusiveLock);

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));
	memset(replaces, false, sizeof(replaces));

	old_rel_tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(old_relid));
	old_rel_form = (Form_pg_class) GETSTRUCT(old_rel_tuple);
	new_rel_old_tuple =
		SearchSysCache1(RELOID, ObjectIdGetDatum(new_relid));

	acl_datum = SysCacheGetAttr(RELOID, old_rel_tuple,
								Anum_pg_class_relacl, &is_null);

	/* Copy relacl value. */
	if (!is_null)
	{
		values[Anum_pg_class_relacl - 1] =
			PointerGetDatum(aclcopy(DatumGetAclP(acl_datum)));
		replaces[Anum_pg_class_relacl - 1] = true;
	}
	else
		nulls[Anum_pg_class_relacl - 1] = true;

	/* Copy reltuples value. */
	values[Anum_pg_class_reltuples - 1] =
		Float4GetDatum(old_rel_form->reltuples);
	replaces[Anum_pg_class_reltuples - 1] = true;

	/* Copy relrowsecurity value. */
	values[Anum_pg_class_relrowsecurity - 1] =
		old_rel_form->relrowsecurity;
	replaces[Anum_pg_class_relrowsecurity - 1] = true;

	/* Copy relforcerowsecurity value. */
	values[Anum_pg_class_relforcerowsecurity - 1] =
		old_rel_form->relforcerowsecurity;
	replaces[Anum_pg_class_relforcerowsecurity - 1] = true;

	/* Create modified tuple with the new values. */
	new_rel_new_tuple = heap_modify_tuple(new_rel_old_tuple,
											RelationGetDescr(pg_class),
											values, nulls, replaces);

	/* Update new relation's pg_class tuple. */
	CatalogTupleUpdate(pg_class, &new_rel_new_tuple->t_self,
						new_rel_new_tuple);

	heap_freetuple(new_rel_new_tuple);
	ReleaseSysCache(new_rel_old_tuple);
	ReleaseSysCache(old_rel_tuple);
	table_close(pg_class, RowExclusiveLock);

	/*
	 * Copy attacl and attstattarget values from old relation's attributes'
	 * pg_attribute entries.
	 */
	Relation	pg_attribute =
		table_open(AttributeRelationId, RowExclusiveLock);
	for (int attno = 1; attno <= RelationGetDescr(new_rel)->natts; attno++)
	{
		Datum			  values[Natts_pg_attribute];
		bool			  nulls[Natts_pg_attribute];
		bool			  replaces[Natts_pg_attribute];
		bool			  is_null;
		Datum			  acl_datum;
		HeapTuple		  old_rel_att_tuple, new_rel_att_tuple,
							new_rel_new_att_tuple;
		Form_pg_attribute old_rel_attform;

		memset(values, 0, sizeof(values));
		memset(nulls, false, sizeof(nulls));
		memset(replaces, false, sizeof(replaces));

		old_rel_att_tuple = SearchSysCache2(
			ATTNUM,
			ObjectIdGetDatum(old_relid),
			Int16GetDatum(attmap->attnums[attno - 1]));
		old_rel_attform = (Form_pg_attribute) GETSTRUCT(old_rel_att_tuple);
		acl_datum = heap_getattr(old_rel_att_tuple,
									Anum_pg_attribute_attacl,
									RelationGetDescr(pg_attribute),
									&is_null);

		/* Copy attacl value. */
		if (!is_null)
		{
			values[Anum_pg_attribute_attacl - 1] =
				PointerGetDatum(aclcopy(DatumGetAclP(acl_datum)));
			replaces[Anum_pg_attribute_attacl - 1] = true;
		}
		else
			nulls[Anum_pg_attribute_attacl - 1] = true;

		/* Copy attstattarget value. */
		values[Anum_pg_attribute_attstattarget - 1] =
			old_rel_attform->attstattarget;
		replaces[Anum_pg_attribute_attstattarget - 1] = true;

		new_rel_att_tuple = SearchSysCache2(ATTNUM, ObjectIdGetDatum(new_relid),
											Int16GetDatum(attno));

		new_rel_new_att_tuple =
			heap_modify_tuple(new_rel_att_tuple,
								RelationGetDescr(pg_attribute),
								values, nulls, replaces);

		/* Update new relation's attribute's pg_attribute tuple. */
		CatalogTupleUpdate(pg_attribute, &new_rel_new_att_tuple->t_self,
							new_rel_new_att_tuple);

		heap_freetuple(new_rel_new_att_tuple);
		ReleaseSysCache(new_rel_att_tuple);
		ReleaseSysCache(old_rel_att_tuple);
	}
	table_close(pg_attribute, RowExclusiveLock);
}

/*
 * Check that changing the primary key of this relation using the given
 * IndexStmt is valid.
 */
static void
YbATValidateChangePrimaryKey(Relation rel, IndexStmt *stmt)
{
	Assert(IsYBRelation(rel));

	bool is_object_part_of_xrepl;
	HandleYBStatus(YBCIsObjectPartOfXRepl(MyDatabaseId,
										  YbGetRelfileNodeId(rel),
										  &is_object_part_of_xrepl));
	if (is_object_part_of_xrepl)
		ereport(ERROR,
				(errmsg("cannot change the primary key of a table that is a "
						"part of CDC or XCluster replication."),
				 errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						 "16625.")));

	/*
	 * Recreating a table will change its OID, which is not tolerable
	 * for system tables.
	 */
	if (IsCatalogRelation(rel))
		elog(ERROR, "cannot change a primary key of a system table");

	if (rel->rd_partkey != NULL || rel->rd_rel->relispartition)
		ereport(ERROR,
			   (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("changing primary key of a partitioned table is not yet "
					   "implemented"),
				errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						"16980. React with thumbs up to raise its priority")));

	if (rel->rd_rules != NULL && rel->rd_rules->numLocks > 0)
		ereport(ERROR,
			   (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("changing primary key of a table with rules is not yet "
					   "implemented"),
				errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						"16981. React with thumbs up to raise its priority")));

	/*
	 * TODO: This works as a sanity check for now, but after we support
	 * inheritance we'd need to check for the presence of actual children. If we
	 * decide to support ALTER ADD PK for inherited tables, there will be issues
	 * with inherited constraints being recreated. Also note that partitioned
	 * tables have relhassubclass set as well.
	 */
	if (rel->rd_rel->relhassubclass)
		elog(ERROR, "changing primary key of a table having children tables "
					"is not yet implemented");

	YbGetTableProperties(rel); /* Force lazy loading */

	/*
	 * If we're adding a PK, at this point we're already sure that the table
	 * has no explicit PK - meaning it's PK has to be (ybctid HASH),
	 * or (ybctid ASC) for colocated table.
	 */
	Assert(!stmt || rel->yb_table_properties->is_colocated ||
		   rel->yb_table_properties->num_hash_key_columns == 1);

	/* We should have at least one index parameter. */
	Assert(!stmt || stmt->indexParams->length > 0);
}

static RenameStmt *
YbATGetRenameStmt(const char *namespace_name, const char *current_name,
				  const char *new_name)
{
	RenameStmt *rename_stmt = makeNode(RenameStmt);
	rename_stmt->renameType = OBJECT_TABLE;
	rename_stmt->relation = makeRangeVar(
		pstrdup(namespace_name), pstrdup(current_name), -1 /* location */);
	rename_stmt->subname = NULL;
	rename_stmt->newname = pstrdup(new_name);
	rename_stmt->missing_ok = false;
	return rename_stmt;
}

static bool
YbATIsRangePk(SortByDir ordering, bool is_colocated, bool is_tablegroup)
{
	SortByDir yb_ordering =
		YbSortOrdering(ordering, is_colocated, is_tablegroup,
					   true /* is_first_key */);

	if (yb_ordering == SORTBY_ASC || yb_ordering == SORTBY_DESC)
		return true;

	return false;
}

static CreateStmt *
YbATGetCloneTableStmt(const char *namespace_name, const char *table_name,
					  const Relation rel, bool clone_split_options)
{
	TupleConstr *constr;
	HeapTuple	 tuple;
	bool		 is_null;

	constr = RelationGetDescr(rel)->constr;

	CreateStmt *create_stmt = makeNode(CreateStmt);
	create_stmt->relation = makeRangeVar(
		pstrdup(namespace_name), pstrdup(table_name), -1 /* location */);
	create_stmt->ofTypename =
		(OidIsValid(rel->rd_rel->reloftype) ?
			 makeTypeNameFromOid(rel->rd_rel->reloftype, -1 /* typmod */) :
			 NULL);
	create_stmt->tablespacename =
		get_tablespace_name(rel->rd_rel->reltablespace);
	create_stmt->tablegroupname = NULL;

	/*
	 * Initialize reloptions.
	 * Note that we're not allowed to look for reloptions in rd_rel, we have to
	 * look up the real HeapTuple.
	 */

	tuple = SearchSysCache1(RELOID, ObjectIdGetDatum(RelationGetRelid(rel)));
	Datum datum =
		SysCacheGetAttr(RELOID, tuple, Anum_pg_class_reloptions, &is_null);
	if (!is_null)
		create_stmt->options = untransformRelOptions(datum);
	ReleaseSysCache(tuple);

	const Oid tablegroup_oid = rel->yb_table_properties->tablegroup_oid;

	/*
	 * In a colocated database, tablegroups are created under the hood,
	 * so don't fill tablegroup name in the CREATE TABLE statement because the
	 * tablegroup for each colocated table is chosen by us.
	 */
	if (!MyDatabaseColocated && OidIsValid(tablegroup_oid))
	{
		create_stmt->tablegroupname = get_tablegroup_name(tablegroup_oid);
		Assert(create_stmt->tablegroupname);
	}

	if (clone_split_options)
		create_stmt->split_options = YbGetSplitOptions(rel);

	/*
	 * Set attributes and their defaults.
	 */
	AttrDefault *attrdef = constr ? constr->defval : NULL;
	for (int attno = 1; attno <= RelationGetDescr(rel)->natts; attno++)
	{
		Form_pg_attribute attr_form =
			TupleDescAttr(RelationGetDescr(rel), attno - 1);

		if (attr_form->attisdropped)
			continue;

		/*
		 * Non-default collations are not supported yet (#1127) so we can't test
		 * it. This acts as a safeguard.
		 */
		if (OidIsValid(attr_form->attcollation))
		{
			tuple =
				SearchSysCache1(TYPEOID, ObjectIdGetDatum(attr_form->atttypid));
			if (!HeapTupleIsValid(tuple))
				elog(ERROR, "cache lookup failed for type %u",
					 attr_form->atttypid);
			Form_pg_type attr_type_form = (Form_pg_type) GETSTRUCT(tuple);

			if (!YBIsCollationEnabled() &&
				attr_form->attcollation != attr_type_form->typcollation)
				elog(ERROR, "adding primary key to a table with collated "
							"columns is not yet implemented");

			ReleaseSysCache(tuple);
		}

		ColumnDef *col_def =
			makeColumnDef(NameStr(attr_form->attname), attr_form->atttypid,
						  attr_form->atttypmod, attr_form->attcollation);
		col_def->inhcount = attr_form->attinhcount;
		col_def->is_local = attr_form->attislocal;
		col_def->storage = attr_form->attstorage;
		col_def->identity = attr_form->attidentity;
		col_def->is_not_null = attr_form->attnotnull;
		for (int j = 0; j < (constr ? constr->num_defval : 0); j++)
		{
			if (attrdef[j].adnum == attno)
			{
				col_def->cooked_default = stringToNode(attrdef[j].adbin);
				break;
			}
		}
		for (int j = 0; j < attr_form->attndims; j++)
		{
			/*
			 * Individual elements of arrayBounds do not matter, see
			 * https://www.postgresql.org/docs/11/arrays.html#ARRAYS-DECLARATION
			 */
			col_def->typeName->arrayBounds =
				lappend(col_def->typeName->arrayBounds, makeInteger(-1));
		}

		create_stmt->tableElts = lappend(create_stmt->tableElts, col_def);
	}

	return create_stmt;
}

/*
 * Update a create statement to include a primary key, as specified by an index
 * statement. This function is used if there is an existing relation which the
 * create statement is trying to clone.
 */
static void
YbATAddPrimaryKeyToCreateStmt(IndexStmt *index_stmt, CreateStmt *create_stmt,
							  Relation rel)
{
	ListCell *cell;

	/*
	 * The only constraint we care about here is the PK constraint needed for
	 * YB.
	 */
	Constraint *pk_constr = makeNode(Constraint);
	pk_constr->contype = CONSTR_PRIMARY;
	pk_constr->conname = index_stmt->idxname;
	pk_constr->options = index_stmt->options;
	pk_constr->indexspace = index_stmt->tableSpace;
	foreach(cell, index_stmt->indexParams)
	{
		IndexElem *ielem = lfirst(cell);
		pk_constr->keys = lappend(pk_constr->keys, makeString(ielem->name));
		pk_constr->yb_index_params = lappend(pk_constr->yb_index_params, ielem);
	}
	create_stmt->constraints = lappend(create_stmt->constraints, pk_constr);

	ListCell *table_element = list_head(create_stmt->tableElts);
	for (int attno = 1; attno <= RelationGetDescr(rel)->natts; attno++)
	{
		Assert(table_element != NULL);

		Form_pg_attribute attr_form =
			TupleDescAttr(RelationGetDescr(rel), attno - 1);
		if (attr_form->attisdropped)
			continue;

		ColumnDef *col_def = lfirst(table_element);
		/* If this is a PK column now, is should be made non-nullable */
		foreach(cell, index_stmt ? index_stmt->indexParams : NIL)
		{
			IndexElem *ielem = lfirst(cell);
			if (strcmp(col_def->colname, ielem->name) == 0)
				col_def->is_not_null = true;
		}

		table_element = lnext(create_stmt->tableElts, table_element);
	}
}

/*
 * Execute a CreateStmt meant to clone a table, and get the attribute mappings
 * from the old table to the new table and vice versa. This function also opens
 * the new relation and sets new_relid.
 *
 * Do not clang-format this declaration because otherwise extra spaces are
 * inserted before ignore_type_mismatch.
 */
/* clang-format off */
static void
YbATCloneTableAndGetMappings(CreateStmt *create_stmt, const Relation old_rel,
							 Relation *new_rel, AttrMap **old2new_attmap,
							 AttrMap **new2old_attmap,
							 bool ignore_type_mismatch)
{

	if (!YBSuppressUnsafeAlterNotice())
		ereport(NOTICE,
				(errmsg("table rewrite may lead to inconsistencies"),
				 errdetail("Concurrent DMLs may not be reflected in the new"
						   " table."),
				 errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						 "19860. Set 'ysql_suppress_unsafe_alter_notice'"
						 " yb-tserver gflag to true to suppress this"
						 " notice.")));

	/* clang-format on */
	ObjectAddress address =
		DefineRelation(create_stmt, RELKIND_RELATION, old_rel->rd_rel->relowner,
					   NULL /* typaddress */, "" /* queryString */);
	Oid new_relid = address.objectId;
	Assert(OidIsValid(new_relid));
	*new_rel = table_open(new_relid, AccessExclusiveLock);

	*old2new_attmap = build_attrmap_by_name(
		RelationGetDescr(old_rel), RelationGetDescr(*new_rel), ignore_type_mismatch);

	*new2old_attmap = build_attrmap_by_name(
		RelationGetDescr(*new_rel), RelationGetDescr(old_rel), ignore_type_mismatch);
}

/*
 * Create a FK constraint similar to the one in the given pg_constraint
 * HeapTuple. Will use different base/FK relations, and will remap attribute
 * numbers if mapping is provided. Returns the oid of the newly created
 * constraint.
 */
static Oid
YbATCreateSimilarForeignKey(HeapTuple tuple, const char *fk_name,
							Relation base_rel, Relation fk_rel,
							const AttrMap *conkey_attmap,
							const AttrMap *confkey_attmap)
{
	Form_pg_constraint con_form = (Form_pg_constraint) GETSTRUCT(tuple);

	/* attnums of constrained columns. */
	Datum conkey_val = YBGetNotNullConstraintAttr(tuple, conkey);
	/* attnums of referenced columns. */
	Datum confkey_val= YBGetNotNullConstraintAttr(tuple, confkey);
	/* equality operators for PK = FK comparisons */
	Datum pfeqop_val = YBGetNotNullConstraintAttr(tuple, conpfeqop);
	/* equality operators for PK = PK comparisons */
	Datum ppeqop_val = YBGetNotNullConstraintAttr(tuple, conppeqop);
	/* equality operators for FK = FK comparisons */
	Datum ffeqop_val = YBGetNotNullConstraintAttr(tuple, conffeqop);

	int numkeys = ARR_DIMS(DatumGetArrayTypeP(conkey_val))[0];

	bool is_confdelsetcols_null;
	Datum confdelsetcols_val =
		SysCacheGetAttr(CONSTROID, tuple, Anum_pg_constraint_confdelsetcols,
						&is_confdelsetcols_null);

	int16 conkey[numkeys];
	int16 confkey[numkeys];
	Oid   pfeqop[numkeys];
	Oid   ppeqop[numkeys];
	Oid   ffeqop[numkeys];

	Oid index_oid;
	Oid index_opclasses[numkeys];

	int16 fkdelsetcols[numkeys];
	int numFkDeleteSetCols = 0;

	if (!is_confdelsetcols_null)
	{
		ArrayType *arr = DatumGetArrayTypeP(confdelsetcols_val);
		numFkDeleteSetCols = ARR_DIMS(arr)[0];
		memcpy(fkdelsetcols, ARR_DATA_PTR(arr),
			   numFkDeleteSetCols * sizeof(int16));
	}

	memcpy(conkey, ARR_DATA_PTR(DatumGetArrayTypeP(conkey_val)),
		   numkeys * sizeof(int16));
	memcpy(confkey, ARR_DATA_PTR(DatumGetArrayTypeP(confkey_val)),
		   numkeys * sizeof(int16));
	memcpy(pfeqop, ARR_DATA_PTR(DatumGetArrayTypeP(pfeqop_val)),
		   numkeys * sizeof(Oid));
	memcpy(ppeqop, ARR_DATA_PTR(DatumGetArrayTypeP(ppeqop_val)),
		   numkeys * sizeof(Oid));
	memcpy(ffeqop, ARR_DATA_PTR(DatumGetArrayTypeP(ffeqop_val)),
		   numkeys * sizeof(Oid));

	/* Remap the attribute numbers. */
	for (int i = 0; i < numkeys; ++i)
	{
		if (conkey_attmap)
			conkey[i]  = conkey_attmap->attnums[conkey[i] - 1];
		if (confkey_attmap)
			confkey[i] = confkey_attmap->attnums[confkey[i] - 1];
	}


	/* Look for an index matching the column list */
	index_oid = transformFkeyCheckAttrs(fk_rel, numkeys, confkey, index_opclasses);

	/* Record the FK constraint in pg_constraint. */
	Oid constr_oid = CreateConstraintEntry(
		fk_name, con_form->connamespace, CONSTRAINT_FOREIGN,
		con_form->condeferrable, con_form->condeferred, con_form->convalidated,
		con_form->conparentid, RelationGetRelid(base_rel), conkey, numkeys,
		numkeys, InvalidOid /* not a domain constraint */, index_oid,
		RelationGetRelid(fk_rel), confkey, pfeqop, ppeqop, ffeqop, numkeys,
		con_form->confupdtype, con_form->confdeltype,
		is_confdelsetcols_null ? NULL : fkdelsetcols, numFkDeleteSetCols,
		con_form->confmatchtype,
		NULL /* exclOp - not an exclusion constraint */,
		NULL /* conExpr - not a check constraint */,
		NULL /* conBin - not a check constraint */, true /* islocal */,
		0 /* inhcount */, con_form->connoinherit /* conNoInherit */,
		false /* is_internal */);

	/* Postgres no longer has this function. Need to use new Postgres's implementation. */
	Constraint* entity = makeNode(Constraint);
	entity->deferrable      = con_form->condeferrable;
	entity->initdeferred    = con_form->condeferred;
	entity->location        = -1;
	entity->skip_validation = !con_form->convalidated;
	entity->initially_valid = con_form->convalidated;
	entity->is_no_inherit   = con_form->connoinherit;
	entity->fk_matchtype    = con_form->confmatchtype;
	entity->fk_upd_action   = con_form->confupdtype;
	entity->fk_del_action   = con_form->confdeltype;

	/*
	 * Create the triggers that will enforce the constraint.
	 * Note that we also call CommandCounterIncrement().
	 */

	/*
	 * For the referenced side, create action triggers, if requested.  (If the
	 * referencing side is partitioned, there is still only one trigger, which
	 * runs on the referenced side and points to the top of the referencing
	 * hierarchy.)
	 */
	createForeignKeyActionTriggers(base_rel, RelationGetRelid(fk_rel), entity,
								   constr_oid, index_oid, InvalidOid,
								   InvalidOid, NULL, NULL);

	/*
	 * For the referencing side, create the check triggers.
	 */
	if (base_rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
	{
		Oid insertTriggerOid, updateTriggerOid;
		createForeignKeyCheckTriggers(RelationGetRelid(base_rel),
									  RelationGetRelid(fk_rel), entity,
									  constr_oid, index_oid, InvalidOid,
									  InvalidOid, &insertTriggerOid,
									  &updateTriggerOid);
	}
	return constr_oid;
}

/*
 * Validate that the given foreign key constraint's types are valid. This is
 * used if a column type is altered on base_rel or fk_rel.
 *
 * new_rel is either a clone of base_rel, or fk_rel, depending on which relation
 * had a column type altered.
 *
 * base_rel_altered denotes which relation a column type was altered on. If
 * false, then fk_rel was the relation that was altered.
 *
 * This function will throw an error for any constraint whose relevant column
 * types were altered.
 *
 * TODO(mislam): Rather than failing all altered column types, check if there is
 * a valid equality operator, as is done in ATAddForeignKeyConstraint, and error
 * out only if no such operator was found. This would allow eg. an int8 column
 * to reference an int4 foreign key.
 */
static void
YbATValidateChangeForeignKeyType(HeapTuple constraint_tuple, Relation base_rel,
								 Relation fk_rel, Relation new_rel,
								 const char *altered_column_name,
								 bool base_rel_altered)
{
	Datum conkey_val = YBGetNotNullConstraintAttr(constraint_tuple, conkey);
	int	  numkeys = ARR_DIMS(DatumGetArrayTypeP(conkey_val))[0];
	int16 conkey[numkeys];
	memcpy(conkey, ARR_DATA_PTR(DatumGetArrayTypeP(conkey_val)),
		   numkeys * sizeof(int16));

	Datum confkey_val = YBGetNotNullConstraintAttr(constraint_tuple, confkey);
	Assert(numkeys == ARR_DIMS(DatumGetArrayTypeP(confkey_val))[0]);
	int16 confkey[numkeys];
	memcpy(confkey, ARR_DATA_PTR(DatumGetArrayTypeP(confkey_val)),
		   numkeys * sizeof(int16));

	TupleDesc baseTupDesc = RelationGetDescr(base_rel);
	TupleDesc fkTupDesc = RelationGetDescr(fk_rel);
	/* For all constraints in base_rel */
	for (int i = 0; i < numkeys; ++i)
	{
		/*
		 * If base_rel was altered, compare the referring column's name with the
		 * altered column's name.
		 */
		bool rel_column_altered =
			base_rel_altered &&
			strcmp(baseTupDesc->attrs[conkey[i] - 1].attname.data,
				   altered_column_name) == 0;
		/*
		 * If base_rel was not altered, compare the referred column's name with the
		 * altered column's name.
		 */
		bool fk_rel_column_altered =
			!base_rel_altered &&
			strcmp(fkTupDesc->attrs[confkey[i] - 1].attname.data,
				   altered_column_name) == 0;
		/* If either was a match, throw an error. */
		if (rel_column_altered || fk_rel_column_altered)
		{
			ereport(ERROR,
				   (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					errmsg("Altering type of foreign key is not supported"),
					errhint("See https://github.com/yugabyte/yugabyte-db/"
							"issues/17037. React with thumbs up to raise its "
							"priority")));
		}
	}
}

/*
 * Copy FK and check constraints from one table to another. PK constraints will
 * optionally be copied as well, with the old PK constraints being renamed as we
 * go. This function also sets the provided check_constraints parameter to be
 * the list of check constraints that will be copied, for future reference. This
 * function should be used when the target relation is intended to be a clone of
 * the source relation with some changes.
 */
static void
YbATCopyFkAndCheckConstraints(const Relation old_rel, Relation new_rel,
							  Relation pg_constraint,
							  List **new_check_constraints,
							  List **new_fk_constraint_oids,
							  const AttrMap *new2old_attmap,
							  bool has_altered_column_type,
							  const char *altered_column_name,
							  bool expect_dummy_pk, bool expect_no_dummy_pk)
{
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;

	ScanKeyInit(&key, Anum_pg_constraint_conrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(old_rel)));
	scan = systable_beginscan(pg_constraint, ConstraintRelidTypidNameIndexId,
							  true /* indexOK */, NULL /* snapshot */,
							  1 /* nkeys */, &key);
	bool has_dummy_pk = false; /* Sanity check, whether dummy PK index has been
								  found. */
	List *checks_list = NIL;
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_constraint con_form = (Form_pg_constraint) GETSTRUCT(tuple);

		/*
		 * Sanity check, should never happen as we've already checked for
		 * inheritance.
		 */
		if (con_form->coninhcount > 0)
			elog(ERROR, "constraint '%s' is inherited!",
				 NameStr(con_form->conname));

		switch (con_form->contype)
		{
			case CONSTRAINT_CHECK:
			{
				Constraint *entity = NULL;
				/*
				 * An alternative would be to directly use
				 * StoreRelCheck + SetRelationNumChecks which is more
				 * straightforward and incurs slightly less overhead, but those
				 * are private to heap.c and that's probably not a good enough
				 * reason to change it.
				 */
				if (has_altered_column_type)
				{
					/*
					 * If a column type was changed, we should reparse
					 * constraints so that they expect the correct type.
					 *
					 * TODO(mislam): use memory contexts to free up space after
					 * each iteration.
					 */
					char *defstring = pg_get_constraintdef_command(
						get_relation_constraint_oid(RelationGetRelid(old_rel),
													NameStr(con_form->conname),
													false));
					List	*raw_parsetree_list = raw_parser(defstring, RAW_PARSE_DEFAULT);
					RawStmt *rs =
						lfirst_node(RawStmt, list_head(raw_parsetree_list));
					AlterTableStmt *stmt = (AlterTableStmt *) rs->stmt;
					AlterTableCmd  *cmd =
						castNode(AlterTableCmd, lfirst(list_head(stmt->cmds)));
					entity = castNode(Constraint, cmd->def);

					/*
					 * Create a dummy ParseState and insert the target relation
					 * as its sole rangetable entry.
					 */
					ParseState *pstate = make_parsestate(NULL);
					pstate->p_sourcetext = NULL;
					ParseNamespaceItem *nsitem = addRangeTableEntryForRelation(
						pstate, new_rel, AccessShareLock, NULL, false, true);
					addNSItemToQuery(pstate, nsitem, true, true, true);

					entity->cooked_expr = nodeToString(
						YbCookConstraint(pstate, entity->raw_expr,
										 RelationGetRelationName(new_rel)));
					entity->raw_expr = NULL;
				}
				else
				{
					Node *expr;
					bool  found_whole_row;
					Datum conbin_val =
						YBGetNotNullConstraintAttr(tuple, conbin);

					// NOTE: Expression diverges, locations are -1
					char *conbin = TextDatumGetCString(conbin_val);

					/*
					 * Same-named CHECK constraints on different relations do
					 * not conflict.
					 */
					entity = makeNode(Constraint);
					entity->contype = CONSTR_CHECK;
					entity->conname = NameStr(con_form->conname);
					entity->deferrable = false;
					entity->initdeferred = false;
					entity->location = -1;
					entity->skip_validation = !con_form->convalidated;
					entity->initially_valid = con_form->convalidated;
					entity->is_no_inherit = con_form->connoinherit;

					expr = (Node *) map_variable_attnos(
						stringToNode(conbin), 1 /* fromrel_varno */,
						0 /* sublevels_up */, new2old_attmap,
						RelationGetForm(new_rel)->reltype, &found_whole_row);
					if (found_whole_row)
						elog(ERROR,
							 "unexpected whole-row reference found in CHECK "
							 "constraint %s",
							 entity->conname);

					entity->raw_expr = NULL;
					entity->cooked_expr = nodeToString(expr);
				}

				Assert(entity != NULL);
				checks_list = lappend(checks_list, entity);
				break;
			}
			case CONSTRAINT_FOREIGN:
			{
				Relation fk_rel =
					table_open(con_form->confrelid, ShareRowExclusiveLock);

				if (has_altered_column_type)
				{
					/*
					 * In this context, the referencing relation has an altered
					 * column type, hence base_rel_altered is true.
					 */
					YbATValidateChangeForeignKeyType(
						tuple, old_rel, fk_rel, new_rel, altered_column_name,
						true /* base_rel_altered */);
				}

				/*
				 * Same-named FK constraints on different relations do not
				 * conflict.
				 */

				*new_fk_constraint_oids =
					lappend_oid(*new_fk_constraint_oids,
								YbATCreateSimilarForeignKey(
									tuple, NameStr(con_form->conname), new_rel,
									fk_rel, new2old_attmap, NULL));

				table_close(fk_rel, ShareRowExclusiveLock);
				break;
			}
			case CONSTRAINT_PRIMARY:
				has_dummy_pk = true;
				break;
			case CONSTRAINT_UNIQUE:
				/* UNIQUE constraints are indexes and will be copied as such. */
				break;
			case CONSTRAINT_TRIGGER:
				/* Triggers are processed separately later on. */
				break;
			case CONSTRAINT_EXCLUSION:
				/*
				 * EXCLUDE constraints are not yet implemented, see #3944 - so
				 * we can't test them.
				 */
				elog(ERROR, "adding primary key to a table with EXCLUDE "
							"constraints "
							"is not yet implemented");
				break;
			default:
				elog(ERROR, "invalid constraint type \"%c\"",
					 con_form->contype);
				break;
		}
	}
	systable_endscan(scan);

	if (expect_dummy_pk && !has_dummy_pk)
		elog(ERROR, "expected dummy primary key index to be defined");
	if (expect_no_dummy_pk && has_dummy_pk)
		elog(ERROR, "expected dummy primary key index to not be defined");

	/* We don't close pg_constraint just yet. */
	*new_check_constraints = AddRelationNewConstraints(
		new_rel, NULL /* newColDefaults - they are already in place */,
		checks_list, false /* allow_merge */, true /* is_local */,
		true /* is_internal */, NULL /* queryString - not available here */);
}

/*
 * Copy all rows from one table to the other.
 * Performs constraint checks when a column type is altered.
 *
 * This is a based on ATRewriteTable, but adopted for YB and with attribute
 * remapping, accounting for old_rel columns dropped in new_rel (we don't expect
 * any dropped columns in new_rel).
 *
 * old_rel is the relation to copy table rows from.
 * new_rel is the relation to copy rows to.
 * old2new_attmap is a mapping such that old2new_attmap[i] = j implies
 * attnum i in the new relation maps to attnum j in the old relation.
 * new2old_attmap is a mapping such that new2old_attmap[i] = j implies
 * attnum i in the old relation maps to attnum j in the new relation.
 *
 * has_altered_column_type represents whether the new relation has a different
 * type for a column.
 *
 * altered_column_name is the name of the altered column if
 * has_altered_column_type is true, NULL otherwise.
 *
 * altered_column_new_column_values encodes information about converting from
 * the old type to the new type if has_altered_column_type is true, NULL
 * otherwise.
 *
 * new_check_constraints is the list of constraints belonging to the
 * new table if has_altered_column_type is true, NULL otherwise.
 *
 * new_fk_constraint_oids is the list of oids of foreign keys belonging to the
 * new table if has_altered_column_type is true, NULL otherwise.
 */
static void
YbATCopyTableRowsUnchecked(Relation old_rel, Relation new_rel,
						   const AttrMap *old2new_attmap,
						   const AttrMap *new2old_attmap,
						   bool has_altered_column_type,
						   const List *altered_column_new_column_values,
						   const char *altered_column_name,
						   List *new_check_constraints,
						   List *new_fk_constraint_oids)
{
	TupleDesc		oldTupDesc, newTupDesc;
	Datum		   *old_values;
	bool		   *old_isnull;
	Datum		   *new_values;
	bool		   *new_isnull;
	TupleTableSlot *oldslot;
	TupleTableSlot *newslot;
	TableScanDesc	scan;
	HeapTuple		tuple;
	MemoryContext	oldcxt;
	Snapshot		snapshot;
	EState		   *estate;
	ExprContext	   *econtext;
	ListCell	   *cell;
	List		   *notnull_attrs = NIL;

	Assert(IsYBRelation(new_rel));

	estate = CreateExecutorState();
	econtext = GetPerTupleExprContext(estate);

	oldTupDesc = RelationGetDescr(old_rel);
	newTupDesc = RelationGetDescr(new_rel);

	oldslot = MakeSingleTupleTableSlot(oldTupDesc, &TTSOpsHeapTuple);
	newslot = MakeSingleTupleTableSlot(newTupDesc, &TTSOpsHeapTuple);

	/* Preallocate values/isnull arrays */
	old_values = (Datum *) palloc(oldTupDesc->natts * sizeof(Datum));
	old_isnull = (bool *) palloc(oldTupDesc->natts * sizeof(bool));
	new_values = (Datum *) palloc(newTupDesc->natts * sizeof(Datum));
	new_isnull = (bool *) palloc(newTupDesc->natts * sizeof(bool));

	foreach(cell, altered_column_new_column_values)
	{
		NewColumnValue *new_column_value = lfirst(cell);
		/*
		 * At the time the new column values expressions were created, the
		 * original attnum was used. We need to update the attnum after YB
		 * table rewrite (because attnum can change if there are dropped
		 * columns in the original relation).
		 */
		new_column_value->attnum =
			new2old_attmap->attnums[new_column_value->attnum - 1];
		/* expr already planned */
		new_column_value->exprstate =
			ExecInitExpr((Expr *) new_column_value->expr, NULL);
	}

	for (int i = 0; i < newTupDesc->natts; i++)
	{
		Form_pg_attribute attr = TupleDescAttr(newTupDesc, i);
		if (attr->attnotnull && !attr->attisdropped)
			notnull_attrs = lappend_int(notnull_attrs, i);
	}

	/*
	 * Scan through the rows, generating a new row if needed and then
	 * checking all the constraints.
	 */
	snapshot = RegisterSnapshot(GetLatestSnapshot());
	scan = heap_beginscan(old_rel, snapshot, 0, NULL, NULL, SO_TYPE_SEQSCAN);

	/*
	 * Switch to per-tuple memory context and reset it for each tuple
	 * produced, so we don't leak memory.
	 */
	oldcxt = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);

	/* The following while loop is similar to the one in ATRewriteTable. */
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{

		/* Extract data from old tuple */
		heap_deform_tuple(tuple, oldTupDesc, old_values, old_isnull);

		/* Remap the attribute numbers. */
		for (int i = 0; i < newTupDesc->natts; ++i)
		{
			if (has_altered_column_type &&
				strcmp(newTupDesc->attrs[i].attname.data,
					   altered_column_name) == 0)
			{
				/*
				 * Process supplied expressions to replace selected columns.
				 * Expression inputs come from the old tuple.
				 */
				ExecStoreHeapTuple(tuple, oldslot, false);
				econtext->ecxt_scantuple = oldslot;

				foreach(cell, altered_column_new_column_values)
				{
					NewColumnValue *ex = lfirst(cell);

					/*
					 * If there are multiple ALTER TYPE subcmds, YB does a
					 * table rewrite for each subcmd, whereas PG does a single
					 * table rewrite. The list of new column values
					 * is generated by PG code and will contain fresh values
					 * for all altered columns, so we need to filter them out.
					 */
					if (ex->attnum == newTupDesc->attrs[i].attnum)
					{
						new_values[ex->attnum - 1] = ExecEvalExpr(
							ex->exprstate, econtext,
							&new_isnull[ex->attnum - 1]);
						break;
					}
				}
			}
			else
			{
				new_values[i] = old_values[old2new_attmap->attnums[i] - 1];
				new_isnull[i] = old_isnull[old2new_attmap->attnums[i] - 1];
			}
		}

		/*
		 * Form the new tuple. Note that we don't explicitly pfree it,
		 * since the per-tuple memory context will be reset shortly.
		 */
		tuple = heap_form_tuple(newTupDesc, new_values, new_isnull);

		/* If we performed type conversions, re-check constraints */
		if (has_altered_column_type)
		{
			foreach(cell, notnull_attrs)
			{
				int			attn = lfirst_int(cell);

				if (heap_attisnull(tuple, attn + 1, newTupDesc))
				{
					Form_pg_attribute attr = TupleDescAttr(newTupDesc, attn);

					ereport(ERROR,
							(errcode(ERRCODE_NOT_NULL_VIOLATION),
							 errmsg("column \"%s\" of relation \"%s\" contains null values",
									NameStr(attr->attname),
									RelationGetRelationName(old_rel)),
							 errtablecol(old_rel, attn + 1)));
				}
			}

			foreach(cell, new_check_constraints)
			{
				CookedConstraint *constraint = lfirst(cell);
				ExprState *exprstate = ExecPrepareExpr(
					(Expr *) constraint->expr, estate);
				econtext->ecxt_scantuple = newslot;
				ExecStoreHeapTuple(tuple, newslot, false);
				if (!ExecCheck(exprstate, econtext))
					ereport(ERROR,
							(errcode(ERRCODE_CHECK_VIOLATION),
							 errmsg("check constraint \"%s\""
									" is violated by some row",
									constraint->name),
							 errtableconstraint(new_rel,
												constraint->name)));
			}

		/*
		 * TODO(fizaa): When we add support for altering the type of a foreign
		 * key column, we should check the foreign key constraints here. See
		 * https://github.com/yugabyte/yugabyte-db/issues/17037.
		 */

		}

		ExecStoreHeapTuple(tuple, newslot, false);

		/* Write the tuple out to the new relation */
		YBCExecuteInsert(new_rel, newslot, ONCONFLICT_NONE);

		MemoryContextReset(econtext->ecxt_per_tuple_memory);

		CHECK_FOR_INTERRUPTS();
	}

	/*
	 * TODO(mislam): When paritioned tables are supported, their constraints
	 * should be checked here, like they are in ATRewriteTable. See
	 * https://github.com/yugabyte/yugabyte-db/issues/16980.
	 */

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(econtext->ecxt_per_tuple_memory);

	heap_endscan(scan);
	UnregisterSnapshot(snapshot);

	ExecDropSingleTupleTableSlot(oldslot);
	ExecDropSingleTupleTableSlot(newslot);
}

/*
 * Copy indexes from one table to another by renaming the existing indexes and
 * adding new indexes with the same name to the target relation. This function
 * should be used only if the target relation is intended to be a clone of the
 * source relation with some changes.
 *
 * If new_index_addr is provided, this function will set it to be the address of
 * the primary key index of the target relation.
 */
static void
YbATCopyIndexes(Relation old_rel, Oid new_relid, const AttrMap *new2old_attmap,
				const char *temp_suffix, const Oid namespace_oid,
				RenameStmt *rename_stmt, const char *namespace_name,
				ObjectAddress *new_index_addr, const char *altered_column_name)
{
	ListCell *cell;
	List	 *idx_list = RelationGetIndexList(old_rel);
	foreach(cell, idx_list)
	{
		ObjectAddress idx_addr;
		Relation idx_rel = index_open(lfirst_oid(cell), AccessExclusiveLock);

		IndexStmt *idx_stmt = generateClonedIndexStmt(
			NULL /* heapRel, we provide an oid instead */, idx_rel,
			new2old_attmap,
			NULL /* parent constraint OID pointer */);

		/*
		* For range partitioned secondary indexes, we only clone split options
		* when the altered column (if any) is not a part of the index's range
		* key (as in this case the split options cannot be copied in
		* a straight-forward way).
		*/
		if (!idx_rel->rd_index->indisprimary)
		{
			YbGetTableProperties(idx_rel);
			if (!idx_rel->yb_table_properties->is_colocated &&
				!(YbIsColumnPartOfKey(idx_rel, altered_column_name) &&
				idx_rel->yb_table_properties->num_range_key_columns > 0 &&
				idx_rel->yb_table_properties->num_hash_key_columns == 0))
				idx_stmt->split_options = YbGetSplitOptions(idx_rel);
		}

		/*
		 * Index names on different tables conflict with each other, so
		 * we rename old indexes as we go.
		 */
		const char *idx_orig_name = pstrdup(RelationGetRelationName(idx_rel));
		const char *idx_temp_old_name = ChooseRelationName(
			idx_orig_name, NULL /* name2 */, temp_suffix /* label */,
			namespace_oid, idx_stmt->isconstraint);

		/* Free up original index name. */
		rename_stmt->relation = makeRangeVar(
			pstrdup(namespace_name), pstrdup(idx_orig_name), -1 /* location */);
		rename_stmt->newname = pstrdup(idx_temp_old_name);
		RenameRelation(rename_stmt, true /* yb_is_internal_clone_rename */);
		CommandCounterIncrement();

		/* Create a new index taking up the freed name. */
		idx_stmt->idxname = pstrdup(idx_orig_name);
		/*
		 * Do not clang-format this call because parameter comments become
		 * difficult to read.
		 */
		/* clang-format off */
		idx_addr = DefineIndex(new_relid, idx_stmt,
							   InvalidOid, /* no predefined OID */
							   InvalidOid, /* no parent index */
							   InvalidOid, /* no parent constraint */
							   false,	   /* is_alter_table */
							   false,	   /* check_rights */
							   false,	   /* check_not_in_use */
							   false,	   /* skip_build */
							   true /* quiet */);
		/* clang-format on */

		if (new_index_addr && idx_rel->rd_index->indisprimary)
			*new_index_addr = idx_addr;

		index_close(idx_rel, AccessExclusiveLock);
	}
	list_free(idx_list);
}

/*
 * Make sequences and FKs referencing old_rel refer to new_rel instead.
 *
 * FK referenced columns will be remapped according to the given attmap (if
 * any).
 *
 * FK constraints referencing the old table will be dropped and re-created
 * as the results.
 */
static void
YbATMoveRelDependencies(Relation old_rel, Relation new_rel, Relation pg_depend,
						Relation pg_constraint, bool has_altered_column_type,
						const char *altered_column_name, const AttrMap *attmap)
{
	ScanKeyData key[2];
	SysScanDesc scan;
	HeapTuple	dep_tuple, con_tuple;
	ObjectAddresses *cons_to_drop;

	/* Move sequences dependencies. */
	ScanKeyInit(&key[0], Anum_pg_depend_refclassid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationRelationId));
	ScanKeyInit(&key[1], Anum_pg_depend_refobjid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(old_rel)));
	scan = systable_beginscan(pg_depend, DependReferenceIndexId,
							  true /* indexOK */, NULL /* snapshot */,
							  2 /* nkeys */, key);

	while (HeapTupleIsValid(dep_tuple = systable_getnext(scan)))
	{
		Form_pg_depend dep_form = (Form_pg_depend) GETSTRUCT(dep_tuple);

		if (get_rel_relkind(dep_form->objid) != RELKIND_SEQUENCE)
			continue;

		/* make a modifiable copy */
		dep_tuple = heap_copytuple(dep_tuple);
		dep_form = (Form_pg_depend) GETSTRUCT(dep_tuple);
		dep_form->refobjid = RelationGetRelid(new_rel);

		CatalogTupleUpdate(pg_depend, &dep_tuple->t_self, dep_tuple);

		heap_freetuple(dep_tuple);
	}
	systable_endscan(scan);
	CommandCounterIncrement();

	/* Move FKs referencing old_rel. */
	ScanKeyInit(&key[0], Anum_pg_constraint_confrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(old_rel)));
	ScanKeyInit(&key[1], Anum_pg_constraint_contype, BTEqualStrategyNumber,
				F_OIDEQ, CharGetDatum(CONSTRAINT_FOREIGN));
	scan = systable_beginscan(pg_constraint, InvalidOid /* no index */,
							  true /* indexOK */, NULL /* snapshot */,
							  2 /* nkeys */, key);
	cons_to_drop = new_object_addresses();
	while (HeapTupleIsValid(con_tuple = systable_getnext(scan)))
	{
		Form_pg_constraint con_form = (Form_pg_constraint) GETSTRUCT(con_tuple);
		ObjectAddress con_addr;
		/*
		 * We need to rename this FK constraint and create a new one in its
		 * stead. Dropping old constraints will be postponed until the end of
		 * the scan.
		 */

		const char *con_origname = pstrdup(NameStr(con_form->conname));
		const char *con_tempname = ChooseConstraintName(
			NameStr(con_form->conname), NULL /* name2 */,
			"temp_old" /* label */, con_form->connamespace, NIL /* others */);

		con_tuple = heap_copytuple(con_tuple);
		con_form = (Form_pg_constraint) GETSTRUCT(con_tuple);
		namestrcpy(&(con_form->conname), con_tempname);
		CatalogTupleUpdate(pg_constraint, &con_tuple->t_self, con_tuple);
		CommandCounterIncrement();

		ObjectAddressSet(con_addr, ConstraintRelationId,
						 con_form->oid);
		add_exact_object_address(&con_addr, cons_to_drop);

		/*
		 * We don't need AccessExclusiveLock since old constraint
		 * can't be violated at any point.
		 */
		Relation base_rel =
			table_open(con_form->conrelid, ShareUpdateExclusiveLock);

		if (has_altered_column_type)
		{
			/*
			 * In this context, the relation that was referenced has an altered
			 * column type, hence base_rel_altered is false.
			 */
			YbATValidateChangeForeignKeyType(con_tuple, base_rel, old_rel,
											 new_rel, altered_column_name,
											 false /* base_rel_altered */);
		}

		YbATCreateSimilarForeignKey(con_tuple, con_origname, base_rel, new_rel,
									NULL, attmap);

		heap_freetuple(con_tuple);
		table_close(base_rel, ShareUpdateExclusiveLock);
	}
	systable_endscan(scan);

	performMultipleDeletions(cons_to_drop, DROP_RESTRICT,
							 PERFORM_DELETION_INTERNAL);
}

static void
YbATCopyTriggers(const Relation old_rel, const Relation new_rel,
				 Relation pg_trigger, const AttrMap *new2old_attmap)
{
	ScanKeyData	  key;
	SysScanDesc	  scan;
	MemoryContext oldcxt, per_tup_cxt;
	HeapTuple	  tuple;

	ScanKeyInit(&key, Anum_pg_trigger_tgrelid, BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(old_rel)));
	scan = systable_beginscan(pg_trigger, TriggerRelidNameIndexId,
							  true /* indexOK */, NULL /* snapshot */,
							  1 /* nkeys */, &key);

	per_tup_cxt = AllocSetContextCreate(GetCurrentMemoryContext(),
										"copy triggers", ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(per_tup_cxt);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_trigger trig_form = (Form_pg_trigger) GETSTRUCT(tuple);
		CreateTrigStmt *trig_stmt;
		Node		   *qual = NULL;
		Datum           value;
		bool            isnull;
		List		   *cols = NIL;

		/*
		 * Don't copy internal triggers as they are constraint-related
		 * and have already been copied.
		 */
		if (trig_form->tgisinternal)
			continue;

		/*
		 * If there is a WHEN clause, generate a 'cooked' version of it that's
		 * appropriate for the new attnums.
		 */
		value = heap_getattr(tuple, Anum_pg_trigger_tgqual,
							 RelationGetDescr(pg_trigger), &isnull);
		if (!isnull)
		{
			bool found_whole_row;
			qual = stringToNode(TextDatumGetCString(value));
			/* 'OLD' is guaranteed to have varno equal to 1 and 'NEW' equal to 2. */
			for (int fromrel_varno = 1; fromrel_varno <= 2; ++fromrel_varno)
			{
				qual = (Node *) map_variable_attnos(qual,
													fromrel_varno,
													0 /* sublevels_up */,
													new2old_attmap,
													RelationGetForm(new_rel)->reltype,
													&found_whole_row);
				if (found_whole_row)
					elog(ERROR,
						 "unexpected whole-row reference found in WHEN clause "
						 "of trigger %s",
						 NameStr(trig_form->tgname));
			}
		}

		/*
		 * If there is a column list, transform it to a list of column names.
		 */
		for (int i = 0; i < trig_form->tgattr.dim1; i++)
		{
			AttrNumber attnum = trig_form->tgattr.values[i];
			Form_pg_attribute col_form =
				TupleDescAttr(old_rel->rd_att, attnum - 1);
			cols = lappend(cols, makeString(pstrdup(NameStr(col_form->attname))));
		}

		/* Same-named triggers on different relations do not conflict. */
		trig_stmt = makeNode(CreateTrigStmt);
		trig_stmt->trigname       = NameStr(trig_form->tgname);
		trig_stmt->relation       = NULL; /* passed separately (as OID) */
		trig_stmt->funcname       = NULL; /* passed separately */
		trig_stmt->args           = NULL; /* no args for trigger funcs */
		trig_stmt->row            = TRIGGER_FOR_ROW(trig_form->tgtype);
		trig_stmt->timing         = trig_form->tgtype & TRIGGER_TYPE_TIMING_MASK;
		trig_stmt->events         = trig_form->tgtype & TRIGGER_TYPE_EVENT_MASK;
		trig_stmt->columns        = cols;
		trig_stmt->whenClause     = NULL; /* passed separately */
		trig_stmt->isconstraint   = OidIsValid(trig_form->tgconstraint);
		trig_stmt->deferrable     = trig_form->tgdeferrable;
		trig_stmt->initdeferred   = trig_form->tginitdeferred;
		trig_stmt->constrrel      = NULL; /* passed separately */

		CreateTrigger(trig_stmt, NULL /* queryString */,
					  RelationGetRelid(new_rel), InvalidOid /* refRelOid */,
					  InvalidOid /* constraintOid */, InvalidOid /* indexOid */,
					  trig_form->tgfoid, InvalidOid /* parentTriggerOid */,
					  qual, false /* isInternal */, false /* in_partition */);

		MemoryContextReset(per_tup_cxt);
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(per_tup_cxt);

	systable_endscan(scan);
}

/*
 * Copy data and metadata from one table to another. This should be used when
 * the target relation is intended to be a clone of the source relation with
 * some changes. The target relation should already be created, and have the
 * desired schema.
 *
 * Metadata includes FK and check constraints, indexes, dependencies, triggers,
 * stats, policies, and misc metadata. Data includes all rows of the table.
 *
 * expect_dummy_pk and expect_no_dummy_pk are used for asserting whether a PK
 * exists in the old relation or not.
 *
 * If new_index_addr is provided, this function will set it to be the address of
 * the primary key index of the target relation. If index_stmt is provided, the
 * validity of new_index_addr will be checked.
 */
static void
YbATCopyMetadataAndData(Relation old_rel, Relation new_rel,
						Relation pg_constraint, const AttrMap *old2new_attmap,
						const AttrMap *new2old_attmap, bool expect_dummy_pk,
						bool expect_no_dummy_pk, bool has_altered_column_type,
						const List *altered_column_new_column_values,
						const char *altered_column_name,
						const char *temp_suffix, const Oid namespace_oid,
						RenameStmt *rename_stmt, const char *namespace_name,
						ObjectAddress	*new_index_addr, bool dropping_pk,
						const IndexStmt *index_stmt, const char *old_table_name,
						const List *view_oids, const List *view_queries)
{
	Relation pg_trigger, pg_depend;
	List	*new_check_constraints = NIL;
	List	*new_fk_constraint_oids = NIL;
	int		altered_old_attnum = 0;

	YbATCopyFkAndCheckConstraints(old_rel, new_rel, pg_constraint,
								  &new_check_constraints,
								  &new_fk_constraint_oids, new2old_attmap,
								  has_altered_column_type, altered_column_name,
								  expect_dummy_pk, expect_no_dummy_pk);

	/* Make caches changes visible. */
	CommandCounterIncrement();

	/*
	 * Copy table content.
	 */
	YbATCopyTableRowsUnchecked(old_rel, new_rel, old2new_attmap,
							   new2old_attmap, has_altered_column_type,
							   altered_column_new_column_values,
							   altered_column_name, new_check_constraints,
							   new_fk_constraint_oids);
	list_free(new_check_constraints);

	/*
	 * Copy indexes (including constraint indexes).
	 *
	 * We're doing this after data migration to not bother populating
	 * indexes manually.
	 */
	pg_depend = table_open(DependRelationId, RowExclusiveLock);
	YbATCopyIndexes(old_rel, RelationGetRelid(new_rel), new2old_attmap,
					temp_suffix, namespace_oid, rename_stmt, namespace_name,
					new_index_addr, altered_column_name);
	/*
	 * Either we're not changing indexes (new_index_addr passed in was null),
	 * or otherwise new_index_addr is valid or invalid depending on whether
	 * we are adding an index or not.
	 */
	Assert((new_index_addr == NULL) ||
		   (dropping_pk ^ OidIsValid(new_index_addr->objectId)));

	/*
	 * Migrate dependencies: owned sequences and external FK constraints.
	 */
	YbATMoveRelDependencies(old_rel, new_rel, pg_depend, pg_constraint,
							has_altered_column_type, altered_column_name,
							new2old_attmap);
	table_close(pg_depend, RowExclusiveLock);

	/*
	 * Copy triggers.
	 */
	pg_trigger = table_open(TriggerRelationId, RowExclusiveLock);
	YbATCopyTriggers(old_rel, new_rel, pg_trigger, new2old_attmap);
	table_close(pg_trigger, RowExclusiveLock);

	/*
	 * Update pg_statistic and pg_statistic_ext entries.
	 */
	RangeVar *new_rel_rangevar = makeRangeVar(
		pstrdup(namespace_name), pstrdup(old_table_name), -1 /* location */);

	if (has_altered_column_type)
	{
		HeapTuple attTup = SearchSysCacheAttName(RelationGetRelid(old_rel),
												 altered_column_name);
		Assert(HeapTupleIsValid(attTup));
		altered_old_attnum = ((Form_pg_attribute) GETSTRUCT(attTup))->attnum;
		ReleaseSysCache(attTup);
	}

	YbATCopyStats(RelationGetRelid(old_rel), new_rel_rangevar,
				  RelationGetRelid(new_rel), new2old_attmap,
				  altered_old_attnum);

	/*
	 * Copy policy objects.
	 */
	YbATCopyPolicyObjects(old_rel, new_rel, new2old_attmap);

	/*
	 * Update views' and materialized views' rules to reference the new table.
	 */
	YbATReplaceViewQueries(view_oids, view_queries);

	/*
	 * Copy pg_class and pg_attribute metadata.
	 */
	YbATCopyMiscMetadata(old_rel, new_rel, old2new_attmap);
}

static void
YbATDropTable(const char *namespace_name, const char *table_name)
{
	DropStmt *drop_stmt = makeNode(DropStmt);
	drop_stmt->removeType = OBJECT_TABLE;
	drop_stmt->missing_ok = false;
	drop_stmt->objects = list_make1(list_make2(
		makeString(pstrdup(namespace_name)), makeString(pstrdup(table_name))));
	drop_stmt->behavior = DROP_CASCADE;
	drop_stmt->concurrent = false;
	RemoveRelations(drop_stmt);
}

/*
 * Primary key is an inherent part of a DocDB table, we can't literally "add"
 * or "drop" a primary key of an existing table.
 *
 * As a workaround, we create a new table with the desired schema and replace
 * the old table with it.
 *
 * If result_addr is not NULL, it will contain an address of the new primary
 * key (dummy) index, or InvalidObjectAddress if primary key has been dropped.
 *
 * Returns a new relation representing a recreated table.
 */
static Relation
YbATCloneRelationSetPrimaryKey(Relation old_rel, IndexStmt *stmt,
							   ObjectAddress *result_addr)
{
	CreateStmt *create_stmt;
	RenameStmt *rename_stmt;
	Relation	new_rel = NULL;
	AttrMap	   *old2new_attmap = NULL;
	AttrMap	   *new2old_attmap = NULL;
	bool		is_range_pk = false;

	List *view_oids = NIL, *view_queries = NIL;

	YbATValidateChangePrimaryKey(old_rel, stmt);

	const Oid	namespace_oid = RelationGetNamespace(old_rel);
	const char *namespace_name = get_namespace_name(namespace_oid);

	const char *temp_old_suffix = "temp_old";

	const char *orig_table_name = pstrdup(RelationGetRelationName(old_rel));
	const char *temp_old_table_name = ChooseRelationName(
		orig_table_name, NULL /* name2 */, temp_old_suffix /* label */,
		namespace_oid, false /* isconstraint */);

	/* Get dependent views' queries before we rename the table. */
	yb_get_dependent_views(RelationGetRelid(old_rel), &view_oids,
						   &view_queries);

	/*
	 * PHASE 1
	 * -------
	 * Rename the old table to free up the name.
	 */
	rename_stmt =
		YbATGetRenameStmt(namespace_name, orig_table_name, temp_old_table_name);
	RenameRelation(rename_stmt, true /* yb_is_internal_clone_rename */);

	/* Make caches changes visible. */
	CommandCounterIncrement();

	/*
	 * PHASE 2
	 * -------
	 * Create a replacement table with a correct PK.
	 */

	/*
	 * Previous calls to CommandCounterIncrement have discarded
	 * yb_table_properties, so we fetch it again.
	 */
	YbGetTableProperties(old_rel);

	if (stmt)
		is_range_pk = YbATIsRangePk(
			linitial_node(IndexElem, stmt->indexParams)->ordering,
			old_rel->yb_table_properties->is_colocated,
			OidIsValid(old_rel->yb_table_properties->tablegroup_oid));

	/*
	 * Prepare a statement to clone the old relation. Do not clang-format this
	 * because otherwise the boolean becomes misaligned.
	 */
	/* clang-format off */
	create_stmt =
		YbATGetCloneTableStmt(namespace_name, orig_table_name, old_rel,
							  !old_rel->yb_table_properties->is_colocated &&
							  !is_range_pk /* clone_split_options */);
	/* clang-format on */

	if (stmt)
		YbATAddPrimaryKeyToCreateStmt(stmt, create_stmt, old_rel);

	YbATCloneTableAndGetMappings(create_stmt, old_rel, &new_rel,
								 &old2new_attmap, &new2old_attmap,
								 false /* yb_ignore_type_mismatch */);

	Relation pg_constraint = table_open(ConstraintRelationId, RowExclusiveLock);
	/*
	 * PHASE 3
	 * -------
	 * Copy table metadata and data from the old table to the new table.
	 */
	YbATCopyMetadataAndData(old_rel, new_rel, pg_constraint, old2new_attmap,
							new2old_attmap, stmt != NULL /* expect_dummy_pk */,
							stmt == NULL /* expect_no_dummy_pk */,
							false /* has_altered_column_type */,
							NULL /* altered_column_new_column_value */,
							NULL /* altered_column_name */, temp_old_suffix,
							namespace_oid, rename_stmt, namespace_name,
							result_addr, stmt == NULL /* dropping_pk */, stmt, orig_table_name, view_oids,
							view_queries);
	table_close(pg_constraint, RowExclusiveLock);

	/*
	 * PHASE 4
	 * -------
	 * Drop the old table.
	 *
	 * This will drop everything associated with it except sequences
	 * (which we migrated) and external FK constraints referencing it
	 * (which we already dropped in phase 6).
	 */

	/*
	 * "Close" a relation (decrement the refcount) to allow removing it, and do
	 * so.
	 */
	RelationClose(old_rel);

	YbATDropTable(namespace_name, temp_old_table_name);

	return new_rel;
}

/*
 * Update a particular column type in a create stmt. Specifically, update the
 * collation id and type name for the column with the specified name. This
 * function should be used when there is an existing relation which the create
 * stmt is intended to clone, with some changes.
 */
static void
YbATUpdateColumnTypeForCreateStmt(CreateStmt *create_stmt, Relation rel,
								  const char *altered_column_name,
								  Oid altered_collation_id,
								  TypeName *altered_type_name)
{
	ListCell *table_element = list_head(create_stmt->tableElts);
	/* For each column in the table. */
	for (int attno = 1; attno <= RelationGetDescr(rel)->natts; attno++)
	{
		Assert(table_element != NULL);

		Form_pg_attribute attr_form =
			TupleDescAttr(RelationGetDescr(rel), attno - 1);
		if (attr_form->attisdropped)
			continue;

		ColumnDef *col_def = lfirst(table_element);
		/* Update the column if its type was altered */
		if (strcmp(col_def->colname, altered_column_name) == 0)
		{
			col_def->typeName = altered_type_name;
			col_def->collOid = altered_collation_id;
			break;
		}

		table_element = lnext(create_stmt->tableElts, table_element);
	}
}

/*
 * Validate that altering the column type of the provided relation is valid.
 */
static void
YbATValidateAlterColumnType(Relation rel)
{
	Assert(IsYBRelation(rel));

	bool is_object_part_of_xrepl;
	HandleYBStatus(YBCIsObjectPartOfXRepl(MyDatabaseId,
										  YbGetRelfileNodeId(rel),
										  &is_object_part_of_xrepl));
	if (is_object_part_of_xrepl)
		ereport(ERROR,
				(errmsg("cannot change a column type of a table that is a "
						"part of CDC or XCluster replication."),
				 errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						 "16625.")));

	/*
	 * Recreating a table will change its OID, which is not tolerable
	 * for system tables.
	 */
	if (IsCatalogRelation(rel))
		elog(ERROR, "cannot change a column type of a system table");

	if (rel->rd_partkey != NULL || rel->rd_rel->relispartition)
		ereport(ERROR,
			   (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("changing column type of a partitioned table is not yet "
					   "implemented"),
				errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						"16980. React with thumbs up to raise its priority")));

	if (rel->rd_rules != NULL && rel->rd_rules->numLocks > 0)
		ereport(ERROR,
			   (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("changing column type of a table with rules is not yet "
					   "implemented"),
				errhint("See https://github.com/yugabyte/yugabyte-db/issues/"
						"16981. React with thumbs up to raise its priority")));

	/*
	 * TODO: This works as a sanity check for now, but after we support
	 * inheritance we'd need to check for the presence of actual children. If we
	 * decide to support alter column type for inherited tables, there will be
	 * issues with inherited constraints being recreated. Also note that
	 * partitioned tables have relhassubclass set as well.
	 */
	if (rel->rd_rel->relhassubclass)
		elog(ERROR, "changing primary key of a table having children tables "
					"is not yet implemented");
}

/*
 * Rather than altering the column on the existing table, we will create a new
 * table with the updated column type
 *
 * Returns a new relation representing a recreated table.
 */
static Relation
YbATCloneRelationSetColumnType(Relation old_rel,
							   const char *altered_column_name,
							   Oid altered_collation_id,
							   TypeName *altered_type_name,
							   List *new_column_values)
{
	CreateStmt	*create_stmt;
	RenameStmt	*rename_stmt;
	Relation	 new_rel = NULL;
	AttrMap 	*old2new_attmap;
	AttrMap 	*new2old_attmap;
	Oid			 old_relid;

	List *view_oids = NIL, *view_queries = NIL;

	YbATValidateAlterColumnType(old_rel);

	old_relid = RelationGetRelid(old_rel);

	const Oid	namespace_oid = RelationGetNamespace(old_rel);
	const char *namespace_name = get_namespace_name(namespace_oid);

	const char *temp_old_suffix = "temp_old";

	const char *orig_table_name = pstrdup(RelationGetRelationName(old_rel));
	const char *temp_old_table_name = ChooseRelationName(
		orig_table_name, NULL /* name2 */, temp_old_suffix /* label */,
		namespace_oid, false /* isconstraint */);

	/* Get dependent views' queries before we rename the table. */
	yb_get_dependent_views(old_relid, &view_oids, &view_queries);

	/*
	 * PHASE 1
	 * -------
	 * Rename the old table to free up the name.
	 */
	rename_stmt =
		YbATGetRenameStmt(namespace_name, orig_table_name, temp_old_table_name);
	RenameRelation(rename_stmt, true /* yb_is_internal_clone_rename */);

	/* Make caches changes visible. */
	CommandCounterIncrement();

	/*
	 * PHASE 2
	 * -------
	 * Create a replacement table with the updated column type.
	 */

	YbGetTableProperties(old_rel); /* Force lazy loading */

	/*
	 * Prepare a statement to clone the old relation.
	 */

	/*
	 * For range partitioned tables, we only clone split options
	 * when the altered column (if any) is not a part of the table's range key
	 * (as in this case the split options cannot be copied in
	 * a straight-forward way).
	 */
	bool clone_split_options =
		!old_rel->yb_table_properties->is_colocated &&
		!(YbIsColumnPartOfKey(old_rel, altered_column_name) &&
		  old_rel->yb_table_properties->num_range_key_columns > 0);
	create_stmt = YbATGetCloneTableStmt(
		namespace_name, orig_table_name, old_rel, clone_split_options);

	YbATUpdateColumnTypeForCreateStmt(create_stmt, old_rel, altered_column_name,
									  altered_collation_id, altered_type_name);

	/*
	 * Copy the primary key to the create stmt before creating the table because
	 * copying the PK constraint after creating the table will not enforce
	 * uniqueness because the table in DocDB is already using ybctid as the
	 * primary index.
	 */
	Relation pg_constraint = table_open(ConstraintRelationId, RowExclusiveLock);
	YbATCopyPrimaryKeyToCreateStmt(old_rel, pg_constraint, create_stmt);

	/*
	 * Create an altered table and open it.
	 */
	YbATCloneTableAndGetMappings(create_stmt, old_rel, &new_rel,
								 &old2new_attmap, &new2old_attmap,
								 true /* yb_ignore_type_mismatch */);

	/*
	 * PHASE 3
	 * -------
	 * Copy table metadata and data from the old table to the new table.
	 */
	YbATCopyMetadataAndData(
		old_rel, new_rel, pg_constraint, old2new_attmap, new2old_attmap,
		false /* expect_dummy_pk */, false /* expect_no_dummy_pk */,
		true /* has_altered_column_type */, new_column_values,
		altered_column_name, temp_old_suffix, namespace_oid, rename_stmt,
		namespace_name, NULL /* new_index_addr */, false /* dropping_pk */, NULL /* index_stmt */,
		orig_table_name, view_oids, view_queries);
	table_close(pg_constraint, RowExclusiveLock);

	/*
	 * PHASE 4
	 * -------
	 * Drop the old table.
	 *
	 * This will drop everything associated with it except sequences
	 * (which we migrated) and external FK constraints referencing it
	 * (which we already dropped in phase 6).
	 */
	RelationClose(old_rel);
	YbATDropTable(namespace_name, temp_old_table_name);

	return new_rel;
}

/*
 * Used in YB during the ADD/DROP primary key operation to add the children of
 * a partitioned table to the ALTER TABLE work queue.
 * The children will be rewritten in Phase 3 (ATRewriteTables).
 */
static void YbATSetPKRewriteChildPartitions(List **yb_wqueue,
											AlteredTableInfo *tab,
											bool skip_copy_split_options)
{
	Assert(yb_wqueue);
	List *children = find_all_inheritors(tab->relid, NoLock, NULL);
	ListCell *child;
	foreach(child, children)
	{
		Oid			childrelid = lfirst_oid(child);
		Relation	partition = table_open(childrelid, NoLock);
		AlteredTableInfo *childtab;
		/* Find or create work queue entry for this partition */
		childtab = ATGetQueueEntry(yb_wqueue, partition);
		childtab->rewrite |= YB_AT_REWRITE_ALTER_PRIMARY_KEY;
		table_close(partition, NoLock);
		childtab->yb_skip_copy_split_options =
			childtab->yb_skip_copy_split_options
			|| skip_copy_split_options;
	}
}

/*
 * Used in YB during the ALTER TYPE operation to copy split options for
 * affected indexes.
 */
static void YbATCopyIndexSplitOptions(Oid oldId, IndexStmt *stmt,
									  AlteredTableInfo *tab)
{
	Relation idx_rel = RelationIdGetRelation(oldId);
	if (IsYBRelation(idx_rel) && !idx_rel->rd_index->indisprimary)
	{
		ListCell   *lcmd;
		bool yb_copy_split_options = true;
		YbGetTableProperties(idx_rel);
		/*
		 * If the index has a range key, omit copying the split
		 * options if any of the altered columns are a part of the
		 * index's key.
		 */
		if (idx_rel->yb_table_properties->num_range_key_columns > 0
			&& idx_rel->yb_table_properties->num_hash_key_columns == 0)
		{
			foreach(lcmd, tab->subcmds[AT_PASS_ALTER_TYPE])
			{
				AlterTableCmd *cmd =
					castNode(AlterTableCmd, lfirst(lcmd));
				if (YbIsColumnPartOfKey(idx_rel, cmd->name))
					yb_copy_split_options = false;
			}
		}
		if (yb_copy_split_options)
			stmt->split_options = YbGetSplitOptions(idx_rel);
		RelationClose(idx_rel);
	}
}

/*
 * Used in YB to re-invalidate table cache entries at the end of an ALTER TABLE
 * operation.
 */
static void YbATInvalidateTableCacheAfterAlter(List *ybAlteredTableIds)
{
	if (YbDdlRollbackEnabled() && ybAlteredTableIds)
	{
		/*
		 * As part of DDL transaction verification, we may have incremented
		 * the schema version for the affected tables. So, re-invalidate
		 * the table cache entries of the affected tables.
		 */
		ListCell *lc = NULL;
		foreach(lc, ybAlteredTableIds)
		{
			Oid relid = lfirst_oid(lc);
			Relation rel = RelationIdGetRelation(relid);
			/*
			 * The relation may no longer exist if it was dropped as part of
			 * a legacy rewrite operation. We can skip invalidation in that
			 * case.
			 */
			if (!rel)
			{
				Assert(!yb_enable_alter_table_rewrite);
				continue;
			}
			YBCPgAlterTableInvalidateTableByOid(YBCGetDatabaseOidByRelid(relid),
				YbGetRelfileNodeIdFromRelId(relid));
			RelationClose(rel);
		}
	}
}
