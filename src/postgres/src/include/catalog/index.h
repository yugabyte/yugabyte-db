/*-------------------------------------------------------------------------
 *
 * index.h
 *	  prototypes for catalog/index.c.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/catalog/index.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef INDEX_H
#define INDEX_H

#include "catalog/objectaddress.h"
#include "nodes/execnodes.h"

/* Yugabyte includes */
#include "access/tableam.h"

#define DEFAULT_INDEX_TYPE	"btree"

#define DEFAULT_YB_INDEX_TYPE	"lsm"

/* Action code for index_set_state_flags */
typedef enum
{
	INDEX_CREATE_SET_READY,
	INDEX_CREATE_SET_VALID,
	INDEX_DROP_CLEAR_VALID,
	INDEX_DROP_SET_DEAD
} IndexStateFlagsAction;

/* options for REINDEX */
typedef struct ReindexParams
{
	bits32		options;		/* bitmask of REINDEXOPT_* */
	Oid			tablespaceOid;	/* New tablespace to move indexes to.
								 * InvalidOid to do nothing. */
} ReindexParams;

/* flag bits for ReindexParams->flags */
#define REINDEXOPT_VERBOSE		0x01	/* print progress info */
#define REINDEXOPT_REPORT_PROGRESS 0x02 /* report pgstat progress */
#define REINDEXOPT_MISSING_OK 	0x04	/* skip missing relations */
#define REINDEXOPT_CONCURRENTLY	0x08	/* concurrent mode */

/* state info for validate_index bulkdelete callback */
typedef struct ValidateIndexState
{
	Tuplesortstate *tuplesort;	/* for sorting the index TIDs */
	/* statistics (for debug purposes only): */
	double		htups,
				itups,
				tups_inserted;
} ValidateIndexState;

extern void index_check_primary_key(Relation heapRel,
									IndexInfo *indexInfo,
									bool is_alter_table,
									IndexStmt *stmt);

#define	INDEX_CREATE_IS_PRIMARY				(1 << 0)
#define	INDEX_CREATE_ADD_CONSTRAINT			(1 << 1)
#define	INDEX_CREATE_SKIP_BUILD				(1 << 2)
#define	INDEX_CREATE_CONCURRENT				(1 << 3)
#define	INDEX_CREATE_IF_NOT_EXISTS			(1 << 4)
#define	INDEX_CREATE_PARTITIONED			(1 << 5)
#define INDEX_CREATE_INVALID				(1 << 6)

extern Oid	index_create(Relation heapRelation,
						 const char *indexRelationName,
						 Oid indexRelationId,
						 Oid parentIndexRelid,
						 Oid parentConstraintId,
						 Oid relFileNode,
						 IndexInfo *indexInfo,
						 List *indexColNames,
						 Oid accessMethodObjectId,
						 Oid tableSpaceId,
						 Oid *collationObjectId,
						 Oid *classObjectId,
						 int16 *coloptions,
						 Datum reloptions,
						 bits16 flags,
						 bits16 constr_flags,
						 bool allow_system_table_mods,
						 bool is_internal,
						 Oid *constraintId,
						 YbOptSplit *split_options,
						 const bool skip_index_backfill,
						 bool is_colocated,
						 Oid tablegroupId,
						 Oid colocationId);

#define	INDEX_CONSTR_CREATE_MARK_AS_PRIMARY	(1 << 0)
#define	INDEX_CONSTR_CREATE_DEFERRABLE		(1 << 1)
#define	INDEX_CONSTR_CREATE_INIT_DEFERRED	(1 << 2)
#define	INDEX_CONSTR_CREATE_UPDATE_INDEX	(1 << 3)
#define	INDEX_CONSTR_CREATE_REMOVE_OLD_DEPS	(1 << 4)

extern Oid	index_concurrently_create_copy(Relation heapRelation,
										   Oid oldIndexId,
										   Oid tablespaceOid,
										   const char *newName);

extern void index_concurrently_build(Oid heapRelationId,
									 Oid indexRelationId);

extern void index_concurrently_swap(Oid newIndexId,
									Oid oldIndexId,
									const char *oldName);

extern void index_concurrently_set_dead(Oid heapId,
										Oid indexId);

extern ObjectAddress index_constraint_create(Relation heapRelation,
											 Oid indexRelationId,
											 Oid parentConstraintId,
											 IndexInfo *indexInfo,
											 const char *constraintName,
											 char constraintType,
											 bits16 constr_flags,
											 bool allow_system_table_mods,
											 bool is_internal);

extern void index_drop(Oid indexId, bool concurrent, bool concurrent_lock_mode);

extern IndexInfo *BuildIndexInfo(Relation index);

extern IndexInfo *BuildDummyIndexInfo(Relation index);

