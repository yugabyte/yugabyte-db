/*-------------------------------------------------------------------------
 *
 * tablecmds.c
 *	  Commands for creating and altering table structures and settings
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
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

#include "access/genam.h"
#include "access/heapam.h"
#include "access/heapam_xlog.h"
#include "access/multixact.h"
#include "access/reloptions.h"
#include "access/relscan.h"
#include "access/sysattr.h"
#include "access/tupconvert.h"
#include "access/xact.h"
#include "access/xlog.h"
#include "catalog/catalog.h"
#include "catalog/dependency.h"
#include "catalog/heap.h"
#include "catalog/index.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/objectaccess.h"
#include "catalog/partition.h"
#include "catalog/pg_am.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_depend.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_namespace.h"
#include "catalog/pg_opclass.h"
#include "catalog/pg_tablespace.h"
#include "catalog/pg_trigger.h"
#include "catalog/pg_type.h"
#include "catalog/binary_upgrade.h"
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
#include "commands/tablegroup.h"
#include "commands/trigger.h"
#include "commands/typecmds.h"
#include "commands/user.h"
#include "executor/executor.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "nodes/nodeFuncs.h"
#include "nodes/parsenodes.h"
#include "optimizer/clauses.h"
#include "optimizer/planner.h"
#include "optimizer/predtest.h"
#include "optimizer/prep.h"
#include "optimizer/var.h"
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
#include "pgstat.h"
#include "rewrite/rewriteDefine.h"
#include "rewrite/rewriteHandler.h"
#include "rewrite/rewriteManip.h"
#include "storage/bufmgr.h"
#include "storage/lmgr.h"
#include "storage/lock.h"
#include "storage/predicate.h"
#include "storage/smgr.h"
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
#include "utils/tqual.h"
#include "utils/typcache.h"

/* YB includes. */
#include "catalog/pg_authid.h"
#include "catalog/pg_database.h"
#include "catalog/pg_policy.h"
#include "catalog/pg_publication_rel_d.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_statistic_ext.h"
#include "catalog/pg_shdepend.h"
#include "catalog/pg_shdepend_d.h"
#include "catalog/pg_yb_tablegroup_d.h"
#include "commands/dbcommands.h"
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
#define AT_PASS_COL_ATTRS		4	/* set other column attributes */
/* We could support a RENAME COLUMN pass here, but not currently used */
#define AT_PASS_ADD_COL			5	/* ADD COLUMN */
#define AT_PASS_ADD_INDEX		6	/* ADD indexes */
#define AT_PASS_ADD_CONSTR		7	/* ADD constraints, defaults */
#define AT_PASS_MISC			8	/* other stuff */
#define AT_NUM_PASSES			9

typedef struct AlteredTableInfo
{
	/* Information saved before any work commences: */
	Oid			relid;			/* Relation to work on */
	char		relkind;		/* Its relkind */
	TupleDesc	oldDesc;		/* Pre-modification tuple descriptor */
	/* Information saved by Phase 1 for Phase 2: */
	List	   *subcmds[AT_NUM_PASSES]; /* Lists of AlterTableCmd */
	/* Information saved by Phases 1/2 for Phase 3: */
	List	   *constraints;	/* List of NewConstraint */
	List	   *newvals;		/* List of NewColumnValue */
	bool		new_notnull;	/* T if we added new NOT NULL constraints */
	int			rewrite;		/* Reason for forced rewrite, if any */
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
 * expr is an expression over *old* table values.
 */
typedef struct NewColumnValue
{
	AttrNumber	attnum;			/* which column */
	Expr	   *expr;			/* expression to compute */
	ExprState  *exprstate;		/* execution state */
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

struct DropRelationCallbackState
{
	char		relkind;
	Oid			heapOid;
	Oid			partParentOid;
	bool		concurrent;
};

/* Alter table target-type flags for ATSimplePermissions */
#define		ATT_TABLE				0x0001
#define		ATT_VIEW				0x0002
#define		ATT_MATVIEW				0x0004
#define		ATT_INDEX				0x0008
#define		ATT_COMPOSITE_TYPE		0x0010
#define		ATT_FOREIGN_TABLE		0x0020
#define		ATT_PARTITIONED_INDEX	0x0040

/*
 * Partition tables are expected to be dropped when the parent partitioned
 * table gets dropped. Hence for partitioning we use AUTO dependency.
 * Otherwise, for regular inheritance use NORMAL dependency.
 */
#define child_dependency_type(child_is_partition)	\
	((child_is_partition) ? DEPENDENCY_AUTO : DEPENDENCY_NORMAL)

static void truncate_check_rel(Relation rel);
static List *MergeAttributes(List *schema, List *supers, char relpersistence,
				bool is_partition, List **supconstr, int *supOidCount);
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
static ObjectAddress ATExecValidateConstraint(Relation rel, char *constrName,
						 bool recurse, bool recursing, LOCKMODE lockmode);
static int transformColumnNameList(Oid relId, List *colList,
						int16 *attnums, Oid *atttypids);
static int transformFkeyGetPrimaryKey(Relation pkrel, Oid *indexOid,
						   List **attnamelist,
						   int16 *attnums, Oid *atttypids,
						   Oid *opclasses);
static Oid transformFkeyCheckAttrs(Relation pkrel,
						int numattrs, int16 *attnums,
						Oid *opclasses);
static void checkFkeyPermissions(Relation rel, int16 *attnums, int natts);
static CoercionPathType findFkeyCast(Oid targetTypeId, Oid sourceTypeId,
			 Oid *funcid);
static void validateCheckConstraint(Relation rel, HeapTuple constrtup);
static void validateForeignKeyConstraint(char *conname,
							 Relation rel, Relation pkrel,
							 Oid pkindOid, Oid constraintOid);
static void ATController(AlterTableStmt *parsetree,
			 Relation rel, List *cmds, bool recurse, LOCKMODE lockmode);
static void ATPrepCmd(List **wqueue, Relation rel, AlterTableCmd *cmd,
		  bool recurse, bool recursing, LOCKMODE lockmode);
static void ATRewriteCatalogs(List **wqueue,
							  LOCKMODE lockmode,
							  List **rollbackHandles,
							  List *volatile *handles);
static void ATExecCmd(List **wqueue, AlteredTableInfo *tab, Relation *mutable_rel,
		  AlterTableCmd *cmd, LOCKMODE lockmode);
static void ATRewriteTables(AlterTableStmt *parsetree,
				List **wqueue, LOCKMODE lockmode);
static void ATRewriteTable(AlteredTableInfo *tab, Oid OIDNewHeap, LOCKMODE lockmode);
static AlteredTableInfo *ATGetQueueEntry(List **wqueue, Relation rel);
static void ATSimplePermissions(Relation rel, int allowed_targets);
static void ATWrongRelkindError(Relation rel, int allowed_targets);
static void ATSimpleRecursion(List **wqueue, Relation rel,
				  AlterTableCmd *cmd, bool recurse, LOCKMODE lockmode);
static void ATTypedTableRecursion(List **wqueue, Relation rel, AlterTableCmd *cmd,
					  LOCKMODE lockmode);
static List *find_typed_table_dependencies(Oid typeOid, const char *typeName,
							  DropBehavior behavior);
static void ATPrepAddColumn(List **wqueue, Relation rel, bool recurse, bool recursing,
				bool is_view, AlterTableCmd *cmd, LOCKMODE lockmode);
static ObjectAddress ATExecAddColumn(List **wqueue, AlteredTableInfo *tab,
				Relation rel, ColumnDef *colDef, bool isOid,
				bool recurse, bool recursing,
				bool if_not_exists, LOCKMODE lockmode);
static bool check_for_column_name_collision(Relation rel, const char *colname,
								bool if_not_exists);
static void add_column_datatype_dependency(Oid relid, int32 attnum, Oid typid);
static void add_column_collation_dependency(Oid relid, int32 attnum, Oid collid);
static void ATPrepAddOids(List **wqueue, Relation rel, bool recurse,
			  AlterTableCmd *cmd, LOCKMODE lockmode);
static void ATPrepDropNotNull(Relation rel, bool recurse, bool recursing);
static ObjectAddress ATExecDropNotNull(Relation rel, const char *colName, LOCKMODE lockmode);
static void ATPrepSetNotNull(Relation rel, bool recurse, bool recursing);
static ObjectAddress ATExecSetNotNull(AlteredTableInfo *tab, Relation rel,
				 const char *colName, LOCKMODE lockmode);
static ObjectAddress ATExecColumnDefault(Relation rel, const char *colName,
					Node *newDefault, LOCKMODE lockmode);
static ObjectAddress ATExecAddIdentity(Relation rel, const char *colName,
				  Node *def, LOCKMODE lockmode);
static ObjectAddress ATExecSetIdentity(Relation rel, const char *colName,
				  Node *def, LOCKMODE lockmode);
static ObjectAddress ATExecDropIdentity(Relation rel, const char *colName, bool missing_ok, LOCKMODE lockmode);
static void ATPrepSetStatistics(Relation rel, const char *colName, int16 colNum,
					Node *newValue, LOCKMODE lockmode);
static ObjectAddress ATExecSetStatistics(Relation rel, const char *colName, int16 colNum,
					Node *newValue, LOCKMODE lockmode);
static ObjectAddress ATExecSetOptions(Relation rel, const char *colName,
				 Node *options, bool isReset, LOCKMODE lockmode);
static ObjectAddress ATExecSetStorage(Relation rel, const char *colName,
				 Node *newValue, LOCKMODE lockmode);
static void ATPrepDropColumn(List **wqueue, Relation rel, bool recurse, bool recursing,
				 AlterTableCmd *cmd, LOCKMODE lockmode);
static ObjectAddress ATExecDropColumn(List **wqueue, AlteredTableInfo *yb_tab, Relation rel,
				 const char *colName,
				 DropBehavior behavior,
				 bool recurse, bool recursing,
				 bool missing_ok, LOCKMODE lockmode);
static ObjectAddress ATExecAddIndex(List **yb_wqueue, AlteredTableInfo *tab, Relation *mutable_rel,
			   IndexStmt *stmt, bool is_rebuild, LOCKMODE lockmode);
static ObjectAddress ATExecAddConstraint(List **wqueue,
					AlteredTableInfo *tab, Relation rel,
					Constraint *newConstraint, bool recurse, bool is_readd,
					LOCKMODE lockmode);
static ObjectAddress ATExecAddIndexConstraint(AlteredTableInfo *tab, Relation rel,
						 IndexStmt *stmt, LOCKMODE lockmode, List **yb_wqueue);
static ObjectAddress ATAddCheckConstraint(List **wqueue,
					 AlteredTableInfo *tab, Relation rel,
					 Constraint *constr,
					 bool recurse, bool recursing, bool is_readd,
					 LOCKMODE lockmode);
static ObjectAddress ATAddForeignKeyConstraint(List **wqueue, AlteredTableInfo *tab,
						  Relation rel, Constraint *fkconstraint, Oid parentConstr,
						  bool recurse, bool recursing,
						  LOCKMODE lockmode);
static void CloneFkReferencing(Relation pg_constraint, Relation parentRel,
				   Relation partRel, List *clone, List **cloned);
static void ATExecDropConstraint(List **yb_wqueue, AlteredTableInfo *tab,  Relation *mutable_rel,
					 const char *constrName, DropBehavior behavior,
					 bool recurse, bool recursing,
					 bool missing_ok, LOCKMODE lockmode);
static void ATPrepAlterColumnType(List **wqueue,
					  AlteredTableInfo *tab, Relation rel,
					  bool recurse, bool recursing,
					  AlterTableCmd *cmd, LOCKMODE lockmode);
static bool ATColumnChangeRequiresRewrite(Node *expr, AttrNumber varattno);
static ObjectAddress ATExecAlterColumnType(AlteredTableInfo *tab,
										   Relation			*yb_mutable_rel,
										   AlterTableCmd	*cmd,
										   LOCKMODE			 lockmode);
static ObjectAddress ATExecAlterColumnGenericOptions(Relation rel, const char *colName,
								List *options, LOCKMODE lockmode);
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
static void change_owner_fix_column_acls(Oid relationOid,
							 Oid oldOwnerId, Oid newOwnerId);
static void change_owner_recurse_to_sequences(Oid relationOid,
								  Oid newOwnerId, LOCKMODE lockmode);
static ObjectAddress ATExecClusterOn(Relation rel, const char *indexName,
				LOCKMODE lockmode);
static void ATExecDropCluster(Relation rel, LOCKMODE lockmode);
static bool ATPrepChangePersistence(Relation rel, bool toLogged);
static void ATPrepSetTableSpace(AlteredTableInfo *tab, Relation rel,
					const char *tablespacename, LOCKMODE lockmode, bool cascade);
static void ATExecSetTableSpace(Oid tableOid, Oid newTableSpace, LOCKMODE lockmode);
static void ATExecSetTableSpaceNoStorage(Relation rel, Oid newTableSpace);
static void ATExecSetRelOptions(Relation rel, List *defList,
					AlterTableType operation,
					LOCKMODE lockmode);
static void ATExecEnableDisableTrigger(Relation rel, const char *trigname,
						   char fires_when, bool skip_system, LOCKMODE lockmode);
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
static void ATExecEnableRowSecurity(Relation rel);
static void ATExecDisableRowSecurity(Relation rel);
static void ATExecForceNoForceRowSecurity(Relation rel, bool force_rls);

static void copy_relation_data(SMgrRelation rel, SMgrRelation dst,
				   ForkNumber forkNum, char relpersistence);
static const char *storage_name(char c);

static void RangeVarCallbackForDropRelation(const RangeVar *rel, Oid relOid,
								Oid oldRelOid, void *arg);
static void RangeVarCallbackForAlterRelation(const RangeVar *rv, Oid relid,
								 Oid oldrelid, void *arg);
static PartitionSpec *transformPartitionSpec(Relation rel, PartitionSpec *partspec, char *strategy);
static void ComputePartitionAttrs(Relation rel, List *partParams, AttrNumber *partattrs,
					  List **partexprs, Oid *partopclass, Oid *partcollation, char strategy);
static void CreateInheritance(Relation child_rel, Relation parent_rel);
static void RemoveInheritance(Relation child_rel, Relation parent_rel);
static ObjectAddress ATExecAttachPartition(List **wqueue, Relation rel,
					  PartitionCmd *cmd);
static void AttachPartitionEnsureIndexes(Relation rel, Relation attachrel,
										 List **yb_wqueue);
static void QueuePartitionConstraintValidation(List **wqueue, Relation scanrel,
								   List *partConstraint,
								   bool validate_default);
static void CloneRowTriggersToPartition(Relation parent, Relation partition);
static void DropClonedTriggersFromPartition(Oid partitionId);
static ObjectAddress ATExecDetachPartition(Relation rel, RangeVar *name);
static ObjectAddress ATExecAttachPartitionIdx(List **wqueue, Relation rel,
						 RangeVar *name);
static void validatePartitionedIndex(Relation partedIdx, Relation partedTbl);
static void refuseDupeIndexAttach(Relation parentIdx, Relation partIdx,
					  Relation partitionTbl);
static void update_relispartition(Relation classRel, Oid relationId,
					  bool newval);
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
static bool YbIsTablePartOfPublication(Oid relOid);

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
	bool		localHasOids;
	int			parentOidCount;
	List	   *rawDefaults;
	List	   *cookedDefaults;
	Datum		reloptions;
	ListCell   *listptr;
	AttrNumber	attnum;
	static char *validnsps[] = HEAP_RELOPT_NAMESPACES;
	Oid			ofTypeId;
	ObjectAddress address;
	LOCKMODE	parentLockmode;

	/* YB variables. */
	Oid			rowTypeId = InvalidOid;
	bool		relisshared = false;
	bool		use_initdb_acl = false;
	ListCell   *opt_cell;

	/*
	 * Truncate relname to appropriate length (probably a waste of time, as
	 * parser should have done this already).
	 */
	StrNCpy(relname, stmt->relation->relname, NAMEDATALEN);

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
	}

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
	 * Select tablespace to use.  If not specified, use default tablespace
	 * (which may in turn default to database's default).
	 */
	if (stmt->tablespacename)
	{
		tablespaceId = get_tablespace_oid(stmt->tablespacename, false);
	}
	else if (stmt->partbound)
	{
		HeapTuple	tup;

		/*
		 * For partitions, when no other tablespace is specified, we default
		 * the tablespace to the parent partitioned table's.
		 */
		Assert(list_length(inheritOids) == 1);
		tup = SearchSysCache1(RELOID,
							  DatumGetObjectId(linitial_oid(inheritOids)));

		tablespaceId = ((Form_pg_class) GETSTRUCT(tup))->reltablespace;

		if (!OidIsValid(tablespaceId))
			tablespaceId = GetDefaultTablespace(stmt->relation->relpersistence);

		ReleaseSysCache(tup);
	}
	else
	{
		tablespaceId = GetDefaultTablespace(stmt->relation->relpersistence);
		/* note InvalidOid is OK in this case */
	}

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
			else if (!IsSystemNamespace(namespaceId))
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

				aclresult = pg_database_aclcheck(TemplateDbOid, GetUserId(), ACL_CREATE);

				if (aclresult != ACLCHECK_OK)
					aclcheck_error(aclresult, OBJECT_DATABASE,
								   get_database_name(TemplateDbOid));
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
		ownerId = (IsYugaByteEnabled() && IsYsqlUpgrade && IsSystemNamespace(namespaceId))
					? BOOTSTRAP_SUPERUSERID
					: GetUserId();

	/*
	 * Parse and validate reloptions, if any.
	 */
	reloptions = transformRelOptions((Datum) 0, stmt->options, NULL, validnsps,
									 true, false);

	if (relkind == RELKIND_VIEW)
		(void) view_reloptions(reloptions, true);
	else
		(void) heap_reloptions(relkind, reloptions, true);

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
						&old_constraints, &parentOidCount);

	/*
	 * Create a tuple descriptor from the relation schema.  Note that this
	 * deals with column names, types, and NOT NULL constraints, but not
	 * default values or CHECK constraints; we handle those below.
	 */
	descriptor = BuildDescForRelation(stmt->tableElts);

	/*
	 * Notice that we allow OIDs here only for plain tables and partitioned
	 * tables, even though some other relkinds can support them.  This is
	 * necessary because the default_with_oids GUC must apply only to plain
	 * tables and not any other relkind; doing otherwise would break existing
	 * pg_dump files.  We could allow explicit "WITH OIDS" while not allowing
	 * default_with_oids to affect other relkinds, but it would complicate
	 * interpretOidsOption().
	 */
	localHasOids = interpretOidsOption(stmt->options,
									   (relkind == RELKIND_RELATION ||
										relkind == RELKIND_PARTITIONED_TABLE));
	descriptor->tdhasoid = (localHasOids || parentOidCount > 0);

	/*
	 * If a partitioned table doesn't have the system OID column, then none of
	 * its partitions should have it.
	 */
	if (stmt->partbound && parentOidCount == 0 && localHasOids)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot create table with OIDs as partition of table without OIDs")));

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
	}

	/* Handles WITH (table_oid = x). */
	relationId = GetTableOidFromRelOptions(
		stmt->options, tablespaceId, stmt->relation->relpersistence);

	/* Handles WITH (row_type_oid = x). */
	rowTypeId = GetRowTypeOidFromRelOptions(stmt->options);

	if (relkind == RELKIND_RELATION)
	{
		use_initdb_acl = IsYsqlUpgrade && IsSystemNamespace(namespaceId);
	}
	else
	{
		/* Handles WITH (use_initdb_acl = x). */
		use_initdb_acl = YbGetUseInitdbAclFromRelOptions(stmt->options);
		if (use_initdb_acl && !(IsYsqlUpgrade && IsSystemNamespace(namespaceId)))
		{
			ereport(ERROR,
					(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
					 errmsg("use_initdb_acl cannot be used outside of YSQL upgrade for pg_catalog")));
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
										  descriptor,
										  list_concat(cookedDefaults,
													  old_constraints),
										  relkind,
										  stmt->relation->relpersistence,
										  relisshared,
										  false,
										  localHasOids,
										  parentOidCount,
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

	if (IsYugaByteEnabled())
	{
		CheckIsYBSupportedRelationByKind(relkind);
		YBCCreateTable(stmt, relname, relkind, descriptor, relationId,
					   namespaceId, tablegroupId, colocation_id, tablespaceId,
					   InvalidOid /* pgTableId */,
					   InvalidOid /* oldRelfileNodeId */,
					   false /* isTruncate */);
	}

	/*
	 * Open the new relation and acquire exclusive lock on it.  This isn't
	 * really necessary for locking out other backends (since they can't see
	 * the new rel anyway until we commit), but it keeps the lock manager from
	 * complaining about deadlock risks.
	 */
	rel = relation_open(relationId, AccessExclusiveLock);

	/* Process and store partition bound, if any. */
	if (stmt->partbound)
	{
		PartitionBoundSpec *bound;
		ParseState *pstate;
		Oid			parentId = linitial_oid(inheritOids),
					defaultPartOid;
		Relation	parent,
					defaultRel = NULL;

		/* Already have strong enough lock on the parent */
		parent = heap_open(parentId, NoLock);

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
			get_default_oid_from_partdesc(RelationGetPartitionDesc(parent));
		if (OidIsValid(defaultPartOid))
			defaultRel = heap_open(defaultPartOid, AccessExclusiveLock);

		/* Tranform the bound values */
		pstate = make_parsestate(NULL);
		pstate->p_sourcetext = queryString;

		bound = transformPartitionBound(pstate, parent, stmt->partbound);

		/*
		 * Check first that the new partition's bound is valid and does not
		 * overlap with any of existing partitions of the parent.
		 */
		check_new_partition_bound(relname, parent, bound);

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
			heap_close(defaultRel, NoLock);
		}

		/* Update the pg_class entry. */
		StorePartitionBound(rel, parent, bound);

		heap_close(parent, NoLock);
	}

	/* Store inheritance information for new rel. */
	StoreCatalogInheritance(relationId, inheritOids, stmt->partbound != NULL);

	/*
	 * Process the partitioning specification (if any) and store the partition
	 * key information into the catalog.
	 */
	if (stmt->partspec)
	{
		char		strategy;
		int			partnatts;
		AttrNumber	partattrs[PARTITION_MAX_KEYS];
		Oid			partopclass[PARTITION_MAX_KEYS];
		Oid			partcollation[PARTITION_MAX_KEYS];
		List	   *partexprs = NIL;

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

		ComputePartitionAttrs(rel, stmt->partspec->partParams,
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
		parent = heap_open(parentId, NoLock);
		idxlist = RelationGetIndexList(parent);

		/*
		 * For each index in the parent table, create one in the partition
		 */
		foreach(cell, idxlist)
		{
			Relation	idxRel = index_open(lfirst_oid(cell), AccessShareLock);
			AttrNumber *attmap;
			IndexStmt  *idxstmt;
			Oid			constraintOid;

			attmap = convert_tuples_by_name_map(
				RelationGetDescr(rel), RelationGetDescr(parent),
				gettext_noop("could not convert row type"),
				false /* yb_ignore_type_mismatch */);
			idxstmt =
				generateClonedIndexStmt(NULL, RelationGetRelid(rel), idxRel,
										attmap, RelationGetDescr(rel)->natts,
										&constraintOid);
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
		CloneForeignKeyConstraints(parentId, relationId, NULL);

		heap_close(parent, NoLock);
	}

	/*
	 * Now add any newly specified column default values and CHECK constraints
	 * to the new relation.  These are passed to us in the form of raw
	 * parsetrees; we need to transform them to executable expression trees
	 * before they can be added. The most convenient way to do that is to
	 * apply the parser's transformExpr routine, but transformExpr doesn't
	 * work unless we have a pre-existing relation. So, the transformation has
	 * to be postponed to this final step of CREATE TABLE.
	 */
	if (rawDefaults || stmt->constraints)
		AddRelationNewConstraints(rel, rawDefaults, stmt->constraints,
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
		flags |= PERFORM_DELETION_CONCURRENTLY;
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
		state.relkind = relkind;
		state.heapOid = InvalidOid;
		state.partParentOid = InvalidOid;
		state.concurrent = drop->concurrent;
		relOid = RangeVarGetRelidExtended(rel, lockmode, RVR_MISSING_OK,
										  RangeVarCallbackForDropRelation,
										  (void *) &state);

		/* Not there? */
		if (!OidIsValid(relOid))
		{
			DropErrorMsgNonExistent(rel, relkind, drop->missing_ok);
			continue;
		}

		if (IsYugaByteEnabled() && YbIsTablePartOfPublication(relOid))
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_IN_USE),
					 errmsg("cannot drop a table which is part of a publication."),
					 errhint("Use pg_publication_tables to find all such publications and retry after dropping the table from them.")));

		only_temp_tables = only_temp_tables &&
						   get_rel_persistence(relOid) == RELPERSISTENCE_TEMP;

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
	char		relkind;
	char		expected_relkind;
	bool		is_partition;
	Form_pg_class classform;
	LOCKMODE	heap_lockmode;

	state = (struct DropRelationCallbackState *) arg;
	relkind = state->relkind;
	heap_lockmode = state->concurrent ?
		ShareUpdateExclusiveLock : AccessExclusiveLock;

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

	if (relkind != expected_relkind)
		DropErrorMsgWrongType(rel->relname, classform->relkind, relkind);

	/* Allow DROP to either table owner or schema owner */
	if (!pg_class_ownercheck(relOid, GetUserId()) &&
		!pg_namespace_ownercheck(classform->relnamespace, GetUserId()) &&
		!IsYbDbAdminUser(GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(get_rel_relkind(relOid)),
					   rel->relname);

	if (!allowSystemTableMods && IsSystemClass(relOid, classform))
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
	 * entry, though --- the relation may have been dropped.
	 */
	if ((relkind == RELKIND_INDEX || relkind == RELKIND_PARTITIONED_INDEX) &&
		relOid != oldRelOid)
	{
		state->heapOid = IndexGetRelation(relOid, true);
		if (OidIsValid(state->heapOid))
			LockRelationOid(state->heapOid, heap_lockmode);
	}

	/*
	 * Similarly, if the relation is a partition, we must acquire lock on its
	 * parent before locking the partition.  That's because queries lock the
	 * parent before its partitions, so we risk deadlock it we do it the other
	 * way around.
	 */
	if (is_partition && relOid != oldRelOid)
	{
		state->partParentOid = get_partition_parent(relOid);
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
 * verifying that the relation is OK for truncation.  In CASCADE mode,
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

		rel = heap_openrv(rv, lockmode);
		myrelid = RelationGetRelid(rel);
		/* don't throw error for "TRUNCATE foo, foo" */
		if (list_member_oid(relids, myrelid))
		{
			heap_close(rel, lockmode);
			continue;
		}
		truncate_check_rel(rel);
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
				rel = heap_open(childrelid, NoLock);

				/*
				 * It is possible that the parent table has children that are
				 * temp tables of other backends.  We cannot safely access
				 * such tables (because of buffering issues), and the best
				 * thing to do is to silently ignore them.  Note that this
				 * check is the same as one of the checks done in
				 * truncate_check_rel() called below, still it is kept
				 * here for simplicity.
				 */
				if (RELATION_IS_OTHER_TEMP(rel))
				{
					heap_close(rel, lockmode);
					continue;
				}

				truncate_check_rel(rel);
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

		heap_close(rel, NoLock);
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
ExecuteTruncateGuts(List *explicit_rels, List *relids, List *relids_logged,
					DropBehavior behavior, bool restart_seqs)
{
	List	   *rels;
	List	   *seq_relids = NIL;
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

				rel = heap_open(relid, AccessExclusiveLock);
				ereport(NOTICE,
						(errmsg("truncate cascades to table \"%s\"",
								RelationGetRelationName(rel))));
				truncate_check_rel(rel);
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
			List	   *seqlist = getOwnedSequences(RelationGetRelid(rel), 0);
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
		resultRelInfo++;
	}
	estate->es_result_relations = resultRelInfos;
	estate->es_num_result_relations = list_length(rels);

	/*
	 * Process all BEFORE STATEMENT TRUNCATE triggers before we begin
	 * truncating (this is because one of them might throw an error). Also, if
	 * we were to allow them to prevent statement execution, that would need
	 * to be handled here.
	 */
	resultRelInfo = resultRelInfos;
	foreach(cell, rels)
	{
		estate->es_result_relation_info = resultRelInfo;
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
			MultiXactId minmulti;

			/*
			 * This effectively deletes all rows in the table, and may be done
			 * in a serializable transaction.  In that case we must record a
			 * rw-conflict in to this transaction from each transaction
			 * holding a predicate lock on the table.
			 */
			CheckTableForSerializableConflictIn(rel);

			minmulti = GetOldestMultiXactId();

			/*
			 * Need the full transaction-safe pushups.
			 *
			 * Create a new empty storage file for the relation, and assign it
			 * as the relfilenode value. The old storage file is scheduled for
			 * deletion at commit.
			 */
			RelationSetNewRelfilenode(rel, rel->rd_rel->relpersistence,
									  RecentXmin, minmulti,
									  false /* yb_copy_split_options */);
			if (rel->rd_rel->relpersistence == RELPERSISTENCE_UNLOGGED)
				heap_create_init_fork(rel);

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
										  RecentXmin, minmulti,
										  false /* yb_copy_split_options */);
				if (toastrel->rd_rel->relpersistence == RELPERSISTENCE_UNLOGGED)
					heap_create_init_fork(toastrel);
				heap_close(toastrel, NoLock);
			}

			/*
			 * Reconstruct the indexes to match, and we're done.
			 */
			reindex_relation(heap_relid, REINDEX_REL_PROCESS_TOAST, 0,
							 true /* is_yb_table_rewrite */,
							 false /* yb_copy_split_options */);
		}

		pgstat_count_truncate(rel);
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
		estate->es_result_relation_info = resultRelInfo;
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

		heap_close(rel, NoLock);
	}
}

