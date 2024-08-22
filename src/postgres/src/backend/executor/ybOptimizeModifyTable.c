/*-------------------------------------------------------------------------
 *
 * ybOptimizeModifyTable.c
 *	  Support routines for optimizing modify table operations in YugabyteDB
 *	  relations.
 *
 * Copyright (c) YugabyteDB, Inc.
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
 *
 * IDENTIFICATION
 *	  src/backend/executor/ybOptimizeModifyTable.c
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "catalog/catalog.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_trigger.h"
#include "executor/executor.h"
#include "executor/ybOptimizeModifyTable.h"
#include "nodes/ybbitmatrix.h"
#include "optimizer/ybcplan.h"
#include "parser/parsetree.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/datum.h"
#include "utils/syscache.h"

static inline int
YbGetNextEntityFromBitMatrix(const YbBitMatrix *matrix, int field_idx,
							 int prev_entity_idx)
{
	return YbBitMatrixNextMemberInRow(matrix, field_idx, prev_entity_idx);
}

static inline int
YbGetNextFieldFromBitMatrix(const YbBitMatrix *matrix, int entity_idx,
							int prev_field_idx)
{
	return YbBitMatrixNextMemberInColumn(matrix, entity_idx, prev_field_idx);
}

static void
YbLogOptimizationSummary(const struct YbUpdateEntity *entity_list,
						 Bitmapset *modified_entities, int nentities)
{
	if (log_min_messages >= DEBUG1)
		return;

	for (int i = 0; i < nentities; i++)
	{
		if (bms_is_member(i, modified_entities))
			elog(DEBUG2, "Index/constraint with oid %d requires an update",
				 entity_list[i].oid);
		else
			elog(DEBUG2, "Index/constraint with oid %d is unmodified",
				 entity_list[i].oid);
	}
}

static void
YbLogColumnList(Relation rel, const Bitmapset *cols, const char *message)
{
	const int ncols = bms_num_members(cols);
	if (!ncols)
	{
		elog(DEBUG2, "No cols in category: %s", message);
		return;
	}

	char *col_str = (char *) palloc0(10 * ncols * sizeof(char));

	int length, prev_len, col;
	const AttrNumber offset = YBGetFirstLowInvalidAttributeNumber(rel);
	col = -1;
	prev_len = 0;
	while ((col = bms_next_member(cols, col)) >= 0)
	{
		AttrNumber attnum = col + offset;
		length = snprintf(NULL, 0, "%d (%d) ", attnum, col);
		snprintf(&col_str[prev_len], length + 1, "%d (%d) ", attnum, col);
		prev_len += length;
	}

	elog(DEBUG2, "Relation: %u\t%s: %s", rel->rd_id, message, col_str);
}

static void
YbLogInspectedColumns(Relation rel, const Bitmapset *updated_cols,
					  const Bitmapset *modified_cols,
					  const Bitmapset *unmodified_cols)
{
	if (log_min_messages >= DEBUG1)
		return;

	YbLogColumnList(rel, modified_cols,
					"Columns that are inspected and modified");

	YbLogColumnList(rel, unmodified_cols,
					"Columns that are inspected and unmodified");

	YbLogColumnList(rel, updated_cols, "Columns that are marked for update");
}

static bool
YbIsColumnComparisonAllowed(const Bitmapset *modified_cols,
							const Bitmapset *unmodified_cols)
{
	return (
		(bms_num_members(modified_cols) + bms_num_members(unmodified_cols)) <=
		yb_update_optimization_options.num_cols_to_compare);
}

/* ----------------------------------------------------------------
 * YBEqualDatums
 *
 * Function compares values of lhs and rhs datums with respect to value type
 * and collation.
 *
 * Returns true in case value of lhs and rhs datums match.
 * ----------------------------------------------------------------
 */