extern bool CompareIndexInfo(IndexInfo *info1, IndexInfo *info2,
							 Oid *collations1, Oid *collations2,
							 Oid *opfamilies1, Oid *opfamilies2,
							 AttrMap *attmap);

extern void BuildSpeculativeIndexInfo(Relation index, IndexInfo *ii);

extern void FormIndexDatum(IndexInfo *indexInfo,
						   TupleTableSlot *slot,
						   EState *estate,
						   Datum *values,
						   bool *isnull);

extern void index_build(Relation heapRelation,
						Relation indexRelation,
						IndexInfo *indexInfo,
						bool isreindex,
						bool parallel);

extern void index_backfill(Relation heapRelation,
						   Relation indexRelation,
						   IndexInfo *indexInfo,
						   bool isprimary,
						   YbBackfillInfo *bfinfo,
						   YbPgExecOutParam *bfresult);

/* TODO: add Yb prefix. */
extern double IndexBackfillHeapRangeScan(Relation heapRelation,
										 Relation indexRelation,
										 IndexInfo *indexInfo,
										 YbIndexBuildCallback ybcallback,
										 void *callback_state,
										 YbBackfillInfo *bfinfo,
										 YbPgExecOutParam *bfresult);

extern void validate_index(Oid heapId, Oid indexId, Snapshot snapshot);

extern void index_set_state_flags(Oid indexId, IndexStateFlagsAction action);

extern Oid	IndexGetRelation(Oid indexId, bool missing_ok);

extern void reindex_index(Oid indexId, bool skip_constraint_checks,
						  char relpersistence, ReindexParams *params,
						  bool is_yb_table_rewrite, bool yb_copy_split_options);

/* Flag bits for reindex_relation(): */
#define REINDEX_REL_PROCESS_TOAST			0x01
#define REINDEX_REL_SUPPRESS_INDEX_USE		0x02
#define REINDEX_REL_CHECK_CONSTRAINTS		0x04
#define REINDEX_REL_FORCE_INDEXES_UNLOGGED	0x08
#define REINDEX_REL_FORCE_INDEXES_PERMANENT 0x10

extern bool reindex_relation(Oid relid, int flags, ReindexParams *params,
							 bool is_yb_table_rewrite,
							 bool yb_copy_split_options);

extern bool ReindexIsProcessingHeap(Oid heapOid);
extern bool ReindexIsProcessingIndex(Oid indexOid);

extern void ResetReindexState(int nestLevel);
extern Size EstimateReindexStateSpace(void);
extern void SerializeReindexState(Size maxsize, char *start_address);
extern void RestoreReindexState(void *reindexstate);

extern void IndexSetParentIndex(Relation idx, Oid parentOid);

/*
 * itemptr_encode - Encode ItemPointer as int64/int8
 *
 * This representation must produce values encoded as int64 that sort in the
 * same order as their corresponding original TID values would (using the
 * default int8 opclass to produce a result equivalent to the default TID
 * opclass).
 *
 * As noted in validate_index(), this can be significantly faster.
 */
static inline int64
itemptr_encode(ItemPointer itemptr)
{
	BlockNumber block = ItemPointerGetBlockNumber(itemptr);
	OffsetNumber offset = ItemPointerGetOffsetNumber(itemptr);
	int64		encoded;

	/*
	 * Use the 16 least significant bits for the offset.  32 adjacent bits are
	 * used for the block number.  Since remaining bits are unused, there
	 * cannot be negative encoded values (We assume a two's complement
	 * representation).
	 */
	encoded = ((uint64) block << 16) | (uint16) offset;

	return encoded;
}

/*
 * itemptr_decode - Decode int64/int8 representation back to ItemPointer
 */
static inline void
itemptr_decode(ItemPointer itemptr, int64 encoded)
{
	BlockNumber block = (BlockNumber) (encoded >> 16);
	OffsetNumber offset = (OffsetNumber) (encoded & 0xFFFF);

	ItemPointerSet(itemptr, block, offset);
}

/*
 * This should exactly match the IndexPermissions enum in
 * src/yb/common/common.proto.  See the definition there for details.
 */
typedef enum
{
	YB_INDEX_PERM_DELETE_ONLY = 0,
	YB_INDEX_PERM_WRITE_AND_DELETE = 2,
	YB_INDEX_PERM_DO_BACKFILL = 4,
	YB_INDEX_PERM_READ_WRITE_AND_DELETE = 6,
	YB_INDEX_PERM_WRITE_AND_DELETE_WHILE_REMOVING = 8,
	YB_INDEX_PERM_DELETE_ONLY_WHILE_REMOVING = 10,
	YB_INDEX_PERM_INDEX_UNUSED = 12,
} YBIndexPermissions;

extern bool YBRelationHasPrimaryKey(Relation rel);

#endif							/* INDEX_H */