/*
 * Check that a given rel is safe to truncate.  Subroutine for ExecuteTruncate
 */
static void
truncate_check_rel(Relation rel)
{
	AclResult	aclresult;

	/*
	 * Only allow truncate on regular tables and partitioned tables (although,
	 * the latter are only being included here for the following checks; no
	 * physical truncation will occur in their case.)
	 */
	if (rel->rd_rel->relkind != RELKIND_RELATION &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a table",
						RelationGetRelationName(rel))));

	/* Permissions checks */
	aclresult = pg_class_aclcheck(RelationGetRelid(rel), GetUserId(),
								  ACL_TRUNCATE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, get_relkind_objtype(rel->rd_rel->relkind),
					   RelationGetRelationName(rel));

	if (!allowSystemTableMods && IsSystemRelation(rel))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied: \"%s\" is a system catalog",
						RelationGetRelationName(rel))));

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
		case 'p':
			return "PLAIN";
		case 'm':
			return "MAIN";
		case 'x':
			return "EXTENDED";
		case 'e':
			return "EXTERNAL";
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
 * 'relpersistence' is a persistence type of the table.
 * 'is_partition' tells if the table is a partition
 *
 * Output arguments:
 * 'supconstr' receives a list of constraints belonging to the parents,
 *		updated as necessary to be valid for the child.
 * 'supOidCount' is set to the number of parents that have OID columns.
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
				bool is_partition, List **supconstr, int *supOidCount)
{
	ListCell   *entry;
	List	   *inhSchema = NIL;
	List	   *constraints = NIL;
	int			parentsWithOids = 0;
	bool		have_bogus_defaults = false;
	int			child_attno;
	static Node bogus_marker = {0}; /* marks conflicting defaults */
	List	   *saved_schema = NIL;

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
	 */
	foreach(entry, schema)
	{
		ColumnDef  *coldef = lfirst(entry);
		ListCell   *rest = lnext(entry);
		ListCell   *prev = entry;

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

		while (rest != NULL)
		{
			ColumnDef  *restdef = lfirst(rest);
			ListCell   *next = lnext(rest); /* need to save it in case we
											 * delete it */

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
					list_delete_cell(schema, rest, prev);
				}
				else
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_COLUMN),
							 errmsg("column \"%s\" specified more than once",
									coldef->colname)));
			}
			prev = rest;
			rest = next;
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
		AttrNumber *newattno;
		AttrNumber	parent_attno;

		/* caller already got lock */
		relation = heap_open(parent, NoLock);

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

		if (relation->rd_rel->relhasoids)
			parentsWithOids++;

		tupleDesc = RelationGetDescr(relation);
		constr = tupleDesc->constr;

		/*
		 * newattno[] will contain the child-table attribute numbers for the
		 * attributes of this parent table.  (They are not the same for
		 * parents after the first one, nor if we have dropped columns.)
		 */
		newattno = (AttrNumber *)
			palloc0(tupleDesc->natts * sizeof(AttrNumber));

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
				continue;		/* leave newattno entry as zero */

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

				/* Copy storage parameter */
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

				def->inhcount++;
				/* Merge of NOT NULL constraints = OR 'em together */
				def->is_not_null |= attribute->attnotnull;
				/* Default and other constraints are handled below */
				newattno[parent_attno - 1] = exist_attno;
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
				def->collClause = NULL;
				def->collOid = attribute->attcollation;
				def->constraints = NIL;
				def->location = -1;
				inhSchema = lappend(inhSchema, def);
				newattno[parent_attno - 1] = ++child_attno;
			}

			/*
			 * Copy default if any
			 */
			if (attribute->atthasdef)
			{
				Node	   *this_default = NULL;
				AttrDefault *attrdef;
				int			i;

				/* Find default in constraint structure */
				Assert(constr != NULL);
				attrdef = constr->defval;
				for (i = 0; i < constr->num_defval; i++)
				{
					if (attrdef[i].adnum == parent_attno)
					{
						this_default = stringToNode(attrdef[i].adbin);
						break;
					}
				}
				Assert(this_default != NULL);

				/*
				 * If default expr could contain any vars, we'd need to fix
				 * 'em, but it can't; so default is ready to apply to child.
				 *
				 * If we already had a default from some prior parent, check
				 * to see if they are the same.  If so, no problem; if not,
				 * mark the column as having a bogus default. Below, we will
				 * complain if the bogus default isn't overridden by the child
				 * schema.
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
		}

		/*
		 * Now copy the CHECK constraints of this parent, adjusting attnos
		 * using the completed newattno[] map.  Identically named constraints
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
										   newattno, tupleDesc->natts,
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

		pfree(newattno);

		/*
		 * Close the parent rel, but keep our lock on it until xact commit.
		 * That will prevent someone else from deleting or ALTERing the parent
		 * before the child is committed.
		 */
		heap_close(relation, NoLock);
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

				/* Mark the column as locally defined */
				def->is_local = true;
				/* Merge of NOT NULL constraints = OR 'em together */
				def->is_not_null |= newdef->is_not_null;
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
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_COLUMN_DEFINITION),
						 errmsg("column \"%s\" inherits conflicting default values",
								def->colname),
						 errhint("To resolve the conflict, specify a default explicitly.")));
		}
	}

	*supconstr = constraints;
	*supOidCount = parentsWithOids;
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
	relation = heap_open(InheritsRelationId, RowExclusiveLock);

	seqNumber = 1;
	foreach(entry, supers)
	{
		Oid			parentOid = lfirst_oid(entry);

		StoreCatalogInheritance1(relationId, parentOid, seqNumber, relation,
								 child_is_partition);
		seqNumber++;
	}

	heap_close(relation, RowExclusiveLock);
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
	relationRelation = heap_open(RelationRelationId, RowExclusiveLock);
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
	heap_close(relationRelation, RowExclusiveLock);
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
				 errmsg("\"%s\" is not a table, view, materialized view, composite type, index, or foreign table",
						NameStr(classform->relname))));

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

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);

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

	heap_close(attrelation, RowExclusiveLock);

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
		YBCRename(relid, stmt->renameType, stmt->newname, stmt->subname);
		if (stmt->renameType == OBJECT_COLUMN)
		{
			ListCell *child;
			List *children = find_all_inheritors(relid, NoLock, NULL);
			foreach(child, children)
			{
				Oid childrelid = lfirst_oid(child);
				if (childrelid != relid)
					YBCRename(childrelid, stmt->renameType, stmt->newname,
							  stmt->subname);
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
	{
		/* rename the index; this renames the constraint as well */
		RenameRelationInternal(con->conindid, newconname, false);
		/* in YB, rename the DocDB table for the index */
		if (IsYBRelationById(con->conindid) && con->contype != CONSTRAINT_PRIMARY)
			YBCRename(con->conindid, OBJECT_INDEX, newconname, NULL /* colname */ );
	}
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
		rel = heap_open(TypeRelationId, RowExclusiveLock);
		tup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typid));
		if (!HeapTupleIsValid(tup))
			elog(ERROR, "cache lookup failed for type %u", typid);
		checkDomainOwner(tup);
		ReleaseSysCache(tup);
		heap_close(rel, NoLock);
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
	Oid           relid;
	ObjectAddress address;
	Relation      rel;
	bool          needs_yb_rename;

	/*
	 * Grab an exclusive lock on the target table, index, sequence, view,
	 * materialized view, or foreign table, which we will NOT release until
	 * end of transaction.
	 *
	 * Lock level used here should match RenameRelationInternal, to avoid lock
	 * escalation.
	 */
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

	RenameRelationInternal(relid, stmt->newname, false);

	/* YB rename is not needed for a primary key dummy index. */
	rel             = RelationIdGetRelation(relid);
	needs_yb_rename = (IsYBRelation(rel) &&
					   !((rel->rd_rel->relkind == RELKIND_INDEX ||
						  rel->rd_rel->relkind == RELKIND_PARTITIONED_INDEX) &&
						 rel->rd_index->indisprimary) &&
					   !yb_is_internal_clone_rename);
	RelationClose(rel);

	/* Do the work */
	if (needs_yb_rename)
	{
		YBCRename(relid, stmt->renameType, stmt->newname, stmt->subname);
	}

	ObjectAddressSet(address, RelationRelationId, relid);

	return address;
}

/*
 *		RenameRelationInternal - change the name of a relation
 */