static bool
YBEqualDatums(Datum lhs, Datum rhs, Oid atttypid, Oid collation)
{
	LOCAL_FCINFO(locfcinfo, 2);
	TypeCacheEntry *typentry =
		lookup_type_cache(atttypid, TYPECACHE_CMP_PROC_FINFO);
	if (!OidIsValid(typentry->cmp_proc_finfo.fn_oid))
		ereport(ERROR, (errcode(ERRCODE_UNDEFINED_FUNCTION),
						errmsg("could not identify a comparison function for "
							   "type %s",
							   format_type_be(typentry->type_id))));

	/* To ensure that there is an upper bound on the size of data compared */
	const int lhslength = datumGetSize(lhs, typentry->typbyval, typentry->typlen);
	const int rhslength = datumGetSize(rhs, typentry->typbyval, typentry->typlen);

	if (lhslength != rhslength ||
		lhslength > yb_update_optimization_options.max_cols_size_to_compare)
		return false;

	InitFunctionCallInfoData(*locfcinfo, &typentry->cmp_proc_finfo, 2, collation,
							 NULL, NULL);
	locfcinfo->args[0].value = lhs;
	locfcinfo->args[0].isnull = false;
	locfcinfo->args[1].value = rhs;
	locfcinfo->args[1].isnull = false;
	return DatumGetInt32(FunctionCallInvoke(locfcinfo)) == 0;
}

/* ----------------------------------------------------------------------------
 * YBIsColumnModified
 *
 * Function to figure out if a given column identified by 'attnum'
 * belonging to a relation 'rel' is modified as part of an update operation.
 * This function assumes that both the old and new tuples are populated.
 * Note - This function returns false if both the old and the new values are
 * NULL. This is because changing a value from NULL to NULL does not modify the
 * underlying datum.
 * ----------------------------------------------------------------------------
 */
static bool
YBIsColumnModified(Relation rel, HeapTuple oldtuple, HeapTuple newtuple,
				   const FormData_pg_attribute *attdesc)
{
	const AttrNumber attnum = attdesc->attnum;

	/* Skip virtual (system) and dropped columns */
	if (!IsRealYBColumn(rel, attnum))
		return false;

	bool old_is_null = false;
	bool new_is_null = false;
	TupleDesc relTupdesc = RelationGetDescr(rel);
	Datum old_value = heap_getattr(oldtuple, attnum, relTupdesc, &old_is_null);
	Datum new_value = heap_getattr(newtuple, attnum, relTupdesc, &new_is_null);

	return (
		(old_is_null != new_is_null) ||
		(!old_is_null && !YBEqualDatums(old_value, new_value, attdesc->atttypid,
										attdesc->attcollation)));
}

/* ----------------------------------------------------------------
 * YBComputeExtraUpdatedCols
 *
 * Function to compute a list of columns (that are not in that query's
 * targetlist) that are modified by before-row triggers. It returns a set of
 * columns that are modified and a set of columns that are unmodified.
 * If this function is invoked with update optimizations turned off, it compares
 * all the columns that are not in updated_cols.
 * When update optimizations are turned on, it compares only those columns that
 * have column-specific after-row triggers defined on them. Any other column
 * that has been not been compared is marked as modified.
 * ----------------------------------------------------------------
 */
