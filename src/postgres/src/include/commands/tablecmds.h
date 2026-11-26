/*-------------------------------------------------------------------------
 *
 * tablecmds.h
 *	  prototypes for tablecmds.c.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/tablecmds.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef TABLECMDS_H
#define TABLECMDS_H

#include "access/htup.h"
#include "catalog/dependency.h"
#include "catalog/objectaddress.h"
#include "nodes/parsenodes.h"
#include "storage/lock.h"
#include "utils/relcache.h"

struct AlterTableUtilityContext;	/* avoid including tcop/utility.h here */


extern ObjectAddress DefineRelation(CreateStmt *stmt, char relkind, Oid ownerId,
									ObjectAddress *typaddress, const char *queryString);

extern void RemoveRelations(DropStmt *drop);

extern Oid	AlterTableLookupRelation(AlterTableStmt *stmt, LOCKMODE lockmode);

extern void AlterTable(AlterTableStmt *stmt, LOCKMODE lockmode,
					   struct AlterTableUtilityContext *context);

extern LOCKMODE AlterTableGetLockLevel(List *cmds);

extern void ATExecChangeOwner(Oid relationOid, Oid newOwnerId, bool recursing, LOCKMODE lockmode);

extern void AlterTableInternal(Oid relid, List *cmds, bool recurse);

extern Oid	AlterTableMoveAll(AlterTableMoveAllStmt *stmt);

extern ObjectAddress AlterTableNamespace(AlterObjectSchemaStmt *stmt,
										 Oid *oldschema);

extern void AlterTableNamespaceInternal(Relation rel, Oid oldNspOid,
										Oid nspOid, ObjectAddresses *objsMoved);

extern void AlterRelationNamespaceInternal(Relation classRel, Oid relOid,
										   Oid oldNspOid, Oid newNspOid,
										   bool hasDependEntry,
										   ObjectAddresses *objsMoved);

extern void CheckTableNotInUse(Relation rel, const char *stmt);

extern void ExecuteTruncate(TruncateStmt *stmt, bool yb_is_top_level, List **yb_relids);
extern void ExecuteTruncateGuts(List *explicit_rels,
								List *relids,
								List *relids_logged,
								DropBehavior behavior,
								bool restart_seqs,
								bool yb_is_top_level);

extern void SetRelationHasSubclass(Oid relationId, bool relhassubclass);

extern bool CheckRelationTableSpaceMove(Relation rel, Oid newTableSpaceId);
extern void SetRelationTableSpace(Relation rel, Oid newTableSpaceId,
								  Oid newRelFileNode);

extern ObjectAddress renameatt(RenameStmt *stmt);

extern ObjectAddress RenameConstraint(RenameStmt *stmt);

extern ObjectAddress RenameRelation(RenameStmt *stmt,
									bool yb_is_internal_clone_rename);

extern void RenameRelationInternal(Oid myrelid,
								   const char *newrelname, bool is_internal,
								   bool is_index);

extern void ResetRelRewrite(Oid myrelid);

extern void find_composite_type_dependencies(Oid typeOid,
											 Relation origRelation,
											 const char *origTypeName);

extern void check_of_type(HeapTuple typetuple);

extern void register_on_commit_action(Oid relid, OnCommitAction action);
extern void remove_on_commit_action(Oid relid);

extern void PreCommit_on_commit_actions(void);
extern void AtEOXact_on_commit_actions(bool isCommit);
extern void AtEOSubXact_on_commit_actions(bool isCommit,
										  SubTransactionId mySubid,
										  SubTransactionId parentSubid);

extern void RangeVarCallbackOwnsTable(const RangeVar *relation,
									  Oid relId, Oid oldRelId, void *arg);

extern void RangeVarCallbackOwnsRelation(const RangeVar *relation,
										 Oid relId, Oid oldRelId, void *arg);
extern bool PartConstraintImpliedByRelConstraint(Relation scanrel,
												 List *partConstraint);

/*
 * YB: interface between ATController and the ALTER TABLE ... CONCURRENTLY
 * driver.
 *
 * yb_alter_table_capture, when set, is filled for the main relation of the
 * outermost ALTER TABLE between phase 2 (catalog changes) and phase 3
 * (rewrite), so that the driver can run the ALTER on the empty shadow and
 * read back what phase 2 decided.  newvals is allocated in the
 * CurrentMemoryContext of the ATController call, so it outlives the ALTER.
 */
typedef struct YbNewColumnValue
{
	AttrNumber	attnum;			/* which column of the altered relation */
	Expr	   *expr;			/* over the old tuple; over the new tuple
								 * when is_generated */
	bool		is_generated;
} YbNewColumnValue;

typedef struct YbAlterTableCapture
{
	Oid			relid;			/* main relation of the ALTER */
	int			rewrite;		/* AT_REWRITE_* bitmask, 0 when no rewrite */
	List	   *newvals;		/* of YbNewColumnValue * */
	bool		verify_new_notnull;
	List	   *changed_index_oids; /* pre-ALTER OIDs of the indexes that
									 * ATPostAlterTypeCleanup dropped and
									 * recreated under new OIDs */
} YbAlterTableCapture;

extern YbAlterTableCapture *yb_alter_table_capture;

/*
 * Cutover of ALTER TABLE ... CONCURRENTLY: while yb_osc_cutover_shadow_oid is
 * set, the outermost ALTER TABLE applies its catalog changes to the altered
 * relation, sends no DocDB alter, and in phase 3 swaps relfilenodes with the
 * shadow (base table and indexes pairwise) instead of copying rows.
 * yb_osc_index_pairs holds two-element OID lists (source index, shadow index)
 * for indexes that exist on both sides before the ALTER; any other index is
 * paired by definition.  yb_osc_keep_old_table renames the shadow, which then
 * holds the old DocDB table, to yb_osc_old_<source oid> instead of dropping
 * it.
 */
extern Oid	yb_osc_cutover_shadow_oid;
extern List *yb_osc_index_pairs;
extern bool yb_osc_keep_old_table;

#endif							/* TABLECMDS_H */