void
RenameRelationInternal(Oid myrelid, const char *newrelname, bool is_internal)
{
	Relation	targetrelation;
	Relation	relrelation;	/* for RELATION relation */
	HeapTuple	reltup;
	Form_pg_class relform;
	Oid			namespaceId;

	/*
	 * Grab an exclusive lock on the target table, index, sequence, view,
	 * materialized view, or foreign table, which we will NOT release until
	 * end of transaction.
	 */
	targetrelation = relation_open(myrelid, AccessExclusiveLock);
	namespaceId = RelationGetNamespace(targetrelation);

	/*
	 * Find relation's pg_class tuple, and make sure newrelname isn't in use.
	 */
	relrelation = heap_open(RelationRelationId, RowExclusiveLock);

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
	 * Update pg_class tuple with new relname.  (Scribbling on reltup is OK
	 * because it's a copy...)
	 */
	namestrcpy(&(relform->relname), newrelname);

	CatalogTupleUpdate(relrelation, &reltup->t_self, reltup);

	InvokeObjectPostAlterHookArg(RelationRelationId, myrelid, 0,
								 InvalidOid, is_internal);

	heap_freetuple(reltup);
	heap_close(relrelation, RowExclusiveLock);

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
	 * Close rel, but keep exclusive lock!
	 */
	relation_close(targetrelation, NoLock);
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
				 errmsg("cannot %s \"%s\" because "
						"it is being used by active queries in this session",
						stmt, RelationGetRelationName(rel))));

	if (rel->rd_rel->relkind != RELKIND_INDEX &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX &&
		AfterTriggerPendingOnRel(RelationGetRelid(rel)))
		ereport(ERROR,
				(errcode(ERRCODE_OBJECT_IN_USE),
		/* translator: first %s is a SQL command, eg ALTER TABLE */
				 errmsg("cannot %s \"%s\" because "
						"it has pending trigger events",
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
 *		2. Update system catalogs.
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
 * validation of the subcommands, including parse transformation of those
 * expressions that need to be evaluated with respect to the old table
 * schema.
 *
 * ATRewriteCatalogs performs phase 2 for each affected table.  (Note that
 * phases 2 and 3 normally do no explicit recursion, since phase 1 already
 * did it --- although some subcommands have to recurse in phase 2 instead.)
 * Certain subcommands need to be performed before others to avoid
 * unnecessary conflicts; for example, DROP COLUMN should come before
 * ADD COLUMN.  Therefore phase 1 divides the subcommands into multiple
 * lists, one for each logical "pass" of phase 2.
 *
 * ATRewriteTables performs phase 3 for those tables that need it.
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
 */
void
AlterTable(Oid relid, LOCKMODE lockmode, AlterTableStmt *stmt)
{
	Relation	rel;

	/* Caller is required to provide an adequate lock. */
	rel = relation_open(relid, NoLock);

	CheckTableNotInUse(rel, "ALTER TABLE");

	if (IsYugaByteEnabled() &&
		rel->rd_rel->relpersistence == RELPERSISTENCE_TEMP)
		YBCForceAllowCatalogModifications(true);

	ATController(stmt, rel, stmt->cmds, stmt->relation->inh, lockmode);
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
 */
void
AlterTableInternal(Oid relid, List *cmds, bool recurse)
{
	Relation	rel;
	LOCKMODE	lockmode = AlterTableGetLockLevel(cmds);

	rel = relation_open(relid, lockmode);

	EventTriggerAlterTableRelid(relid);

	ATController(NULL, rel, cmds, recurse, lockmode);
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
 * Note that Hot Standby only knows about AccessExclusiveLocks on the master
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
			case AT_SetTableSpace:	/* must rewrite heap */
			case AT_AlterColumnType:	/* must rewrite heap */
			case AT_AddOids:	/* must rewrite heap */
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
				 * optimised assuming the constraint holds true.
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
			case AT_DropOids:	/* calls AT_DropColumn */
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
				cmd_lockmode = AccessExclusiveLock;
				break;

			case AT_AddConstraint:
			case AT_ProcessedConstraint:	/* becomes AT_AddConstraint */
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
			case AT_DetachPartition:
				cmd_lockmode = AccessExclusiveLock;
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
			 Relation rel, List *cmds, bool recurse, LOCKMODE lockmode)
{
	List	   *wqueue = NIL;
	ListCell   *lcmd;

	/* Phase 1: preliminary examination of commands, create work queue */
	foreach(lcmd, cmds)
	{
		AlterTableCmd *cmd = (AlterTableCmd *) lfirst(lcmd);
		ATPrepCmd(&wqueue, rel, cmd, recurse, false, lockmode);
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
		ATRewriteCatalogs(&wqueue, lockmode, &rollbackHandles,
						   &ybAlteredTableIds);
	}
	PG_CATCH();
	{
		YbATInvalidateTableCacheAfterAlter(ybAlteredTableIds);
		PG_RE_THROW();
	}
	PG_END_TRY();

	/* Phase 3: scan/rewrite tables as needed */
	PG_TRY();
	{
		ATRewriteTables(parsetree, &wqueue, lockmode);
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
		  bool recurse, bool recursing, LOCKMODE lockmode)
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
	 * Copy the original subcommand for each table.  This avoids conflicts
	 * when different child tables need to make different parse
	 * transformations (for example, the same column may have different column
	 * numbers in different children).
	 */
	cmd = copyObject(cmd);

	/*
	 * Do permissions checking, recursion to child tables if needed, and any
	 * additional phase-1 processing needed.
	 */
	switch (cmd->subtype)
	{
		case AT_AddColumn:		/* ADD COLUMN */
			ATSimplePermissions(rel,
								ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE);
			ATPrepAddColumn(wqueue, rel, recurse, recursing, false, cmd,
							lockmode);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_ADD_COL;
			break;
		case AT_AddColumnToView:	/* add column via CREATE OR REPLACE VIEW */
			ATSimplePermissions(rel, ATT_VIEW);
			ATPrepAddColumn(wqueue, rel, recurse, recursing, true, cmd,
							lockmode);
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
			ATSimplePermissions(rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode);
			/* No command-specific prep needed */
			pass = cmd->def ? AT_PASS_ADD_CONSTR : AT_PASS_DROP;
			break;
		case AT_AddIdentity:
			ATSimplePermissions(rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			pass = AT_PASS_ADD_CONSTR;
			break;
		case AT_DropIdentity:
			ATSimplePermissions(rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			pass = AT_PASS_DROP;
			break;
		case AT_SetIdentity:
			ATSimplePermissions(rel, ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE);
			pass = AT_PASS_COL_ATTRS;
			break;
		case AT_DropNotNull:	/* ALTER COLUMN DROP NOT NULL */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			ATPrepDropNotNull(rel, recurse, recursing);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode);
			/* No command-specific prep needed */
			pass = AT_PASS_DROP;
			break;
		case AT_SetNotNull:		/* ALTER COLUMN SET NOT NULL */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			ATPrepSetNotNull(rel, recurse, recursing);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode);
			/* No command-specific prep needed */
			pass = AT_PASS_ADD_CONSTR;
			break;
		case AT_SetStatistics:	/* ALTER COLUMN SET STATISTICS */
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode);
			/* Performs own permission checks */
			ATPrepSetStatistics(rel, cmd->name, cmd->num, cmd->def, lockmode);
			pass = AT_PASS_MISC;
			break;
		case AT_SetOptions:		/* ALTER COLUMN SET ( options ) */
		case AT_ResetOptions:	/* ALTER COLUMN RESET ( options ) */
			ATSimplePermissions(rel, ATT_TABLE | ATT_MATVIEW | ATT_INDEX | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			pass = AT_PASS_MISC;
			break;
		case AT_SetStorage:		/* ALTER COLUMN SET STORAGE */
			ATSimplePermissions(rel, ATT_TABLE | ATT_MATVIEW | ATT_FOREIGN_TABLE);
			ATSimpleRecursion(wqueue, rel, cmd, recurse, lockmode);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_DropColumn:		/* DROP COLUMN */
			ATSimplePermissions(rel,
								ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE);
			ATPrepDropColumn(wqueue, rel, recurse, recursing, cmd, lockmode);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_DROP;
			break;
		case AT_AddIndex:		/* ADD INDEX */
			ATSimplePermissions(rel, ATT_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_ADD_INDEX;
			break;
		case AT_AddConstraint:	/* ADD CONSTRAINT */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Recursion occurs during execution phase */
			/* No command-specific prep needed except saving recurse flag */
			if (recurse)
				cmd->subtype = AT_AddConstraintRecurse;
			pass = AT_PASS_ADD_CONSTR;
			break;
		case AT_AddIndexConstraint: /* ADD CONSTRAINT USING INDEX */
			ATSimplePermissions(rel, ATT_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_ADD_CONSTR;
			break;
		case AT_DropConstraint: /* DROP CONSTRAINT */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Recursion occurs during execution phase */
			/* No command-specific prep needed except saving recurse flag */
			if (recurse)
				cmd->subtype = AT_DropConstraintRecurse;
			pass = AT_PASS_DROP;
			break;
		case AT_AlterColumnType:	/* ALTER COLUMN TYPE */
			ATSimplePermissions(rel,
								ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE);
			/* Performs own recursion */
			ATPrepAlterColumnType(wqueue, tab, rel, recurse, recursing, cmd, lockmode);
			pass = AT_PASS_ALTER_TYPE;
			break;
		case AT_AlterColumnGenericOptions:
			ATSimplePermissions(rel, ATT_FOREIGN_TABLE);
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
			ATSimplePermissions(rel, ATT_TABLE | ATT_MATVIEW);
			/* These commands never recurse */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_SetLogged:		/* SET LOGGED */
			ATSimplePermissions(rel, ATT_TABLE);
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
			ATSimplePermissions(rel, ATT_TABLE);
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
		case AT_AddOids:		/* SET WITH OIDS */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			if (!rel->rd_rel->relhasoids || recursing)
				ATPrepAddOids(wqueue, rel, recurse, cmd, lockmode);
			/* Recursion occurs during execution phase */
			pass = AT_PASS_ADD_COL;
			break;
		case AT_DropOids:		/* SET WITHOUT OIDS */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Performs own recursion */
			if (rel->rd_rel->relhasoids)
			{
				AlterTableCmd *dropCmd = makeNode(AlterTableCmd);

				dropCmd->subtype = AT_DropColumn;
				dropCmd->name = pstrdup("oid");
				dropCmd->behavior = cmd->behavior;
				ATPrepCmd(wqueue, rel, dropCmd, recurse, false, lockmode);
			}
			pass = AT_PASS_DROP;
			break;
		case AT_SetTableSpace:	/* SET TABLESPACE */
			ATSimplePermissions(rel, ATT_TABLE | ATT_MATVIEW | ATT_INDEX |
								ATT_PARTITIONED_INDEX);
			/* This command never recurses */
			ATPrepSetTableSpace(tab, rel, cmd->name, lockmode, cmd->yb_cascade);
			pass = AT_PASS_MISC;	/* doesn't actually matter */
			break;
		case AT_SetRelOptions:	/* SET (...) */
		case AT_ResetRelOptions:	/* RESET (...) */
		case AT_ReplaceRelOptions:	/* reset them all, then set just these */
			ATSimplePermissions(rel, ATT_TABLE | ATT_VIEW | ATT_MATVIEW | ATT_INDEX);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_AddInherit:		/* INHERIT */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			ATPrepAddInherit(rel);
			pass = AT_PASS_MISC;
			break;
		case AT_DropInherit:	/* NO INHERIT */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* This command never recurses */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_AlterConstraint:	/* ALTER CONSTRAINT */
			ATSimplePermissions(rel, ATT_TABLE);
			pass = AT_PASS_MISC;
			break;
		case AT_ValidateConstraint: /* VALIDATE CONSTRAINT */
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
			/* Recursion occurs during execution phase */
			/* No command-specific prep needed except saving recurse flag */
			if (recurse)
				cmd->subtype = AT_ValidateConstraintRecurse;
			pass = AT_PASS_MISC;
			break;
		case AT_ReplicaIdentity:	/* REPLICA IDENTITY ... */
			ATSimplePermissions(rel, ATT_TABLE | ATT_MATVIEW);
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
			ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);
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
			ATSimplePermissions(rel, ATT_TABLE);
			/* These commands never recurse */
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_GenericOptions:
			ATSimplePermissions(rel, ATT_FOREIGN_TABLE);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_AttachPartition:
			ATSimplePermissions(rel, ATT_TABLE | ATT_PARTITIONED_INDEX);
			/* No command-specific prep needed */
			pass = AT_PASS_MISC;
			break;
		case AT_DetachPartition:
			ATSimplePermissions(rel, ATT_TABLE);
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
ATRewriteCatalogs(List **wqueue,
				  LOCKMODE lockmode,
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
										 false /* isPartitionOfAlteredTable */);
	if (handles)
		*ybAlteredTableIds = lappend_oid(*ybAlteredTableIds, main_relid);
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
												   true /*isPartitionOfAlteredTable */);
		if (child_handles)
			*ybAlteredTableIds = lappend_oid(*ybAlteredTableIds, childrelid);
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
			Relation	rel;
			ListCell   *lcmd;

			if (subcmds == NIL)
				continue;

			/*
			 * Appropriate lock was obtained by phase 1, needn't get it again
			 */
			rel = relation_open(tab->relid, NoLock);

			foreach (lcmd, subcmds)
			{
				/* Do not clang-format because this is PG code */
				/* clang-format off */
				ATExecCmd(wqueue, tab, &rel,
						  castNode(AlterTableCmd, lfirst(lcmd)),
						  lockmode);
				/* clang-format on */

				/*
				 * YB Note: The following only applies to the old ADD/DROP PK
				 * and ALTER TYPE code. This code path will be removed in a
				 * future release.
				 * It is possible that we create a new table in ATExecCmd, so we
				 * need to update main_relid and later update the rest of our
				 * commands so that they know to use the new relid
				 */
				if (IsYBRelation(rel) && !yb_enable_alter_table_rewrite &&
					(pass == AT_PASS_ALTER_TYPE || pass == AT_PASS_ADD_INDEX))
				{
					main_relid = RelationGetRelid(rel);
					relfilenode_id = YbGetRelfileNodeId(rel);
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
				(!IsYBRelation(rel) || yb_enable_alter_table_rewrite))
				ATPostAlterTypeCleanup(wqueue, tab, lockmode);

			relation_close(rel, NoLock);
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
ATExecCmd(List **wqueue, AlteredTableInfo *tab, Relation *mutable_rel,
		  AlterTableCmd *cmd, LOCKMODE lockmode)
{
	Relation      rel     = *mutable_rel;
	ObjectAddress address = InvalidObjectAddress;

	switch (cmd->subtype)
	{
		case AT_AddColumn:		/* ADD COLUMN */
		case AT_AddColumnToView:	/* add column via CREATE OR REPLACE VIEW */
			address = ATExecAddColumn(wqueue, tab, rel, (ColumnDef *) cmd->def,
									  false, false, false,
									  cmd->missing_ok, lockmode);
			break;
		case AT_AddColumnRecurse:
			address = ATExecAddColumn(wqueue, tab, rel, (ColumnDef *) cmd->def,
									  false, true, false,
									  cmd->missing_ok, lockmode);
			break;
		case AT_ColumnDefault:	/* ALTER COLUMN DEFAULT */
			address = ATExecColumnDefault(rel, cmd->name, cmd->def, lockmode);
			break;
		case AT_AddIdentity:
			address = ATExecAddIdentity(rel, cmd->name, cmd->def, lockmode);
			break;
		case AT_SetIdentity:
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
		case AT_DropColumn:		/* DROP COLUMN */
			address = ATExecDropColumn(wqueue, tab, rel, cmd->name,
									   cmd->behavior, false, false,
									   cmd->missing_ok, lockmode);
			break;
		case AT_DropColumnRecurse:	/* DROP COLUMN with recursion */
			address = ATExecDropColumn(wqueue, tab, rel, cmd->name,
									   cmd->behavior, true, false,
									   cmd->missing_ok, lockmode);
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
		case AT_AddConstraint:	/* ADD CONSTRAINT */
			address =
				ATExecAddConstraint(wqueue, tab, rel, (Constraint *) cmd->def,
									false, false, lockmode);
			break;
		case AT_AddConstraintRecurse:	/* ADD CONSTRAINT with recursion */
			address =
				ATExecAddConstraint(wqueue, tab, rel, (Constraint *) cmd->def,
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
			address = ATExecValidateConstraint(rel, cmd->name, false, false,
											   lockmode);
			break;
		case AT_ValidateConstraintRecurse:	/* VALIDATE CONSTRAINT with
											 * recursion */
			address = ATExecValidateConstraint(rel, cmd->name, true, false,
											   lockmode);
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
		case AT_AddOids:		/* SET WITH OIDS */
			/* Use the ADD COLUMN code, unless prep decided to do nothing */
			if (cmd->def != NULL)
				address =
					ATExecAddColumn(wqueue, tab, rel, (ColumnDef *) cmd->def,
									true, false, false,
									cmd->missing_ok, lockmode);
			break;
		case AT_AddOidsRecurse: /* SET WITH OIDS */
			/* Use the ADD COLUMN code, unless prep decided to do nothing */
			if (cmd->def != NULL)
				address =
					ATExecAddColumn(wqueue, tab, rel, (ColumnDef *) cmd->def,
									true, true, false,
									cmd->missing_ok, lockmode);
			break;
		case AT_DropOids:		/* SET WITHOUT OIDS */

			/*
			 * Nothing to do here; we'll have generated a DropColumn
			 * subcommand to do the real work
			 */
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
									   TRIGGER_FIRES_ON_ORIGIN, false, lockmode);
			break;
		case AT_EnableAlwaysTrig:	/* ENABLE ALWAYS TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_FIRES_ALWAYS, false, lockmode);
			break;
		case AT_EnableReplicaTrig:	/* ENABLE REPLICA TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_FIRES_ON_REPLICA, false, lockmode);
			break;
		case AT_DisableTrig:	/* DISABLE TRIGGER name */
			ATExecEnableDisableTrigger(rel, cmd->name,
									   TRIGGER_DISABLED, false, lockmode);
			break;
		case AT_EnableTrigAll:	/* ENABLE TRIGGER ALL */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_FIRES_ON_ORIGIN, false, lockmode);
			break;
		case AT_DisableTrigAll: /* DISABLE TRIGGER ALL */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_DISABLED, false, lockmode);
			break;
		case AT_EnableTrigUser: /* ENABLE TRIGGER USER */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_FIRES_ON_ORIGIN, true, lockmode);
			break;
		case AT_DisableTrigUser:	/* DISABLE TRIGGER USER */
			ATExecEnableDisableTrigger(rel, NULL,
									   TRIGGER_DISABLED, true, lockmode);
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
			ATExecEnableRowSecurity(rel);
			break;
		case AT_DisableRowSecurity:
			ATExecDisableRowSecurity(rel);
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
			if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
				ATExecAttachPartition(wqueue, rel, (PartitionCmd *) cmd->def);
			else
				ATExecAttachPartitionIdx(wqueue, rel,
										 ((PartitionCmd *) cmd->def)->name);
			break;
		case AT_DetachPartition:
			/* ATPrepCmd ensures it must be a table */
			Assert(rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE);
			ATExecDetachPartition(rel, ((PartitionCmd *) cmd->def)->name);
			break;
		default:				/* oops */
			elog(ERROR, "unrecognized alter table type: %d",
				 (int) cmd->subtype);
			break;
	}

	/*
	 * Report the subcommand to interested event triggers.
	 */
	EventTriggerCollectAlterTableSubcmd((Node *) cmd, address);

	/*
	 * Bump the command counter to ensure the next subcommand in the sequence
	 * can see the changes so far
	 */
	CommandCounterIncrement();
}

/*
 * ATRewriteTables: ALTER TABLE phase 3
 */
static void
ATRewriteTables(AlterTableStmt *parsetree, List **wqueue, LOCKMODE lockmode)
{
	ListCell   *ltab;

	/* Go through each table that needs to be checked or rewritten */
	foreach(ltab, *wqueue)
	{
		AlteredTableInfo *tab = (AlteredTableInfo *) lfirst(ltab);

		/*
		 * Foreign tables have no storage, nor do partitioned tables and
		 * indexes.
		 * YB: We want to allow rewrites on partitioned tables to avoid
		 * schema inconsistencies during backup/restore (see GH#24458).
		 */
		if (!RELKIND_CAN_HAVE_STORAGE(tab->relkind) &&
			(!IsYugaByteEnabled() || tab->relkind != RELKIND_PARTITIONED_TABLE))
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
			YbSetTxnWithPgOps(YB_TXN_USES_TEMPORARY_RELATIONS);

		/*
		 * If we change column data types or add/remove OIDs, the operation
		 * has to be propagated to tables that use this table's rowtype as a
		 * column type.  tab->newvals will also be non-NULL in the case where
		 * we're adding a column with a default.  We choose to forbid that
		 * case as well, since composite types might eventually support
		 * defaults.
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

			rel = heap_open(tab->relid, NoLock);
			find_composite_type_dependencies(rel->rd_rel->reltype, rel, NULL);
			heap_close(rel, NoLock);
		}

		/*
		 * We only need to rewrite the table if at least one column needs to
		 * be recomputed, we are adding/removing the OID column, or we are
		 * changing its persistence.
		 *
		 * There are two reasons for requiring a rewrite when changing
		 * persistence: on one hand, we need to ensure that the buffers
		 * belonging to each of the two relations are marked with or without
		 * BM_PERMANENT properly.  On the other hand, since rewriting creates
		 * and assigns a new relfilenode, we automatically create or drop an
		 * init fork for the relation as appropriate.
		 */
		if (tab->rewrite > 0)
		{
			/* Build a temporary relation and copy data */
			Relation	OldHeap;
			Oid			OIDNewHeap;
			Oid			NewTableSpace;
			char		persistence;

			OldHeap = heap_open(tab->relid, NoLock);

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
			 * Select persistence of transient table (same as original unless
			 * user requested a change)
			 */
			persistence = tab->chgPersistence ?
				tab->newrelpersistence : OldHeap->rd_rel->relpersistence;

			heap_close(OldHeap, NoLock);

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
			OIDNewHeap = make_new_heap(tab->relid, NewTableSpace, persistence,
									   lockmode,
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
		}
		else
		{
			/*
			 * Test the current data within the table against new constraints
			 * generated by ALTER TABLE commands, but don't rebuild data.
			 */
			if (tab->constraints != NIL || tab->new_notnull ||
				tab->partition_constraint != NULL)
				ATRewriteTable(tab, InvalidOid, lockmode);

			/*
			 * If we had SET TABLESPACE but no reason to reconstruct tuples,
			 * just do a block-by-block copy.
			 */
			if (tab->newTableSpace && !IsYBRelationById(tab->relid))
				ATExecSetTableSpace(tab->relid, tab->newTableSpace, lockmode);
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
					rel = heap_open(tab->relid, NoLock);
				}

				refrel = heap_open(con->refrelid, RowShareLock);

				validateForeignKeyConstraint(fkconstraint->conname, rel, refrel,
											 con->refindid,
											 con->conid);

				/*
				 * No need to mark the constraint row as validated, we did
				 * that when we inserted the row earlier.
				 */

				heap_close(refrel, NoLock);
			}
		}

		if (rel)
			heap_close(rel, NoLock);
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
	int			hi_options;
	ExprState  *partqualstate = NULL;

	/*
	 * Open the relation(s).  We have surely already locked the existing
	 * table.
	 */
	oldrel = heap_open(tab->relid, NoLock);
	oldTupDesc = tab->oldDesc;
	newTupDesc = RelationGetDescr(oldrel);	/* includes all mods */

	if (OidIsValid(OIDNewHeap))
		newrel = heap_open(OIDNewHeap, lockmode);
	else
		newrel = NULL;

	/*
	 * Prepare a BulkInsertState and options for heap_insert. Because we're
	 * building a new heap, we can skip WAL-logging and fsync it to disk at
	 * the end instead (unless WAL-logging is required for archiving or
	 * streaming replication). The FSM is empty too, so don't bother using it.
	 */
	if (newrel)
	{
		mycid = GetCurrentCommandId(true);
		bistate = GetBulkInsertState();

		hi_options = HEAP_INSERT_SKIP_FSM;
		if (!XLogIsNeeded())
			hi_options |= HEAP_INSERT_SKIP_WAL;
	}
	else
	{
		/* keep compiler quiet about using these uninitialized */
		mycid = 0;
		bistate = NULL;
		hi_options = 0;
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
	if (newrel || tab->new_notnull)
	{
		/*
		 * If we are rebuilding the tuples OR if we added any new NOT NULL
		 * constraints, check all not-null constraints.  This is a bit of
		 * overkill but it minimizes risk of bugs, and heap_attisnull is a
		 * pretty cheap test anyway.
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
		Datum	   *values;
		bool	   *isnull;
		TupleTableSlot *oldslot;
		TupleTableSlot *newslot;
		HeapScanDesc scan;
		HeapTuple	tuple;
		MemoryContext oldCxt;
		List	   *dropped_attrs = NIL;
		ListCell   *lc;
		Snapshot	snapshot;

		if (newrel)
			ereport(DEBUG1,
					(errmsg("rewriting table \"%s\"",
							RelationGetRelationName(oldrel))));
		else
			ereport(DEBUG1,
					(errmsg("verifying table \"%s\"",
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
		 * Make tuple slots for old and new tuples.  Note that even when the
		 * tuples are the same, the tupDescs might not be (consider ADD COLUMN
		 * without a default).
		 */
		oldslot = MakeSingleTupleTableSlot(oldTupDesc);
		newslot = MakeSingleTupleTableSlot(newTupDesc);

		/* Preallocate values/isnull arrays */
		i = Max(newTupDesc->natts, oldTupDesc->natts);
		values = (Datum *) palloc(i * sizeof(Datum));
		isnull = (bool *) palloc(i * sizeof(bool));
		memset(values, 0, i * sizeof(Datum));
		memset(isnull, true, i * sizeof(bool));

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
		scan = heap_beginscan(oldrel, snapshot, 0, NULL);

		/*
		 * Switch to per-tuple memory context and reset it for each tuple
		 * produced, so we don't leak memory.
		 */
		oldCxt = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

		while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
		{
			if (tab->rewrite > 0)
			{
				Oid			tupOid = InvalidOid;

				/* Extract data from old tuple */
				heap_deform_tuple(tuple, oldTupDesc, values, isnull);
				if (oldTupDesc->tdhasoid)
					tupOid = HeapTupleGetOid(tuple);

				/* Set dropped attributes to null in new tuple */
				foreach(lc, dropped_attrs)
					isnull[lfirst_int(lc)] = true;

				/*
				 * Process supplied expressions to replace selected columns.
				 * Expression inputs come from the old tuple.
				 */
				ExecStoreHeapTuple(tuple, oldslot, false);
				econtext->ecxt_scantuple = oldslot;

				foreach(l, tab->newvals)
				{
					NewColumnValue *ex = lfirst(l);

					values[ex->attnum - 1] = ExecEvalExpr(ex->exprstate,
														  econtext,
														  &isnull[ex->attnum - 1]);
				}

				/*
				 * Form the new tuple. Note that we don't explicitly pfree it,
				 * since the per-tuple memory context will be reset shortly.
				 */
				tuple = heap_form_tuple(newTupDesc, values, isnull);

				/* Preserve OID, if any */
				if (newTupDesc->tdhasoid)
					HeapTupleSetOid(tuple, tupOid);

				/*
				 * Constraints might reference the tableoid column, so
				 * initialize t_tableOid before evaluating them.
				 */
				tuple->t_tableOid = RelationGetRelid(oldrel);
			}

			/* Now check any constraints on the possibly-changed tuple */
			ExecStoreHeapTuple(tuple, newslot, false);
			econtext->ecxt_scantuple = newslot;

			foreach(l, notnull_attrs)
			{
				int			attn = lfirst_int(l);

				if (heap_attisnull(tuple, attn + 1, newTupDesc))
				{
					Form_pg_attribute attr = TupleDescAttr(newTupDesc, attn);

					ereport(ERROR,
							(errcode(ERRCODE_NOT_NULL_VIOLATION),
							 errmsg("column \"%s\" contains null values",
									NameStr(attr->attname)),
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
							if (IsYugaByteEnabled())
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
									 errmsg("check constraint \"%s\" is violated by some row",
											con->name),
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
							 errmsg("updated partition constraint for default partition would be violated by some row")));
				else
					ereport(ERROR,
							(errcode(ERRCODE_CHECK_VIOLATION),
							 errmsg("partition constraint is violated by some row")));
			}

			/* Write the tuple out to the new relation */
			if (newrel)
			{
				if (IsYBRelation(newrel))
					YBCExecuteInsert(newrel,
					                 newslot,
									 ONCONFLICT_NONE);
				else
					heap_insert(newrel, tuple, mycid, hi_options, bistate);
			}

			ResetExprContext(econtext);

			CHECK_FOR_INTERRUPTS();
		}

		MemoryContextSwitchTo(oldCxt);
		heap_endscan(scan);
		UnregisterSnapshot(snapshot);

		if (IsYBRelation(oldrel) && tab->rewrite & AT_REWRITE_COLUMN_REWRITE)
			/* Revert back to the new tuple desc */
			oldrel->rd_att = newTupDesc;

		ExecDropSingleTupleTableSlot(oldslot);
		ExecDropSingleTupleTableSlot(newslot);
	}

	FreeExecutorState(estate);

	heap_close(oldrel, NoLock);
	if (newrel)
	{
		FreeBulkInsertState(bistate);

		/* If we skipped writing WAL, then we need to sync the heap. */
		if (hi_options & HEAP_INSERT_SKIP_WAL)
			heap_sync(newrel);

		heap_close(newrel, NoLock);
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
	tab->relkind = rel->rd_rel->relkind;
	tab->oldDesc = CreateTupleDescCopyConstr(RelationGetDescr(rel));
	tab->newrelpersistence = RELPERSISTENCE_PERMANENT;
	tab->chgPersistence = false;

	*wqueue = lappend(*wqueue, tab);

	return tab;
}

/*
 * ATSimplePermissions
 *
 * - Ensure that it is a relation (or possibly a view)
 * - Ensure this user is the owner
 * - Ensure that it is not a system table
 */
static void
ATSimplePermissions(Relation rel, int allowed_targets)
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
		default:
			actual_target = 0;
			break;
	}

	/* Wrong target type? */
	if ((actual_target & allowed_targets) == 0)
		ATWrongRelkindError(rel, allowed_targets);

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
 * ATWrongRelkindError
 *
 * Throw an error when a relation has been determined to be of the wrong
 * type.
 */
static void
ATWrongRelkindError(Relation rel, int allowed_targets)
{
	char	   *msg;

	switch (allowed_targets)
	{
		case ATT_TABLE:
			msg = _("\"%s\" is not a table");
			break;
		case ATT_TABLE | ATT_VIEW:
			msg = _("\"%s\" is not a table or view");
			break;
		case ATT_TABLE | ATT_VIEW | ATT_FOREIGN_TABLE:
			msg = _("\"%s\" is not a table, view, or foreign table");
			break;
		case ATT_TABLE | ATT_VIEW | ATT_MATVIEW | ATT_INDEX:
			msg = _("\"%s\" is not a table, view, materialized view, or index");
			break;
		case ATT_TABLE | ATT_MATVIEW:
			msg = _("\"%s\" is not a table or materialized view");
			break;
		case ATT_TABLE | ATT_MATVIEW | ATT_INDEX:
			msg = _("\"%s\" is not a table, materialized view, or index");
			break;
		case ATT_TABLE | ATT_MATVIEW | ATT_FOREIGN_TABLE:
			msg = _("\"%s\" is not a table, materialized view, or foreign table");
			break;
		case ATT_TABLE | ATT_FOREIGN_TABLE:
			msg = _("\"%s\" is not a table or foreign table");
			break;
		case ATT_TABLE | ATT_COMPOSITE_TYPE | ATT_FOREIGN_TABLE:
			msg = _("\"%s\" is not a table, composite type, or foreign table");
			break;
		case ATT_TABLE | ATT_MATVIEW | ATT_INDEX | ATT_FOREIGN_TABLE:
			msg = _("\"%s\" is not a table, materialized view, index, or foreign table");
			break;
		case ATT_VIEW:
			msg = _("\"%s\" is not a view");
			break;
		case ATT_FOREIGN_TABLE:
			msg = _("\"%s\" is not a foreign table");
			break;
		default:
			/* shouldn't get here, add all necessary cases above */
			msg = _("\"%s\" is of the wrong type");
			break;
	}

	ereport(ERROR,
			(errcode(ERRCODE_WRONG_OBJECT_TYPE),
			 errmsg(msg, RelationGetRelationName(rel))));
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
				  AlterTableCmd *cmd, bool recurse, LOCKMODE lockmode)
{
	/*
	 * Propagate to children if desired.  Only plain tables and foreign tables
	 * have children, so no need to search for other relkinds.
	 */
	if (recurse &&
		(rel->rd_rel->relkind == RELKIND_RELATION ||
		 rel->rd_rel->relkind == RELKIND_FOREIGN_TABLE ||
		 rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE))
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
			ATPrepCmd(wqueue, childrel, cmd, false, true, lockmode);
			relation_close(childrel, NoLock);
		}
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
					  LOCKMODE lockmode)
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
		ATPrepCmd(wqueue, childrel, cmd, true, true, lockmode);
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
	depRel = heap_open(DependRelationId, AccessShareLock);

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
	HeapScanDesc scan;
	HeapTuple	tuple;
	List	   *result = NIL;

	classRel = heap_open(RelationRelationId, AccessShareLock);

	ScanKeyInit(&key[0],
				Anum_pg_class_reloftype,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(typeOid));

	scan = heap_beginscan_catalog(classRel, 1, key);

	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		if (behavior == DROP_RESTRICT)
			ereport(ERROR,
					(errcode(ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST),
					 errmsg("cannot alter type \"%s\" because it is the type of a typed table",
							typeName),
					 errhint("Use ALTER ... CASCADE to alter the typed tables too.")));
		else
			result = lappend_oid(result, HeapTupleGetOid(tuple));
	}

	heap_endscan(scan);
	heap_close(classRel, AccessShareLock);

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
						format_type_be(HeapTupleGetOid(typetuple)))));
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
				bool is_view, AlterTableCmd *cmd, LOCKMODE lockmode)
{
	if (rel->rd_rel->reloftype && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot add column to typed table")));

	if (rel->rd_rel->relkind == RELKIND_COMPOSITE_TYPE)
		ATTypedTableRecursion(wqueue, rel, cmd, lockmode);

	if (recurse && !is_view)
		cmd->subtype = AT_AddColumnRecurse;
}

/*
 * Add a column to a table; this handles the AT_AddOids cases as well.  The
 * return value is the address of the new column in the parent relation.
 */
static ObjectAddress
ATExecAddColumn(List **wqueue, AlteredTableInfo *tab, Relation rel,
				ColumnDef *colDef, bool isOid,
				bool recurse, bool recursing,
				bool if_not_exists, LOCKMODE lockmode)
{
	Oid			myrelid = RelationGetRelid(rel);
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
	AclResult	aclresult;
	ObjectAddress address;

	/* At top level, permission check was done in ATPrepCmd, else do it */
	if (recursing)
		ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	if (rel->rd_rel->relispartition && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot add column to a partition")));

	attrdesc = heap_open(AttributeRelationId, RowExclusiveLock);

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

			/* If it's OID, child column must actually be OID */
			if (isOid && childatt->attnum != ObjectIdAttributeNumber)
				ereport(ERROR,
						(errcode(ERRCODE_DATATYPE_MISMATCH),
						 errmsg("child table \"%s\" has a conflicting \"%s\" column",
								RelationGetRelationName(rel), colDef->colname)));

			/* Bump the existing child att's inhcount */
			childatt->attinhcount++;
			CatalogTupleUpdate(attrdesc, &tuple->t_self, tuple);

			heap_freetuple(tuple);

			/* Inform the user about the merge */
			ereport(NOTICE,
					(errmsg("merging definition of column \"%s\" for child \"%s\"",
							colDef->colname, RelationGetRelationName(rel))));

			heap_close(attrdesc, RowExclusiveLock);
			return InvalidObjectAddress;
		}
	}

	pgclass = heap_open(RelationRelationId, RowExclusiveLock);

	reltup = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(myrelid));
	if (!HeapTupleIsValid(reltup))
		elog(ERROR, "cache lookup failed for relation %u", myrelid);
	relkind = ((Form_pg_class) GETSTRUCT(reltup))->relkind;

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

	/* skip if the name already exists and if_not_exists is true */
	if (!check_for_column_name_collision(rel, colDef->colname, if_not_exists))
	{
		heap_close(attrdesc, RowExclusiveLock);
		heap_freetuple(reltup);
		heap_close(pgclass, RowExclusiveLock);
		return InvalidObjectAddress;
	}

	/* Determine the new attribute's number */
	if (isOid)
		newattnum = ObjectIdAttributeNumber;
	else
	{
		newattnum = ((Form_pg_class) GETSTRUCT(reltup))->relnatts + 1;
		if (newattnum > MaxHeapAttributeNumber)
			ereport(ERROR,
					(errcode(ERRCODE_TOO_MANY_COLUMNS),
					 errmsg("tables can have at most %d columns",
							MaxHeapAttributeNumber)));
	}

	typeTuple = typenameType(NULL, colDef->typeName, &typmod);
	tform = (Form_pg_type) GETSTRUCT(typeTuple);
	typeOid = HeapTupleGetOid(typeTuple);

	aclresult = pg_type_aclcheck(typeOid, GetUserId(), ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error_type(aclresult, typeOid);

	collOid = GetColumnDefCollation(NULL, colDef, typeOid);

	/* make sure datatype is legal for a column */
	CheckAttributeType(colDef->colname, typeOid, collOid,
					   list_make1_oid(rel->rd_rel->reltype),
					   false);

	/* construct new attribute's pg_attribute entry */
	attribute.attrelid = myrelid;
	namestrcpy(&(attribute.attname), colDef->colname);
	attribute.atttypid = typeOid;
	attribute.attstattarget = (newattnum > 0) ? -1 : 0;
	attribute.attlen = tform->typlen;
	attribute.attcacheoff = -1;
	attribute.atttypmod = typmod;
	attribute.attnum = newattnum;
	attribute.attbyval = tform->typbyval;
	attribute.attndims = list_length(colDef->typeName->arrayBounds);
	attribute.attstorage = tform->typstorage;
	attribute.attalign = tform->typalign;
	attribute.attnotnull = colDef->is_not_null;
	attribute.atthasdef = false;
	attribute.atthasmissing = false;
	attribute.attidentity = colDef->identity;
	attribute.attisdropped = false;
	attribute.attislocal = colDef->is_local;
	attribute.attinhcount = colDef->inhcount;
	attribute.attcollation = collOid;
	/* attribute.attacl is handled by InsertPgAttributeTuple */

	ReleaseSysCache(typeTuple);

	InsertPgAttributeTuple(attrdesc, &attribute, NULL,
		((Form_pg_class) GETSTRUCT(reltup))->relisshared && !IsBootstrapProcessingMode());

	heap_close(attrdesc, RowExclusiveLock);

	/*
	 * Update pg_class tuple as appropriate
	 */
	if (isOid)
		((Form_pg_class) GETSTRUCT(reltup))->relhasoids = true;
	else
		((Form_pg_class) GETSTRUCT(reltup))->relnatts = newattnum;

	CatalogTupleUpdate(pgclass, &reltup->t_self, reltup);

	heap_freetuple(reltup);

	/* Post creation hook for new attribute */
	InvokeObjectPostCreateHook(RelationRelationId, myrelid, newattnum);

	heap_close(pgclass, RowExclusiveLock);

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
		rawEnt->missingMode = true;

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
	 * We skip this step completely for views and foreign tables.  For a view,
	 * we can only get here from CREATE OR REPLACE VIEW, which historically
	 * doesn't set up defaults, not even for domain-typed columns.  And in any
	 * case we mustn't invoke Phase 3 on a view or foreign table, since they
	 * have no storage.
	 */
	if (relkind != RELKIND_VIEW && relkind != RELKIND_COMPOSITE_TYPE
		&& relkind != RELKIND_FOREIGN_TABLE && attribute.attnum > 0)
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

			tab->newvals = lappend(tab->newvals, newval);
		}

		if (DomainHasConstraints(typeOid))
			tab->rewrite |= AT_REWRITE_DEFAULT_VAL;

		if (!TupleDescAttr(rel->rd_att, attribute.attnum - 1)->atthasmissing)
		{
			/*
			 * If the new column is NOT NULL, and there is no missing value,
			 * tell Phase 3 it needs to test that. (Note we don't do this for
			 * an OID column.  OID will be marked not null, but since it's
			 * filled specially, there's no need to test anything.)
			 */
			tab->new_notnull |= colDef->is_not_null;
		}
	}

	/*
	 * If we are adding an OID column, we have to tell Phase 3 to rewrite the
	 * table to fix that.
	 */
	if (isOid)
		tab->rewrite |= AT_REWRITE_ALTER_OID;

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
	children = find_inheritance_children(RelationGetRelid(rel), lockmode);

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
		colDef = copyObject(colDef);
		colDef->inhcount = 1;
		colDef->is_local = false;
	}

	foreach(child, children)
	{
		Oid			childrelid = lfirst_oid(child);
		Relation	childrel;
		AlteredTableInfo *childtab;

		/* find_inheritance_children already got lock */
		childrel = heap_open(childrelid, NoLock);
		CheckTableNotInUse(childrel, "ALTER TABLE");

		/* Find or create work queue entry for this table */
		childtab = ATGetQueueEntry(wqueue, childrel);

		/* Recurse to child; return value is ignored */
		ATExecAddColumn(wqueue, childtab, childrel,
						colDef, isOid, recurse, true,
						if_not_exists, lockmode);

		heap_close(childrel, NoLock);
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
 * ALTER TABLE SET WITH OIDS
 *
 * Basically this is an ADD COLUMN for the special OID column.  We have
 * to cons up a ColumnDef node because the ADD COLUMN code needs one.
 */
static void
ATPrepAddOids(List **wqueue, Relation rel, bool recurse, AlterTableCmd *cmd, LOCKMODE lockmode)
{
	/* If we're recursing to a child table, the ColumnDef is already set up */
	if (cmd->def == NULL)
	{
		ColumnDef  *cdef = makeNode(ColumnDef);

		cdef->colname = pstrdup("oid");
		cdef->typeName = makeTypeNameFromOid(OIDOID, -1);
		cdef->inhcount = 0;
		cdef->is_local = true;
		cdef->is_not_null = true;
		cdef->storage = 0;
		cdef->location = -1;
		cmd->def = (Node *) cdef;
	}
	ATPrepAddColumn(wqueue, rel, recurse, false, false, cmd, lockmode);

	if (recurse)
		cmd->subtype = AT_AddOidsRecurse;
}

/*
 * ALTER TABLE ALTER COLUMN DROP NOT NULL
 *
 * Return the address of the modified column.  If the column was already
 * nullable, InvalidObjectAddress is returned.
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
		PartitionDesc partdesc = RelationGetPartitionDesc(rel);

		Assert(partdesc != NULL);
		if (partdesc->nparts > 0 && !recurse && !recursing)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot remove constraint from only the partitioned table when partitions exist"),
					 errhint("Do not specify the ONLY keyword.")));
	}
}
static ObjectAddress
ATExecDropNotNull(Relation rel, const char *colName, LOCKMODE lockmode)
{
	HeapTuple	tuple;
	AttrNumber	attnum;
	Relation	attr_rel;
	List	   *indexoidlist;
	ListCell   *indexoidscan;
	ObjectAddress address;

	/*
	 * lookup the attribute
	 */
	attr_rel = heap_open(AttributeRelationId, RowExclusiveLock);

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

	if (get_attidentity(RelationGetRelid(rel), attnum))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("column \"%s\" of relation \"%s\" is an identity column",
						colName, RelationGetRelationName(rel))));

	/*
	 * Check that the attribute is not in a primary key
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

		/* If the index is not a primary key, skip the check */
		if (indexStruct->indisprimary)
		{
			/*
			 * Loop over each attribute in the primary key and see if it
			 * matches the to-be-altered attribute
			 */
			for (i = 0; i < indexStruct->indnkeyatts; i++)
			{
				if (indexStruct->indkey.values[i] == attnum)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("column \"%s\" is in a primary key",
									colName)));
			}
		}

		ReleaseSysCache(indexTuple);
	}

	list_free(indexoidlist);

	/* If rel is partition, shouldn't drop NOT NULL if parent has the same */
	if (rel->rd_rel->relispartition)
	{
		Oid			parentId = get_partition_parent(RelationGetRelid(rel));
		Relation	parent = heap_open(parentId, AccessShareLock);
		TupleDesc	tupDesc = RelationGetDescr(parent);
		AttrNumber	parent_attnum;

		parent_attnum = get_attnum(parentId, colName);
		if (TupleDescAttr(tupDesc, parent_attnum - 1)->attnotnull)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("column \"%s\" is marked NOT NULL in parent table",
							colName)));
		heap_close(parent, AccessShareLock);
	}

	/*
	 * Okay, actually perform the catalog change ... if needed
	 */
	if (((Form_pg_attribute) GETSTRUCT(tuple))->attnotnull)
	{
		((Form_pg_attribute) GETSTRUCT(tuple))->attnotnull = false;

		CatalogTupleUpdate(attr_rel, &tuple->t_self, tuple);

		ObjectAddressSubSet(address, RelationRelationId,
							RelationGetRelid(rel), attnum);
	}
	else
		address = InvalidObjectAddress;

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel), attnum);

	heap_close(attr_rel, RowExclusiveLock);

	return address;
}