static void
YBComputeExtraUpdatedCols(Relation rel, HeapTuple oldtuple, HeapTuple newtuple,
						  Bitmapset *updated_cols, Bitmapset **modified_cols,
						  Bitmapset **unmodified_cols,
						  bool is_onconflict_update)
{
	Bitmapset *trig_cond_cols = NULL;
	Bitmapset *skip_cols = updated_cols;
	/*
	* If the optimization is not enabled, we only need to compare the
	* columns that are not in the targetlist of the query.
	*/
	if (YbIsUpdateOptimizationEnabled())
	{
		/*
		 * If the optimization is enabled, we have already compared all the
		 * columns that might save us a round trip. In most cases, we can simply
		 * assume that all of the uncompared columns are indeed modified.
		 * However, if the relation has column-specific after-row triggers, we
		 * need to determine if any of the columns on which the conditional
		 * triggers are dependent, have been changed by the before-row triggers
		 * that have already fired. The deferrability of the after-row triggers
		 * is irrelevant in this context.
		 *
		 * Note - trigdesc cannot be NULL because it has been established that
		 * the relation has before-row triggers.
		 * TODO(kramanathan) - Reevaluate this logic when we support generated
		 * columns in PG15 (#23350).
		 */
		for (int idx = 0; idx < rel->trigdesc->numtriggers; ++idx)
		{
			Trigger *trigger = &rel->trigdesc->triggers[idx];
			if (!(trigger->tgnattr && (trigger->tgtype & TRIGGER_TYPE_AFTER) &&
				((trigger->tgtype & TRIGGER_TYPE_UPDATE) ||
				(is_onconflict_update && (trigger->tgtype & TRIGGER_TYPE_INSERT)))))
				continue;

			/*
			 * We are only interested in UPDATE and INSERT triggers in case of
			 * ON-CONFLICT DO UPDATE clauses.
			 */
			for (int col_idx = 0; col_idx < trigger->tgnattr; col_idx++)
			{
				const int bms_idx = trigger->tgattr[col_idx] -
					YBGetFirstLowInvalidAttributeNumber(rel);
				trig_cond_cols = bms_add_member(trig_cond_cols, bms_idx);
			}
		}

		/* Subtract the cols that have already been compared. */
		Bitmapset *compared_cols = bms_union(*unmodified_cols, *modified_cols);
		trig_cond_cols = bms_difference(trig_cond_cols, compared_cols);
		skip_cols = bms_union(skip_cols, compared_cols);
	}
	TupleDesc tupleDesc = RelationGetDescr(rel);
	const AttrNumber offset = YBGetFirstLowInvalidAttributeNumber(rel);

	for (int idx = 0; idx < tupleDesc->natts; ++idx)
	{
		const FormData_pg_attribute *attdesc = TupleDescAttr(tupleDesc, idx);
		const AttrNumber bms_idx = attdesc->attnum - offset;
		if (bms_is_member(bms_idx, skip_cols) &&
			!bms_is_member(bms_idx, trig_cond_cols))
			continue;

		if (YBIsColumnModified(rel, oldtuple, newtuple, attdesc))
			*modified_cols = bms_add_member(*modified_cols, bms_idx);
		else
			*unmodified_cols = bms_add_member(*unmodified_cols, bms_idx);
	}
}

static void
YbUpdateHandleModifiedField(YbBitMatrix *matrix, Bitmapset **modified_entities,
							int entity_idx, int field_idx,
							Bitmapset **modified_cols, AttrNumber bms_idx)
{
	/*
	 * There is no need to check entities < row_idx as the algorithm guarantees
	 * that this entity is the first one that references this column.
	 */
	int local_entity_idx = entity_idx - 1;
	while ((local_entity_idx = YbGetNextEntityFromBitMatrix(
		matrix, field_idx, local_entity_idx)) >= 0)
		*modified_entities = bms_add_member(*modified_entities, entity_idx);

	*modified_cols = bms_add_member(*modified_cols, bms_idx);
}

static void
YbUpdateHandleUnmodifiedField(YbBitMatrix *matrix, int field_idx,
							  Bitmapset **unmodified_cols, AttrNumber bms_idx)
{
	YbBitMatrixSetRow(matrix, field_idx, false);
	*unmodified_cols = bms_add_member(*unmodified_cols, bms_idx);
}

static void
YbUpdateHandleUnmodifiedEntity(YbUpdateAffectedEntities *affected_entities,
							   int entity_idx,
							   YbSkippableEntities *skip_entities)
{
	struct YbUpdateEntity *entity = &affected_entities->entity_list[entity_idx];
	YbAddEntityToSkipList(entity->etype, entity->oid, skip_entities);
}

