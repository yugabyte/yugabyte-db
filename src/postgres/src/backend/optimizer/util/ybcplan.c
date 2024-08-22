/*--------------------------------------------------------------------------------------------------
 *
 * ybcplan.c
 *	  Utilities for YugaByte scan.
 *
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/backend/executor/ybcplan.c
 *
 *--------------------------------------------------------------------------------------------------
 */


#include "postgres.h"

#include "optimizer/ybcplan.h"
#include "access/htup_details.h"
#include "access/relation.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "executor/ybcExpr.h"
#include "nodes/makefuncs.h"
#include "nodes/nodes.h"
#include "nodes/plannodes.h"
#include "nodes/print.h"
#include "utils/datum.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "utils/lsyscache.h"

/* YB includes. */
#include "yb/yql/pggate/ybc_pggate.h"
#include "catalog/catalog.h"
#include "catalog/pg_am_d.h"
#include "catalog/yb_catalog_version.h"
#include "pg_yb_utils.h"

/*
 * A mapping between columns and their position/index on the col_info_list.
 * The keys in the map are a zero-indexed version of the column's attr number.
 * The values are the position/index of the columns in the col_info_list of the
 * update metadata.
 * This structure allows us to sort the columns arbitrarily, while retaining the
 * ability to access them in constant time, given an attribute number.
 *
 * The number of elements in the map is equal to the number of columns that may
 * be potentially modified by the update query.
 */
typedef size_t *AttrToColIdxMap;

static void YbUpdateComputeIndexColumnReferences(
	const Relation rel, const Bitmapset *maybe_modified_cols,
	const AttrToColIdxMap map, YbUpdateAffectedEntities *affected_entities,
	YbSkippableEntities *skip_entities, int *nentities);

static void YbUpdateComputeForeignKeyColumnReferences(
	const Relation rel, const List *fkeylist,
	const Bitmapset *maybe_modified_cols, const AttrToColIdxMap map,
	YbUpdateAffectedEntities *affected_entities,
	YbSkippableEntities *skip_entities, bool is_referencing_rel,
	int *nentities);

/*
 * Check if statement can be implemented by a single request to the DocDB.
 *
 * An insert, update, or delete command makes one or more write requests to
 * the DocDB to apply the changes, and may also make read requests to find
 * the target row, its id, current values, etc. Complex expressions (e.g.
 * subqueries, stored functions) may also make requests to DocDB.
 *
 * Typically multiple requests require a transaction to maintain consistency.
 * However, if the command is about to make single write request, it is OK to
 * skip the transaction. The ModifyTable plan node makes one write request per
 * row it fetches from its subplans, therefore the key criteria of single row
 * modify is a single Result plan node in the ModifyTable's plans list.
 * Plain Result plan node produces exactly one row without making requests to
 * the DocDB, unless it has a subplan or complex expressions to evaluate.
 *
 * Full list of the conditions we check here:
 *  - there is only one target table;
 *  - there is no ON CONFLICT clause;
 *  - there is no init plan;
 *  - there is only one source plan, which is a simple form of Result;
 *  - all expressions in the Result's target list and in the returning list are
 *    simple, that means they do not need to access the DocDB.
 *
 * Additionally, during execution we will also check:
 *  - not in transaction block;
 *  - is a single-plan execution;
 *  - target table has no triggers to fire;
 *  - target table has no indexes to update.
 * And if all are true we will execute this op as a single-row transaction
 * rather than a distributed transaction.
 */
static bool ModifyTableIsSingleRowWrite(ModifyTable *modifyTable)
{
	/* Support INSERT, UPDATE, and DELETE. */
	if (modifyTable->operation != CMD_INSERT &&
		modifyTable->operation != CMD_UPDATE &&
		modifyTable->operation != CMD_DELETE)
		return false;

	/* Multi-relation implies multi-shard. */
	if (list_length(modifyTable->resultRelations) != 1)
		return false;

	/* ON CONFLICT clause may require another write request */
	if (modifyTable->onConflictAction != ONCONFLICT_NONE)
		return false;

	/* Init plan execution would require request(s) to DocDB */
	if (modifyTable->plan.initPlan != NIL)
		return false;

	Plan *plan = outerPlan(&modifyTable->plan);

	/*
	 * Only Result plan without a subplan produces single tuple without making
	 * DocDB requests
	 */
	if (!IsA(plan, Result) || outerPlan(plan))
		return false;

	/* Complex expressions in the target list may require DocDB requests */
	if (YbIsTransactionalExpr((Node *) plan->targetlist))
		return false;

	/* Same for the returning expressions */
	if (YbIsTransactionalExpr((Node *) modifyTable->returningLists))
		return false;

	/* If all our checks passed return true */
	return true;
}