/*
 * ALTER TABLE ALTER COLUMN SET NOT NULL
 */

static void
ATPrepSetNotNull(Relation rel, bool recurse, bool recursing)
{
	/*
	 * If the parent is a partitioned table, like check constraints, NOT NULL
	 * constraints must be added to the child tables.  Complain if requested
	 * otherwise and partitions exist.
	 */
	if (rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		PartitionDesc partdesc = RelationGetPartitionDesc(rel);

		if (partdesc && partdesc->nparts > 0 && !recurse && !recursing)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot add constraint to only the partitioned table when partitions exist"),
					 errhint("Do not specify the ONLY keyword.")));
	}
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
	attr_rel = heap_open(AttributeRelationId, RowExclusiveLock);

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

		/* Tell Phase 3 it needs to test the constraint */
		tab->new_notnull = true;

		ObjectAddressSubSet(address, RelationRelationId,
							RelationGetRelid(rel), attnum);
	}
	else
		address = InvalidObjectAddress;

	InvokeObjectPostAlterHook(RelationRelationId,
							  RelationGetRelid(rel), attnum);

	heap_close(attr_rel, RowExclusiveLock);

	return address;
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

	if (get_attidentity(RelationGetRelid(rel), attnum))
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("column \"%s\" of relation \"%s\" is an identity column",
						colName, RelationGetRelationName(rel)),
				 newDefault ? 0 : errhint("Use ALTER TABLE ... ALTER COLUMN ... DROP IDENTITY instead.")));

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
					  newDefault == NULL ? false : true);

	if (newDefault)
	{
		/* SET DEFAULT */
		RawColumnDefault *rawEnt;

		rawEnt = (RawColumnDefault *) palloc(sizeof(RawColumnDefault));
		rawEnt->attnum = attnum;
		rawEnt->raw_default = newDefault;
		rawEnt->missingMode = false;

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

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);

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

	heap_close(attrelation, RowExclusiveLock);

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

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);
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
	heap_close(attrelation, RowExclusiveLock);

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

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);
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
			heap_close(attrelation, RowExclusiveLock);
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

	heap_close(attrelation, RowExclusiveLock);

	/* drop the internal sequence */
	seqid = getOwnedSequence(RelationGetRelid(rel), attnum);
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
 * ALTER TABLE ALTER COLUMN SET STATISTICS
 */
static void
ATPrepSetStatistics(Relation rel, const char *colName, int16 colNum, Node *newValue, LOCKMODE lockmode)
{
	/*
	 * We do our own permission checking because (a) we want to allow SET
	 * STATISTICS on indexes (for expressional index columns), and (b) we want
	 * to allow SET STATISTICS on system catalogs without requiring
	 * allowSystemTableMods to be turned on.
	 */
	if (rel->rd_rel->relkind != RELKIND_RELATION &&
		rel->rd_rel->relkind != RELKIND_MATVIEW &&
		rel->rd_rel->relkind != RELKIND_INDEX &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_INDEX &&
		rel->rd_rel->relkind != RELKIND_FOREIGN_TABLE &&
		rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a table, materialized view, index, or foreign table",
						RelationGetRelationName(rel))));

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

	/* Permissions checks */
	if (!pg_class_ownercheck(RelationGetRelid(rel), GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, get_relkind_objtype(rel->rd_rel->relkind),
					   RelationGetRelationName(rel));
}