/* ----------------------------------------------------------------
 * YbComputeModifiedEntities
 *
 * This function computes the entities (indexes, constraints) that need updates
 * as part of the ModifyTable query, returning a collection of skippable
 * entities.
 *
 * To compute updated entities, the old (current) and new (updated) values of
 * columns (referenced by the entity) are compared. This comparison can be an
 * expensive operation, especially when the column is large or when the column
 * is of a composite/user defined type. To minimize the number of comparisons,
 * this function prioritizes those columns whose modification could cause
 * extra round trips to the storage layer in the form of index updates and
 * constraint checks.
 * This optimization is upped bounded in terms of the number of columns
 * (GUC: yb_update_num_cols_to_compare) that are compared and the maximum size
 * (GUC: yb_update_max_cols_size_to_compare) of a column that is compared.
 * ----------------------------------------------------------------
 */
static YbSkippableEntities *
YbComputeModifiedEntities(ResultRelInfo *resultRelInfo, HeapTuple oldtuple,
						  HeapTuple newtuple, Bitmapset **modified_cols,
						  Bitmapset **unmodified_cols,
						  YbUpdateAffectedEntities *affected_entities,
						  YbSkippableEntities *skip_entities)
{
	/*
	 * If the relation is simple ie. has no primary key, indexes, constraints or
	 * triggers, computation of affected entities will be skipped.
	 */
	if (affected_entities == NULL)
		return NULL;

	/*
	 * Clone the update matrix to create a working copy for this tuple. We would
	 * like to preserve a clean copy for subsequent tuples in this query/plan.
	 */
	YbBitMatrix matrix;
	YbCopyBitMatrix(&matrix, &affected_entities->matrix);

	Relation rel = resultRelInfo->ri_RelationDesc;
	TupleDesc relTupdesc = RelationGetDescr(rel);
	const int nentities = YB_UPDATE_AFFECTED_ENTITIES_NUM_ENTITIES(affected_entities);

	Bitmapset *modified_entities =
		bms_del_member(bms_add_member(NULL, nentities), nentities);

	for (int entity_idx = 0; entity_idx < nentities; entity_idx++)
	{
		/*
		 * We need to make a decision for each entity that may be affected,
		 * adding the entity to a skip-list if all referenced columns are
		 * unmodified or proceeding with the relevant action if any of the
		 * columns are indeed modified.
		 */
		struct YbUpdateEntity *entity = &affected_entities->entity_list[entity_idx];

		/*
		 * If we already know that the entity is modified, skip checking its
		 * columns.
		 */
		bool is_modified = bms_is_member(entity_idx, modified_entities);

		int field_idx = -1;
		while (!is_modified && (field_idx = YbGetNextFieldFromBitMatrix(
									&matrix, entity_idx, field_idx)) >= 0)
		{
			AttrNumber attnum = affected_entities->col_info_list[field_idx].attnum;
			const int idx = YBBmsIndexToAttnum(rel, attnum);
			const FormData_pg_attribute *attdesc =
				TupleDescAttr(relTupdesc, idx);
			if (!YbIsColumnComparisonAllowed(*modified_cols, *unmodified_cols) ||
				YBIsColumnModified(rel, oldtuple, newtuple, attdesc))
			{
				/*
				 * If we have already exceeded the max number of columns
				 * that we are allowed to compare, assume that the column
				 * is changing in value.
				 */
				YbUpdateHandleModifiedField(&matrix, &modified_entities,
											entity_idx, field_idx,
											modified_cols, attnum);
				break;
			}
			else
				YbUpdateHandleUnmodifiedField(&matrix, field_idx,
											  unmodified_cols, attnum);
		}

		if (bms_is_member(entity_idx, modified_entities))
		{
			/* Mark the primary key as updated */
			if (entity->oid == rel->rd_pkindex)
			{
				/*
				 * In case the primary key is updated, the entire row must be
				 * deleted and re-inserted. The no update index list is not
				 * useful in such cases.
				 */
				skip_entities->index_list = NIL;
				break;
			}
		}
		else
			YbUpdateHandleUnmodifiedEntity(
				affected_entities, entity_idx, skip_entities);
	}

	YbLogOptimizationSummary(affected_entities->entity_list, modified_entities,
							 nentities);
	return skip_entities;
}