bool YBCIsSingleRowModify(PlannedStmt *pstmt)
{
	if (pstmt->planTree && IsA(pstmt->planTree, ModifyTable))
	{
		ModifyTable *node = castNode(ModifyTable, pstmt->planTree);
		return ModifyTableIsSingleRowWrite(node);
	}

	return false;
}

/*
 * Returns true if this ModifyTable can be executed by a single RPC, without
 * an initial table scan fetching a target tuple.
 *
 * Right now, this is true iff:
 *  - it is UPDATE or DELETE command.
 *  - source data is a Result node (meaning we are skipping scan and thus
 *    are single row).
 */
bool YbCanSkipFetchingTargetTupleForModifyTable(ModifyTable *modifyTable)
{
	/* Support UPDATE and DELETE. */
	if (modifyTable->operation != CMD_UPDATE &&
		modifyTable->operation != CMD_DELETE)
		return false;

	/*
	 * Verify the single data source is a Result node and does not have outer plan.
	 * Note that Result node never has inner plan.
	 */
	if (!IsA(outerPlan(&modifyTable->plan), Result) ||
		outerPlan(outerPlan(&modifyTable->plan)))
		return false;

	return true;
}

/*
 * Returns true if provided Bitmapset of attribute numbers
 * matches the primary key attribute numbers of the relation.
 * Expects YBGetFirstLowInvalidAttributeNumber to be subtracted from attribute numbers.
 */
bool YBCAllPrimaryKeysProvided(Relation rel, Bitmapset *attrs)
{
	if (bms_is_empty(attrs))
	{
		/*
		 * If we don't explicitly check for empty attributes it is possible
		 * for this function to improperly return true. This is because in the
		 * case where a table does not have any primary key attributes we will
		 * use a hidden RowId column which is not exposed to the PG side, so
		 * both the YB primary key attributes and the input attributes would
		 * appear empty and would be equal, even though this is incorrect as
		 * the YB table has the hidden RowId primary key column.
		 */
		return false;
	}

	Bitmapset *primary_key_attrs = YBGetTablePrimaryKeyBms(rel);

	/* Verify the sets are the same. */
	return bms_equal(attrs, primary_key_attrs);
}

/*
 * is_index_only_attribute_nums
 *		Check if all column attribute numbers from the list are available from
 *		the index described by the indexinfo.
 *
 * 		It is necessary for the caller to validate that the colrefs are
 *		referring to the same index as the indexinfo.
 */