/*
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

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);

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

	heap_close(attrelation, RowExclusiveLock);

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

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);

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

	heap_close(attrelation, RowExclusiveLock);

	return address;
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
		newstorage = 'p';
	else if (pg_strcasecmp(storagemode, "external") == 0)
		newstorage = 'e';
	else if (pg_strcasecmp(storagemode, "extended") == 0)
		newstorage = 'x';
	else if (pg_strcasecmp(storagemode, "main") == 0)
		newstorage = 'm';
	else
	{
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("invalid storage type \"%s\"",
						storagemode)));
		newstorage = 0;			/* keep compiler quiet */
	}

	attrelation = heap_open(AttributeRelationId, RowExclusiveLock);

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
	if (newstorage == 'p' || TypeIsToastable(attrtuple->atttypid))
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

	heap_close(attrelation, RowExclusiveLock);

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
				 AlterTableCmd *cmd, LOCKMODE lockmode)
{
	if (rel->rd_rel->reloftype && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot drop column from typed table")));

	if (rel->rd_rel->relkind == RELKIND_COMPOSITE_TYPE)
		ATTypedTableRecursion(wqueue, rel, cmd, lockmode);

	if (recurse)
		cmd->subtype = AT_DropColumnRecurse;
}

/*
 * Return value is the address of the dropped column.
 */
static ObjectAddress
ATExecDropColumn(List **wqueue, AlteredTableInfo *yb_tab, Relation rel,
				 const char *colName,
				 DropBehavior behavior,
				 bool recurse, bool recursing,
				 bool missing_ok, LOCKMODE lockmode)
{
	HeapTuple	tuple;
	Form_pg_attribute targetatt;
	AttrNumber	attnum;
	List	   *children;
	ObjectAddress object;
	bool		is_expr;

	/* At top level, permission check was done in ATPrepCmd, else do it */
	if (recursing)
		ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);

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
	if (attnum <= 0 && attnum != ObjectIdAttributeNumber)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot drop system column \"%s\"",
						colName)));

	/* Don't drop inherited columns */
	if (targetatt->attinhcount > 0 && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot drop inherited column \"%s\"",
						colName)));

	/* Don't drop columns used in the partition key */
	if (has_partition_attrs(rel,
							bms_make_singleton(attnum - YBGetFirstLowInvalidAttributeNumber(rel)),
							&is_expr))
	{
		if (!is_expr)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot drop column named in partition key")));
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot drop column referenced in partition key expression")));
	}

	ReleaseSysCache(tuple);

	/*
	 * Propagate to children as appropriate.  Unlike most other ALTER
	 * routines, we have to do this one level of recursion at a time; we can't
	 * use find_all_inheritors to do it in one pass.
	 */
	children = find_inheritance_children(RelationGetRelid(rel), lockmode);

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

		attr_rel = heap_open(AttributeRelationId, RowExclusiveLock);
		foreach(child, children)
		{
			Oid			childrelid = lfirst_oid(child);
			Relation	childrel;
			Form_pg_attribute childatt;

			/* find_inheritance_children already got lock */
			childrel = heap_open(childrelid, NoLock);
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
					AlteredTableInfo *yb_childtab = ATGetQueueEntry(wqueue, childrel);
					/* Time to delete this child column, too */
					ATExecDropColumn(wqueue, yb_childtab, childrel, colName,
									 behavior, true, true,
									 false, lockmode);
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

			heap_close(childrel, NoLock);
		}
		heap_close(attr_rel, RowExclusiveLock);
	}

	/*
	 * Perform the actual column deletion
	 */
	object.classId = RelationRelationId;
	object.objectId = RelationGetRelid(rel);
	object.objectSubId = attnum;

	/*
	 * YB: Skip YB drop on the column, as that will be handled separately by
	 * the ALTER TABLE flow.
	 */
	performDeletion(&object, behavior,
		IsYugaByteEnabled() ? YB_SKIP_YB_DROP_COLUMN : 0);

	/*
	 * If we dropped the OID column, must adjust pg_class.relhasoids and tell
	 * Phase 3 to physically get rid of the column.  We formerly left the
	 * column in place physically, but this caused subtle problems.  See
	 * http://archives.postgresql.org/pgsql-hackers/2009-02/msg00363.php
	 */
	if (attnum == ObjectIdAttributeNumber)
	{
		Relation	class_rel;
		Form_pg_class tuple_class;
		AlteredTableInfo *tab;

		class_rel = heap_open(RelationRelationId, RowExclusiveLock);

		tuple = SearchSysCacheCopy1(RELOID,
									ObjectIdGetDatum(RelationGetRelid(rel)));
		if (!HeapTupleIsValid(tuple))
			elog(ERROR, "cache lookup failed for relation %u",
				 RelationGetRelid(rel));
		tuple_class = (Form_pg_class) GETSTRUCT(tuple);

		tuple_class->relhasoids = false;
		CatalogTupleUpdate(class_rel, &tuple->t_self, tuple);

		heap_close(class_rel, RowExclusiveLock);

		/* Find or create work queue entry for this table */
		tab = ATGetQueueEntry(wqueue, rel);

		/* Tell Phase 3 to physically remove the OID column */
		tab->rewrite |= AT_REWRITE_ALTER_OID;
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
		Datum result = SysCacheGetAttr(CONSTROID, \
		                               tuple, \
		                               Anum_pg_constraint_##attname, \
		                               &isnull); \
		if (isnull) \
			elog(ERROR, "null " #attname " for constraint %u", HeapTupleGetOid(tuple)); \
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
	 * index instead of building from scratch.  The DROP of the old edition of
	 * this index will have scheduled the storage for deletion at commit, so
	 * cancel that pending deletion.
	 */
	if (OidIsValid(stmt->oldNode))
	{
		Relation	irel = index_open(address.objectId, NoLock);

		RelationPreserveStorage(irel->rd_node, true);
		index_close(irel, NoLock);
	}

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
		RenameRelationInternal(index_oid, constraintName, false);
		/* in YB, rename the DocDB table for the index */
		if (IsYBRelationById(index_oid) && !stmt->primary)
			YBCRename(index_oid, OBJECT_INDEX, constraintName,
					  NULL /* colname */ );
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
			 * Note that we currently never recurse for FK constraints, so the
			 * "recurse" flag is silently ignored.
			 *
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
										 strVal(linitial(newConstraint->fk_attrs)),
										 "fkey",
										 RelationGetNamespace(rel),
										 NIL);

			address = ATAddForeignKeyConstraint(wqueue, tab, rel,
												newConstraint, InvalidOid,
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
		ATSimplePermissions(rel, ATT_TABLE | ATT_FOREIGN_TABLE);

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
										NULL);		/* queryString not available here */

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
	children = find_inheritance_children(RelationGetRelid(rel), lockmode);

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
		childrel = heap_open(childrelid, NoLock);
		CheckTableNotInUse(childrel, "ALTER TABLE");

		/* Find or create work queue entry for this table */
		childtab = ATGetQueueEntry(wqueue, childrel);

		/* Recurse to child */
		ATAddCheckConstraint(wqueue, childtab, childrel,
							 constr, recurse, true, is_readd, lockmode);

		heap_close(childrel, NoLock);
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
 */
static ObjectAddress
ATAddForeignKeyConstraint(List **wqueue, AlteredTableInfo *tab, Relation rel,
						  Constraint *fkconstraint, Oid parentConstr,
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
	bool		connoinherit;
	int			i;
	int			numfks,
				numpks;
	Oid			indexOid;
	Oid			constrOid;
	bool		old_check_ok;
	ObjectAddress address;
	ListCell   *old_pfeqop_item = list_head(fkconstraint->old_conpfeqop);

	/*
	 * Grab ShareRowExclusiveLock on the pk table, so that someone doesn't
	 * delete rows out from under us.
	 */
	if (OidIsValid(fkconstraint->old_pktable_oid))
		pkrel = heap_open(fkconstraint->old_pktable_oid, ShareRowExclusiveLock);
	else
		pkrel = heap_openrv(fkconstraint->pktable, ShareRowExclusiveLock);

	/*
	 * Validity checks (permission checks wait till we have the column
	 * numbers)
	 */
	if (pkrel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot reference partitioned table \"%s\"",
						RelationGetRelationName(pkrel))));

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

	if (pkrel->rd_rel->relkind != RELKIND_RELATION)
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
			if (pkrel->rd_rel->relpersistence != RELPERSISTENCE_PERMANENT)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
						 errmsg("constraints on permanent tables may reference only permanent tables")));
			break;
		case RELPERSISTENCE_UNLOGGED:
			if (pkrel->rd_rel->relpersistence != RELPERSISTENCE_PERMANENT
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

	numfks = transformColumnNameList(RelationGetRelid(rel),
									 fkconstraint->fk_attrs,
									 fkattnum, fktypoid);

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
					 errmsg("foreign key constraint \"%s\" "
							"cannot be implemented",
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
			old_pfeqop_item = lnext(old_pfeqop_item);
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
	 * FKs always inherit for partitioned tables, and never for legacy
	 * inheritance.
	 */
	connoinherit = rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE;

	/*
	 * Record the FK constraint in pg_constraint.
	 */
	constrOid = CreateConstraintEntry(fkconstraint->conname,
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
									  numpks,
									  fkconstraint->fk_upd_action,
									  fkconstraint->fk_del_action,
									  fkconstraint->fk_matchtype,
									  NULL, /* no exclusion constraint */
									  NULL, /* no check constraint */
									  NULL,
									  NULL,
									  true, /* islocal */
									  0,	/* inhcount */
									  connoinherit,	/* conNoInherit */
									  false);	/* is_internal */
	ObjectAddressSet(address, ConstraintRelationId, constrOid);

	/*
	 * Create the triggers that will enforce the constraint.  We only want the
	 * action triggers to appear for the parent partitioned relation, even
	 * though the constraints also exist below.
	 */
	createForeignKeyTriggers(rel, RelationGetRelid(pkrel), fkconstraint,
							 constrOid, indexOid, !recursing);

	/*
	 * Tell Phase 3 to check that the constraint is satisfied by existing
	 * rows. We can skip this during table creation, when requested explicitly
	 * by specifying NOT VALID in an ADD FOREIGN KEY command, and when we're
	 * recreating a constraint following a SET DATA TYPE operation that did
	 * not impugn its validity.
	 */
	if (!old_check_ok && !fkconstraint->skip_validation)
	{
		NewConstraint *newcon;

		newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
		newcon->name = fkconstraint->conname;
		newcon->contype = CONSTR_FOREIGN;
		newcon->refrelid = RelationGetRelid(pkrel);
		newcon->refindid = indexOid;
		newcon->conid = constrOid;
		newcon->qual = (Node *) fkconstraint;

		tab->constraints = lappend(tab->constraints, newcon);
	}

	/*
	 * When called on a partitioned table, recurse to create the constraint on
	 * the partitions also.
	 */
	if (recurse && rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		PartitionDesc partdesc;
		Relation	pg_constraint;
		List	   *cloned = NIL;
		ListCell   *cell;

		pg_constraint = heap_open(ConstraintRelationId, RowExclusiveLock);
		partdesc = RelationGetPartitionDesc(rel);

		for (i = 0; i < partdesc->nparts; i++)
		{
			Oid			partitionId = partdesc->oids[i];
			Relation	partition = heap_open(partitionId, lockmode);

			CheckTableNotInUse(partition, "ALTER TABLE");

			CloneFkReferencing(pg_constraint, rel, partition,
							   list_make1_oid(constrOid),
							   &cloned);

			heap_close(partition, NoLock);
		}
		heap_close(pg_constraint, RowExclusiveLock);

		foreach(cell, cloned)
		{
			ClonedConstraint *cc = (ClonedConstraint *) lfirst(cell);
			Relation    partition = heap_open(cc->relid, lockmode);
			AlteredTableInfo *childtab;
			NewConstraint *newcon;

			/* Find or create work queue entry for this partition */
			childtab = ATGetQueueEntry(wqueue, partition);

			newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
			newcon->name = cc->constraint->conname;
			newcon->contype = CONSTR_FOREIGN;
			newcon->refrelid = cc->refrelid;
			newcon->refindid = cc->conindid;
			newcon->conid = cc->conid;
			newcon->qual = (Node *) fkconstraint;

			childtab->constraints = lappend(childtab->constraints, newcon);

			heap_close(partition, lockmode);
		}
	}

	/*
	 * Close pk table, but keep lock until we've committed.
	 */
	heap_close(pkrel, NoLock);

	return address;
}

/*
 * CloneForeignKeyConstraints
 *		Clone foreign keys from a partitioned table to a newly acquired
 *		partition.
 *
 * relationId is a partition of parentId, so we can be certain that it has the
 * same columns with the same datatypes.  The columns may be in different
 * order, though.
 *
 * The *cloned list is appended ClonedConstraint elements describing what was
 * created, for the purposes of validating the constraint in ALTER TABLE's
 * Phase 3.
 */
void
CloneForeignKeyConstraints(Oid parentId, Oid relationId, List **cloned)
{
	Relation	pg_constraint;
	Relation	parentRel;
	Relation	rel;
	ScanKeyData key;
	SysScanDesc scan;
	HeapTuple	tuple;
	List	   *clone = NIL;

	parentRel = heap_open(parentId, NoLock);	/* already got lock */
	/* see ATAddForeignKeyConstraint about lock level */
	rel = heap_open(relationId, AccessExclusiveLock);
	pg_constraint = heap_open(ConstraintRelationId, RowShareLock);

	/* Obtain the list of constraints to clone or attach */
	ScanKeyInit(&key,
				Anum_pg_constraint_conrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(parentId));
	scan = systable_beginscan(pg_constraint, ConstraintRelidTypidNameIndexId, true,
							  NULL, 1, &key);
	while ((tuple = systable_getnext(scan)) != NULL)
	{
		Oid		oid = HeapTupleGetOid(tuple);

		clone = lappend_oid(clone, oid);
	}
	systable_endscan(scan);

	/* Do the actual work, recursing to partitions as needed */
	CloneFkReferencing(pg_constraint, parentRel, rel, clone, cloned);

	/* We're done.  Clean up */
	heap_close(parentRel, NoLock);
	heap_close(rel, NoLock);	/* keep lock till commit */
	heap_close(pg_constraint, RowShareLock);
}

/*
 * CloneFkReferencing
 *		Recursive subroutine for CloneForeignKeyConstraints, referencing side
 *
 * Clone the given list of FK constraints when a partition is attached on the
 * referencing side of those constraints.
 *
 * When cloning foreign keys to a partition, it may happen that equivalent
 * constraints already exist in the partition for some of them.  We can skip
 * creating a clone in that case, and instead just attach the existing
 * constraint to the one in the parent.
 *
 * This function recurses to partitions, if the new partition is partitioned;
 * of course, only do this for FKs that were actually cloned.
 */
static void
CloneFkReferencing(Relation pg_constraint, Relation parentRel,
				   Relation partRel, List *clone, List **cloned)
{
	AttrNumber *attmap;
	List	   *partFKs;
	List	   *subclone = NIL;
	ListCell   *cell;

	/*
	 * The constraint key may differ, if the columns in the partition are
	 * different.  This map is used to convert them.
	 */
	attmap = convert_tuples_by_name_map(RelationGetDescr(partRel),
										RelationGetDescr(parentRel),
										gettext_noop("could not convert row "
													 "type"),
										false /* yb_ignore_type_mismatch */);

	partFKs = copyObject(RelationGetFKeyList(partRel));

	foreach(cell, clone)
	{
		Oid			parentConstrOid = lfirst_oid(cell);
		Form_pg_constraint constrForm;
		HeapTuple	tuple;
		int			numfks;
		AttrNumber	conkey[INDEX_MAX_KEYS];
		AttrNumber	mapped_conkey[INDEX_MAX_KEYS];
		AttrNumber	confkey[INDEX_MAX_KEYS];
		Oid			conpfeqop[INDEX_MAX_KEYS];
		Oid			conppeqop[INDEX_MAX_KEYS];
		Oid			conffeqop[INDEX_MAX_KEYS];
		Constraint *fkconstraint;
		bool		attach_it;
		Oid			constrOid;
		ObjectAddress parentAddr,
					childAddr;
		ListCell   *cell;
		int			i;

		tuple = SearchSysCache1(CONSTROID, parentConstrOid);
		if (!tuple)
			elog(ERROR, "cache lookup failed for constraint %u",
				 parentConstrOid);
		constrForm = (Form_pg_constraint) GETSTRUCT(tuple);

		/* only foreign keys */
		if (constrForm->contype != CONSTRAINT_FOREIGN)
		{
			ReleaseSysCache(tuple);
			continue;
		}

		ObjectAddressSet(parentAddr, ConstraintRelationId, parentConstrOid);

		DeconstructFkConstraintRow(tuple, &numfks, conkey, confkey,
								   conpfeqop, conppeqop, conffeqop);
		for (i = 0; i < numfks; i++)
			mapped_conkey[i] = attmap[conkey[i] - 1];

		/*
		 * Before creating a new constraint, see whether any existing FKs are
		 * fit for the purpose.  If one is, attach the parent constraint to it,
		 * and don't clone anything.  This way we avoid the expensive
		 * verification step and don't end up with a duplicate FK.  This also
		 * means we don't consider this constraint when recursing to
		 * partitions.
		 */
		attach_it = false;
		foreach(cell, partFKs)
		{
			ForeignKeyCacheInfo *fk = lfirst_node(ForeignKeyCacheInfo, cell);
			Form_pg_constraint partConstr;
			HeapTuple	partcontup;
			Relation	trigrel;
			HeapTuple	trigtup;
			SysScanDesc scan;
			ScanKeyData key;

			attach_it = true;

			/*
			 * Do some quick & easy initial checks.  If any of these fail, we
			 * cannot use this constraint, but keep looking.
			 */
			if (fk->confrelid != constrForm->confrelid || fk->nkeys != numfks)
			{
				attach_it = false;
				continue;
			}
			for (i = 0; i < numfks; i++)
			{
				if (fk->conkey[i] != mapped_conkey[i] ||
					fk->confkey[i] != confkey[i] ||
					fk->conpfeqop[i] != conpfeqop[i])
				{
					attach_it = false;
					break;
				}
			}
			if (!attach_it)
				continue;

			/*
			 * Looks good so far; do some more extensive checks.  Presumably
			 * the check for 'convalidated' could be dropped, since we don't
			 * really care about that, but let's be careful for now.
			 */
			partcontup = SearchSysCache1(CONSTROID,
										 ObjectIdGetDatum(fk->conoid));
			if (!partcontup)
				elog(ERROR, "cache lookup failed for constraint %u",
					 fk->conoid);
			partConstr = (Form_pg_constraint) GETSTRUCT(partcontup);
			if (OidIsValid(partConstr->conparentid) ||
				!partConstr->convalidated ||
				partConstr->condeferrable != constrForm->condeferrable ||
				partConstr->condeferred != constrForm->condeferred ||
				partConstr->confupdtype != constrForm->confupdtype ||
				partConstr->confdeltype != constrForm->confdeltype ||
				partConstr->confmatchtype != constrForm->confmatchtype)
			{
				ReleaseSysCache(partcontup);
				attach_it = false;
				continue;
			}

			ReleaseSysCache(partcontup);

			/*
			 * Looks good!  Attach this constraint.  The action triggers in
			 * the new partition become redundant -- the parent table already
			 * has equivalent ones, and those will be able to reach the
			 * partition.  Remove the ones in the partition.  We identify them
			 * because they have our constraint OID, as well as being on the
			 * referenced rel.
			 */
			trigrel = heap_open(TriggerRelationId, RowExclusiveLock);
			ScanKeyInit(&key,
						Anum_pg_trigger_tgconstraint,
						BTEqualStrategyNumber, F_OIDEQ,
						ObjectIdGetDatum(fk->conoid));

			scan = systable_beginscan(trigrel, TriggerConstraintIndexId, true,
									  NULL, 1, &key);
			while ((trigtup = systable_getnext(scan)) != NULL)
			{
				Form_pg_trigger	trgform = (Form_pg_trigger) GETSTRUCT(trigtup);
				ObjectAddress	trigger;

				if (trgform->tgconstrrelid != fk->conrelid)
					continue;
				if (trgform->tgrelid != fk->confrelid)
					continue;

				/*
				 * The constraint is originally set up to contain this trigger
				 * as an implementation object, so there's a dependency record
				 * that links the two; however, since the trigger is no longer
				 * needed, we remove the dependency link in order to be able
				 * to drop the trigger while keeping the constraint intact.
				 */
				deleteDependencyRecordsFor(TriggerRelationId,
										   HeapTupleGetOid(trigtup),
										   false);
				/* make dependency deletion visible to performDeletion */
				CommandCounterIncrement();
				ObjectAddressSet(trigger, TriggerRelationId,
								 HeapTupleGetOid(trigtup));
				performDeletion(&trigger, DROP_RESTRICT, 0);
				/* make trigger drop visible, in case the loop iterates */
				CommandCounterIncrement();
			}

			systable_endscan(scan);
			heap_close(trigrel, RowExclusiveLock);

			ConstraintSetParentConstraint(fk->conoid, parentConstrOid);
			CommandCounterIncrement();
			attach_it = true;
			break;
		}

		/*
		 * If we attached to an existing constraint, there is no need to
		 * create a new one.  In fact, there's no need to recurse for this
		 * constraint to partitions, either.
		 */
		if (attach_it)
		{
			ReleaseSysCache(tuple);
			continue;
		}

		constrOid =
			CreateConstraintEntry(NameStr(constrForm->conname),
								  constrForm->connamespace,
								  CONSTRAINT_FOREIGN,
								  constrForm->condeferrable,
								  constrForm->condeferred,
								  constrForm->convalidated,
								  parentConstrOid,
								  RelationGetRelid(partRel),
								  mapped_conkey,
								  numfks,
								  numfks,
								  InvalidOid,	/* not a domain constraint */
								  constrForm->conindid, /* same index */
								  constrForm->confrelid,	/* same foreign rel */
								  confkey,
								  conpfeqop,
								  conppeqop,
								  conffeqop,
								  numfks,
								  constrForm->confupdtype,
								  constrForm->confdeltype,
								  constrForm->confmatchtype,
								  NULL,
								  NULL,
								  NULL,
								  NULL,
								  false,
								  1, false, true);
		subclone = lappend_oid(subclone, constrOid);

		ObjectAddressSet(childAddr, ConstraintRelationId, constrOid);
		recordDependencyOn(&childAddr, &parentAddr, DEPENDENCY_INTERNAL_AUTO);

		fkconstraint = makeNode(Constraint);
		/* for now this is all we need */
		fkconstraint->conname = pstrdup(NameStr(constrForm->conname));
		fkconstraint->fk_upd_action = constrForm->confupdtype;
		fkconstraint->fk_del_action = constrForm->confdeltype;
		fkconstraint->deferrable = constrForm->condeferrable;
		fkconstraint->initdeferred = constrForm->condeferred;

		createForeignKeyTriggers(partRel, constrForm->confrelid, fkconstraint,
								 constrOid, constrForm->conindid, false);

		if (cloned)
		{
			ClonedConstraint *newc;

			/*
			 * Feed back caller about the constraints we created, so that they
			 * can set up constraint verification.
			 */
			newc = palloc(sizeof(ClonedConstraint));
			newc->relid = RelationGetRelid(partRel);
			newc->refrelid = constrForm->confrelid;
			newc->conindid = constrForm->conindid;
			newc->conid = constrOid;
			newc->constraint = fkconstraint;

			*cloned = lappend(*cloned, newc);
		}

		ReleaseSysCache(tuple);
	}

	pfree(attmap);
	list_free_deep(partFKs);

	/*
	 * If the partition is partitioned, recurse to handle any constraints that
	 * were cloned.
	 */
	if (partRel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE &&
		subclone != NIL)
	{
		PartitionDesc partdesc = RelationGetPartitionDesc(partRel);
		int			i;

		for (i = 0; i < partdesc->nparts; i++)
		{
			Relation	childRel;

			childRel = heap_open(partdesc->oids[i], AccessExclusiveLock);
			CloneFkReferencing(pg_constraint,
							   partRel,
							   childRel,
							   subclone,
							   cloned);
			heap_close(childRel, NoLock);	/* keep lock till commit */
		}
	}
}

/*
 * ALTER TABLE ALTER CONSTRAINT
 *
 * Update the attributes of a constraint.
 *
 * Currently only works for Foreign Key constraints.
 * Foreign keys do not inherit, so we purposely ignore the
 * recursion bit here, but we keep the API the same for when
 * other constraint types are supported.
 *
 * If the constraint is modified, returns its address; otherwise, return
 * InvalidObjectAddress.
 */
static ObjectAddress
ATExecAlterConstraint(Relation rel, AlterTableCmd *cmd,
					  bool recurse, bool recursing, LOCKMODE lockmode)
{
	Constraint *cmdcon;
	Relation	conrel;
	SysScanDesc scan;
	ScanKeyData skey[3];
	HeapTuple	contuple;
	Form_pg_constraint currcon;
	ObjectAddress address;

	cmdcon = castNode(Constraint, cmd->def);

	conrel = heap_open(ConstraintRelationId, RowExclusiveLock);

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

	if (currcon->condeferrable != cmdcon->deferrable ||
		currcon->condeferred != cmdcon->initdeferred)
	{
		HeapTuple	copyTuple;
		HeapTuple	tgtuple;
		Form_pg_constraint copy_con;
		List	   *otherrelids = NIL;
		ScanKeyData tgkey;
		SysScanDesc tgscan;
		Relation	tgrel;
		ListCell   *lc;

		/*
		 * Now update the catalog, while we have the door open.
		 */
		copyTuple = heap_copytuple(contuple);
		copy_con = (Form_pg_constraint) GETSTRUCT(copyTuple);
		copy_con->condeferrable = cmdcon->deferrable;
		copy_con->condeferred = cmdcon->initdeferred;
		CatalogTupleUpdate(conrel, &copyTuple->t_self, copyTuple);

		InvokeObjectPostAlterHook(ConstraintRelationId,
								  HeapTupleGetOid(contuple), 0);

		heap_freetuple(copyTuple);

		/*
		 * Now we need to update the multiple entries in pg_trigger that
		 * implement the constraint.
		 */
		tgrel = heap_open(TriggerRelationId, RowExclusiveLock);

		ScanKeyInit(&tgkey,
					Anum_pg_trigger_tgconstraint,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(HeapTupleGetOid(contuple)));

		tgscan = systable_beginscan(tgrel, TriggerConstraintIndexId, true,
									NULL, 1, &tgkey);

		while (HeapTupleIsValid(tgtuple = systable_getnext(tgscan)))
		{
			Form_pg_trigger tgform = (Form_pg_trigger) GETSTRUCT(tgtuple);
			Form_pg_trigger copy_tg;

			/*
			 * Remember OIDs of other relation(s) involved in FK constraint.
			 * (Note: it's likely that we could skip forcing a relcache inval
			 * for other rels that don't have a trigger whose properties
			 * change, but let's be conservative.)
			 */
			if (tgform->tgrelid != RelationGetRelid(rel))
				otherrelids = list_append_unique_oid(otherrelids,
													 tgform->tgrelid);

			/*
			 * Update deferrability of RI_FKey_noaction_del,
			 * RI_FKey_noaction_upd, RI_FKey_check_ins and RI_FKey_check_upd
			 * triggers, but not others; see createForeignKeyTriggers and
			 * CreateFKCheckTrigger.
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

			InvokeObjectPostAlterHook(TriggerRelationId,
									  HeapTupleGetOid(tgtuple), 0);

			heap_freetuple(copyTuple);
		}

		systable_endscan(tgscan);

		heap_close(tgrel, RowExclusiveLock);

		/*
		 * Invalidate relcache so that others see the new attributes.  We must
		 * inval both the named rel and any others having relevant triggers.
		 * (At present there should always be exactly one other rel, but
		 * there's no need to hard-wire such an assumption here.)
		 */
		CacheInvalidateRelcache(rel);
		foreach(lc, otherrelids)
		{
			CacheInvalidateRelcacheByRelid(lfirst_oid(lc));
		}

		ObjectAddressSet(address, ConstraintRelationId,
						 HeapTupleGetOid(contuple));
	}
	else
		address = InvalidObjectAddress;

	systable_endscan(scan);

	heap_close(conrel, RowExclusiveLock);

	return address;
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
ATExecValidateConstraint(Relation rel, char *constrName, bool recurse,
						 bool recursing, LOCKMODE lockmode)
{
	Relation	conrel;
	SysScanDesc scan;
	ScanKeyData skey[3];
	HeapTuple	tuple;
	Form_pg_constraint con;
	ObjectAddress address;

	conrel = heap_open(ConstraintRelationId, RowExclusiveLock);

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
		HeapTuple	copyTuple;
		Form_pg_constraint copy_con;

		if (con->contype == CONSTRAINT_FOREIGN)
		{
			Relation	refrel;

			/*
			 * Triggers are already in place on both tables, so a concurrent
			 * write that alters the result here is not possible. Normally we
			 * can run a query here to do the validation, which would only
			 * require AccessShareLock. In some cases, it is possible that we
			 * might need to fire triggers to perform the check, so we take a
			 * lock at RowShareLock level just in case.
			 */
			refrel = heap_open(con->confrelid, RowShareLock);

			validateForeignKeyConstraint(constrName, rel, refrel,
										 con->conindid,
										 HeapTupleGetOid(tuple));
			heap_close(refrel, NoLock);

			/*
			 * We disallow creating invalid foreign keys to or from
			 * partitioned tables, so ignoring the recursion bit is okay.
			 */
		}
		else if (con->contype == CONSTRAINT_CHECK)
		{
			List	   *children = NIL;
			ListCell   *child;

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
				childrel = heap_open(childoid, NoLock);

				ATExecValidateConstraint(childrel, constrName, false,
										 true, lockmode);
				heap_close(childrel, NoLock);
			}

			validateCheckConstraint(rel, tuple);

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

		InvokeObjectPostAlterHook(ConstraintRelationId,
								  HeapTupleGetOid(tuple), 0);

		heap_freetuple(copyTuple);

		ObjectAddressSet(address, ConstraintRelationId,
						 HeapTupleGetOid(tuple));
	}
	else
		address = InvalidObjectAddress; /* already validated */

	systable_endscan(scan);

	heap_close(conrel, RowExclusiveLock);

	return address;
}


/*
 * transformColumnNameList - transform list of column names
 *
 * Lookup each name and return its attnum and type OID
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
		if (indexStruct->indisprimary && IndexIsValid(indexStruct))
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
			IndexIsValid(indexStruct) &&
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

/*
 * Scan the existing rows in a table to verify they meet a proposed
 * CHECK constraint.
 *
 * The caller must have opened and locked the relation appropriately.
 */
static void
validateCheckConstraint(Relation rel, HeapTuple constrtup)
{
	EState	   *estate;
	Datum		val;
	char	   *conbin;
	Expr	   *origexpr;
	ExprState  *exprstate;
	TupleDesc	tupdesc;
	HeapScanDesc scan;
	HeapTuple	tuple;
	ExprContext *econtext;
	MemoryContext oldcxt;
	TupleTableSlot *slot;
	Form_pg_constraint constrForm;
	bool		isnull;
	Snapshot	snapshot;

	/*
	 * VALIDATE CONSTRAINT is a no-op for foreign tables and partitioned
	 * tables.
	 */
	if (rel->rd_rel->relkind == RELKIND_FOREIGN_TABLE ||
		rel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
		return;

	constrForm = (Form_pg_constraint) GETSTRUCT(constrtup);

	estate = CreateExecutorState();

	/*
	 * XXX this tuple doesn't really come from a syscache, but this doesn't
	 * matter to SysCacheGetAttr, because it only wants to be able to fetch
	 * the tupdesc
	 */
	val = SysCacheGetAttr(CONSTROID, constrtup, Anum_pg_constraint_conbin,
						  &isnull);
	if (isnull)
		elog(ERROR, "null conbin for constraint %u",
			 HeapTupleGetOid(constrtup));
	conbin = TextDatumGetCString(val);
	origexpr = (Expr *) stringToNode(conbin);
	exprstate = ExecPrepareExpr(origexpr, estate);

	econtext = GetPerTupleExprContext(estate);
	tupdesc = RelationGetDescr(rel);
	slot = MakeSingleTupleTableSlot(tupdesc);
	econtext->ecxt_scantuple = slot;

	snapshot = RegisterSnapshot(GetLatestSnapshot());
	scan = heap_beginscan(rel, snapshot, 0, NULL);

	/*
	 * Switch to per-tuple memory context and reset it for each tuple
	 * produced, so we don't leak memory.
	 */
	oldcxt = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		ExecStoreHeapTuple(tuple, slot, false);

		if (!ExecCheck(exprstate, econtext))
			ereport(ERROR,
					(errcode(ERRCODE_CHECK_VIOLATION),
					 errmsg("check constraint \"%s\" is violated by some row",
							NameStr(constrForm->conname)),
					 errtableconstraint(rel, NameStr(constrForm->conname))));

		ResetExprContext(econtext);
	}

	MemoryContextSwitchTo(oldcxt);
	heap_endscan(scan);
	UnregisterSnapshot(snapshot);
	ExecDropSingleTupleTableSlot(slot);
	FreeExecutorState(estate);
}

/*
 * Helper structure to emulate virtual functions for YbFKTriggerScan.
 * This scan works as regular heap_scan in non-YB mode and has some extra
 * functionality in YB mode.
 */
typedef struct YbFKTriggerVTable
{
	HeapTuple (*get_next)();
	Buffer (*get_buffer)();
} YbFKTriggerVTable;

/*
 * YbFKTriggerScanDescData holds the state of the YbFKTriggerScan which reads multiple
 * tuples and preload FK for them. After preloading FK in a batch it returns each tuple one
 * after another like regular heap_scan.
 */
typedef struct YbFKTriggerScanDescData
{
	HeapScanDesc scan;
	ScanDirection scan_direction;
	MemoryContext cxt;
	MemoryContext old_cxt;
	YbFKTriggerVTable* vptr;
	Trigger* trigger;
	Relation pkrel;
	int buffered_tuples_capacity;
	int buffered_tuples_size;
	int current_tuple_idx;
	bool all_tuples_processed;
	HeapTuple buffered_tuples[];
} YbFKTriggerScanDescData;

typedef struct YbFKTriggerScanDescData *YbFKTriggerScanDesc;

static HeapTuple
YbPgGetNext(YbFKTriggerScanDesc desc)
{
	/* Clear per-tuple context */
	MemoryContextReset(desc->cxt);
	return heap_getnext(desc->scan, desc->scan_direction);
}

static Buffer
YbPgGetBuffer(YbFKTriggerScanDesc desc)
{
	return desc->scan->rs_cbuf;
}

static HeapTuple
YbGetNext(YbFKTriggerScanDesc desc)
{
	if (desc->current_tuple_idx >= desc->buffered_tuples_size && !desc->all_tuples_processed)
	{
		/* Clear context of previously buffered tuples */
		MemoryContextReset(desc->cxt);
		desc->current_tuple_idx = 0;
		desc->buffered_tuples_size = 0;
		while (desc->buffered_tuples_size < desc->buffered_tuples_capacity)
		{
			HeapTuple tuple = heap_getnext(desc->scan, desc->scan_direction);
			if (tuple == NULL)
			{
				desc->all_tuples_processed = true;
				break;
			}
			YbAddTriggerFKReferenceIntent(desc->trigger, desc->pkrel,
										  tuple, /* is_deferred= */ false);
			desc->buffered_tuples[desc->buffered_tuples_size++] = tuple;
		}
	}
	return desc->current_tuple_idx < desc->buffered_tuples_size
		? desc->buffered_tuples[desc->current_tuple_idx++]
		: NULL;
}

static Buffer
YbGetBuffer(YbFKTriggerScanDesc desc)
{
	/*
	 * In YB mode data from trigdata.tg_trigtuplebuf is not used.
	 * It is safe to return InvalidBuffer.
	 */
	return InvalidBuffer;
}

static YbFKTriggerVTable YbFKTriggerScanVTableNotYugaByteEnabled =
	{
		.get_next = &YbPgGetNext,
		.get_buffer = &YbPgGetBuffer
	};

static YbFKTriggerVTable YbFKTriggerScanVTableIsYugaByteEnabled =
	{
		.get_next = &YbGetNext,
		.get_buffer = &YbGetBuffer
	};

static YbFKTriggerScanDesc
YbFKTriggerScanBegin(HeapScanDesc scan,
                     ScanDirection direction,
                     Trigger* trigger,
                     Relation pkrel,
                     int buffer_capacity)
{
	YbFKTriggerScanDesc descr = (YbFKTriggerScanDesc) palloc(
		sizeof(YbFKTriggerScanDescData) + buffer_capacity * sizeof(HeapTuple));
	MemSet(descr, 0, sizeof(YbFKTriggerScanDescData));
	descr->scan = scan;
	descr->scan_direction = direction;
	descr->trigger = trigger;
	descr->pkrel = pkrel;
	descr->buffered_tuples_capacity = buffer_capacity;
	if (IsYBRelation(scan->rs_rd))
	{
		descr->vptr = &YbFKTriggerScanVTableIsYugaByteEnabled;
		descr->cxt = AllocSetContextCreate(
			CurrentMemoryContext, "validateForeignKeyConstraint", ALLOCSET_DEFAULT_SIZES);
	}
	else
	{
		descr->vptr = &YbFKTriggerScanVTableNotYugaByteEnabled;
		descr->cxt = AllocSetContextCreate(
			CurrentMemoryContext, "validateForeignKeyConstraint", ALLOCSET_SMALL_SIZES);
	}
	descr->old_cxt = MemoryContextSwitchTo(descr->cxt);
	return descr;
}

static void
YbFKTriggerScanEnd(YbFKTriggerScanDesc descr)
{
	MemoryContextSwitchTo(descr->old_cxt);
	MemoryContextDelete(descr->cxt);
	heap_endscan(descr->scan);
	pfree(descr);
}

static HeapTuple
YbFKTriggerScanGetNext(YbFKTriggerScanDesc descr)
{
	return descr->vptr->get_next(descr);
}

static Buffer
YbFKTriggerScanGetBuffer(YbFKTriggerScanDesc descr)
{
	return descr->vptr->get_buffer(descr);
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
	HeapTuple	tuple;
	Trigger		trig;
	Snapshot	snapshot;

	ereport(DEBUG1,
			(errmsg("validating foreign key constraint \"%s\"", conname)));

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
	YbFKTriggerScanDesc fk_scan = YbFKTriggerScanBegin(
		heap_beginscan(rel, snapshot, 0, NULL),
		ForwardScanDirection,
		&trig,
		pkrel,
		*YBCGetGFlags()->ysql_session_max_batch_size);
	while ((tuple = YbFKTriggerScanGetNext(fk_scan)) != NULL)
	{
		FunctionCallInfoData fcinfo;
		TriggerData trigdata;

		CHECK_FOR_INTERRUPTS();

		/*
		 * Make a call to the trigger function
		 *
		 * No parameters are passed, but we do set a context
		 */
		MemSet(&fcinfo, 0, sizeof(fcinfo));

		/*
		 * We assume RI_FKey_check_ins won't look at flinfo...
		 */
		trigdata.type = T_TriggerData;
		trigdata.tg_event = TRIGGER_EVENT_INSERT | TRIGGER_EVENT_ROW;
		trigdata.tg_relation = rel;
		trigdata.tg_trigtuple = tuple;
		trigdata.tg_newtuple = NULL;
		trigdata.tg_trigger = &trig;
		trigdata.tg_trigtuplebuf = YbFKTriggerScanGetBuffer(fk_scan);
		trigdata.tg_newtuplebuf = InvalidBuffer;

		fcinfo.context = (Node *) &trigdata;

		RI_FKey_check_ins(&fcinfo);
	}

	YbFKTriggerScanEnd(fk_scan);
	UnregisterSnapshot(snapshot);
}

static void
CreateFKCheckTrigger(Oid myRelOid, Oid refRelOid, Constraint *fkconstraint,
					 Oid constraintOid, Oid indexOid, bool on_insert)
{
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
	fk_trigger->trigname = "RI_ConstraintTrigger_c";
	fk_trigger->relation = NULL;
	fk_trigger->row = true;
	fk_trigger->timing = TRIGGER_TYPE_AFTER;

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

	fk_trigger->columns = NIL;
	fk_trigger->transitionRels = NIL;
	fk_trigger->whenClause = NULL;
	fk_trigger->isconstraint = true;
	fk_trigger->deferrable = fkconstraint->deferrable;
	fk_trigger->initdeferred = fkconstraint->initdeferred;
	fk_trigger->constrrel = NULL;
	fk_trigger->args = NIL;

	(void) CreateTrigger(fk_trigger, NULL, myRelOid, refRelOid, constraintOid,
						 indexOid, InvalidOid, InvalidOid, NULL, true, false);

	/* Make changes-so-far visible */
	CommandCounterIncrement();
}

/*
 * createForeignKeyActionTriggers
 *		Create the referenced-side "action" triggers that implement a foreign
 *		key.
 */
static void
createForeignKeyActionTriggers(Relation rel, Oid refRelOid, Constraint *fkconstraint,
							   Oid constraintOid, Oid indexOid)
{
	CreateTrigStmt *fk_trigger;

	/*
	 * Build and execute a CREATE CONSTRAINT TRIGGER statement for the ON
	 * DELETE action on the referenced table.
	 */
	fk_trigger = makeNode(CreateTrigStmt);
	fk_trigger->trigname = "RI_ConstraintTrigger_a";
	fk_trigger->relation = NULL;
	fk_trigger->row = true;
	fk_trigger->timing = TRIGGER_TYPE_AFTER;
	fk_trigger->events = TRIGGER_TYPE_DELETE;
	fk_trigger->columns = NIL;
	fk_trigger->transitionRels = NIL;
	fk_trigger->whenClause = NULL;
	fk_trigger->isconstraint = true;
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
	fk_trigger->args = NIL;

	(void) CreateTrigger(fk_trigger, NULL, refRelOid, RelationGetRelid(rel),
						 constraintOid,
						 indexOid, InvalidOid, InvalidOid, NULL, true, false);

	/* Make changes-so-far visible */
	CommandCounterIncrement();

	/*
	 * Build and execute a CREATE CONSTRAINT TRIGGER statement for the ON
	 * UPDATE action on the referenced table.
	 */
	fk_trigger = makeNode(CreateTrigStmt);
	fk_trigger->trigname = "RI_ConstraintTrigger_a";
	fk_trigger->relation = NULL;
	fk_trigger->row = true;
	fk_trigger->timing = TRIGGER_TYPE_AFTER;
	fk_trigger->events = TRIGGER_TYPE_UPDATE;
	fk_trigger->columns = NIL;
	fk_trigger->transitionRels = NIL;
	fk_trigger->whenClause = NULL;
	fk_trigger->isconstraint = true;
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
	fk_trigger->args = NIL;

	(void) CreateTrigger(fk_trigger, NULL, refRelOid, RelationGetRelid(rel),
						 constraintOid,
						 indexOid, InvalidOid, InvalidOid, NULL, true, false);
}

/*
 * createForeignKeyCheckTriggers
 *		Create the referencing-side "check" triggers that implement a foreign
 *		key.
 */
static void
createForeignKeyCheckTriggers(Oid myRelOid, Oid refRelOid,
							  Constraint *fkconstraint, Oid constraintOid,
							  Oid indexOid)
{
	CreateFKCheckTrigger(myRelOid, refRelOid, fkconstraint, constraintOid,
						 indexOid, true);
	CreateFKCheckTrigger(myRelOid, refRelOid, fkconstraint, constraintOid,
						 indexOid, false);
}

/*
 * Create the triggers that implement an FK constraint.
 *
 * NB: if you change any trigger properties here, see also
 * ATExecAlterConstraint.
 */
void
createForeignKeyTriggers(Relation rel, Oid refRelOid, Constraint *fkconstraint,
						 Oid constraintOid, Oid indexOid, bool create_action)
{
	/*
	 * For the referenced side, create action triggers, if requested.  (If the
	 * referencing side is partitioned, there is still only one trigger, which
	 * runs on the referenced side and points to the top of the referencing
	 * hierarchy.)
	 */
	if (create_action)
		createForeignKeyActionTriggers(rel, refRelOid, fkconstraint, constraintOid,
									   indexOid);

	/*
	 * For the referencing side, create the check triggers.  We only need
	 * these on the partitions.
	 */
	if (rel->rd_rel->relkind != RELKIND_PARTITIONED_TABLE)
		createForeignKeyCheckTriggers(RelationGetRelid(rel), refRelOid,
									  fkconstraint, constraintOid, indexOid);

	CommandCounterIncrement();
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
		ATSimplePermissions(*mutable_rel, ATT_TABLE | ATT_FOREIGN_TABLE);

	conrel = heap_open(ConstraintRelationId, RowExclusiveLock);

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
			frel = heap_open(con->confrelid, AccessExclusiveLock);
			CheckTableNotInUse(frel, "ALTER TABLE");
			heap_close(frel, NoLock);
		}

		/*
		 * Perform the actual constraint deletion
		 */
		conobj.classId = ConstraintRelationId;
		conobj.objectId = HeapTupleGetOid(tuple);
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
			heap_close(conrel, RowExclusiveLock);
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
		heap_close(conrel, RowExclusiveLock);
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
		childrel = heap_open(childrelid, NoLock);
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

		heap_close(childrel, NoLock);
	}

	heap_close(conrel, RowExclusiveLock);
}