/* ----------------------------------------------------------------
 * YbComputeModifiedColumnsAndSkippableEntities
 *
 * This function computes a list of columns that are modified by the query and
 * a list of entities (indexes, constraints) whose bookkeeping updates can be
 * skipped as a result of unmodified columns.
 * A list of modified columns (updated_cols) is required:
 * - To construct the payload of updated values to be send to the storage layer.
 * - To determine if column-specific after-row triggers are elgible to fire.
 * ----------------------------------------------------------------
 */
void
YbComputeModifiedColumnsAndSkippableEntities(
	ModifyTable *plan, ResultRelInfo *resultRelInfo, EState *estate,
	HeapTuple oldtuple, HeapTuple newtuple, Bitmapset **updated_cols,
	bool beforeRowUpdateTriggerFired)
{
	/*
	 * InvalidAttrNumber indicates that the whole row is to be changed. This
	 * happens when the primary key is modified. Performing any optimization is
	 * redundant in such a case.
	 */
	if (bms_is_member(InvalidAttrNumber, *updated_cols))
		return;

	/*
	 * There are scenarios where it is possible to update a tuple without
	 * reading in its current state (oldtuple) -- the update query fully
	 * specifies the primary key, the columns being updated do not have indexes
	 * or constraints on them, the values to which the columns are being
	 * updated are absolute and do not depend on the current values, and there
	 * are no "before-row" triggers. Modified columns cannot be computed in this
	 * scenario as there is nothing (oltuple) to compare against, and there is
	 * not much to be saved in doing this anyway.
	 */
	if (oldtuple == NULL)
		return;

	Relation rel = resultRelInfo->ri_RelationDesc;

	/*
	 * Maintain two bitmapsets - one to track columns that have indeed been
	 * modified (after comparison of their old and new values) and another to
	 * track columns that remain unmodified.
	 * The intersection of these two bitmapsets is always a null set.
	 * Further, there may be columns that we chose to not compare because:
	 * - it does not affect any entity
	 * - we already know that all the entities it affects are indeed modified
	 * In other words, the union of unmodified_cols and modified_cols may NOT be
	 * equal to updated_cols.
	 * Additionally, the union of unmodified_cols and modified_cols may include
	 * columns that are not in updated_cols.
	 */
	Bitmapset *unmodified_cols = NULL;
	Bitmapset *modified_cols = NULL;

	if (YbIsUpdateOptimizationEnabled())
	{
		YbComputeModifiedEntities(resultRelInfo, oldtuple, newtuple,
								  &modified_cols, &unmodified_cols,
								  plan->yb_update_affected_entities,
								  &estate->yb_skip_entities);
	}

	if (beforeRowUpdateTriggerFired)
	{
		YBComputeExtraUpdatedCols(rel, oldtuple, newtuple, *updated_cols,
								  &modified_cols, &unmodified_cols,
								  (plan->operation == CMD_INSERT &&
								   plan->onConflictAction == ONCONFLICT_UPDATE));
	}

	*updated_cols = bms_del_members(*updated_cols, unmodified_cols);
	*updated_cols = bms_add_members(*updated_cols, modified_cols);

	YbLogInspectedColumns(rel, *updated_cols, modified_cols, unmodified_cols);

	bms_free(unmodified_cols);
	bms_free(modified_cols);
}

bool
YbIsPrimaryKeyUpdated(Relation rel, const Bitmapset *updated_cols)
{
	return bms_overlap(YBGetTablePrimaryKeyBms(rel), updated_cols);
}