bool
is_index_only_attribute_nums(List *colrefs, IndexOptInfo *indexinfo,
							 bool bitmapindex)
{
	ListCell *lc;
	foreach (lc, colrefs)
	{
		bool found = false;
		YbExprColrefDesc *colref = castNode(YbExprColrefDesc, lfirst(lc));
		for (int i = 0; i < indexinfo->ncolumns; i++)
		{
			if (colref->attno == indexinfo->indexkeys[i])
			{
				/*
				 * If index key can not return, it does not have actual value
				 * to evaluate the expression.
				 */
				if (indexinfo->canreturn[i])
				{
					found = true;
					break;
				}
				/*
				 * Special case for LSM bitmap index scans: in Yugabyte, these
				 * indexes claim they cannot return if they are a primary index.
				 * Generally it is simpler for primary indexes to pushdown their
				 * conditions at the table level, rather than the index level.
				 * Primary indexes don't need to first request ybctids, and then
				 * request rows matching the ybctids.
				 *
				 * However, bitmap index scans benefit from pushing down
				 * conditions to the index (whether its primary or secondary),
				 * because they collect ybctids from both. If we can filter
				 * out more ybctids earlier, it reduces network costs and the
				 * size of the ybctid bitmap.
				 */
				else if (IsYugaByteEnabled() && bitmapindex &&
						 indexinfo->relam == LSM_AM_OID)
				{
					Relation index;
					index = RelationIdGetRelation(indexinfo->indexoid);
					bool is_primary = index->rd_index->indisprimary;
					RelationClose(index);

					if (is_primary)
					{
						found = true;
						break;
					}
				}

				return false;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

/*
 * extract_pushdown_clauses
 *	  Extract actual clauses from RestrictInfo list and distribute them
 * 	  between three groups:
 *	  - local_quals - conditions not eligible for pushdown. They are evaluated
 *	  on the Postgres side on the rows fetched from DocDB;
 *	  - rel_remote_quals - conditions to pushdown with the request to the main
 *	  scanned relation. In the case of sequential scan or index only scan
 *	  the DocDB table or DocDB index respectively is the main (and only)
 *	  scanned relation, so the function returns only two groups;
 *	  - idx_remote_quals - conditions to pushdown with the request to the
 *	  secondary (index) relation. Used with the index scan on a secondary
 *	  index, and caller must provide IndexOptInfo record for the index.
 *	  - rel_colrefs, idx_colrefs are columns referenced by respective
 *	  rel_remote_quals or idx_remote_quals.
 *	  The output parameters local_quals, rel_remote_quals, rel_colrefs must
 *	  point to valid lists. The output parameters idx_remote_quals and
 *	  idx_colrefs may be NULL if the indexinfo is NULL.
 */
void
extract_pushdown_clauses(List *restrictinfo_list,
						 IndexOptInfo *indexinfo,
						 bool bitmapindex,
						 List **local_quals,
						 List **rel_remote_quals,
						 List **rel_colrefs,
						 List **idx_remote_quals,
						 List **idx_colrefs)
{
	ListCell *lc;
	foreach(lc, restrictinfo_list)
	{
		RestrictInfo *ri = lfirst_node(RestrictInfo, lc);
		/* ignore pseudoconstants */
		if (ri->pseudoconstant)
			continue;

		if (ri->yb_pushable)
		{
			List *colrefs = NIL;
			bool pushable PG_USED_FOR_ASSERTS_ONLY;

			/*
			 * Find column references. It has already been determined that
			 * the expression is pushable.
			 */
			pushable = YbCanPushdownExpr(ri->clause, &colrefs);
			Assert(pushable);

			/*
			 * If there are both main and secondary (index) relations,
			 * determine one to pushdown the condition. It is more efficient
			 * to apply filter earlier, so prefer index, if it has all the
			 * necessary columns.
			 */
			if (indexinfo == NULL ||
				!is_index_only_attribute_nums(colrefs, indexinfo, bitmapindex))
			{
				*rel_colrefs = list_concat(*rel_colrefs, colrefs);
				*rel_remote_quals = lappend(*rel_remote_quals, ri->clause);
			}
			else
			{
				*idx_colrefs = list_concat(*idx_colrefs, colrefs);
				*idx_remote_quals = lappend(*idx_remote_quals, ri->clause);
			}
		}
		else
		{
			*local_quals = lappend(*local_quals, ri->clause);
		}
	}
}

static YbUpdateAffectedEntities *
YbInitUpdateAffectedEntities(Relation rel, int nfields, int maxentities)
{
	YbUpdateAffectedEntities *affected_entities = makeNode(YbUpdateAffectedEntities);

	affected_entities->col_info_list =
		palloc0(nfields * (sizeof(struct YbUpdateColInfo)));

	/*
	 * There is a possibility that not all the entities in maxentities will be
	 * affected by the update. However, we allocate the extra space here to
	 * avoid doing multiple allocations. We can also realloc to shrink the
	 * space later, but that probably isn't worth the hassle.
	 */
	affected_entities->entity_list =
		palloc0(maxentities * sizeof(struct YbUpdateEntity));

	return affected_entities;
}

YbSkippableEntities *
YbInitSkippableEntities(List *no_update_index_list)
{
	YbSkippableEntities *skip_entities = makeNode(YbSkippableEntities);
	skip_entities->index_list = no_update_index_list;
	return skip_entities;
}

void
YbCopySkippableEntities(YbSkippableEntities *dst,
						const YbSkippableEntities *src)
{
	if (src == NULL)
		return;

	dst->index_list = list_copy(src->index_list);
	dst->referencing_fkey_list = list_copy(src->referencing_fkey_list);
	dst->referenced_fkey_list = list_copy(src->referenced_fkey_list);
}

/* ----------------------------------------------------------------
 * YbAddEntityToSkipList
 *
 * Function to add the given entity to the appropriate skip list based on its
 * type.
 * ----------------------------------------------------------------
 */
void
YbAddEntityToSkipList(YbSkippableEntityType etype, Oid oid,
					  YbSkippableEntities *skip_entities)
{
	List **skip_list;
	switch (etype)
	{
		case SKIP_PRIMARY_KEY:
			/* Nothing to be done here */
			return;
		case SKIP_SECONDARY_INDEX:
			skip_list = &(skip_entities->index_list);
			break;
		case SKIP_REFERENCING_FKEY:
			skip_list = &(skip_entities->referencing_fkey_list);
			break;
		case SKIP_REFERENCED_FKEY:
			skip_list = &(skip_entities->referenced_fkey_list);
			break;
		default:
			elog(ERROR, "Unsupported update entity type: %d", etype);
			return;
	}

	*skip_list = lappend_oid(*skip_list, oid);
}

/* ----------------------------------------------------------------
 * YbClearSkippableEntities
 *
 * Function to clear the contents of YbSkippableEntities.
 * Note - This does not free the memory associated with YbSkippableEntities
 * struct itself.
 * ----------------------------------------------------------------
 */
void
YbClearSkippableEntities(YbSkippableEntities *skip_entities)
{
	if (!skip_entities)
		return;

	list_free(skip_entities->index_list);
	list_free(skip_entities->referencing_fkey_list);
	list_free(skip_entities->referenced_fkey_list);
}

static AttrToColIdxMap
YbInitAttrToIdxMap(const Relation rel, const Bitmapset *maybe_modified_cols,
				   size_t maxcols)
{
	/*
	 * Construct a map between the attnum of a column to its position in a list
	 * of potentially modified columns. Account for the fact that attnums are
	 * 1-indexed and are offset by 'YBFirstLowInvalidAttributeNumber' or
	 * 'FirstLowInvalidHeapAttributeNumber' in the bitmapset.
	 */
	AttrToColIdxMap attr_to_idx_map = palloc0(maxcols * (sizeof(size_t)));
	size_t idx = 0;
	int bms_idx = -1;

	while ((bms_idx = bms_next_member(maybe_modified_cols, bms_idx)) >= 0)
	{
		attr_to_idx_map[YBBmsIndexToAttnum(rel, bms_idx)] = idx;
		++idx;
	}

	return attr_to_idx_map;
}

static void
YbInitUpdateEntity(YbUpdateAffectedEntities *affected_entities, size_t idx,
				   Oid oid, YbSkippableEntityType etype)
{
	affected_entities->entity_list[idx].oid = oid;
	affected_entities->entity_list[idx].etype = etype;
}

/* ----------------------------------------------------------------
 * YbAddColumnReference
 *
 * Function to record the fact the given entity references the given column.
 * The bitmapset index of the column is converted into a zero-indexed number
 * (colattr - 1) so that the relationship can be recorded in a zero-indexed
 * array of columns.
 *
 * ----------------------------------------------------------------
 */
static void
YbAddColumnReference(const Relation rel,
					 YbUpdateAffectedEntities *affected_entities,
					 AttrToColIdxMap map, int bms_idx, size_t ref_entity_idx)
{
	size_t idx = map[YBBmsIndexToAttnum(rel, bms_idx)];
	struct YbUpdateColInfo *col_info = &affected_entities->col_info_list[idx];
	col_info->attnum = bms_idx;
	col_info->entity_refs =
		lappend_int(col_info->entity_refs, (int) ref_entity_idx);
}

/* ----------------------------------------------------------------
 * YbConsiderColumnAndEntityForUpdate
 *
 * Function to record the relationship between unique pairs of (entity, column)
 * in the context of an UPDATE query.
 *
 * ----------------------------------------------------------------
 */
static void
YbConsiderColumnAndEntityForUpdate(const Relation rel,
								   YbUpdateAffectedEntities *affected_entities,
								   AttrToColIdxMap map, AttrNumber colattr,
								   Oid oid, YbSkippableEntityType etype,
								   int *nentities)
{
	/*
	 * Check if the entity has already been inserted by a previous column
	 * that references the entity. If not, add the entity to the list.
	 */
	if (!(*nentities) || affected_entities->entity_list[*nentities - 1].oid != oid)
	{
		YbInitUpdateEntity(affected_entities, *nentities, oid, etype);
		++(*nentities);
	}

	YbAddColumnReference(rel, affected_entities, map, colattr, *nentities - 1);
}

static void
YbLogUpdateMatrix(const YbUpdateAffectedEntities *affected_entities)
{
	if (log_min_messages >= DEBUG1)
		return;

	const int nfields = YB_UPDATE_AFFECTED_ENTITIES_NUM_FIELDS(affected_entities);
	if (!nfields)
	{
		elog(DEBUG2, "No columns benefit from update optimization");
		return;
	}

	const int nentities = YB_UPDATE_AFFECTED_ENTITIES_NUM_ENTITIES(affected_entities);
	if (!nentities)
	{
		elog(DEBUG2, "No entities benefit from update optimization");
		return;
	}

	int length, prev_len, val_len, header_len;
	val_len = Max(nfields * 2, 10);
	header_len = Max(nfields * 3, 10);

	char *vals = (char *) palloc0(val_len * sizeof(char));
	char *headers = (char *) palloc0(header_len * sizeof(char));

	/* Print headers */
	headers[0] = '-';
	headers[1] = '\t';
	prev_len = 2; /* for '-\t' chars */
	for (int j = 0; j < nentities; j++)
	{
		AttrNumber attnum = affected_entities->col_info_list[j].attnum;
		/* Check if the column is populated */
		if (!attnum)
			break;
		length = snprintf(NULL, 0, "\t%d", attnum);
		snprintf(&headers[prev_len], length + 1, "\t%d", attnum);
		prev_len += length;
	}

	elog(DEBUG2, "Update matrix: rows represent OID of entities, columns represent attnum of cols");
	elog(DEBUG2, "%s", headers);

	/* Print body */
	for (int i = 0; i < nentities; i++)
	{
		for (int j = 0; j < nfields; j++)
		{
			/* Check if the column is populated */
			if (!affected_entities->col_info_list[j].attnum)
				break;
			vals[j * 2] =
				YbBitMatrixGetValue(&affected_entities->matrix, j, i) ? 'Y' : '-';
			vals[j * 2 + 1] = '\t';
		}

		elog(DEBUG2, "%d\t%s", affected_entities->entity_list[i].oid, vals);
	}

	pfree(headers);
	pfree(vals);
}

/* ----------------------------------------------------------------
 * YbGetMaybeModifiedCols - Pessimistically computes a bitmapset of columns
 * that the given query has the potential to modify.
 *
 * This function is invoked at planning time, so the only information used to
 * construct this list is a bitmapset of columns that we have permission to
 * update for the given query, and whether the relation has any before row
 * triggers.
 * ----------------------------------------------------------------
 */
static Bitmapset *
YbGetMaybeModifiedCols(Relation rel, Bitmapset *update_attrs)
{
	TupleDesc desc = RelationGetDescr(rel);

	/*
	 * A BEFORE ROW UPDATE trigger could have modified any of the columns in the
	 * tuple. In such a case, we do not eliminate any column from consideration
	 * in this stage.
	 * TODO(kramanathan) - Account for disabled triggers. This is not very hard
	 * to do, but it involves refactoring code in trigger.c so that it can be
	 * imported at planning time. Vanilla postgres does most of its trigger
	 * checks at execution time.
	 */
	if (rel->trigdesc && rel->trigdesc->trig_update_before_row)
	{
		Bitmapset *maybe_modified_cols = NULL;
		const AttrNumber offset = YBGetFirstLowInvalidAttributeNumber(rel);
		for (int idx = 0; idx < desc->natts; ++idx)
		{
			const FormData_pg_attribute *attdesc = TupleDescAttr(desc, idx);
			const AttrNumber bms_idx = attdesc->attnum - offset;
			maybe_modified_cols = bms_add_member(maybe_modified_cols, bms_idx);
		}

		return maybe_modified_cols;
	}

	/*
	 * Fetch a list of columns on which we have permission to do updates
	 * as the targetlist in the plan is not accurate.
	 */
	return bms_copy(update_attrs);
}

/*
 * YbQsortCompareColRefsList -- Comparator function to sort columns in
 * decreasing order of the number of entities referencing the column.
 */
static int
YbQsortCompareColRefsList(const void *a, const void *b)
{
	const struct YbUpdateColInfo *acol_info = (const struct YbUpdateColInfo *) a;
	const struct YbUpdateColInfo *bcol_info = (const struct YbUpdateColInfo *) b;

	if (acol_info == NULL || acol_info->entity_refs == NULL)
		return 1;

	if (bcol_info == NULL || bcol_info->entity_refs == NULL)
		return -1;

	return acol_info->entity_refs->length > bcol_info->entity_refs->length ? -1 : 1;
}

static void
YbPopulateUpdateMatrix(YbUpdateAffectedEntities *affected_entities,
					   int nentities,
					   int nfields)
{
	YbInitBitMatrix(&affected_entities->matrix, nfields, nentities);

	ListCell *refCell;
	for (int field_idx = 0; field_idx < nfields; field_idx++)
	{
		foreach (refCell, affected_entities->col_info_list[field_idx].entity_refs)
		{
			YbBitMatrixSetValue(&affected_entities->matrix, field_idx,
								lfirst_int(refCell), true /* value */);
		}
	}

	YbLogUpdateMatrix(affected_entities);
}

/* ----------------------------------------------------------------
 * YbComputeAffectedEntitiesForRelation
 *
 * Computes a list of entities (indexes, foreign keys, etc.) that may be
 * affected by the given ModifyTable (update) plan. This information is used
 * during the execution of the statement to determine if bookkeeping operations
 * on a subset of these entities can be skipped, thus reducing the amount of
 * work done by the system. This function is invoked at planning time so that
 * the results can be cached and reused in prepared statements.
 *
 * The list of entities examined are those that can trigger a request to the
 * storage layer. Scenarios where this may have happen include the following:
 * - Primary Key updates
 * - Secondary Index updates
 * - Foreign Key Constraints (both as a 'referencing' relation and as a
 *   'referenced' relation)
 * - Uniqueness Constraints (implemented as secondary indexes)
 *
 * Other types of constraints do not benefit from this optimization because
 * they are either:
 * - Not pushed down and thus do not need this optimization, such as
 *   CHECK constraints (OR)
 * - Not supported, such as exclusion contraints (#3944) and constraint
 *   triggers (#1709). Re-evaluate when implemented.
 *
 * ----------------------------------------------------------------
 */
YbUpdateAffectedEntities *
YbComputeAffectedEntitiesForRelation(ModifyTable *modifyTable,
									 const Relation rel,
									 Bitmapset *update_attrs)
{
	if (!YbIsUpdateOptimizationEnabled())
		return NULL;

	/* Fetch a list of candidate entities that may be impacted by the update. */
	List *fkeylist = copyObject(RelationGetFKeyList(rel));
	List *fkeyreflist = YbRelationGetFKeyReferencedByList(rel);

	const int maxentities = list_length(rel->rd_indexlist) +
							(rel->rd_pkindex != InvalidOid ? 1 : 0) +
							(fkeylist ? fkeylist->length : 0) +
							(fkeyreflist ? fkeyreflist->length : 0);

	/* If there are no entities that benefit from the optimization, skip it */
	if (!maxentities)
		return NULL;

	Bitmapset *maybe_modified_cols = YbGetMaybeModifiedCols(rel, update_attrs);
	const int nfields = bms_num_members(maybe_modified_cols);
	YbUpdateAffectedEntities *affected_entities =
		YbInitUpdateAffectedEntities(rel, nfields, maxentities);

	TupleDesc desc = RelationGetDescr(rel);
	AttrToColIdxMap map =
		YbInitAttrToIdxMap(rel, maybe_modified_cols, desc->natts + 1);

	int nentities = 0;
	/*
	 * Compute a list of indexes (primary or secondary) that potentially need to
	 * be updated.
	 */
	YbUpdateComputeIndexColumnReferences(rel, maybe_modified_cols, map,
										 affected_entities, 
										 modifyTable->yb_skip_entities,
										 &nentities);

	/* Compute a list of affected 'referencing' foreign key constraints */
	YbUpdateComputeForeignKeyColumnReferences(
		rel, fkeylist, maybe_modified_cols, map, affected_entities,
		modifyTable->yb_skip_entities,	true /* is_referencing_rel */,
		&nentities);

	/* Compute a list of affected 'referenced' foreign key constraints */
	YbUpdateComputeForeignKeyColumnReferences(
		rel, fkeyreflist, maybe_modified_cols, map, affected_entities,
		modifyTable->yb_skip_entities, false /* is_referencing_rel */,
		&nentities);

	Assert(nentities <= maxentities);

	/*
	 * Sort the columns in descending order of the number of entities that
	 * reference them.
	 */
	qsort(affected_entities->col_info_list /* what to sort? */,
		  nfields /* how many? */,
		  sizeof(struct YbUpdateColInfo) /* how is it stored? */,
		  YbQsortCompareColRefsList) /* how to sort? */; 

	/* Finally create the update matrix */
	YbPopulateUpdateMatrix(affected_entities, nentities, nfields);

	pfree(map);
	bms_free(maybe_modified_cols);
	list_free(fkeyreflist);
	list_free(fkeylist);

	return affected_entities;
}

/* ----------------------------------------------------------------
 * YbUpdateComputeIndexColumnReferences
 *
 * Note - At first glance, this function may seem rather similar to
 * 'has_applicable_indices' in createplan.c. However, the two functions serve
 * orthogonal purposes:
 * has_applicable_indices - Primary purpose is to determine if the given update
 * query can be executed as a non-distributed transaction. When the columns
 * being updated are not part of any index, then the index need not be updated.
 * If no index needs to be updated, and all updated rows in the main table are
 * part of the same shard/tablet, then the query can be executed as a
 * non-distributed transaction (assuming all other conditions are satisfied).
 * A side effect of this computation is that we now know which indexes are not
 * updated as a part of the query. This is the 'no_update_index_list' which is
 * now a part of YbSkippableEntities.
 *
 * YbUpdateComputeIndexColumnReferences - This function is executed only when
 * we know that the query will be executed as a distributed transaction. For
 * each index that is NOT a part of the 'no_update_index_list', this function
 * computes which columns of the index maybe potentially updated.
 * If all the columns of an index are identical in the old and new tuples, the
 * update of the index can be skipped. This function does not distinguish
 * between the key and non-key columns of an index.
 * ----------------------------------------------------------------
 */
static void
YbUpdateComputeIndexColumnReferences(const Relation rel,
									 const Bitmapset *maybe_modified_cols,
									 const AttrToColIdxMap map,
									 YbUpdateAffectedEntities *affected_entities,
									 YbSkippableEntities *skip_entities,
									 int *nentities)
{
	/*
	 * Add the primary key to the head of the entity list so that it gets
	 * evaluated first.
	 */
	if (RelationGetPrimaryKeyIndex(rel) != InvalidOid)
	{
		Bitmapset *pkbms =
			RelationGetIndexAttrBitmap(rel, INDEX_ATTR_BITMAP_PRIMARY_KEY);
		bool pk_maybe_modified = false;
		int attnum = -1;
		while ((attnum = bms_next_member(pkbms, attnum)) >= 0)
		{
			if (bms_is_member(attnum, maybe_modified_cols))
			{
				YbConsiderColumnAndEntityForUpdate(rel, affected_entities, map,
												   attnum, rel->rd_pkindex,
												   SKIP_PRIMARY_KEY, nentities);
				pk_maybe_modified = true;
			}
		}

		if (!pk_maybe_modified)
			YbAddEntityToSkipList(SKIP_PRIMARY_KEY, rel->rd_pkindex,
								  skip_entities);
	}

	ListCell *lc = NULL;
	foreach (lc, rel->rd_indexlist)
	{
		Oid index_oid = lfirst_oid(lc);

		/* Avoid checking indexes that we already know to be not updated. */
		if (list_member_oid(skip_entities->index_list, index_oid))
			continue;

		/* Avoid adding the primary key again. */
		if (index_oid == rel->rd_pkindex)
			continue;

		Relation indexDesc = RelationIdGetRelation(index_oid);
		YbSkippableEntityType etype = SKIP_SECONDARY_INDEX;
		bool index_maybe_modified = false;

		const AttrNumber offset = YBGetFirstLowInvalidAttributeNumber(rel);
		for (int j = 0; j < indexDesc->rd_index->indnatts; j++)
		{
			const AttrNumber bms_idx =
				indexDesc->rd_index->indkey.values[j] - offset;
			if (bms_is_member(bms_idx, maybe_modified_cols))
			{
				YbConsiderColumnAndEntityForUpdate(rel, affected_entities, map,
												   bms_idx, index_oid, etype,
												   nentities);
				index_maybe_modified = true;
			}
		}

		/*
		 * If the index contains a predicate or an expression, we need to walk
		 * over the respective expression trees. It is possible that the
		 * expression or predicate isn't applicable to this update, but the cost
		 * of validating that will be prohibitively expensive.
		 */
		Bitmapset *extraattrs = NULL;
		if (indexDesc->rd_indpred)
		{
			YbComputeIndexExprOrPredicateAttrs(
				&extraattrs, indexDesc, Anum_pg_index_indpred, offset);
		}

		if (indexDesc->rd_indexprs)
		{
			YbComputeIndexExprOrPredicateAttrs(
				&extraattrs, indexDesc, Anum_pg_index_indexprs, offset);
		}

		if (extraattrs)
		{
			int attnum = -1;
			while ((attnum = bms_next_member(extraattrs, attnum)) >= 0)
			{
				if (bms_is_member(attnum, maybe_modified_cols))
				{
					YbConsiderColumnAndEntityForUpdate(rel, affected_entities,
													   map, attnum, index_oid,
													   etype, nentities);
					index_maybe_modified = true;
				}
			}
		}

		if (!index_maybe_modified)
			YbAddEntityToSkipList(etype, index_oid, skip_entities);

		RelationClose(indexDesc);
	}
}

static void
YbUpdateComputeForeignKeyColumnReferences(const Relation rel,
										  const List *fkeylist,
										  const Bitmapset *maybe_modified_cols,
										  const AttrToColIdxMap map,
										  YbUpdateAffectedEntities *affected_entities,
										  YbSkippableEntities *skip_entities,
										  bool is_referencing_rel,
										  int *nentities)
{
	YbSkippableEntityType etype =
		is_referencing_rel ? SKIP_REFERENCING_FKEY : SKIP_REFERENCED_FKEY;
	const AttrNumber offset = YBGetFirstLowInvalidAttributeNumber(rel);
	ListCell *cell;
	foreach (cell, fkeylist)
	{
		ForeignKeyCacheInfo *fkey = (ForeignKeyCacheInfo *) lfirst(cell);
		bool fkey_maybe_modified = false;
		for (int idx = 0; idx < fkey->nkeys; idx++)
		{
			const int bms_idx =
				(is_referencing_rel ? fkey->conkey[idx] : fkey->confkey[idx]) - offset;
			if (bms_is_member(bms_idx, maybe_modified_cols))
			{
				YbConsiderColumnAndEntityForUpdate(rel, affected_entities, map,
												   bms_idx, fkey->conoid, etype,
												   nentities);
				fkey_maybe_modified = true;
			}
		}

		if (!fkey_maybe_modified)
			YbAddEntityToSkipList(etype, fkey->conoid, skip_entities);
	}
}