/*
 * ALTER COLUMN TYPE
 */
static void
ATPrepAlterColumnType(List **wqueue,
					  AlteredTableInfo *tab, Relation rel,
					  bool recurse, bool recursing,
					  AlterTableCmd *cmd, LOCKMODE lockmode)
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

	/* Don't alter inherited columns */
	if (attTup->attinhcount > 0 && !recursing)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
				 errmsg("cannot alter inherited column \"%s\"",
						colName)));

	/* Don't alter columns used in the partition key */
	if (has_partition_attrs(rel,
							bms_make_singleton(attnum - YBGetFirstLowInvalidAttributeNumber(rel)),
							&is_expr))
	{
		if (!is_expr)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot alter type of column named in partition key")));
		else
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
					 errmsg("cannot alter type of column referenced in partition key expression")));
	}

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
					   false);

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

	if (tab->relkind == RELKIND_COMPOSITE_TYPE ||
		tab->relkind == RELKIND_FOREIGN_TABLE)
	{
		/*
		 * For composite types, do this check now.  Tables will check it later
		 * when the table is being rewritten.
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

			/*
			 * Remap the attribute numbers.  If no USING expression was
			 * specified, there is no need for this step.
			 */
			if (def->cooked_default)
			{
				AttrNumber *attmap;
				bool		found_whole_row;

				/* create a copy to scribble on */
				cmd = copyObject(cmd);

				attmap = convert_tuples_by_name_map(
					RelationGetDescr(childrel), RelationGetDescr(rel),
					gettext_noop("could not convert row type"),
					false /* yb_ignore_type_mismatch */);
				((ColumnDef *) cmd->def)->cooked_default =
					map_variable_attnos(def->cooked_default,
										1, 0,
										attmap, RelationGetDescr(rel)->natts,
										InvalidOid, &found_whole_row);
				if (found_whole_row)
					ereport(ERROR,
							(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
							 errmsg("cannot convert whole-row table reference"),
							 errdetail("USING expression contains a whole-row table reference.")));
				pfree(attmap);
			}
			ATPrepCmd(wqueue, childrel, cmd, false, true, lockmode);
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
		ATTypedTableRecursion(wqueue, rel, cmd, lockmode);
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
		if (IsA(expr, Var) &&((Var *) expr)->varattno == varattno)
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
		Relation    newrel;

		newrel = heap_open(RelationGetRelid(rel), NoLock);
		RelationClearMissing(newrel);
		relation_close(newrel, NoLock);
		/* make sure we don't conflict with later attribute modifications */
		CommandCounterIncrement();
	}

	bool yb_clone_table = IsYBRelation(rel) && !yb_enable_alter_table_rewrite
		&& tab->rewrite > 0;

	if (!yb_clone_table)
		attrelation = heap_open(AttributeRelationId, RowExclusiveLock);

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
	targettype = HeapTupleGetOid(typeTuple);
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
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("default for column \"%s\" cannot be cast automatically to type %s",
							colName, format_type_be(targettype))));
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
	 *
	 * There could be multiple entries for the same object, so we must check
	 * to ensure we process each one only once.  Note: we assume that an index
	 * that implements a constraint will not show a direct dependency on the
	 * column.
	 */
	depRel = heap_open(DependRelationId, RowExclusiveLock);

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

		/* We don't expect any PIN dependencies on columns */
		if (foundDep->deptype == DEPENDENCY_PIN)
			elog(ERROR, "cannot alter type of a pinned column");

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
						if (!list_member_oid(tab->changedIndexOids, foundObject.objectId))
						{
							tab->changedIndexOids = lappend_oid(tab->changedIndexOids,
																foundObject.objectId);
							tab->changedIndexDefs = lappend(tab->changedIndexDefs,
											pg_get_indexdef_string(foundObject.objectId));
						}
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
							 getObjectDescription(&foundObject));
					}
					break;
				}

			case OCLASS_CONSTRAINT:
				Assert(foundObject.objectSubId == 0);
				if (!list_member_oid(tab->changedConstraintOids,
									 foundObject.objectId))
				{
					char	   *defstring = pg_get_constraintdef_command(foundObject.objectId);

					tab->changedConstraintOids =
						lappend_oid(tab->changedConstraintOids,
									foundObject.objectId);
					tab->changedConstraintDefs =
						lappend(tab->changedConstraintDefs,
								defstring);
				}
				break;

			case OCLASS_REWRITE:
				/* XXX someday see if we can cope with revising views */
				ereport(ERROR,
						(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
						 errmsg("cannot alter type of a column used by a view or rule"),
						 errdetail("%s depends on column \"%s\"",
								   getObjectDescription(&foundObject),
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
								   getObjectDescription(&foundObject),
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
								   getObjectDescription(&foundObject),
								   colName)));
				break;

			case OCLASS_DEFAULT:

				/*
				 * Ignore the column's default expression, since we will fix
				 * it below.
				 */
				Assert(defaultexpr);
				break;

			case OCLASS_STATISTIC_EXT:

				/*
				 * Give the extended-stats machinery a chance to fix anything
				 * that this column type change would break.
				 */
				UpdateStatisticsForTypeChange(foundObject.objectId,
											  RelationGetRelid(rel), attnum,
											  attTup->atttypid, targettype);
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
			case OCLASS_PUBLICATION:
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
					 getObjectDescription(&foundObject));
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
	 * thing we should find is the dependency on the column datatype, which we
	 * want to remove, and possibly a collation dependency.
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

		if (foundDep->deptype != DEPENDENCY_NORMAL)
			elog(ERROR, "found unexpected dependency type '%c'",
				 foundDep->deptype);
		if (!(foundDep->refclassid == TypeRelationId &&
			  foundDep->refobjid == attTup->atttypid) &&
			!(foundDep->refclassid == CollationRelationId &&
			  foundDep->refobjid == attTup->attcollation))
			elog(ERROR, "found unexpected dependency for column");

		CatalogTupleDelete(depRel, depTup);
	}

	systable_endscan(scan);

	heap_close(depRel, RowExclusiveLock);

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
	 * heapTup is a copy of the syscache entry, so okay to scribble on.)
	 * First fix up the missing value if any.
	 */
	if (!yb_clone_table && attTup->atthasmissing)
	{
		Datum       missingval;
		bool        missingNull;

		/* if rewrite is true the missing value should already be cleared */
		Assert(tab->rewrite == 0);

		/* Get the missing value datum */
		missingval = heap_getattr(heapTup,
								  Anum_pg_attribute_attmissingval,
								  attrelation->rd_att,
								  &missingNull);

		/* if it's a null array there is nothing to do */

		if (! missingNull)
		{
			/*
			 * Get the datum out of the array and repack it in a new array
			 * built with the new type data. We assume that since the table
			 * doesn't need rewriting, the actual Datum doesn't need to be
			 * changed, only the array metadata.
			 */

			int one = 1;
			bool isNull;
			Datum       valuesAtt[Natts_pg_attribute];
			bool        nullsAtt[Natts_pg_attribute];
			bool        replacesAtt[Natts_pg_attribute];
			HeapTuple   newTup;

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
			missingval = PointerGetDatum(
				construct_array(&missingval,
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

		heap_close(attrelation, RowExclusiveLock);

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
		/* Must make new row visible since it will be updated again */
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
	ftrel = heap_open(ForeignTableRelationId, AccessShareLock);
	tuple = SearchSysCache1(FOREIGNTABLEREL, rel->rd_id);
	if (!HeapTupleIsValid(tuple))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("foreign table \"%s\" does not exist",
						RelationGetRelationName(rel))));
	fttableform = (Form_pg_foreign_table) GETSTRUCT(tuple);
	server = GetForeignServer(fttableform->ftserver);
	fdw = GetForeignDataWrapper(server->fdwid);

	heap_close(ftrel, AccessShareLock);
	ReleaseSysCache(tuple);

	attrel = heap_open(AttributeRelationId, RowExclusiveLock);
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

	heap_close(attrel, RowExclusiveLock);

	heap_freetuple(newtuple);

	return address;
}

/*
 * Cleanup after we've finished all the ALTER TYPE operations for a
 * particular relation.  We have to drop and recreate all the indexes
 * and constraints that depend on the altered columns.
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
	 * parse_analyze() or the rewriter, but instead we need to pass them
	 * through parse_utilcmd.c to make them ready for execution.
	 */
	raw_parsetree_list = raw_parser(cmd);
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
			querytree_list = list_concat(querytree_list,
										 transformAlterTableStmt(oldRelId,
																 (AlterTableStmt *) stmt,
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
				AlterTableCmd *cmd = castNode(AlterTableCmd, lfirst(lcmd));

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

		stmt->oldNode = irel->rd_node.relNode;
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
		con->old_conpfeqop = lcons_oid(rawarr[i], con->old_conpfeqop);

	ReleaseSysCache(tup);
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
	class_rel = heap_open(RelationRelationId, RowExclusiveLock);

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
					 errmsg("\"%s\" is not a table, view, sequence, or foreign table",
							NameStr(tuple_class->relname))));
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
		if (tuple_class->relkind != RELKIND_INDEX &&
			tuple_class->relkind != RELKIND_PARTITIONED_INDEX)
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
	heap_close(class_rel, RowExclusiveLock);
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

	attRelation = heap_open(AttributeRelationId, RowExclusiveLock);
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
	heap_close(attRelation, RowExclusiveLock);
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
	depRel = heap_open(DependRelationId, AccessShareLock);

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
	check_index_is_clusterable(rel, indexOid, false, lockmode);

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
 * ALTER TABLE SET TABLESPACE
 */
static void
ATPrepSetTableSpace(AlteredTableInfo *tab, Relation rel,
					const char *tablespacename, LOCKMODE lockmode, bool cascade)
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

	if (IsYugaByteEnabled() && !cascade && MyDatabaseColocated &&
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

	pgclass = heap_open(RelationRelationId, RowExclusiveLock);

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
		case RELKIND_PARTITIONED_TABLE:
			(void) heap_reloptions(rel->rd_rel->relkind, newOptions, true);
			break;
		case RELKIND_VIEW:
			(void) view_reloptions(newOptions, true);
			break;
		case RELKIND_INDEX:
		case RELKIND_PARTITIONED_INDEX:
			(void) index_reloptions(rel->rd_amroutine->amoptions, newOptions, true);
			break;
		default:
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("\"%s\" is not a table, view, materialized view, index, or TOAST table",
							RelationGetRelationName(rel))));
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

		toastrel = heap_open(toastid, lockmode);

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

		heap_close(toastrel, NoLock);
	}

	heap_close(pgclass, RowExclusiveLock);
}

/*
 * Execute ALTER TABLE SET TABLESPACE for cases where there is no tuple
 * rewriting to be done, so we just want to copy the data as fast as possible.
 */
static void
ATExecSetTableSpace(Oid tableOid, Oid newTableSpace, LOCKMODE lockmode)
{
	Relation	rel;
	Oid			oldTableSpace;
	Oid			reltoastrelid;
	Oid			newrelfilenode;
	RelFileNode newrnode;
	SMgrRelation dstrel;
	Relation	pg_class;
	HeapTuple	tuple;
	Form_pg_class rd_rel;
	ForkNumber	forkNum;
	List	   *reltoastidxids = NIL;
	ListCell   *lc;

	/*
	 * Need lock here in case we are recursing to toast table or index
	 */
	rel = relation_open(tableOid, lockmode);

	/*
	 * Should only be called on relations having storage, namely non-Yugabyte and
	 * non-parititoned relations.
	 */
	Assert(!IsYBRelation(rel) && RELKIND_CAN_HAVE_STORAGE(rel->rd_rel->relkind));

	/*
	 * No work if no change in tablespace.
	 */
	oldTableSpace = rel->rd_rel->reltablespace;
	if (newTableSpace == oldTableSpace ||
		(newTableSpace == MyDatabaseTableSpace && oldTableSpace == 0))
	{
		InvokeObjectPostAlterHook(RelationRelationId,
								  RelationGetRelid(rel), 0);

		relation_close(rel, NoLock);
		return;
	}

	/*
	 * We cannot support moving mapped relations into different tablespaces.
	 * (In particular this eliminates all shared catalogs.)
	 */
	if (RelationIsMapped(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot move system relation \"%s\"",
						RelationGetRelationName(rel))));

	/* Can't move a non-shared relation into pg_global */
	if (newTableSpace == GLOBALTABLESPACE_OID)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
				 errmsg("only shared relations can be placed in pg_global tablespace")));

	/*
	 * Don't allow moving temp tables of other backends ... their local buffer
	 * manager is not going to cope.
	 */
	if (RELATION_IS_OTHER_TEMP(rel))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot move temporary tables of other sessions")));

	reltoastrelid = rel->rd_rel->reltoastrelid;
	/* Fetch the list of indexes on toast relation if necessary */
	if (OidIsValid(reltoastrelid))
	{
		Relation	toastRel = relation_open(reltoastrelid, lockmode);

		reltoastidxids = RelationGetIndexList(toastRel);
		relation_close(toastRel, lockmode);
	}

	/* Get a modifiable copy of the relation's pg_class row */
	pg_class = heap_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(tableOid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", tableOid);
	rd_rel = (Form_pg_class) GETSTRUCT(tuple);

	/*
	 * Since we copy the file directly without looking at the shared buffers,
	 * we'd better first flush out any pages of the source relation that are
	 * in shared buffers.  We assume no new changes will be made while we are
	 * holding exclusive lock on the rel.
	 */
	FlushRelationBuffers(rel);

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
	dstrel = smgropen(newrnode, rel->rd_backend);

	RelationOpenSmgr(rel);

	/*
	 * Create and copy all forks of the relation, and schedule unlinking of
	 * old physical files.
	 *
	 * NOTE: any conflict in relfilenode value will be caught in
	 * RelationCreateStorage().
	 */
	RelationCreateStorage(newrnode, rel->rd_rel->relpersistence);

	/* copy main fork */
	copy_relation_data(rel->rd_smgr, dstrel, MAIN_FORKNUM,
					   rel->rd_rel->relpersistence);

	/* copy those extra forks that exist */
	for (forkNum = MAIN_FORKNUM + 1; forkNum <= MAX_FORKNUM; forkNum++)
	{
		if (smgrexists(rel->rd_smgr, forkNum))
		{
			smgrcreate(dstrel, forkNum, false);

			/*
			 * WAL log creation if the relation is persistent, or this is the
			 * init fork of an unlogged relation.
			 */
			if (rel->rd_rel->relpersistence == RELPERSISTENCE_PERMANENT ||
				(rel->rd_rel->relpersistence == RELPERSISTENCE_UNLOGGED &&
				 forkNum == INIT_FORKNUM))
				log_smgrcreate(&newrnode, forkNum);
			copy_relation_data(rel->rd_smgr, dstrel, forkNum,
							   rel->rd_rel->relpersistence);
		}
	}

	/* drop old relation, and close new one */
	RelationDropStorage(rel);
	smgrclose(dstrel);

	/* update the pg_class row */
	rd_rel->reltablespace = (newTableSpace == MyDatabaseTableSpace) ? InvalidOid : newTableSpace;
	rd_rel->relfilenode = newrelfilenode;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId, RelationGetRelid(rel), 0);

	heap_freetuple(tuple);

	heap_close(pg_class, RowExclusiveLock);

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
	HeapTuple	tuple;
	Oid			oldTableSpace;
	Relation	pg_class;
	Form_pg_class rd_rel;
	Oid			reloid = RelationGetRelid(rel);

	/*
	 * Shouldn't be called on relations having storage; these are processed
	 * in phase 3.  Yugabyte tables do not use the Postgres store so it appears to
	 * Postgres as if there is no associated storage.
	 */
	Assert(IsYBRelation(rel) || !RELKIND_CAN_HAVE_STORAGE(rel->rd_rel->relkind));

	/* Can't allow a non-shared relation in pg_global */
	if (newTableSpace == GLOBALTABLESPACE_OID)
		ereport(ERROR,
		(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
			errmsg("only shared relations can be placed in pg_global tablespace")));

	/*
	 * No work if no change in tablespace.
	 */
	oldTableSpace = rel->rd_rel->reltablespace;
	if (newTableSpace == oldTableSpace ||
		(newTableSpace == MyDatabaseTableSpace && oldTableSpace == 0))
	{
		InvokeObjectPostAlterHook(RelationRelationId, reloid, 0);
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
			const char *placement_str = "replica_placement=";
			int placement_strlen = strlen(placement_str);
			if (strncmp(option, placement_str, placement_strlen) == 0)
			{
				YBCValidatePlacement(option + placement_strlen,
						/* check_satisfiable */ true);
			}
			else
			{
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
						 errmsg("expected replica_placement option. Got %s", option)));
			}
			pfree(option);
		}
	}

	/* Get a modifiable copy of the relation's pg_class row */
	pg_class = heap_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(reloid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", reloid);
	rd_rel = (Form_pg_class) GETSTRUCT(tuple);

	/* update the pg_class row */
	rd_rel->reltablespace = (newTableSpace == MyDatabaseTableSpace) ? InvalidOid : newTableSpace;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	/* Record dependency on tablespace */
	changeDependencyOnTablespace(RelationRelationId,
								 reloid, rd_rel->reltablespace);

	InvokeObjectPostAlterHook(RelationRelationId, reloid, 0);

	heap_freetuple(tuple);

	heap_close(pg_class, RowExclusiveLock);

	/* Make sure the reltablespace change is visible */
	CommandCounterIncrement();

	/* Notify the user that this command is async */
	ereport(NOTICE, (errmsg("Data movement for table %s is successfully "
							"initiated.",
							RelationGetRelationName(rel)),
					 errdetail("Data movement is a long running asynchronous "
							   "process "
							   "and can be monitored by checking the tablet "
							   "placement "
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
	HeapScanDesc scan;
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

	rel = heap_open(RelationRelationId, AccessShareLock);
	scan = heap_beginscan_catalog(rel, 1, key);
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Oid			relOid = HeapTupleGetOid(tuple);
		Form_pg_class relForm;

		relForm = (Form_pg_class) GETSTRUCT(tuple);

		/*
		 * Do not move objects in pg_catalog as part of this, if an admin
		 * really wishes to do so, they can issue the individual ALTER
		 * commands directly.
		 *
		 * Also, explicitly avoid any shared tables, temp tables, or TOAST
		 * (TOAST will be moved with the main table).
		 */
		if (IsSystemNamespace(relForm->relnamespace) || relForm->relisshared ||
			isAnyTempNamespace(relForm->relnamespace) ||
			relForm->relnamespace == PG_TOAST_NAMESPACE)
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
					yb_pg_class = heap_open(RelationRelationId,
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

					heap_close(yb_pg_class, RowExclusiveLock);

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

	heap_endscan(scan);
	heap_close(rel, AccessShareLock);

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

/*
 * Copy data, block by block
 */
static void
copy_relation_data(SMgrRelation src, SMgrRelation dst,
				   ForkNumber forkNum, char relpersistence)
{
	PGAlignedBlock buf;
	Page		page;
	bool		use_wal;
	bool		copying_initfork;
	BlockNumber nblocks;
	BlockNumber blkno;

	page = (Page) buf.data;

	/*
	 * The init fork for an unlogged relation in many respects has to be
	 * treated the same as normal relation, changes need to be WAL logged and
	 * it needs to be synced to disk.
	 */
	copying_initfork = relpersistence == RELPERSISTENCE_UNLOGGED &&
		forkNum == INIT_FORKNUM;

	/*
	 * We need to log the copied data in WAL iff WAL archiving/streaming is
	 * enabled AND it's a permanent relation.
	 */
	use_wal = XLogIsNeeded() &&
		(relpersistence == RELPERSISTENCE_PERMANENT || copying_initfork);

	nblocks = smgrnblocks(src, forkNum);

	for (blkno = 0; blkno < nblocks; blkno++)
	{
		/* If we got a cancel signal during the copy of the data, quit */
		CHECK_FOR_INTERRUPTS();

		smgrread(src, forkNum, blkno, buf.data);

		if (!PageIsVerified(page, blkno))
			ereport(ERROR,
					(errcode(ERRCODE_DATA_CORRUPTED),
					 errmsg("invalid page in block %u of relation %s",
							blkno,
							relpathbackend(src->smgr_rnode.node,
										   src->smgr_rnode.backend,
										   forkNum))));

		/*
		 * WAL-log the copied page. Unfortunately we don't know what kind of a
		 * page this is, so we have to log the full page including any unused
		 * space.
		 */
		if (use_wal)
			log_newpage(&dst->smgr_rnode.node, forkNum, blkno, page, false);

		PageSetChecksumInplace(page, blkno);

		/*
		 * Now write the page.  We say isTemp = true even if it's not a temp
		 * rel, because there's no need for smgr to schedule an fsync for this
		 * write; we'll do it ourselves below.
		 */
		smgrextend(dst, forkNum, blkno, buf.data, true);
	}

	/*
	 * If the rel is WAL-logged, must fsync before commit.  We use heap_sync
	 * to ensure that the toast table gets fsync'd too.  (For a temp or
	 * unlogged rel we don't care since the data will be gone after a crash
	 * anyway.)
	 *
	 * It's obvious that we must do this when not WAL-logging the copy. It's
	 * less obvious that we have to do it even if we did WAL-log the copied
	 * pages. The reason is that since we're copying outside shared buffers, a
	 * CHECKPOINT occurring during the copy has no way to flush the previously
	 * written data to disk (indeed it won't know the new rel even exists).  A
	 * crash later on would replay WAL from the checkpoint, therefore it
	 * wouldn't replay our earlier WAL entries. If we do not fsync those pages
	 * here, they might still not be on disk when the crash occurs.
	 */
	if (relpersistence == RELPERSISTENCE_PERMANENT || copying_initfork)
		smgrimmedsync(dst, forkNum);
}

/*
 * ALTER TABLE ENABLE/DISABLE TRIGGER
 *
 * We just pass this off to trigger.c.
 */
static void
ATExecEnableDisableTrigger(Relation rel, const char *trigname,
						   char fires_when, bool skip_system, LOCKMODE lockmode)
{
	EnableDisableTrigger(rel, trigname, fires_when, skip_system, lockmode);
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
	parent_rel = heap_openrv(parent, ShareUpdateExclusiveLock);

	/*
	 * Must be owner of both parent and child -- child was checked by
	 * ATSimplePermissions call in ATPrepCmd
	 */
	ATSimplePermissions(parent_rel, ATT_TABLE | ATT_FOREIGN_TABLE);

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

	/* If parent has OIDs then child must have OIDs */
	if (parent_rel->rd_rel->relhasoids && !child_rel->rd_rel->relhasoids)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("table \"%s\" without OIDs cannot inherit from table \"%s\" with OIDs",
						RelationGetRelationName(child_rel),
						RelationGetRelationName(parent_rel))));

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
	heap_close(parent_rel, NoLock);

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
	catalogRelation = heap_open(InheritsRelationId, RowExclusiveLock);

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
	heap_close(catalogRelation, RowExclusiveLock);
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
		elog(ERROR, "null conbin for constraint %u", HeapTupleGetOid(contup));

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

	attrrel = heap_open(AttributeRelationId, RowExclusiveLock);

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

	/*
	 * If the parent has an OID column, so must the child, and we'd better
	 * update the child's attinhcount and attislocal the same as for normal
	 * columns.  We needn't check data type or not-nullness though.
	 */
	if (tupleDesc->tdhasoid)
	{
		/*
		 * Here we match by column number not name; the match *must* be the
		 * system column, not some random column named "oid".
		 */
		tuple = SearchSysCacheCopy2(ATTNUM,
									ObjectIdGetDatum(RelationGetRelid(child_rel)),
									Int16GetDatum(ObjectIdAttributeNumber));
		if (HeapTupleIsValid(tuple))
		{
			Form_pg_attribute childatt = (Form_pg_attribute) GETSTRUCT(tuple);

			/* See comments above; these changes should be the same */
			childatt->attinhcount++;

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
							"oid")));
		}
	}

	heap_close(attrrel, RowExclusiveLock);
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

	catalog_relation = heap_open(ConstraintRelationId, RowExclusiveLock);
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
	heap_close(catalog_relation, RowExclusiveLock);
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
	parent_rel = heap_openrv(parent, AccessShareLock);

	/*
	 * We don't bother to check ownership of the parent table --- ownership of
	 * the child is presumed enough rights.
	 */

	/* Off to RemoveInheritance() where most of the work happens */
	RemoveInheritance(rel, parent_rel);

	ObjectAddressSet(address, RelationRelationId,
					 RelationGetRelid(parent_rel));

	/* keep our lock on the parent relation until commit */
	heap_close(parent_rel, NoLock);

	return address;
}

/*
 * RemoveInheritance
 *
 * Drop a parent from the child's parents. This just adjusts the attinhcount
 * and attislocal of the columns and removes the pg_inherit and pg_depend
 * entries.
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
RemoveInheritance(Relation child_rel, Relation parent_rel)
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
								RelationGetRelid(parent_rel));
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
	catalogRelation = heap_open(AttributeRelationId, RowExclusiveLock);
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
	heap_close(catalogRelation, RowExclusiveLock);

	/*
	 * Likewise, find inherited check constraints and disinherit them. To do
	 * this, we first need a list of the names of the parent's check
	 * constraints.  (We cheat a bit by only checking for name matches,
	 * assuming that the expressions will match.)
	 */
	catalogRelation = heap_open(ConstraintRelationId, RowExclusiveLock);
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
	heap_close(catalogRelation, RowExclusiveLock);

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

	catalogRelation = heap_open(DependRelationId, RowExclusiveLock);

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
	heap_close(catalogRelation, RowExclusiveLock);
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
	typeid = HeapTupleGetOid(typetuple);

	/* Fail if the table has any inheritance parents. */
	inheritsRelation = heap_open(InheritsRelationId, AccessShareLock);
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
	heap_close(inheritsRelation, AccessShareLock);

	/*
	 * Check the tuple descriptors for compatibility.  Unlike inheritance, we
	 * require that the order also match.  However, attnotnull need not match.
	 * Also unlike inheritance, we do not require matching relhasoids.
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
	DecrTupleDescRefCount(typeTupleDesc);

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
	relationRelation = heap_open(RelationRelationId, RowExclusiveLock);
	classtuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(classtuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	((Form_pg_class) GETSTRUCT(classtuple))->reloftype = typeid;
	CatalogTupleUpdate(relationRelation, &classtuple->t_self, classtuple);

	InvokeObjectPostAlterHook(RelationRelationId, relid, 0);

	heap_freetuple(classtuple);
	heap_close(relationRelation, RowExclusiveLock);

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
	relationRelation = heap_open(RelationRelationId, RowExclusiveLock);
	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));
	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);
	((Form_pg_class) GETSTRUCT(tuple))->reloftype = InvalidOid;
	CatalogTupleUpdate(relationRelation, &tuple->t_self, tuple);

	InvokeObjectPostAlterHook(RelationRelationId, relid, 0);

	heap_freetuple(tuple);
	heap_close(relationRelation, RowExclusiveLock);
}

/*
 * relation_mark_replica_identity: Update a table's replica identity
 *
 * Iff ri_type = REPLICA_IDENTITY_INDEX, indexOid must be the Oid of a suitable
 * index. Otherwise, it should be InvalidOid.
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
	pg_class = heap_open(RelationRelationId, RowExclusiveLock);
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
	heap_close(pg_class, RowExclusiveLock);
	heap_freetuple(pg_class_tuple);

	/*
	 * Check whether the correct index is marked indisreplident; if so, we're
	 * done.
	 */
	if (OidIsValid(indexOid))
	{
		Assert(ri_type == REPLICA_IDENTITY_INDEX);

		pg_index_tuple = SearchSysCache1(INDEXRELID, ObjectIdGetDatum(indexOid));
		if (!HeapTupleIsValid(pg_index_tuple))
			elog(ERROR, "cache lookup failed for index %u", indexOid);
		pg_index_form = (Form_pg_index) GETSTRUCT(pg_index_tuple);

		if (pg_index_form->indisreplident)
		{
			ReleaseSysCache(pg_index_tuple);
			return;
		}
		ReleaseSysCache(pg_index_tuple);
	}

	/*
	 * Clear the indisreplident flag from any index that had it previously,
	 * and set it for any index that should have it now.
	 */
	pg_index = heap_open(IndexRelationId, RowExclusiveLock);
	foreach(index, RelationGetIndexList(rel))
	{
		Oid			thisIndexOid = lfirst_oid(index);
		bool		dirty = false;

		pg_index_tuple = SearchSysCacheCopy1(INDEXRELID,
											 ObjectIdGetDatum(thisIndexOid));
		if (!HeapTupleIsValid(pg_index_tuple))
			elog(ERROR, "cache lookup failed for index %u", thisIndexOid);
		pg_index_form = (Form_pg_index) GETSTRUCT(pg_index_tuple);

		/*
		 * Unset the bit if set.  We know it's wrong because we checked this
		 * earlier.
		 */
		if (pg_index_form->indisreplident)
		{
			dirty = true;
			pg_index_form->indisreplident = false;
		}
		else if (thisIndexOid == indexOid)
		{
			dirty = true;
			pg_index_form->indisreplident = true;
		}

		if (dirty)
		{
			CatalogTupleUpdate(pg_index, &pg_index_tuple->t_self, pg_index_tuple);
			InvokeObjectPostAlterHookArg(IndexRelationId, thisIndexOid, 0,
										 InvalidOid, is_internal);
		}
		heap_freetuple(pg_index_tuple);
	}

	heap_close(pg_index, RowExclusiveLock);
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
	if (!indexRel->rd_amroutine->amcanunique ||
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
	/* And neither are invalid indexes. */
	if (!IndexIsValid(indexRel->rd_index))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot use invalid index \"%s\" as replica identity",
						RelationGetRelationName(indexRel))));

	/* Check index for nullable columns. */
	for (key = 0; key < IndexRelationGetNumberOfKeyAttributes(indexRel); key++)
	{
		int16		attno = indexRel->rd_index->indkey.values[key];
		Form_pg_attribute attr;

		/* Allow OID column to be indexed; it's certainly not nullable */
		if (attno == ObjectIdAttributeNumber)
			continue;

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
ATExecEnableRowSecurity(Relation rel)
{
	Relation	pg_class;
	Oid			relid;
	HeapTuple	tuple;

	relid = RelationGetRelid(rel);

	pg_class = heap_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);

	((Form_pg_class) GETSTRUCT(tuple))->relrowsecurity = true;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	heap_close(pg_class, RowExclusiveLock);
	heap_freetuple(tuple);
}

static void
ATExecDisableRowSecurity(Relation rel)
{
	Relation	pg_class;
	Oid			relid;
	HeapTuple	tuple;

	relid = RelationGetRelid(rel);

	/* Pull the record for this relation and update it */
	pg_class = heap_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);

	((Form_pg_class) GETSTRUCT(tuple))->relrowsecurity = false;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	heap_close(pg_class, RowExclusiveLock);
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

	pg_class = heap_open(RelationRelationId, RowExclusiveLock);

	tuple = SearchSysCacheCopy1(RELOID, ObjectIdGetDatum(relid));

	if (!HeapTupleIsValid(tuple))
		elog(ERROR, "cache lookup failed for relation %u", relid);

	((Form_pg_class) GETSTRUCT(tuple))->relforcerowsecurity = force_rls;
	CatalogTupleUpdate(pg_class, &tuple->t_self, tuple);

	heap_close(pg_class, RowExclusiveLock);
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

	ftrel = heap_open(ForeignTableRelationId, RowExclusiveLock);

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

	heap_close(ftrel, RowExclusiveLock);

	heap_freetuple(tuple);
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
	pg_constraint = heap_open(ConstraintRelationId, AccessShareLock);

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
				if (foreignrel->rd_rel->relpersistence != RELPERSISTENCE_PERMANENT)
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_TABLE_DEFINITION),
							 errmsg("could not change table \"%s\" to logged because it references unlogged table \"%s\"",
									RelationGetRelationName(rel),
									RelationGetRelationName(foreignrel)),
							 errtableconstraint(rel, NameStr(con->conname))));
			}
			else
			{
				if (foreignrel->rd_rel->relpersistence == RELPERSISTENCE_PERMANENT)
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

	heap_close(pg_constraint, AccessShareLock);

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
	classRel = heap_open(RelationRelationId, RowExclusiveLock);

	AlterRelationNamespaceInternal(classRel, RelationGetRelid(rel), oldNspOid,
								   nspOid, true, objsMoved);

	/* Fix the table's row type too */
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

	heap_close(classRel, RowExclusiveLock);
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
	depRel = heap_open(DependRelationId, AccessShareLock);

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
		 * Sequences have entries in pg_type. We need to be careful to move
		 * them to the new namespace, too.
		 */
		AlterTypeNamespaceInternal(RelationGetForm(seqRel)->reltype,
								   newNspOid, false, false, objsMoved);

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
				if ((MyXactFlags & XACT_FLAGS_ACCESSEDTEMPREL))
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
	{
		heap_truncate(oids_to_truncate);
		CommandCounterIncrement();	/* XXX needed? */
	}
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
		if (IsYugaByteEnabled())
			YBIncrementDdlNestingLevel(YB_DDL_MODE_SILENT_ALTERING);
		performMultipleDeletions(targetObjects, DROP_CASCADE,
								 PERFORM_DELETION_INTERNAL | PERFORM_DELETION_QUIETLY);
		if (IsYugaByteEnabled())
			YBDecrementDdlNestingLevel();

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
	ListCell   *prev_item;

	prev_item = NULL;
	cur_item = list_head(on_commits);

	while (cur_item != NULL)
	{
		OnCommitItem *oc = (OnCommitItem *) lfirst(cur_item);

		if (isCommit ? oc->deleting_subid != InvalidSubTransactionId :
			oc->creating_subid != InvalidSubTransactionId)
		{
			/* cur_item must be removed */
			on_commits = list_delete_cell(on_commits, cur_item, prev_item);
			pfree(oc);
			if (prev_item)
				cur_item = lnext(prev_item);
			else
				cur_item = list_head(on_commits);
		}
		else
		{
			/* cur_item must be preserved */
			oc->creating_subid = InvalidSubTransactionId;
			oc->deleting_subid = InvalidSubTransactionId;
			prev_item = cur_item;
			cur_item = lnext(prev_item);
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
	ListCell   *prev_item;

	prev_item = NULL;
	cur_item = list_head(on_commits);

	while (cur_item != NULL)
	{
		OnCommitItem *oc = (OnCommitItem *) lfirst(cur_item);

		if (!isCommit && oc->creating_subid == mySubid)
		{
			/* cur_item must be removed */
			on_commits = list_delete_cell(on_commits, cur_item, prev_item);
			pfree(oc);
			if (prev_item)
				cur_item = lnext(prev_item);
			else
				cur_item = list_head(on_commits);
		}
		else
		{
			/* cur_item must be preserved */
			if (oc->creating_subid == mySubid)
				oc->creating_subid = parentSubid;
			if (oc->deleting_subid == mySubid)
				oc->deleting_subid = isCommit ? parentSubid : InvalidSubTransactionId;
			prev_item = cur_item;
			cur_item = lnext(prev_item);
		}
	}
}

/*
 * This is intended as a callback for RangeVarGetRelidExtended().  It allows
 * the relation to be locked only if (1) it's a plain table, materialized
 * view, or TOAST table and (2) the current user is the owner (or the
 * superuser).  This meets the permission-checking needs of CLUSTER, REINDEX
 * TABLE, and REFRESH MATERIALIZED VIEW; we expose it here so that it can be
 * used by all.
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
		reltype = ((AlterTableStmt *) stmt)->relkind;
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
	if (IsA(stmt, AlterObjectSchemaStmt) &&
		relkind != RELKIND_RELATION &&
		relkind != RELKIND_VIEW &&
		relkind != RELKIND_MATVIEW &&
		relkind != RELKIND_SEQUENCE &&
		relkind != RELKIND_FOREIGN_TABLE &&
		relkind != RELKIND_PARTITIONED_TABLE)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("\"%s\" is not a table, view, materialized view, sequence, or foreign table",
						rv->relname)));

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
	RangeTblEntry *rte;
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
	rte = addRangeTableEntryForRelation(pstate, rel, NULL, false, true);
	addRTEtoQuery(pstate, rte, true, true, true);

	/* take care of any partition expressions */
	foreach(l, partspec->partParams)
	{
		PartitionElem *pelem = castNode(PartitionElem, lfirst(l));

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
ComputePartitionAttrs(Relation rel, List *partParams, AttrNumber *partattrs,
					  List **partexprs, Oid *partopclass, Oid *partcollation,
					  char strategy)
{
	int			attn;
	ListCell   *lc;
	Oid			am_oid;

	attn = 0;
	foreach(lc, partParams)
	{
		PartitionElem *pelem = castNode(PartitionElem, lfirst(lc));
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
								pelem->name)));
			attform = (Form_pg_attribute) GETSTRUCT(atttuple);

			if (attform->attnum <= 0)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
						 errmsg("cannot use system column \"%s\" in partition key",
								pelem->name)));

			partattrs[attn] = attform->attnum;
			atttype = attform->atttypid;
			attcollation = attform->attcollation;
			ReleaseSysCache(atttuple);
		}
		else
		{
			/* Expression */
			Node	   *expr = pelem->expr;

			Assert(expr != NULL);
			atttype = exprType(expr);
			attcollation = exprCollation(expr);

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
				 * Cannot have expressions containing whole-row references or
				 * system column references.
				 */
				pull_varattnos(expr, 1, &expr_attrs);
				if (bms_is_member(0 - FirstLowInvalidHeapAttributeNumber,
								  expr_attrs))
					ereport(ERROR,
							(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
							 errmsg("partition key expressions cannot contain whole-row references")));
				for (i = FirstLowInvalidHeapAttributeNumber; i < 0; i++)
				{
					if (bms_is_member(i - FirstLowInvalidHeapAttributeNumber,
									  expr_attrs))
						ereport(ERROR,
								(errcode(ERRCODE_INVALID_OBJECT_DEFINITION),
								 errmsg("partition key expressions cannot contain system column references")));
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
							 errmsg("data type %s has no default hash operator class",
									format_type_be(atttype)),
							 errhint("You must specify a hash operator class or define a default hash operator class for the data type.")));
				else
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_OBJECT),
							 errmsg("data type %s has no default btree operator class",
									format_type_be(atttype)),
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
	int			num_check,
				i;

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
	 * not-false and try to prove the same for partConstraint.
	 *
	 * Note that predicate_implied_by assumes its first argument is known
	 * immutable.  That should always be true for partition constraints, so we
	 * don't test it here.
	 */
	return predicate_implied_by(partConstraint, existConstraint, true);
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
			ereport(INFO,
					(errmsg("partition constraint for table \"%s\" is implied by existing constraints",
							RelationGetRelationName(scanrel))));
		else
			ereport(INFO,
					(errmsg("updated partition constraint for default partition \"%s\" is implied by existing constraints",
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
		PartitionDesc partdesc = RelationGetPartitionDesc(scanrel);
		int			i;

		for (i = 0; i < partdesc->nparts; i++)
		{
			Relation	part_rel;
			bool		found_whole_row;
			List	   *thisPartConstraint;

			/*
			 * This is the minimum lock we need to prevent deadlocks.
			 */
			part_rel = heap_open(partdesc->oids[i], AccessExclusiveLock);

			/*
			 * Adjust the constraint for scanrel so that it matches this
			 * partition's attribute numbers.
			 */
			thisPartConstraint =
				map_partition_varattnos(partConstraint, 1,
										part_rel, scanrel, &found_whole_row);
			/* There can never be a whole-row reference here */
			if (found_whole_row)
				elog(ERROR, "unexpected whole-row reference found in partition constraint");

			QueuePartitionConstraintValidation(wqueue, part_rel,
											   thisPartConstraint,
											   validate_default);
			heap_close(part_rel, NoLock);	/* keep lock till commit */
		}
	}
}

/*
 * ALTER TABLE <name> ATTACH PARTITION <partition-name> FOR VALUES
 *
 * Return the address of the newly attached partition.
 */
static ObjectAddress
ATExecAttachPartition(List **wqueue, Relation rel, PartitionCmd *cmd)
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
	bool		found_whole_row;
	Oid			defaultPartOid;
	List	   *partBoundConstraint;
	List	   *cloned;
	ListCell   *l;

	/*
	 * We must lock the default partition if one exists, because attaching a
	 * new partition will change its partition constraint.
	 */
	defaultPartOid =
		get_default_oid_from_partdesc(RelationGetPartitionDesc(rel));
	if (OidIsValid(defaultPartOid))
		LockRelationOid(defaultPartOid, AccessExclusiveLock);

	attachrel = heap_openrv(cmd->name, AccessExclusiveLock);

	/*
	 * XXX I think it'd be a good idea to grab locks on all tables referenced
	 * by FKs at this point also.
	 */

	/*
	 * Must be owner of both parent and source table -- parent was checked by
	 * ATSimplePermissions call in ATPrepCmd
	 */
	ATSimplePermissions(attachrel, ATT_TABLE | ATT_FOREIGN_TABLE);

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
	catalog = heap_open(InheritsRelationId, AccessShareLock);
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
	heap_close(catalog, AccessShareLock);

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

	/* If parent has OIDs then child must have OIDs */
	if (rel->rd_rel->relhasoids && !attachrel->rd_rel->relhasoids)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach table \"%s\" without OIDs as partition of"
						" table \"%s\" with OIDs", RelationGetRelationName(attachrel),
						RelationGetRelationName(rel))));

	/* OTOH, if parent doesn't have them, do not allow in attachrel either */
	if (attachrel->rd_rel->relhasoids && !rel->rd_rel->relhasoids)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("cannot attach table \"%s\" with OIDs as partition of table"
						" \"%s\" without OIDs", RelationGetRelationName(attachrel),
						RelationGetRelationName(rel))));

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
				 errdetail("ROW triggers with transition tables are not supported on partitions")));

	/*
	 * Check that the new partition's bound is valid and does not overlap any
	 * of existing partitions of the parent - note that it does not return on
	 * error.
	 */
	check_new_partition_bound(RelationGetRelationName(attachrel), rel,
							  cmd->bound);

	/* OK to create inheritance.  Rest of the checks performed there */
	CreateInheritance(attachrel, rel);

	/* Update the pg_class entry. */
	StorePartitionBound(attachrel, rel, cmd->bound);

	/* Ensure there exists a correct set of indexes in the partition. */
	AttachPartitionEnsureIndexes(rel, attachrel, wqueue);

	/* and triggers */
	CloneRowTriggersToPartition(rel, attachrel);

	/*
	 * Clone foreign key constraints, and setup for Phase 3 to verify them.
	 */
	cloned = NIL;
	CloneForeignKeyConstraints(RelationGetRelid(rel),
							   RelationGetRelid(attachrel), &cloned);
	foreach(l, cloned)
	{
		ClonedConstraint *clonedcon = lfirst(l);
		NewConstraint *newcon;
		Relation	clonedrel;
		AlteredTableInfo *parttab;

		clonedrel = relation_open(clonedcon->relid, NoLock);
		parttab = ATGetQueueEntry(wqueue, clonedrel);

		newcon = (NewConstraint *) palloc0(sizeof(NewConstraint));
		newcon->name = clonedcon->constraint->conname;
		newcon->contype = CONSTR_FOREIGN;
		newcon->refrelid = clonedcon->refrelid;
		newcon->refindid = clonedcon->conindid;
		newcon->conid = clonedcon->conid;
		newcon->qual = (Node *) clonedcon->constraint;

		parttab->constraints = lappend(parttab->constraints, newcon);

		relation_close(clonedrel, NoLock);
	}

	/*
	 * Generate partition constraint from the partition bound specification.
	 * If the parent itself is a partition, make sure to include its
	 * constraint as well.
	 */
	partBoundConstraint = get_qual_from_partbound(attachrel, rel, cmd->bound);
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
												 rel, &found_whole_row);
		/* There can never be a whole-row reference here */
		if (found_whole_row)
			elog(ERROR,
				 "unexpected whole-row reference found in partition key");

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
		defaultrel = heap_open(defaultPartOid, NoLock);
		defPartConstraint =
			get_proposed_default_constraint(partBoundConstraint);
		QueuePartitionConstraintValidation(wqueue, defaultrel,
										   defPartConstraint, true);

		/* keep our lock until commit. */
		heap_close(defaultrel, NoLock);
	}

	ObjectAddressSet(address, RelationRelationId, RelationGetRelid(attachrel));

	/*
	 * If the partition we just attached is partitioned itself, invalidate
	 * relcache for all descendent partitions too to ensure that their
	 * rd_partcheck expression trees are rebuilt; partitions already locked
	 * at the beginning of this function.
	 */
	if (attachrel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		ListCell *l;

		foreach(l, attachrel_children)
		{
			CacheInvalidateRelcacheByRelid(lfirst_oid(l));
		}
	}

	/* keep our lock until commit */
	heap_close(attachrel, NoLock);

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
	 * For each index on the partitioned table, find a matching one in the
	 * partition-to-be; if one is not found, create one.
	 */
	foreach(cell, idxes)
	{
		Oid			idx = lfirst_oid(cell);
		Relation	idxRel = index_open(idx, AccessShareLock);
		IndexInfo  *info;
		AttrNumber *attmap;
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
		attmap = convert_tuples_by_name_map(
			RelationGetDescr(attachrel), RelationGetDescr(rel),
			gettext_noop("could not convert row type"),
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
								 attmap,
								 RelationGetDescr(rel)->natts))
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
					ConstraintSetParentConstraint(cldConstrOid, constraintOid);
				update_relispartition(NULL, cldIdxId, true);
				found = true;
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

			stmt = generateClonedIndexStmt(NULL, RelationGetRelid(attachrel),
										   idxRel, attmap,
										   RelationGetDescr(rel)->natts,
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

	/* Clean up. */
	for (i = 0; i < list_length(attachRelIdxs); i++)
		index_close(attachrelIdxRels[i], AccessShareLock);
	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(cxt);
}

/*
 * isPartitionTrigger
 *		Subroutine for CloneRowTriggersToPartition: determine whether
 *		the given trigger has been cloned from another one.
 *
 * We use pg_depend as a proxy for this, since we don't have any direct
 * evidence.  This is an ugly hack to cope with a catalog deficiency.
 * Keep away from children.  Do not stare with naked eyes.  Do not propagate.
 */
static bool
isPartitionTrigger(Oid trigger_oid)
{
	Relation	pg_depend;
	ScanKeyData key[2];
	SysScanDesc	scan;
	HeapTuple	tup;
	bool		found = false;

	pg_depend = heap_open(DependRelationId, AccessShareLock);

	ScanKeyInit(&key[0], Anum_pg_depend_classid,
				BTEqualStrategyNumber,
				F_OIDEQ,
				ObjectIdGetDatum(TriggerRelationId));
	ScanKeyInit(&key[1], Anum_pg_depend_objid,
				BTEqualStrategyNumber,
				F_OIDEQ,
				ObjectIdGetDatum(trigger_oid));

	scan = systable_beginscan(pg_depend, DependDependerIndexId,
							  true, NULL, 2, key);
	while ((tup = systable_getnext(scan)) != NULL)
	{
		Form_pg_depend	dep = (Form_pg_depend) GETSTRUCT(tup);

		if (dep->refclassid == TriggerRelationId)
		{
			found = true;
			break;
		}
	}

	systable_endscan(scan);
	heap_close(pg_depend, AccessShareLock);

	return found;
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
	MemoryContext oldcxt,
				perTupCxt;

	ScanKeyInit(&key, Anum_pg_trigger_tgrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(RelationGetRelid(parent)));
	pg_trigger = heap_open(TriggerRelationId, RowExclusiveLock);
	scan = systable_beginscan(pg_trigger, TriggerRelidNameIndexId,
							  true, NULL, 1, &key);

	perTupCxt = AllocSetContextCreate(GetCurrentMemoryContext(),
									  "clone trig", ALLOCSET_SMALL_SIZES);
	oldcxt = MemoryContextSwitchTo(perTupCxt);

	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_trigger trigForm;
		CreateTrigStmt *trigStmt;
		Node	   *qual = NULL;
		Datum		value;
		bool		isnull;
		List	   *cols = NIL;

		trigForm = (Form_pg_trigger) GETSTRUCT(tuple);

		/*
		 * Ignore statement-level triggers; those are not cloned.
		 */
		if (!TRIGGER_FOR_ROW(trigForm->tgtype))
			continue;

		/*
		 * Internal triggers require careful examination.  Ideally, we don't
		 * clone them.
		 *
		 * However, if our parent is a partitioned relation, there might be
		 * internal triggers that need cloning.  In that case, we must
		 * skip clone it if the trigger on parent depends on another trigger.
		 *
		 * Note we dare not verify that the other trigger belongs to an
		 * ancestor relation of our parent, because that creates deadlock
		 * opportunities.
		 */
		if (trigForm->tgisinternal &&
			(!parent->rd_rel->relispartition ||
			 !isPartitionTrigger(HeapTupleGetOid(tuple))))
			continue;

		/*
		 * Complain if we find an unexpected trigger type.
		 */
		if (!TRIGGER_FOR_AFTER(trigForm->tgtype))
			elog(ERROR, "unexpected trigger \"%s\" found",
				 NameStr(trigForm->tgname));

		/*
		 * If there is a WHEN clause, generate a 'cooked' version of it that's
		 * appropriate for the partition.
		 */
		value = heap_getattr(tuple, Anum_pg_trigger_tgqual,
							 RelationGetDescr(pg_trigger), &isnull);
		if (!isnull)
		{
			bool		found_whole_row;

			qual = stringToNode(TextDatumGetCString(value));
			qual = (Node *) map_partition_varattnos((List *) qual, PRS2_OLD_VARNO,
													partition, parent,
													&found_whole_row);
			if (found_whole_row)
				elog(ERROR, "unexpected whole-row reference found in trigger WHEN clause");
			qual = (Node *) map_partition_varattnos((List *) qual, PRS2_NEW_VARNO,
													partition, parent,
													&found_whole_row);
			if (found_whole_row)
				elog(ERROR, "unexpected whole-row reference found in trigger WHEN clause");
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

		trigStmt = makeNode(CreateTrigStmt);
		trigStmt->trigname = NameStr(trigForm->tgname);
		trigStmt->relation = NULL;
		trigStmt->funcname = NULL;	/* passed separately */
		trigStmt->args = NULL;	/* passed separately */
		trigStmt->row = true;
		trigStmt->timing = trigForm->tgtype & TRIGGER_TYPE_TIMING_MASK;
		trigStmt->events = trigForm->tgtype & TRIGGER_TYPE_EVENT_MASK;
		trigStmt->columns = cols;
		trigStmt->whenClause = NULL;	/* passed separately */
		trigStmt->isconstraint = OidIsValid(trigForm->tgconstraint);
		trigStmt->transitionRels = NIL; /* not supported at present */
		trigStmt->deferrable = trigForm->tgdeferrable;
		trigStmt->initdeferred = trigForm->tginitdeferred;
		trigStmt->constrrel = NULL; /* passed separately */

		CreateTriggerFiringOn(trigStmt, NULL, RelationGetRelid(partition),
							  trigForm->tgconstrrelid, InvalidOid, InvalidOid,
							  trigForm->tgfoid, HeapTupleGetOid(tuple), qual,
							  false, true, trigForm->tgenabled);

		MemoryContextReset(perTupCxt);
	}

	MemoryContextSwitchTo(oldcxt);
	MemoryContextDelete(perTupCxt);

	systable_endscan(scan);
	heap_close(pg_trigger, RowExclusiveLock);
}

/*
 * ALTER TABLE DETACH PARTITION
 *
 * Return the address of the relation that is no longer a partition of rel.
 */
static ObjectAddress
ATExecDetachPartition(Relation rel, RangeVar *name)
{
	Relation	partRel,
				classRel;
	HeapTuple	tuple,
				newtuple;
	Datum		new_val[Natts_pg_class];
	bool		new_null[Natts_pg_class],
				new_repl[Natts_pg_class];
	ObjectAddress address;
	Oid			defaultPartOid;
	List	   *indexes;
	List	   *fks;
	ListCell   *cell;

	/*
	 * We must lock the default partition, because detaching this partition
	 * will change its partition constraint.
	 */
	defaultPartOid =
		get_default_oid_from_partdesc(RelationGetPartitionDesc(rel));
	if (OidIsValid(defaultPartOid))
		LockRelationOid(defaultPartOid, AccessExclusiveLock);

	partRel = heap_openrv(name, ShareUpdateExclusiveLock);

	/* All inheritance related checks are performed within the function */
	RemoveInheritance(partRel, rel);

	/* Update pg_class tuple */
	classRel = heap_open(RelationRelationId, RowExclusiveLock);
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

	/* detach indexes too */
	indexes = RelationGetIndexList(partRel);
	foreach(cell, indexes)
	{
		Oid			idxid = lfirst_oid(cell);
		Relation	idx;
		Oid			constrOid;

		if (!has_superclass(idxid))
			continue;

		Assert((IndexGetRelation(get_partition_parent(idxid), false) ==
				RelationGetRelid(rel)));

		idx = index_open(idxid, AccessExclusiveLock);
		IndexSetParentIndex(idx, InvalidOid);
		update_relispartition(classRel, idxid, false);

		/* If there's a constraint associated with the index, detach it too */
		constrOid = get_relation_idx_constraint_oid(RelationGetRelid(partRel),
													idxid);
		if (OidIsValid(constrOid))
			ConstraintSetParentConstraint(constrOid, InvalidOid);

		index_close(idx, NoLock);
	}
	heap_close(classRel, RowExclusiveLock);

	/* Drop any triggers that were cloned on creation/attach. */
	DropClonedTriggersFromPartition(RelationGetRelid(partRel));

	/*
	 * Detach any foreign keys that are inherited.  This includes creating
	 * additional action triggers.
	 */
	fks = copyObject(RelationGetFKeyList(partRel));
	foreach(cell, fks)
	{
		ForeignKeyCacheInfo *fk = lfirst(cell);
		HeapTuple	contup;
		Form_pg_constraint conform;
		Constraint *fkconstraint;

		contup = SearchSysCache1(CONSTROID, ObjectIdGetDatum(fk->conoid));
		if (!contup)
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
		ConstraintSetParentConstraint(fk->conoid, InvalidOid);

		/*
		 * Make the action triggers on the referenced relation.  When this was
		 * a partition the action triggers pointed to the parent rel (they
		 * still do), but now we need separate ones of our own.
		 */
		fkconstraint = makeNode(Constraint);
		fkconstraint->conname = pstrdup(NameStr(conform->conname));
		fkconstraint->fk_upd_action = conform->confupdtype;
		fkconstraint->fk_del_action = conform->confdeltype;
		fkconstraint->deferrable = conform->condeferrable;
		fkconstraint->initdeferred = conform->condeferred;

		createForeignKeyActionTriggers(partRel, conform->confrelid,
									   fkconstraint, fk->conoid,
									   conform->conindid);

		ReleaseSysCache(contup);
	}
	list_free_deep(fks);

	/*
	 * Invalidate the parent's relcache so that the partition is no longer
	 * included in its partition descriptor.
	 */
	CacheInvalidateRelcache(rel);

	/*
	 * If the partition we just detached is partitioned itself, invalidate
	 * relcache for all descendent partitions too to ensure that their
	 * rd_partcheck expression trees are rebuilt; must lock partitions
	 * before doing so, using the same lockmode as what partRel has been
	 * locked with by the caller.
	 */
	if (partRel->rd_rel->relkind == RELKIND_PARTITIONED_TABLE)
	{
		List   *children;

		children = find_all_inheritors(RelationGetRelid(partRel),
									   AccessExclusiveLock, NULL);
		foreach(cell, children)
		{
			CacheInvalidateRelcacheByRelid(lfirst_oid(cell));
		}
	}

	ObjectAddressSet(address, RelationRelationId, RelationGetRelid(partRel));

	/* keep our lock until commit */
	heap_close(partRel, NoLock);

	return address;
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
	SysScanDesc	scan;
	HeapTuple	trigtup;
	Relation	tgrel;
	ObjectAddresses *objects;

	objects = new_object_addresses();

	/*
	 * Scan pg_trigger to search for all triggers on this rel.
	 */
	ScanKeyInit(&skey, Anum_pg_trigger_tgrelid, BTEqualStrategyNumber,
				F_OIDEQ, ObjectIdGetDatum(partitionId));
	tgrel = heap_open(TriggerRelationId, RowExclusiveLock);
	scan = systable_beginscan(tgrel, TriggerRelidNameIndexId,
							  true, NULL, 1, &skey);
	while (HeapTupleIsValid(trigtup = systable_getnext(scan)))
	{
		Oid			trigoid = HeapTupleGetOid(trigtup);
		ObjectAddress trig;

		/* Ignore triggers that weren't cloned */
		if (!isPartitionTrigger(trigoid))
			continue;

		/*
		 * This is ugly, but necessary: remove the dependency markings on the
		 * trigger so that it can be removed.
		 */
		deleteDependencyRecordsForClass(TriggerRelationId, trigoid,
										TriggerRelationId,
										DEPENDENCY_INTERNAL_AUTO);
		deleteDependencyRecordsForClass(TriggerRelationId, trigoid,
										RelationRelationId,
										DEPENDENCY_INTERNAL_AUTO);

		/* remember this trigger to remove it below */
		ObjectAddressSet(trig, TriggerRelationId, trigoid);
		add_exact_object_address(&trig, objects);
	}

	/* make the dependency removal visible to the deletion below */
	CommandCounterIncrement();
	performMultipleDeletions(objects, DROP_RESTRICT, PERFORM_DELETION_INTERNAL);

	/* done */
	free_object_addresses(objects);
	systable_endscan(scan);
	heap_close(tgrel, RowExclusiveLock);
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
		get_partition_parent(partIdxId) : InvalidOid;
	if (currParent != RelationGetRelid(parentIdx))
	{
		IndexInfo  *childInfo;
		IndexInfo  *parentInfo;
		AttrNumber *attmap;
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
		partDesc = RelationGetPartitionDesc(parentTbl);
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
					(errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
							RelationGetRelationName(partIdx),
							RelationGetRelationName(parentIdx)),
					 errdetail("Index \"%s\" is not an index on any partition of table \"%s\".",
							   RelationGetRelationName(partIdx),
							   RelationGetRelationName(parentTbl))));

		/* Ensure the indexes are compatible */
		childInfo = BuildIndexInfo(partIdx);
		parentInfo = BuildIndexInfo(parentIdx);
		attmap = convert_tuples_by_name_map(
			RelationGetDescr(partTbl), RelationGetDescr(parentTbl),
			gettext_noop("could not convert row type"),
			false /* yb_ignore_type_mismatch */);
		if (!CompareIndexInfo(childInfo, parentInfo,
							  partIdx->rd_indcollation,
							  parentIdx->rd_indcollation,
							  partIdx->rd_opfamily,
							  parentIdx->rd_opfamily,
							  attmap,
							  RelationGetDescr(partTbl)->natts))
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
			ConstraintSetParentConstraint(cldConstrId, constraintOid);
		update_relispartition(NULL, partIdxId, true);

		pfree(attmap);

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
	Relation	pg_inherits;
	ScanKeyData key;
	HeapTuple	tuple;
	SysScanDesc scan;

	pg_inherits = heap_open(InheritsRelationId, AccessShareLock);
	ScanKeyInit(&key, Anum_pg_inherits_inhparent,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(RelationGetRelid(parentIdx)));
	scan = systable_beginscan(pg_inherits, InheritsParentIndexId, true,
							  NULL, 1, &key);
	while (HeapTupleIsValid(tuple = systable_getnext(scan)))
	{
		Form_pg_inherits inhForm;
		Oid			tab;

		inhForm = (Form_pg_inherits) GETSTRUCT(tuple);
		tab = IndexGetRelation(inhForm->inhrelid, false);
		if (tab == RelationGetRelid(partitionTbl))
			ereport(ERROR,
					(errcode(ERRCODE_OBJECT_NOT_IN_PREREQUISITE_STATE),
					 errmsg("cannot attach index \"%s\" as a partition of index \"%s\"",
							RelationGetRelationName(partIdx),
							RelationGetRelationName(parentIdx)),
					 errdetail("Another index is already attached for partition \"%s\".",
							   RelationGetRelationName(partitionTbl))));
	}

	systable_endscan(scan);
	heap_close(pg_inherits, AccessShareLock);
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
	inheritsRel = heap_open(InheritsRelationId, AccessShareLock);
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
		if (!indTup)
			elog(ERROR, "cache lookup failed for index %u",
				 inhForm->inhrelid);
		indexForm = (Form_pg_index) GETSTRUCT(indTup);
		if (IndexIsValid(indexForm))
			tuples += 1;
		ReleaseSysCache(indTup);
	}

	/* Done with pg_inherits */
	systable_endscan(scan);
	heap_close(inheritsRel, AccessShareLock);

	/*
	 * If we found as many inherited indexes as the partitioned table has
	 * partitions, we're good; update pg_index to set indisvalid.
	 */
	if (tuples == RelationGetPartitionDesc(partedTbl)->nparts)
	{
		Relation	idxRel;
		HeapTuple	newtup;

		idxRel = heap_open(IndexRelationId, RowExclusiveLock);

		newtup = heap_copytuple(partedIdx->rd_indextuple);
		((Form_pg_index) GETSTRUCT(newtup))->indisvalid = true;
		updated = true;

		CatalogTupleUpdate(idxRel, &partedIdx->rd_indextuple->t_self, newtup);

		heap_close(idxRel, RowExclusiveLock);
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

		parentIdxId = get_partition_parent(RelationGetRelid(partedIdx));
		parentTblId = get_partition_parent(RelationGetRelid(partedTbl));
		parentIdx = relation_open(parentIdxId, AccessExclusiveLock);
		parentTbl = relation_open(parentTblId, AccessExclusiveLock);
		Assert(!parentIdx->rd_index->indisvalid);

		validatePartitionedIndex(parentIdx, parentTbl);

		relation_close(parentIdx, AccessExclusiveLock);
		relation_close(parentTbl, AccessExclusiveLock);
	}
}

/*
 * Update the relispartition flag of the given relation to the given value.
 *
 * classRel is the pg_class relation, already open and suitably locked.
 * It can be passed as NULL, in which case it's opened and closed locally.
 */
static void
update_relispartition(Relation classRel, Oid relationId, bool newval)
{
	HeapTuple	tup;
	HeapTuple	newtup;
	Form_pg_class classForm;
	bool		opened = false;

	if (classRel == NULL)
	{
		classRel = heap_open(RelationRelationId, RowExclusiveLock);
		opened = true;
	}

	tup = SearchSysCache1(RELOID, ObjectIdGetDatum(relationId));
	newtup = heap_copytuple(tup);
	classForm = (Form_pg_class) GETSTRUCT(newtup);
	classForm->relispartition = newval;
	CatalogTupleUpdate(classRel, &tup->t_self, newtup);
	heap_freetuple(newtup);
	ReleaseSysCache(tup);

	if (opened)
		heap_close(classRel, RowExclusiveLock);
}

/*
 * Used when cloning a table for an alter statement to make copies of the old
 * relation's extended statistics objects and statistics for the new relation.
 */
static void
YbATCopyStats(Oid old_relid, RangeVar *new_rel, Oid new_relid,
			  AttrNumber *attmap, int altered_old_attnum)
{
	Relation		pg_statistic, pg_statistic_ext;
	HeapTuple		tuple;
	ScanKeyData		key;
	SysScanDesc		scan;

	/* Copy extended statistics objects. */
	pg_statistic_ext = heap_open(StatisticExtRelationId, RowExclusiveLock);
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
											HeapTupleGetOid(tuple));
		stmt->defnames = stringToQualifiedNameList(orig_stats_name);
		CreateStatistics(stmt);
	}
	systable_endscan(scan);
	heap_close(pg_statistic_ext, RowExclusiveLock);

	/* Copy pg_statistic entries with updated starelid and staattnum values. */
	pg_statistic =  heap_open(StatisticRelationId, RowExclusiveLock);
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
			Int16GetDatum(attmap[stat_form->staattnum - 1]);
		replaces[Anum_pg_statistic_staattnum - 1] = true;

		newtuple = heap_modify_tuple(tuple, RelationGetDescr(pg_statistic),
									 values, nulls, replaces);

		/* Insert new pg_statistic entry. */
		CatalogTupleInsert(pg_statistic, newtuple);
		heap_freetuple(newtuple);
	}
	systable_endscan(scan);
	heap_close(pg_statistic, RowExclusiveLock);
}

/*
 * Used in YB when cloning a table for an alter statement to make copies of the
 * old relation's policy objects for the new relation. This function is adapted
 * from CreatePolicy().
 */
static void
YbATCopyPolicyObjects(Relation old_rel, Relation new_rel, AttrNumber *attmap)
{
	Relation		pg_policy;
	ScanKeyData		key;
	HeapTuple		old_policy_tuple;
	SysScanDesc		scan;

	pg_policy = heap_open(PolicyRelationId, RowExclusiveLock);
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

		values[Anum_pg_policy_polrelid - 1] =
			ObjectIdGetDatum(RelationGetRelid(new_rel));
		replaces[Anum_pg_policy_polrelid - 1 ] = true;

		Datum qual_datum = heap_getattr(old_policy_tuple,
										Anum_pg_policy_polqual,
										RelationGetDescr(pg_policy),
										&is_null);

		if (!is_null)
		{
			qual = map_variable_attnos(
				stringToNode(TextDatumGetCString(qual_datum)), 1, 0, attmap,
				RelationGetDescr(old_rel)->natts,
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
				attmap, RelationGetDescr(old_rel)->natts,
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

		HeapTupleSetOid(new_policy_tuple, InvalidOid);

		new_policy_oid = CatalogTupleInsert(pg_policy, new_policy_tuple);

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
		RangeTblEntry *rte =
			addRangeTableEntryForRelation(pstate, new_rel, NULL, false, true);
		addRTEtoQuery(pstate, rte, true, true, true);

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
	heap_close(pg_policy, RowExclusiveLock);
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
			(RawStmt *) linitial(raw_parser(query_str));
		Query *viewParse = parse_analyze(rawstmt, query_str, NULL, 0, NULL);
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
YbATCopyMiscMetadata(Relation old_rel, Relation new_rel, AttrNumber *attmap)
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
		heap_open(RelationRelationId, RowExclusiveLock);

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
	heap_close(pg_class, RowExclusiveLock);

	/*
	 * Copy attacl and attstattarget values from old relation's attributes'
	 * pg_attribute entries.
	 */
	Relation	pg_attribute =
		heap_open(AttributeRelationId, RowExclusiveLock);
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
			Int16GetDatum(attmap[attno - 1]));
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
	heap_close(pg_attribute, RowExclusiveLock);
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

		table_element = lnext(table_element);
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
							 Relation *new_rel, AttrNumber **old2new_attmap,
							 AttrNumber **new2old_attmap,
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
	*new_rel = heap_open(new_relid, AccessExclusiveLock);

	*old2new_attmap = convert_tuples_by_name_map(
		RelationGetDescr(old_rel), RelationGetDescr(*new_rel),
		gettext_noop("could not convert row type"), ignore_type_mismatch);

	*new2old_attmap = convert_tuples_by_name_map(
		RelationGetDescr(*new_rel), RelationGetDescr(old_rel),
		gettext_noop("could not convert row type"), ignore_type_mismatch);
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
							AttrNumber *conkey_attmap,
							AttrNumber *confkey_attmap)
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

	int16 conkey[numkeys];
	int16 confkey[numkeys];
	Oid   pfeqop[numkeys];
	Oid   ppeqop[numkeys];
	Oid   ffeqop[numkeys];

	Oid index_oid;
	Oid index_opclasses[numkeys];

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
			conkey[i]  = conkey_attmap[conkey[i] - 1];
		if (confkey_attmap)
			confkey[i] = confkey_attmap[confkey[i] - 1];
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
		con_form->confupdtype, con_form->confdeltype, con_form->confmatchtype,
		NULL /* exclOp - not an exclusion constraint */,
		NULL /* conExpr - not a check constraint */,
		NULL /* conBin - not a check constraint */,
		NULL /* conSrc - not a check constraint */, true /* islocal */,
		0 /* inhcount */, con_form->connoinherit /* conNoInherit */,
		false /* is_internal */);

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
	 * Note that this calls CommandCounterIncrement().
	 */
	createForeignKeyTriggers(base_rel, RelationGetRelid(fk_rel), entity,
							 constr_oid, index_oid, true /* create_action */);

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
							  AttrNumber *new2old_attmap,
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
					List	*raw_parsetree_list = raw_parser(defstring);
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
					RangeTblEntry *rte = addRangeTableEntryForRelation(
						pstate, new_rel, NULL, false, true);
					addRTEtoQuery(pstate, rte, true, true, true);

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
						RelationGetDescr(old_rel)->natts,
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
					heap_open(con_form->confrelid, ShareRowExclusiveLock);

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

				heap_close(fk_rel, ShareRowExclusiveLock);
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
						   AttrNumber *old2new_attmap,
						   AttrNumber *new2old_attmap,
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
	HeapScanDesc	scan;
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

	oldslot = MakeSingleTupleTableSlot(oldTupDesc);
	newslot = MakeSingleTupleTableSlot(newTupDesc);

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
			new2old_attmap[new_column_value->attnum - 1];
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
	scan = heap_beginscan(old_rel, snapshot, 0, NULL);

	/*
	 * Switch to per-tuple memory context and reset it for each tuple
	 * produced, so we don't leak memory.
	 */
	oldcxt = MemoryContextSwitchTo(econtext->ecxt_per_tuple_memory);

	/* The following while loop is similar to the one in ATRewriteTable. */
	while ((tuple = heap_getnext(scan, ForwardScanDirection)) != NULL)
	{
		Oid tupOid = InvalidOid;

		/* Extract data from old tuple */
		heap_deform_tuple(tuple, oldTupDesc, old_values, old_isnull);
		if (oldTupDesc->tdhasoid)
			tupOid = HeapTupleGetOid(tuple);

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
				new_values[i] = old_values[old2new_attmap[i] - 1];
				new_isnull[i] = old_isnull[old2new_attmap[i] - 1];
			}
		}

		/*
		 * Form the new tuple. Note that we don't explicitly pfree it,
		 * since the per-tuple memory context will be reset shortly.
		 */
		tuple = heap_form_tuple(newTupDesc, new_values, new_isnull);

		/* Preserve OID, if any */
		if (newTupDesc->tdhasoid)
			HeapTupleSetOid(tuple, tupOid);

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
							 errmsg("column \"%s\" contains null values",
									NameStr(attr->attname)),
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
YbATCopyIndexes(Relation old_rel, Oid new_relid, AttrNumber *new2old_attmap,
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
			NULL /* heapRel, we provide an oid instead */, new_relid, idx_rel,
			new2old_attmap, RelationGetDescr(old_rel)->natts,
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
						const char *altered_column_name, AttrNumber *attmap)
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
						 HeapTupleGetOid(con_tuple));
		add_exact_object_address(&con_addr, cons_to_drop);

		/*
		 * We don't need AccessExclusiveLock since old constraint
		 * can't be violated at any point.
		 */
		Relation base_rel =
			heap_open(con_form->conrelid, ShareUpdateExclusiveLock);

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
		heap_close(base_rel, ShareUpdateExclusiveLock);
	}
	systable_endscan(scan);

	performMultipleDeletions(cons_to_drop, DROP_RESTRICT,
							 PERFORM_DELETION_INTERNAL);
}

static void
YbATCopyTriggers(const Relation old_rel, const Relation new_rel,
				 Relation pg_trigger, AttrNumber *new2old_attmap)
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
				qual = (Node *) map_variable_attnos(
					qual, fromrel_varno, 0 /* sublevels_up */, new2old_attmap,
					RelationGetDescr(old_rel)->natts,
					RelationGetForm(new_rel)->reltype, &found_whole_row);
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
						Relation pg_constraint, AttrNumber *old2new_attmap,
						AttrNumber *new2old_attmap, bool expect_dummy_pk,
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
	pg_depend = heap_open(DependRelationId, RowExclusiveLock);
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
	heap_close(pg_depend, RowExclusiveLock);

	/*
	 * Copy triggers.
	 */
	pg_trigger = heap_open(TriggerRelationId, RowExclusiveLock);
	YbATCopyTriggers(old_rel, new_rel, pg_trigger, new2old_attmap);
	heap_close(pg_trigger, RowExclusiveLock);

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
	AttrNumber *old2new_attmap = NULL;
	AttrNumber *new2old_attmap = NULL;
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

	Relation pg_constraint = heap_open(ConstraintRelationId, RowExclusiveLock);
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
	heap_close(pg_constraint, RowExclusiveLock);

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

		table_element = lnext(table_element);
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
	AttrNumber	*old2new_attmap = NULL;
	AttrNumber	*new2old_attmap = NULL;
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
	Relation pg_constraint = heap_open(ConstraintRelationId, RowExclusiveLock);
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
	heap_close(pg_constraint, RowExclusiveLock);

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
		Relation	partition = heap_open(childrelid, NoLock);
		AlteredTableInfo *childtab;
		/* Find or create work queue entry for this partition */
		childtab = ATGetQueueEntry(yb_wqueue, partition);
		childtab->rewrite |= YB_AT_REWRITE_ALTER_PRIMARY_KEY;
		heap_close(partition, NoLock);
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
 
/*
 * Used in YB during DROP TABLE to check whether a table belongs to a publication. This does not
 * check if the given table is part of any ALL TABLES publication.
 */
static bool
YbIsTablePartOfPublication(Oid relOid)
{
	Relation pubrel;
	SysScanDesc scan;
	ScanKeyData key;
	bool is_part_of_pub = false;

	pubrel = heap_open(PublicationRelRelationId, AccessShareLock);
	ScanKeyInit(&key, Anum_pg_publication_rel_prrelid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(relOid));
	scan = systable_beginscan(pubrel, PublicationRelPrrelidPrpubidIndexId,
							true, NULL, 1, &key);

	/* If we get at least one tuple, a publication exists for this relation. */
	if ((systable_getnext(scan)) != NULL)
		is_part_of_pub = true;

	systable_endscan(scan);
	heap_close(pubrel, AccessShareLock);

	return is_part_of_pub;
}
