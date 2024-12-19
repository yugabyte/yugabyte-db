/*-------------------------------------------------------------------------
 *
 * execPartition.c
 *	  Support routines for partitioning.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/executor/execPartition.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/table.h"
#include "access/tableam.h"
#include "catalog/partition.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_type.h"
#include "executor/execPartition.h"
#include "executor/executor.h"
#include "executor/nodeModifyTable.h"
#include "foreign/fdwapi.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "partitioning/partbounds.h"
#include "partitioning/partdesc.h"
#include "partitioning/partprune.h"
#include "rewrite/rewriteManip.h"
#include "utils/acl.h"
#include "utils/lsyscache.h"
#include "utils/partcache.h"
#include "utils/rls.h"
#include "utils/ruleutils.h"

/* YB includes. */
#include "pg_yb_utils.h"


/*-----------------------
 * PartitionTupleRouting - Encapsulates all information required to
 * route a tuple inserted into a partitioned table to one of its leaf
 * partitions.
 *
 * partition_root
 *		The partitioned table that's the target of the command.
 *
 * partition_dispatch_info
 *		Array of 'max_dispatch' elements containing a pointer to a
 *		PartitionDispatch object for every partitioned table touched by tuple
 *		routing.  The entry for the target partitioned table is *always*
 *		present in the 0th element of this array.  See comment for
 *		PartitionDispatchData->indexes for details on how this array is
 *		indexed.
 *
 * nonleaf_partitions
 *		Array of 'max_dispatch' elements containing pointers to fake
 *		ResultRelInfo objects for nonleaf partitions, useful for checking
 *		the partition constraint.
 *
 * num_dispatch
 *		The current number of items stored in the 'partition_dispatch_info'
 *		array.  Also serves as the index of the next free array element for
 *		new PartitionDispatch objects that need to be stored.
 *
 * max_dispatch
 *		The current allocated size of the 'partition_dispatch_info' array.
 *
 * partitions
 *		Array of 'max_partitions' elements containing a pointer to a
 *		ResultRelInfo for every leaf partition touched by tuple routing.
 *		Some of these are pointers to ResultRelInfos which are borrowed out of
 *		the owning ModifyTableState node.  The remainder have been built
 *		especially for tuple routing.  See comment for
 *		PartitionDispatchData->indexes for details on how this array is
 *		indexed.
 *
 * is_borrowed_rel
 *		Array of 'max_partitions' booleans recording whether a given entry
 *		in 'partitions' is a ResultRelInfo pointer borrowed from the owning
 *		ModifyTableState node, rather than being built here.
 *
 * num_partitions
 *		The current number of items stored in the 'partitions' array.  Also
 *		serves as the index of the next free array element for new
 *		ResultRelInfo objects that need to be stored.
 *
 * max_partitions
 *		The current allocated size of the 'partitions' array.
 *
 * memcxt
 *		Memory context used to allocate subsidiary structs.
 *-----------------------
 */
struct PartitionTupleRouting
{
	Relation	partition_root;
	PartitionDispatch *partition_dispatch_info;
	ResultRelInfo **nonleaf_partitions;
	int			num_dispatch;
	int			max_dispatch;
	ResultRelInfo **partitions;
	bool	   *is_borrowed_rel;
	int			num_partitions;
	int			max_partitions;
	MemoryContext memcxt;
};

/*-----------------------
 * PartitionDispatch - information about one partitioned table in a partition
 * hierarchy required to route a tuple to any of its partitions.  A
 * PartitionDispatch is always encapsulated inside a PartitionTupleRouting
 * struct and stored inside its 'partition_dispatch_info' array.
 *
 * reldesc
 *		Relation descriptor of the table
 *
 * key
 *		Partition key information of the table
 *
 * keystate
 *		Execution state required for expressions in the partition key
 *
 * partdesc
 *		Partition descriptor of the table
 *
 * tupslot
 *		A standalone TupleTableSlot initialized with this table's tuple
 *		descriptor, or NULL if no tuple conversion between the parent is
 *		required.
 *
 * tupmap
 *		TupleConversionMap to convert from the parent's rowtype to this table's
 *		rowtype  (when extracting the partition key of a tuple just before
 *		routing it through this table). A NULL value is stored if no tuple
 *		conversion is required.
 *
 * indexes
 *		Array of partdesc->nparts elements.  For leaf partitions the index
 *		corresponds to the partition's ResultRelInfo in the encapsulating
 *		PartitionTupleRouting's partitions array.  For partitioned partitions,
 *		the index corresponds to the PartitionDispatch for it in its
 *		partition_dispatch_info array.  -1 indicates we've not yet allocated
 *		anything in PartitionTupleRouting for the partition.
 *-----------------------
 */
typedef struct PartitionDispatchData
{
	Relation	reldesc;
	PartitionKey key;
	List	   *keystate;		/* list of ExprState */
	PartitionDesc partdesc;
	TupleTableSlot *tupslot;
	AttrMap    *tupmap;
	int			indexes[FLEXIBLE_ARRAY_MEMBER];
}			PartitionDispatchData;


static ResultRelInfo *ExecInitPartitionInfo(ModifyTableState *mtstate,
											EState *estate, PartitionTupleRouting *proute,
											PartitionDispatch dispatch,
											ResultRelInfo *rootResultRelInfo,
											int partidx);
static void ExecInitRoutingInfo(ModifyTableState *mtstate,
								EState *estate,
								PartitionTupleRouting *proute,
								PartitionDispatch dispatch,
								ResultRelInfo *partRelInfo,
								int partidx,
								bool is_borrowed_rel);
static PartitionDispatch ExecInitPartitionDispatchInfo(EState *estate,
													   PartitionTupleRouting *proute,
													   Oid partoid, PartitionDispatch parent_pd,
													   int partidx, ResultRelInfo *rootResultRelInfo);
static void FormPartitionKeyDatum(PartitionDispatch pd,
								  TupleTableSlot *slot,
								  EState *estate,
								  Datum *values,
								  bool *isnull);
static int	get_partition_for_tuple(PartitionDispatch pd, Datum *values,
									bool *isnull);
static char *ExecBuildSlotPartitionKeyDescription(Relation rel,
												  Datum *values,
												  bool *isnull,
												  int maxfieldlen);
static List *adjust_partition_colnos(List *colnos, ResultRelInfo *leaf_part_rri);
static List *adjust_partition_colnos_using_map(List *colnos, AttrMap *attrMap);
static PartitionPruneState *CreatePartitionPruneState(PlanState *planstate,
													  PartitionPruneInfo *pruneinfo);
static void InitPartitionPruneContext(PartitionPruneContext *context,
									  List *pruning_steps,
									  PartitionDesc partdesc,
									  PartitionKey partkey,
									  PlanState *planstate,
									  ExprContext *econtext);
static void PartitionPruneFixSubPlanMap(PartitionPruneState *prunestate,
										Bitmapset *initially_valid_subplans,
										int n_total_subplans);
static void find_matching_subplans_recurse(PartitionPruningData *prunedata,
										   PartitionedRelPruningData *pprune,
										   bool initial_prune,
										   Bitmapset **validsubplans);


/*
 * ExecSetupPartitionTupleRouting - sets up information needed during
 * tuple routing for partitioned tables, encapsulates it in
 * PartitionTupleRouting, and returns it.
 *
 * Callers must use the returned PartitionTupleRouting during calls to
 * ExecFindPartition().  The actual ResultRelInfo for a partition is only
 * allocated when the partition is found for the first time.
 *
 * The current memory context is used to allocate this struct and all
 * subsidiary structs that will be allocated from it later on.  Typically
 * it should be estate->es_query_cxt.
 */
PartitionTupleRouting *
ExecSetupPartitionTupleRouting(EState *estate, Relation rel)
{
	PartitionTupleRouting *proute;

	/*
	 * Here we attempt to expend as little effort as possible in setting up
	 * the PartitionTupleRouting.  Each partition's ResultRelInfo is built on
	 * demand, only when we actually need to route a tuple to that partition.
	 * The reason for this is that a common case is for INSERT to insert a
	 * single tuple into a partitioned table and this must be fast.
	 */
	proute = (PartitionTupleRouting *) palloc0(sizeof(PartitionTupleRouting));
	proute->partition_root = rel;
	proute->memcxt = GetCurrentMemoryContext();
	/* Rest of members initialized by zeroing */

	/*
	 * Initialize this table's PartitionDispatch object.  Here we pass in the
	 * parent as NULL as we don't need to care about any parent of the target
	 * partitioned table.
	 */
	ExecInitPartitionDispatchInfo(estate, proute, RelationGetRelid(rel),
								  NULL, 0, NULL);

	return proute;
}

/*
 * ExecFindPartition -- Return the ResultRelInfo for the leaf partition that
 * the tuple contained in *slot should belong to.
 *
 * If the partition's ResultRelInfo does not yet exist in 'proute' then we set
 * one up or reuse one from mtstate's resultRelInfo array.  When reusing a
 * ResultRelInfo from the mtstate we verify that the relation is a valid
 * target for INSERTs and initialize tuple routing information.
 *
 * rootResultRelInfo is the relation named in the query.
 *
 * estate must be non-NULL; we'll need it to compute any expressions in the
 * partition keys.  Also, its per-tuple contexts are used as evaluation
 * scratch space.
 *
 * If no leaf partition is found, this routine errors out with the appropriate
 * error message.  An error may also be raised if the found target partition
 * is not a valid target for an INSERT.
 */
ResultRelInfo *
ExecFindPartition(ModifyTableState *mtstate,
				  ResultRelInfo *rootResultRelInfo,
				  PartitionTupleRouting *proute,
				  TupleTableSlot *slot, EState *estate)
{
	PartitionDispatch *pd = proute->partition_dispatch_info;
	Datum		values[PARTITION_MAX_KEYS];
	bool		isnull[PARTITION_MAX_KEYS];
	Relation	rel;
	PartitionDispatch dispatch;
	PartitionDesc partdesc;
	ExprContext *ecxt = GetPerTupleExprContext(estate);
	TupleTableSlot *ecxt_scantuple_saved = ecxt->ecxt_scantuple;
	TupleTableSlot *rootslot = slot;
	TupleTableSlot *myslot = NULL;
	MemoryContext oldcxt;
	ResultRelInfo *rri = NULL;

	/* use per-tuple context here to avoid leaking memory */
	oldcxt = MemoryContextSwitchTo(GetPerTupleMemoryContext(estate));

	/*
	 * First check the root table's partition constraint, if any.  No point in
	 * routing the tuple if it doesn't belong in the root table itself.
	 */
	if (rootResultRelInfo->ri_RelationDesc->rd_rel->relispartition)
		ExecPartitionCheck(rootResultRelInfo, slot, estate, true);

	/* start with the root partitioned table */
	dispatch = pd[0];
	while (dispatch != NULL)
	{
		int			partidx = -1;
		bool		is_leaf;

		CHECK_FOR_INTERRUPTS();

		rel = dispatch->reldesc;
		partdesc = dispatch->partdesc;

		/*
		 * Extract partition key from tuple. Expression evaluation machinery
		 * that FormPartitionKeyDatum() invokes expects ecxt_scantuple to
		 * point to the correct tuple slot.  The slot might have changed from
		 * what was used for the parent table if the table of the current
		 * partitioning level has different tuple descriptor from the parent.
		 * So update ecxt_scantuple accordingly.
		 */
		ecxt->ecxt_scantuple = slot;
		FormPartitionKeyDatum(dispatch, slot, estate, values, isnull);

		/*
		 * If this partitioned table has no partitions or no partition for
		 * these values, error out.
		 */
		if (partdesc->nparts == 0 ||
			(partidx = get_partition_for_tuple(dispatch, values, isnull)) < 0)
		{
			char	   *val_desc;

			val_desc = ExecBuildSlotPartitionKeyDescription(rel,
															values, isnull, 64);
			Assert(OidIsValid(RelationGetRelid(rel)));
			ereport(ERROR,
					(errcode(ERRCODE_CHECK_VIOLATION),
					 errmsg("no partition of relation \"%s\" found for row",
							RelationGetRelationName(rel)),
					 val_desc ?
					 errdetail("Partition key of the failing row contains %s.",
							   val_desc) : 0,
					 errtable(rel)));
		}

		is_leaf = partdesc->is_leaf[partidx];
		if (is_leaf)
		{
			/*
			 * We've reached the leaf -- hurray, we're done.  Look to see if
			 * we've already got a ResultRelInfo for this partition.
			 */
			if (likely(dispatch->indexes[partidx] >= 0))
			{
				/* ResultRelInfo already built */
				Assert(dispatch->indexes[partidx] < proute->num_partitions);
				rri = proute->partitions[dispatch->indexes[partidx]];
			}
			else
			{
				/*
				 * If the partition is known in the owning ModifyTableState
				 * node, we can re-use that ResultRelInfo instead of creating
				 * a new one with ExecInitPartitionInfo().
				 */
				rri = ExecLookupResultRelByOid(mtstate,
											   partdesc->oids[partidx],
											   true, false);
				if (rri)
				{
					/* Verify this ResultRelInfo allows INSERTs */
					CheckValidResultRel(rri, CMD_INSERT);

					/*
					 * Initialize information needed to insert this and
					 * subsequent tuples routed to this partition.
					 */
					ExecInitRoutingInfo(mtstate, estate, proute, dispatch,
										rri, partidx, true);
				}
				else
				{
					/* We need to create a new one. */
					rri = ExecInitPartitionInfo(mtstate, estate, proute,
												dispatch,
												rootResultRelInfo, partidx);
				}
			}
			Assert(rri != NULL);

			/* Signal to terminate the loop */
			dispatch = NULL;
		}
		else
		{
			/*
			 * Partition is a sub-partitioned table; get the PartitionDispatch
			 */
			if (likely(dispatch->indexes[partidx] >= 0))
			{
				/* Already built. */
				Assert(dispatch->indexes[partidx] < proute->num_dispatch);

				rri = proute->nonleaf_partitions[dispatch->indexes[partidx]];

				/*
				 * Move down to the next partition level and search again
				 * until we find a leaf partition that matches this tuple
				 */
				dispatch = pd[dispatch->indexes[partidx]];
			}
			else
			{
				/* Not yet built. Do that now. */
				PartitionDispatch subdispatch;

				/*
				 * Create the new PartitionDispatch.  We pass the current one
				 * in as the parent PartitionDispatch
				 */
				subdispatch = ExecInitPartitionDispatchInfo(estate,
															proute,
															partdesc->oids[partidx],
															dispatch, partidx,
															mtstate->rootResultRelInfo);
				Assert(dispatch->indexes[partidx] >= 0 &&
					   dispatch->indexes[partidx] < proute->num_dispatch);

				rri = proute->nonleaf_partitions[dispatch->indexes[partidx]];
				dispatch = subdispatch;
			}

			/*
			 * Convert the tuple to the new parent's layout, if different from
			 * the previous parent.
			 */
			if (dispatch->tupslot)
			{
				AttrMap    *map = dispatch->tupmap;
				TupleTableSlot *tempslot = myslot;

				myslot = dispatch->tupslot;
				slot = execute_attr_map_slot(map, slot, myslot);

				if (tempslot != NULL)
					ExecClearTuple(tempslot);
			}
		}

		/*
		 * If this partition is the default one, we must check its partition
		 * constraint now, which may have changed concurrently due to
		 * partitions being added to the parent.
		 *
		 * (We do this here, and do not rely on ExecInsert doing it, because
		 * we don't want to miss doing it for non-leaf partitions.)
		 */
		if (partidx == partdesc->boundinfo->default_index)
		{
			/*
			 * The tuple must match the partition's layout for the constraint
			 * expression to be evaluated successfully.  If the partition is
			 * sub-partitioned, that would already be the case due to the code
			 * above, but for a leaf partition the tuple still matches the
			 * parent's layout.
			 *
			 * Note that we have a map to convert from root to current
			 * partition, but not from immediate parent to current partition.
			 * So if we have to convert, do it from the root slot; if not, use
			 * the root slot as-is.
			 */
			if (is_leaf)
			{
				TupleConversionMap *map = rri->ri_RootToPartitionMap;

				if (map)
					slot = execute_attr_map_slot(map->attrMap, rootslot,
												 rri->ri_PartitionTupleSlot);
				else
					slot = rootslot;
			}

			ExecPartitionCheck(rri, slot, estate, true);
		}
	}

	/* Release the tuple in the lowest parent's dedicated slot. */
	if (myslot != NULL)
		ExecClearTuple(myslot);
	/* and restore ecxt's scantuple */
	ecxt->ecxt_scantuple = ecxt_scantuple_saved;
	MemoryContextSwitchTo(oldcxt);

	return rri;
}

/*
 * ExecInitPartitionInfo
 *		Lock the partition and initialize ResultRelInfo.  Also setup other
 *		information for the partition and store it in the next empty slot in
 *		the proute->partitions array.
 *
 * Returns the ResultRelInfo
 */
static ResultRelInfo *
ExecInitPartitionInfo(ModifyTableState *mtstate, EState *estate,
					  PartitionTupleRouting *proute,
					  PartitionDispatch dispatch,
					  ResultRelInfo *rootResultRelInfo,
					  int partidx)
{
	ModifyTable *node = (ModifyTable *) mtstate->ps.plan;
	Oid			partOid = dispatch->partdesc->oids[partidx];
	Relation	partrel;
	int			firstVarno = mtstate->resultRelInfo[0].ri_RangeTableIndex;
	Relation	firstResultRel = mtstate->resultRelInfo[0].ri_RelationDesc;
	ResultRelInfo *leaf_part_rri;
	MemoryContext oldcxt;
	AttrMap    *part_attmap = NULL;
	bool		found_whole_row;

	oldcxt = MemoryContextSwitchTo(proute->memcxt);

	partrel = table_open(partOid, RowExclusiveLock);

	/*
	 * The result relation's range table index passed into InitResultRelInfo
	 * later gets used in the YB code-path to fetch range table entry
	 * during YBExecUpdateAct(). The actual nominalRelation value needs to be
	 * passed on in order to correctly fetch the entry.
	 */
	int resultRelationIndex =
			(!IsYBRelation(firstResultRel) ||
			 partrel->rd_rel->relkind == RELKIND_FOREIGN_TABLE) ? 0 :
			(node ? node->nominalRelation : 1);
	leaf_part_rri = makeNode(ResultRelInfo);
	InitResultRelInfo(leaf_part_rri,
					  partrel,
					  resultRelationIndex,
					  rootResultRelInfo,
					  estate->es_instrument);

	/*
	 * Verify result relation is a valid target for an INSERT.  An UPDATE of a
	 * partition-key becomes a DELETE+INSERT operation, so this check is still
	 * required when the operation is CMD_UPDATE.
	 */
	CheckValidResultRel(leaf_part_rri, CMD_INSERT);

	/*
	 * Open partition indices.  The user may have asked to check for conflicts
	 * within this leaf partition and do "nothing" instead of throwing an
	 * error.  Be prepared in that case by initializing the index information
	 * needed by ExecInsert() to perform speculative insertions.
	 */
	if (partrel->rd_rel->relhasindex &&
		leaf_part_rri->ri_IndexRelationDescs == NULL)
		ExecOpenIndices(leaf_part_rri,
						(node != NULL &&
						 node->onConflictAction != ONCONFLICT_NONE));

	/*
	 * Build WITH CHECK OPTION constraints for the partition.  Note that we
	 * didn't build the withCheckOptionList for partitions within the planner,
	 * but simple translation of varattnos will suffice.  This only occurs for
	 * the INSERT case or in the case of UPDATE tuple routing where we didn't
	 * find a result rel to reuse.
	 */
	if (node && node->withCheckOptionLists != NIL)
	{
		List	   *wcoList;
		List	   *wcoExprs = NIL;
		ListCell   *ll;

		/*
		 * In the case of INSERT on a partitioned table, there is only one
		 * plan.  Likewise, there is only one WCO list, not one per partition.
		 * For UPDATE, there are as many WCO lists as there are plans.
		 */
		Assert((node->operation == CMD_INSERT &&
				list_length(node->withCheckOptionLists) == 1 &&
				list_length(node->resultRelations) == 1) ||
			   (node->operation == CMD_UPDATE &&
				list_length(node->withCheckOptionLists) ==
				list_length(node->resultRelations)));

		/*
		 * Use the WCO list of the first plan as a reference to calculate
		 * attno's for the WCO list of this partition.  In the INSERT case,
		 * that refers to the root partitioned table, whereas in the UPDATE
		 * tuple routing case, that refers to the first partition in the
		 * mtstate->resultRelInfo array.  In any case, both that relation and
		 * this partition should have the same columns, so we should be able
		 * to map attributes successfully.
		 */
		wcoList = linitial(node->withCheckOptionLists);

		/*
		 * Convert Vars in it to contain this partition's attribute numbers.
		 */
		part_attmap =
			build_attrmap_by_name(RelationGetDescr(partrel),
								  RelationGetDescr(firstResultRel),
								  false /* yb_ignore_type_mismatch */);
		wcoList = (List *)
			map_variable_attnos((Node *) wcoList,
								firstVarno, 0,
								part_attmap,
								RelationGetForm(partrel)->reltype,
								&found_whole_row);
		/* We ignore the value of found_whole_row. */

		foreach(ll, wcoList)
		{
			WithCheckOption *wco = lfirst_node(WithCheckOption, ll);
			ExprState  *wcoExpr = ExecInitQual(castNode(List, wco->qual),
											   &mtstate->ps);

			wcoExprs = lappend(wcoExprs, wcoExpr);
		}

		leaf_part_rri->ri_WithCheckOptions = wcoList;
		leaf_part_rri->ri_WithCheckOptionExprs = wcoExprs;
	}

	/*
	 * Build the RETURNING projection for the partition.  Note that we didn't
	 * build the returningList for partitions within the planner, but simple
	 * translation of varattnos will suffice.  This only occurs for the INSERT
	 * case or in the case of UPDATE tuple routing where we didn't find a
	 * result rel to reuse.
	 */
	if (node && node->returningLists != NIL)
	{
		TupleTableSlot *slot;
		ExprContext *econtext;
		List	   *returningList;

		/* See the comment above for WCO lists. */
		Assert((node->operation == CMD_INSERT &&
				list_length(node->returningLists) == 1 &&
				list_length(node->resultRelations) == 1) ||
			   (node->operation == CMD_UPDATE &&
				list_length(node->returningLists) ==
				list_length(node->resultRelations)));

		/*
		 * Use the RETURNING list of the first plan as a reference to
		 * calculate attno's for the RETURNING list of this partition.  See
		 * the comment above for WCO lists for more details on why this is
		 * okay.
		 */
		returningList = linitial(node->returningLists);

		/*
		 * Convert Vars in it to contain this partition's attribute numbers.
		 */
		if (part_attmap == NULL)
			part_attmap =
				build_attrmap_by_name(RelationGetDescr(partrel),
									  RelationGetDescr(firstResultRel),
									  false /* yb_ignore_type_mismatch */);
		returningList = (List *)
			map_variable_attnos((Node *) returningList,
								firstVarno, 0,
								part_attmap,
								RelationGetForm(partrel)->reltype,
								&found_whole_row);
		/* We ignore the value of found_whole_row. */

		leaf_part_rri->ri_returningList = returningList;

		/*
		 * Initialize the projection itself.
		 *
		 * Use the slot and the expression context that would have been set up
		 * in ExecInitModifyTable() for projection's output.
		 */
		Assert(mtstate->ps.ps_ResultTupleSlot != NULL);
		slot = mtstate->ps.ps_ResultTupleSlot;
		Assert(mtstate->ps.ps_ExprContext != NULL);
		econtext = mtstate->ps.ps_ExprContext;
		leaf_part_rri->ri_projectReturning =
			ExecBuildProjectionInfo(returningList, econtext, slot,
									&mtstate->ps, RelationGetDescr(partrel));
	}

	/* Set up information needed for routing tuples to the partition. */
	ExecInitRoutingInfo(mtstate, estate, proute, dispatch,
						leaf_part_rri, partidx, false);

	/*
	 * If there is an ON CONFLICT clause, initialize state for it.
	 */
	if (node && node->onConflictAction != ONCONFLICT_NONE)
	{
		TupleDesc	partrelDesc = RelationGetDescr(partrel);
		ExprContext *econtext = mtstate->ps.ps_ExprContext;
		ListCell   *lc;
		List	   *arbiterIndexes = NIL;

		/*
		 * If there is a list of arbiter indexes, map it to a list of indexes
		 * in the partition.  We do that by scanning the partition's index
		 * list and searching for ancestry relationships to each index in the
		 * ancestor table.
		 */
		if (list_length(rootResultRelInfo->ri_onConflictArbiterIndexes) > 0)
		{
			List	   *childIdxs;

			childIdxs = RelationGetIndexList(leaf_part_rri->ri_RelationDesc);

			foreach(lc, childIdxs)
			{
				Oid			childIdx = lfirst_oid(lc);
				List	   *ancestors;
				ListCell   *lc2;

				ancestors = get_partition_ancestors(childIdx);
				foreach(lc2, rootResultRelInfo->ri_onConflictArbiterIndexes)
				{
					if (list_member_oid(ancestors, lfirst_oid(lc2)))
						arbiterIndexes = lappend_oid(arbiterIndexes, childIdx);
				}
				list_free(ancestors);
			}
		}

		/*
		 * If the resulting lists are of inequal length, something is wrong.
		 * (This shouldn't happen, since arbiter index selection should not
		 * pick up an invalid index.)
		 */
		if (list_length(rootResultRelInfo->ri_onConflictArbiterIndexes) !=
			list_length(arbiterIndexes))
			elog(ERROR, "invalid arbiter index list");
		leaf_part_rri->ri_onConflictArbiterIndexes = arbiterIndexes;

		/*
		 * In the DO UPDATE case, we have some more state to initialize.
		 */
		if (node->onConflictAction == ONCONFLICT_UPDATE)
		{
			OnConflictSetState *onconfl = makeNode(OnConflictSetState);
			TupleConversionMap *map;

			map = leaf_part_rri->ri_RootToPartitionMap;

			Assert(node->onConflictSet != NIL);
			Assert(rootResultRelInfo->ri_onConflict != NULL);

			leaf_part_rri->ri_onConflict = onconfl;

			/*
			 * Need a separate existing slot for each partition, as the
			 * partition could be of a different AM, even if the tuple
			 * descriptors match.
			 */
			onconfl->oc_Existing =
				table_slot_create(leaf_part_rri->ri_RelationDesc,
								  &mtstate->ps.state->es_tupleTable);

			/*
			 * If the partition's tuple descriptor matches exactly the root
			 * parent (the common case), we can re-use most of the parent's ON
			 * CONFLICT SET state, skipping a bunch of work.  Otherwise, we
			 * need to create state specific to this partition.
			 */
			if (map == NULL)
			{
				/*
				 * It's safe to reuse these from the partition root, as we
				 * only process one tuple at a time (therefore we won't
				 * overwrite needed data in slots), and the results of
				 * projections are independent of the underlying storage.
				 * Projections and where clauses themselves don't store state
				 * / are independent of the underlying storage.
				 */
				onconfl->oc_ProjSlot =
					rootResultRelInfo->ri_onConflict->oc_ProjSlot;
				onconfl->oc_ProjInfo =
					rootResultRelInfo->ri_onConflict->oc_ProjInfo;
				onconfl->oc_WhereClause =
					rootResultRelInfo->ri_onConflict->oc_WhereClause;
			}
			else
			{
				List	   *onconflset;
				List	   *onconflcols;
				bool		found_whole_row;

				/*
				 * Translate expressions in onConflictSet to account for
				 * different attribute numbers.  For that, map partition
				 * varattnos twice: first to catch the EXCLUDED
				 * pseudo-relation (INNER_VAR), and second to handle the main
				 * target relation (firstVarno).
				 */
				onconflset = copyObject(node->onConflictSet);

				if (part_attmap == NULL)
					part_attmap =
						build_attrmap_by_name(RelationGetDescr(partrel),
											  RelationGetDescr(firstResultRel),
											  false /* yb_ignore_type_mismatch */);
				onconflset = (List *)
					map_variable_attnos((Node *) onconflset,
										INNER_VAR, 0,
										part_attmap,
										RelationGetForm(partrel)->reltype,
										&found_whole_row);
				/* We ignore the value of found_whole_row. */
				onconflset = (List *)
					map_variable_attnos((Node *) onconflset,
										firstVarno, 0,
										part_attmap,
										RelationGetForm(partrel)->reltype,
										&found_whole_row);
				/* We ignore the value of found_whole_row. */

				/* Finally, adjust the target colnos to match the partition. */
				onconflcols = adjust_partition_colnos(node->onConflictCols,
													  leaf_part_rri);

				/* create the tuple slot for the UPDATE SET projection */
				onconfl->oc_ProjSlot =
					table_slot_create(partrel,
									  &mtstate->ps.state->es_tupleTable);

				/* build UPDATE SET projection state */
				onconfl->oc_ProjInfo =
					ExecBuildUpdateProjection(onconflset,
											  true,
											  onconflcols,
											  partrelDesc,
											  econtext,
											  onconfl->oc_ProjSlot,
											  &mtstate->ps);

				/*
				 * If there is a WHERE clause, initialize state where it will
				 * be evaluated, mapping the attribute numbers appropriately.
				 * As with onConflictSet, we need to map partition varattnos
				 * to the partition's tupdesc.
				 */
				if (node->onConflictWhere)
				{
					List	   *clause;

					clause = copyObject((List *) node->onConflictWhere);
					clause = (List *)
						map_variable_attnos((Node *) clause,
											INNER_VAR, 0,
											part_attmap,
											RelationGetForm(partrel)->reltype,
											&found_whole_row);
					/* We ignore the value of found_whole_row. */
					clause = (List *)
						map_variable_attnos((Node *) clause,
											firstVarno, 0,
											part_attmap,
											RelationGetForm(partrel)->reltype,
											&found_whole_row);
					/* We ignore the value of found_whole_row. */
					onconfl->oc_WhereClause =
						ExecInitQual((List *) clause, &mtstate->ps);
				}
			}
		}
	}

	/*
	 * Since we've just initialized this ResultRelInfo, it's not in any list
	 * attached to the estate as yet.  Add it, so that it can be found later.
	 *
	 * Note that the entries in this list appear in no predetermined order,
	 * because partition result rels are initialized as and when they're
	 * needed.
	 */
	MemoryContextSwitchTo(estate->es_query_cxt);
	estate->es_tuple_routing_result_relations =
		lappend(estate->es_tuple_routing_result_relations,
				leaf_part_rri);

	/*
	 * Initialize information about this partition that's needed to handle
	 * MERGE.  We take the "first" result relation's mergeActionList as
	 * reference and make copy for this relation, converting stuff that
	 * references attribute numbers to match this relation's.
	 *
	 * This duplicates much of the logic in ExecInitMerge(), so something
	 * changes there, look here too.
	 */
	if (node && node->operation == CMD_MERGE)
	{
		List	   *firstMergeActionList = linitial(node->mergeActionLists);
		ListCell   *lc;
		ExprContext *econtext = mtstate->ps.ps_ExprContext;

		if (part_attmap == NULL)
			part_attmap =
				build_attrmap_by_name(RelationGetDescr(partrel),
									  RelationGetDescr(firstResultRel),
									  false /* yb_ignore_type_mismatch */);

		if (unlikely(!leaf_part_rri->ri_projectNewInfoValid))
			ExecInitMergeTupleSlots(mtstate, leaf_part_rri);

		foreach(lc, firstMergeActionList)
		{
			/* Make a copy for this relation to be safe.  */
			MergeAction *action = copyObject(lfirst(lc));
			MergeActionState *action_state;
			List	  **list;

			/* Generate the action's state for this relation */
			action_state = makeNode(MergeActionState);
			action_state->mas_action = action;

			/* And put the action in the appropriate list */
			if (action->matched)
				list = &leaf_part_rri->ri_matchedMergeAction;
			else
				list = &leaf_part_rri->ri_notMatchedMergeAction;
			*list = lappend(*list, action_state);

			switch (action->commandType)
			{
				case CMD_INSERT:

					/*
					 * ExecCheckPlanOutput() already done on the targetlist
					 * when "first" result relation initialized and it is same
					 * for all result relations.
					 */
					action_state->mas_proj =
						ExecBuildProjectionInfo(action->targetList, econtext,
												leaf_part_rri->ri_newTupleSlot,
												&mtstate->ps,
												RelationGetDescr(partrel));
					break;
				case CMD_UPDATE:

					/*
					 * Convert updateColnos from "first" result relation
					 * attribute numbers to this result rel's.
					 */
					if (part_attmap)
						action->updateColnos =
							adjust_partition_colnos_using_map(action->updateColnos,
															  part_attmap);
					action_state->mas_proj =
						ExecBuildUpdateProjection(action->targetList,
												  true,
												  action->updateColnos,
												  RelationGetDescr(leaf_part_rri->ri_RelationDesc),
												  econtext,
												  leaf_part_rri->ri_newTupleSlot,
												  NULL);
					break;
				case CMD_DELETE:
					break;

				default:
					elog(ERROR, "unknown action in MERGE WHEN clause");
			}

			/* found_whole_row intentionally ignored. */
			action->qual =
				map_variable_attnos(action->qual,
									firstVarno, 0,
									part_attmap,
									RelationGetForm(partrel)->reltype,
									&found_whole_row);
			action_state->mas_whenqual =
				ExecInitQual((List *) action->qual, &mtstate->ps);
		}
	}
	MemoryContextSwitchTo(oldcxt);

	return leaf_part_rri;
}

/*
 * ExecInitRoutingInfo
 *		Set up information needed for translating tuples between root
 *		partitioned table format and partition format, and keep track of it
 *		in PartitionTupleRouting.
 */
static void
ExecInitRoutingInfo(ModifyTableState *mtstate,
					EState *estate,
					PartitionTupleRouting *proute,
					PartitionDispatch dispatch,
					ResultRelInfo *partRelInfo,
					int partidx,
					bool is_borrowed_rel)
{
	ResultRelInfo *rootRelInfo = partRelInfo->ri_RootResultRelInfo;
	MemoryContext oldcxt;
	int			rri_index;

	oldcxt = MemoryContextSwitchTo(proute->memcxt);

	/*
	 * Set up a tuple conversion map to convert a tuple routed to the
	 * partition from the parent's type to the partition's.
	 */
	partRelInfo->ri_RootToPartitionMap =
		convert_tuples_by_name(RelationGetDescr(rootRelInfo->ri_RelationDesc),
							   RelationGetDescr(partRelInfo->ri_RelationDesc));

	/*
	 * If a partition has a different rowtype than the root parent, initialize
	 * a slot dedicated to storing this partition's tuples.  The slot is used
	 * for various operations that are applied to tuples after routing, such
	 * as checking constraints.
	 */
	if (partRelInfo->ri_RootToPartitionMap != NULL)
	{
		Relation	partrel = partRelInfo->ri_RelationDesc;

		/*
		 * Initialize the slot itself setting its descriptor to this
		 * partition's TupleDesc; TupleDesc reference will be released at the
		 * end of the command.
		 */
		partRelInfo->ri_PartitionTupleSlot =
			table_slot_create(partrel, &estate->es_tupleTable);
	}
	else
		partRelInfo->ri_PartitionTupleSlot = NULL;

	/*
	 * If the partition is a foreign table, let the FDW init itself for
	 * routing tuples to the partition.
	 */
	if (partRelInfo->ri_FdwRoutine != NULL &&
		partRelInfo->ri_FdwRoutine->BeginForeignInsert != NULL)
		partRelInfo->ri_FdwRoutine->BeginForeignInsert(mtstate, partRelInfo);

	/*
	 * Determine if the FDW supports batch insert and determine the batch size
	 * (a FDW may support batching, but it may be disabled for the
	 * server/table or for this particular query).
	 *
	 * If the FDW does not support batching, we set the batch size to 1.
	 */
	if (mtstate->operation == CMD_INSERT &&
		partRelInfo->ri_FdwRoutine != NULL &&
		partRelInfo->ri_FdwRoutine->GetForeignModifyBatchSize &&
		partRelInfo->ri_FdwRoutine->ExecForeignBatchInsert)
		partRelInfo->ri_BatchSize =
			partRelInfo->ri_FdwRoutine->GetForeignModifyBatchSize(partRelInfo);
	else
		partRelInfo->ri_BatchSize = 1;

	Assert(partRelInfo->ri_BatchSize >= 1);

	partRelInfo->ri_CopyMultiInsertBuffer = NULL;

	/*
	 * Keep track of it in the PartitionTupleRouting->partitions array.
	 */
	Assert(dispatch->indexes[partidx] == -1);

	rri_index = proute->num_partitions++;

	/* Allocate or enlarge the array, as needed */
	if (proute->num_partitions >= proute->max_partitions)
	{
		if (proute->max_partitions == 0)
		{
			proute->max_partitions = 8;
			proute->partitions = (ResultRelInfo **)
				palloc(sizeof(ResultRelInfo *) * proute->max_partitions);
			proute->is_borrowed_rel = (bool *)
				palloc(sizeof(bool) * proute->max_partitions);
		}
		else
		{
			proute->max_partitions *= 2;
			proute->partitions = (ResultRelInfo **)
				repalloc(proute->partitions, sizeof(ResultRelInfo *) *
						 proute->max_partitions);
			proute->is_borrowed_rel = (bool *)
				repalloc(proute->is_borrowed_rel, sizeof(bool) *
						 proute->max_partitions);
		}
	}

	proute->partitions[rri_index] = partRelInfo;
	proute->is_borrowed_rel[rri_index] = is_borrowed_rel;
	dispatch->indexes[partidx] = rri_index;

	MemoryContextSwitchTo(oldcxt);
}

/*
 * ExecInitPartitionDispatchInfo
 *		Lock the partitioned table (if not locked already) and initialize
 *		PartitionDispatch for a partitioned table and store it in the next
 *		available slot in the proute->partition_dispatch_info array.  Also,
 *		record the index into this array in the parent_pd->indexes[] array in
 *		the partidx element so that we can properly retrieve the newly created
 *		PartitionDispatch later.
 */
static PartitionDispatch
ExecInitPartitionDispatchInfo(EState *estate,
							  PartitionTupleRouting *proute, Oid partoid,
							  PartitionDispatch parent_pd, int partidx,
							  ResultRelInfo *rootResultRelInfo)
{
	Relation	rel;
	PartitionDesc partdesc;
	PartitionDispatch pd;
	int			dispatchidx;
	MemoryContext oldcxt;

	/*
	 * For data modification, it is better that executor does not include
	 * partitions being detached, except when running in snapshot-isolation
	 * mode.  This means that a read-committed transaction immediately gets a
	 * "no partition for tuple" error when a tuple is inserted into a
	 * partition that's being detached concurrently, but a transaction in
	 * repeatable-read mode can still use such a partition.
	 */
	if (estate->es_partition_directory == NULL)
		estate->es_partition_directory =
			CreatePartitionDirectory(estate->es_query_cxt,
									 !IsolationUsesXactSnapshot());

	oldcxt = MemoryContextSwitchTo(proute->memcxt);

	/*
	 * Only sub-partitioned tables need to be locked here.  The root
	 * partitioned table will already have been locked as it's referenced in
	 * the query's rtable.
	 */
	if (partoid != RelationGetRelid(proute->partition_root))
		rel = table_open(partoid, RowExclusiveLock);
	else
		rel = proute->partition_root;
	partdesc = PartitionDirectoryLookup(estate->es_partition_directory, rel);

	pd = (PartitionDispatch) palloc(offsetof(PartitionDispatchData, indexes) +
									partdesc->nparts * sizeof(int));
	pd->reldesc = rel;
	pd->key = RelationGetPartitionKey(rel);
	pd->keystate = NIL;
	pd->partdesc = partdesc;
	if (parent_pd != NULL)
	{
		TupleDesc	tupdesc = RelationGetDescr(rel);

		/*
		 * For sub-partitioned tables where the column order differs from its
		 * direct parent partitioned table, we must store a tuple table slot
		 * initialized with its tuple descriptor and a tuple conversion map to
		 * convert a tuple from its parent's rowtype to its own.  This is to
		 * make sure that we are looking at the correct row using the correct
		 * tuple descriptor when computing its partition key for tuple
		 * routing.
		 */
		pd->tupmap = build_attrmap_by_name_if_req(RelationGetDescr(parent_pd->reldesc),
												  tupdesc);
		pd->tupslot = pd->tupmap ?
			MakeSingleTupleTableSlot(tupdesc, &TTSOpsVirtual) : NULL;
	}
	else
	{
		/* Not required for the root partitioned table */
		pd->tupmap = NULL;
		pd->tupslot = NULL;
	}

	/*
	 * Initialize with -1 to signify that the corresponding partition's
	 * ResultRelInfo or PartitionDispatch has not been created yet.
	 */
	memset(pd->indexes, -1, sizeof(int) * partdesc->nparts);

	/* Track in PartitionTupleRouting for later use */
	dispatchidx = proute->num_dispatch++;

	/* Allocate or enlarge the array, as needed */
	if (proute->num_dispatch >= proute->max_dispatch)
	{
		if (proute->max_dispatch == 0)
		{
			proute->max_dispatch = 4;
			proute->partition_dispatch_info = (PartitionDispatch *)
				palloc(sizeof(PartitionDispatch) * proute->max_dispatch);
			proute->nonleaf_partitions = (ResultRelInfo **)
				palloc(sizeof(ResultRelInfo *) * proute->max_dispatch);
		}
		else
		{
			proute->max_dispatch *= 2;
			proute->partition_dispatch_info = (PartitionDispatch *)
				repalloc(proute->partition_dispatch_info,
						 sizeof(PartitionDispatch) * proute->max_dispatch);
			proute->nonleaf_partitions = (ResultRelInfo **)
				repalloc(proute->nonleaf_partitions,
						 sizeof(ResultRelInfo *) * proute->max_dispatch);
		}
	}
	proute->partition_dispatch_info[dispatchidx] = pd;

	/*
	 * If setting up a PartitionDispatch for a sub-partitioned table, we may
	 * also need a minimally valid ResultRelInfo for checking the partition
	 * constraint later; set that up now.
	 */
	if (parent_pd)
	{
		ResultRelInfo *rri = makeNode(ResultRelInfo);

		InitResultRelInfo(rri, rel, 0, rootResultRelInfo, 0);
		proute->nonleaf_partitions[dispatchidx] = rri;
	}
	else
		proute->nonleaf_partitions[dispatchidx] = NULL;

	/*
	 * Finally, if setting up a PartitionDispatch for a sub-partitioned table,
	 * install a downlink in the parent to allow quick descent.
	 */
	if (parent_pd)
	{
		Assert(parent_pd->indexes[partidx] == -1);
		parent_pd->indexes[partidx] = dispatchidx;
	}

	MemoryContextSwitchTo(oldcxt);

	return pd;
}

/*
 * ExecCleanupTupleRouting -- Clean up objects allocated for partition tuple
 * routing.
 *
 * Close all the partitioned tables, leaf partitions, and their indices.
 */
void
ExecCleanupTupleRouting(ModifyTableState *mtstate,
						PartitionTupleRouting *proute)
{
	int			i;

	/*
	 * Remember, proute->partition_dispatch_info[0] corresponds to the root
	 * partitioned table, which we must not try to close, because it is the
	 * main target table of the query that will be closed by callers such as
	 * ExecEndPlan() or DoCopy(). Also, tupslot is NULL for the root
	 * partitioned table.
	 */
	for (i = 1; i < proute->num_dispatch; i++)
	{
		PartitionDispatch pd = proute->partition_dispatch_info[i];

		table_close(pd->reldesc, NoLock);

		if (pd->tupslot)
			ExecDropSingleTupleTableSlot(pd->tupslot);
	}

	for (i = 0; i < proute->num_partitions; i++)
	{
		ResultRelInfo *resultRelInfo = proute->partitions[i];

		/* Allow any FDWs to shut down */
		if (resultRelInfo->ri_FdwRoutine != NULL &&
			resultRelInfo->ri_FdwRoutine->EndForeignInsert != NULL)
			resultRelInfo->ri_FdwRoutine->EndForeignInsert(mtstate->ps.state,
														   resultRelInfo);

		/*
		 * Close it if it's not one of the result relations borrowed from the
		 * owning ModifyTableState; those will be closed by ExecEndPlan().
		 */
		if (proute->is_borrowed_rel[i])
			continue;

		ExecCloseIndices(resultRelInfo);
		table_close(resultRelInfo->ri_RelationDesc, NoLock);
	}
}

/* ----------------
 *		FormPartitionKeyDatum
 *			Construct values[] and isnull[] arrays for the partition key
 *			of a tuple.
 *
 *	pd				Partition dispatch object of the partitioned table
 *	slot			Heap tuple from which to extract partition key
 *	estate			executor state for evaluating any partition key
 *					expressions (must be non-NULL)
 *	values			Array of partition key Datums (output area)
 *	isnull			Array of is-null indicators (output area)
 *
 * the ecxt_scantuple slot of estate's per-tuple expr context must point to
 * the heap tuple passed in.
 * ----------------
 */
static void
FormPartitionKeyDatum(PartitionDispatch pd,
					  TupleTableSlot *slot,
					  EState *estate,
					  Datum *values,
					  bool *isnull)
{
	ListCell   *partexpr_item;
	int			i;

	if (pd->key->partexprs != NIL && pd->keystate == NIL)
	{
		/* Check caller has set up context correctly */
		Assert(estate != NULL &&
			   GetPerTupleExprContext(estate)->ecxt_scantuple == slot);

		/* First time through, set up expression evaluation state */
		pd->keystate = ExecPrepareExprList(pd->key->partexprs, estate);
	}

	partexpr_item = list_head(pd->keystate);
	for (i = 0; i < pd->key->partnatts; i++)
	{
		AttrNumber	keycol = pd->key->partattrs[i];
		Datum		datum;
		bool		isNull;

		if (keycol != 0)
		{
			/* Plain column; get the value directly from the heap tuple */
			datum = slot_getattr(slot, keycol, &isNull);
		}
		else
		{
			/* Expression; need to evaluate it */
			if (partexpr_item == NULL)
				elog(ERROR, "wrong number of partition key expressions");
			datum = ExecEvalExprSwitchContext((ExprState *) lfirst(partexpr_item),
											  GetPerTupleExprContext(estate),
											  &isNull);
			partexpr_item = lnext(pd->keystate, partexpr_item);
		}
		values[i] = datum;
		isnull[i] = isNull;
	}

	if (partexpr_item != NULL)
		elog(ERROR, "wrong number of partition key expressions");
}

/*
 * get_partition_for_tuple
 *		Finds partition of relation which accepts the partition key specified
 *		in values and isnull
 *
 * Return value is index of the partition (>= 0 and < partdesc->nparts) if one
 * found or -1 if none found.
 */
static int
get_partition_for_tuple(PartitionDispatch pd, Datum *values, bool *isnull)
{
	int			bound_offset;
	int			part_index = -1;
	PartitionKey key = pd->key;
	PartitionDesc partdesc = pd->partdesc;
	PartitionBoundInfo boundinfo = partdesc->boundinfo;

	/* Route as appropriate based on partitioning strategy. */
	switch (key->strategy)
	{
		case PARTITION_STRATEGY_HASH:
			{
				uint64		rowHash;

				rowHash = compute_partition_hash_value(key->partnatts,
													   key->partsupfunc,
													   key->partcollation,
													   values, isnull);

				part_index = boundinfo->indexes[rowHash % boundinfo->nindexes];
			}
			break;

		case PARTITION_STRATEGY_LIST:
			if (isnull[0])
			{
				if (partition_bound_accepts_nulls(boundinfo))
					part_index = boundinfo->null_index;
			}
			else
			{
				bool		equal = false;

				bound_offset = partition_list_bsearch(key->partsupfunc,
													  key->partcollation,
													  boundinfo,
													  values[0], &equal);
				if (bound_offset >= 0 && equal)
					part_index = boundinfo->indexes[bound_offset];
			}
			break;

		case PARTITION_STRATEGY_RANGE:
			{
				bool		equal = false,
							range_partkey_has_null = false;
				int			i;

				/*
				 * No range includes NULL, so this will be accepted by the
				 * default partition if there is one, and otherwise rejected.
				 */
				for (i = 0; i < key->partnatts; i++)
				{
					if (isnull[i])
					{
						range_partkey_has_null = true;
						break;
					}
				}

				if (!range_partkey_has_null)
				{
					bound_offset = partition_range_datum_bsearch(key->partsupfunc,
																 key->partcollation,
																 boundinfo,
																 key->partnatts,
																 values,
																 &equal);

					/*
					 * The bound at bound_offset is less than or equal to the
					 * tuple value, so the bound at offset+1 is the upper
					 * bound of the partition we're looking for, if there
					 * actually exists one.
					 */
					part_index = boundinfo->indexes[bound_offset + 1];
				}
			}
			break;

		default:
			elog(ERROR, "unexpected partition strategy: %d",
				 (int) key->strategy);
	}

	/*
	 * part_index < 0 means we failed to find a partition of this parent. Use
	 * the default partition, if there is one.
	 */
	if (part_index < 0)
		part_index = boundinfo->default_index;

	return part_index;
}

/*
 * ExecBuildSlotPartitionKeyDescription
 *
 * This works very much like BuildIndexValueDescription() and is currently
 * used for building error messages when ExecFindPartition() fails to find
 * partition for a row.
 */
static char *
ExecBuildSlotPartitionKeyDescription(Relation rel,
									 Datum *values,
									 bool *isnull,
									 int maxfieldlen)
{
	StringInfoData buf;
	PartitionKey key = RelationGetPartitionKey(rel);
	int			partnatts = get_partition_natts(key);
	int			i;
	Oid			relid = RelationGetRelid(rel);
	AclResult	aclresult;

	if (check_enable_rls(relid, InvalidOid, true) == RLS_ENABLED)
		return NULL;

	/* If the user has table-level access, just go build the description. */
	aclresult = pg_class_aclcheck(relid, GetUserId(), ACL_SELECT);
	if (aclresult != ACLCHECK_OK)
	{
		/*
		 * Step through the columns of the partition key and make sure the
		 * user has SELECT rights on all of them.
		 */
		for (i = 0; i < partnatts; i++)
		{
			AttrNumber	attnum = get_partition_col_attnum(key, i);

			/*
			 * If this partition key column is an expression, we return no
			 * detail rather than try to figure out what column(s) the
			 * expression includes and if the user has SELECT rights on them.
			 */
			if (attnum == InvalidAttrNumber ||
				pg_attribute_aclcheck(relid, attnum, GetUserId(),
									  ACL_SELECT) != ACLCHECK_OK)
				return NULL;
		}
	}

	initStringInfo(&buf);
	appendStringInfo(&buf, "(%s) = (",
					 pg_get_partkeydef_columns(relid, true));

	for (i = 0; i < partnatts; i++)
	{
		char	   *val;
		int			vallen;

		if (isnull[i])
			val = "null";
		else
		{
			Oid			foutoid;
			bool		typisvarlena;

			getTypeOutputInfo(get_partition_col_typid(key, i),
							  &foutoid, &typisvarlena);
			val = OidOutputFunctionCall(foutoid, values[i]);
		}

		if (i > 0)
			appendStringInfoString(&buf, ", ");

		/* truncate if needed */
		vallen = strlen(val);
		if (vallen <= maxfieldlen)
			appendBinaryStringInfo(&buf, val, vallen);
		else
		{
			vallen = pg_mbcliplen(val, vallen, maxfieldlen);
			appendBinaryStringInfo(&buf, val, vallen);
			appendStringInfoString(&buf, "...");
		}
	}

	appendStringInfoChar(&buf, ')');

	return buf.data;
}

/*
 * adjust_partition_colnos
 *		Adjust the list of UPDATE target column numbers to account for
 *		attribute differences between the parent and the partition.
 *
 * Note: mustn't be called if no adjustment is required.
 */
static List *
adjust_partition_colnos(List *colnos, ResultRelInfo *leaf_part_rri)
{
	TupleConversionMap *map = ExecGetChildToRootMap(leaf_part_rri);

	Assert(map != NULL);

	return adjust_partition_colnos_using_map(colnos, map->attrMap);
}

/*
 * adjust_partition_colnos_using_map
 *		Like adjust_partition_colnos, but uses a caller-supplied map instead
 *		of assuming to map from the "root" result relation.
 *
 * Note: mustn't be called if no adjustment is required.
 */
static List *
adjust_partition_colnos_using_map(List *colnos, AttrMap *attrMap)
{
	List	   *new_colnos = NIL;
	ListCell   *lc;

	Assert(attrMap != NULL);	/* else we shouldn't be here */

	foreach(lc, colnos)
	{
		AttrNumber	parentattrno = lfirst_int(lc);

		if (parentattrno <= 0 ||
			parentattrno > attrMap->maplen ||
			attrMap->attnums[parentattrno - 1] == 0)
			elog(ERROR, "unexpected attno %d in target column list",
				 parentattrno);
		new_colnos = lappend_int(new_colnos,
								 attrMap->attnums[parentattrno - 1]);
	}

	return new_colnos;
}

/*-------------------------------------------------------------------------
 * Run-Time Partition Pruning Support.
 *
 * The following series of functions exist to support the removal of unneeded
 * subplans for queries against partitioned tables.  The supporting functions
 * here are designed to work with any plan type which supports an arbitrary
 * number of subplans, e.g. Append, MergeAppend.
 *
 * When pruning involves comparison of a partition key to a constant, it's
 * done by the planner.  However, if we have a comparison to a non-constant
 * but not volatile expression, that presents an opportunity for run-time
 * pruning by the executor, allowing irrelevant partitions to be skipped
 * dynamically.
 *
 * We must distinguish expressions containing PARAM_EXEC Params from
 * expressions that don't contain those.  Even though a PARAM_EXEC Param is
 * considered to be a stable expression, it can change value from one plan
 * node scan to the next during query execution.  Stable comparison
 * expressions that don't involve such Params allow partition pruning to be
 * done once during executor startup.  Expressions that do involve such Params
 * require us to prune separately for each scan of the parent plan node.
 *
 * Note that pruning away unneeded subplans during executor startup has the
 * added benefit of not having to initialize the unneeded subplans at all.
 *
 *
 * Functions:
 *
 * ExecInitPartitionPruning:
 *		Creates the PartitionPruneState required by ExecFindMatchingSubPlans.
 *		Details stored include how to map the partition index returned by the
 *		partition pruning code into subplan indexes.  Also determines the set
 *		of subplans to initialize considering the result of performing initial
 *		pruning steps if any.  Maps in PartitionPruneState are updated to
 *		account for initial pruning possibly having eliminated some of the
 *		subplans.
 *
 * ExecFindMatchingSubPlans:
 *		Returns indexes of matching subplans after evaluating the expressions
 *		that are safe to evaluate at a given point.  This function is first
 *		called during ExecInitPartitionPruning() to find the initially
 *		matching subplans based on performing the initial pruning steps and
 *		then must be called again each time the value of a Param listed in
 *		PartitionPruneState's 'execparamids' changes.
 *-------------------------------------------------------------------------
 */

/*
 * ExecInitPartitionPruning
 *		Initialize data structure needed for run-time partition pruning and
 *		do initial pruning if needed
 *
 * On return, *initially_valid_subplans is assigned the set of indexes of
 * child subplans that must be initialized along with the parent plan node.
 * Initial pruning is performed here if needed and in that case only the
 * surviving subplans' indexes are added.
 *
 * If subplans are indeed pruned, subplan_map arrays contained in the returned
 * PartitionPruneState are re-sequenced to not count those, though only if the
 * maps will be needed for subsequent execution pruning passes.
 */
PartitionPruneState *
ExecInitPartitionPruning(PlanState *planstate,
						 int n_total_subplans,
						 PartitionPruneInfo *pruneinfo,
						 Bitmapset **initially_valid_subplans)
{
	PartitionPruneState *prunestate;
	EState	   *estate = planstate->state;

	/* We may need an expression context to evaluate partition exprs */
	ExecAssignExprContext(estate, planstate);

	/* Create the working data structure for pruning */
	prunestate = CreatePartitionPruneState(planstate, pruneinfo);

	/*
	 * Perform an initial partition prune pass, if required.
	 */
	if (prunestate->do_initial_prune)
		*initially_valid_subplans = ExecFindMatchingSubPlans(prunestate, true);
	else
	{
		/* No pruning, so we'll need to initialize all subplans */
		Assert(n_total_subplans > 0);
		*initially_valid_subplans = bms_add_range(NULL, 0,
												  n_total_subplans - 1);
	}

	/*
	 * Re-sequence subplan indexes contained in prunestate to account for any
	 * that were removed above due to initial pruning.  No need to do this if
	 * no steps were removed.
	 */
	if (bms_num_members(*initially_valid_subplans) < n_total_subplans)
	{
		/*
		 * We can safely skip this when !do_exec_prune, even though that
		 * leaves invalid data in prunestate, because that data won't be
		 * consulted again (cf initial Assert in ExecFindMatchingSubPlans).
		 */
		if (prunestate->do_exec_prune)
			PartitionPruneFixSubPlanMap(prunestate,
										*initially_valid_subplans,
										n_total_subplans);
	}

	return prunestate;
}

/*
 * CreatePartitionPruneState
 *		Build the data structure required for calling ExecFindMatchingSubPlans
 *
 * 'planstate' is the parent plan node's execution state.
 *
 * 'pruneinfo' is a PartitionPruneInfo as generated by
 * make_partition_pruneinfo.  Here we build a PartitionPruneState containing a
 * PartitionPruningData for each partitioning hierarchy (i.e., each sublist of
 * pruneinfo->prune_infos), each of which contains a PartitionedRelPruningData
 * for each PartitionedRelPruneInfo appearing in that sublist.  This two-level
 * system is needed to keep from confusing the different hierarchies when a
 * UNION ALL contains multiple partitioned tables as children.  The data
 * stored in each PartitionedRelPruningData can be re-used each time we
 * re-evaluate which partitions match the pruning steps provided in each
 * PartitionedRelPruneInfo.
 */
static PartitionPruneState *
CreatePartitionPruneState(PlanState *planstate, PartitionPruneInfo *pruneinfo)
{
	EState	   *estate = planstate->state;
	PartitionPruneState *prunestate;
	int			n_part_hierarchies;
	ListCell   *lc;
	int			i;
	ExprContext *econtext = planstate->ps_ExprContext;

	/* For data reading, executor always omits detached partitions */
	if (estate->es_partition_directory == NULL)
		estate->es_partition_directory =
			CreatePartitionDirectory(estate->es_query_cxt, false);

	n_part_hierarchies = list_length(pruneinfo->prune_infos);
	Assert(n_part_hierarchies > 0);

	/*
	 * Allocate the data structure
	 */
	prunestate = (PartitionPruneState *)
		palloc(offsetof(PartitionPruneState, partprunedata) +
			   sizeof(PartitionPruningData *) * n_part_hierarchies);

	prunestate->execparamids = NULL;
	/* other_subplans can change at runtime, so we need our own copy */
	prunestate->other_subplans = bms_copy(pruneinfo->other_subplans);
	prunestate->do_initial_prune = false;	/* may be set below */
	prunestate->do_exec_prune = false;	/* may be set below */
	prunestate->num_partprunedata = n_part_hierarchies;

	/*
	 * Create a short-term memory context which we'll use when making calls to
	 * the partition pruning functions.  This avoids possible memory leaks,
	 * since the pruning functions call comparison functions that aren't under
	 * our control.
	 */
	prunestate->prune_context =
		AllocSetContextCreate(GetCurrentMemoryContext(),
							  "Partition Prune",
							  ALLOCSET_DEFAULT_SIZES);

	i = 0;
	foreach(lc, pruneinfo->prune_infos)
	{
		List	   *partrelpruneinfos = lfirst_node(List, lc);
		int			npartrelpruneinfos = list_length(partrelpruneinfos);
		PartitionPruningData *prunedata;
		ListCell   *lc2;
		int			j;

		prunedata = (PartitionPruningData *)
			palloc(offsetof(PartitionPruningData, partrelprunedata) +
				   npartrelpruneinfos * sizeof(PartitionedRelPruningData));
		prunestate->partprunedata[i] = prunedata;
		prunedata->num_partrelprunedata = npartrelpruneinfos;

		j = 0;
		foreach(lc2, partrelpruneinfos)
		{
			PartitionedRelPruneInfo *pinfo = lfirst_node(PartitionedRelPruneInfo, lc2);
			PartitionedRelPruningData *pprune = &prunedata->partrelprunedata[j];
			Relation	partrel;
			PartitionDesc partdesc;
			PartitionKey partkey;

			/*
			 * We can rely on the copies of the partitioned table's partition
			 * key and partition descriptor appearing in its relcache entry,
			 * because that entry will be held open and locked for the
			 * duration of this executor run.
			 */
			partrel = ExecGetRangeTableRelation(estate, pinfo->rtindex);
			partkey = RelationGetPartitionKey(partrel);
			partdesc = PartitionDirectoryLookup(estate->es_partition_directory,
												partrel);

			/*
			 * Initialize the subplan_map and subpart_map.
			 *
			 * Because we request detached partitions to be included, and
			 * detaching waits for old transactions, it is safe to assume that
			 * no partitions have disappeared since this query was planned.
			 *
			 * However, new partitions may have been added.
			 */
			Assert(partdesc->nparts >= pinfo->nparts);
			pprune->nparts = partdesc->nparts;
			pprune->subplan_map = palloc(sizeof(int) * partdesc->nparts);
			if (partdesc->nparts == pinfo->nparts)
			{
				/*
				 * There are no new partitions, so this is simple.  We can
				 * simply point to the subpart_map from the plan, but we must
				 * copy the subplan_map since we may change it later.
				 */
				pprune->subpart_map = pinfo->subpart_map;
				memcpy(pprune->subplan_map, pinfo->subplan_map,
					   sizeof(int) * pinfo->nparts);

				/*
				 * Double-check that the list of unpruned relations has not
				 * changed.  (Pruned partitions are not in relid_map[].)
				 */
#ifdef USE_ASSERT_CHECKING
				for (int k = 0; k < pinfo->nparts; k++)
				{
					Assert(partdesc->oids[k] == pinfo->relid_map[k] ||
						   pinfo->subplan_map[k] == -1);
				}
#endif
			}
			else
			{
				int			pd_idx = 0;
				int			pp_idx;

				/*
				 * Some new partitions have appeared since plan time, and
				 * those are reflected in our PartitionDesc but were not
				 * present in the one used to construct subplan_map and
				 * subpart_map.  So we must construct new and longer arrays
				 * where the partitions that were originally present map to
				 * the same sub-structures, and any added partitions map to
				 * -1, as if the new partitions had been pruned.
				 *
				 * Note: pinfo->relid_map[] may contain InvalidOid entries for
				 * partitions pruned by the planner.  We cannot tell exactly
				 * which of the partdesc entries these correspond to, but we
				 * don't have to; just skip over them.  The non-pruned
				 * relid_map entries, however, had better be a subset of the
				 * partdesc entries and in the same order.
				 */
				pprune->subpart_map = palloc(sizeof(int) * partdesc->nparts);
				for (pp_idx = 0; pp_idx < partdesc->nparts; pp_idx++)
				{
					/* Skip any InvalidOid relid_map entries */
					while (pd_idx < pinfo->nparts &&
						   !OidIsValid(pinfo->relid_map[pd_idx]))
						pd_idx++;

					if (pd_idx < pinfo->nparts &&
						pinfo->relid_map[pd_idx] == partdesc->oids[pp_idx])
					{
						/* match... */
						pprune->subplan_map[pp_idx] =
							pinfo->subplan_map[pd_idx];
						pprune->subpart_map[pp_idx] =
							pinfo->subpart_map[pd_idx];
						pd_idx++;
					}
					else
					{
						/* this partdesc entry is not in the plan */
						pprune->subplan_map[pp_idx] = -1;
						pprune->subpart_map[pp_idx] = -1;
					}
				}

				/*
				 * It might seem that we need to skip any trailing InvalidOid
				 * entries in pinfo->relid_map before checking that we scanned
				 * all of the relid_map.  But we will have skipped them above,
				 * because they must correspond to some partdesc->oids
				 * entries; we just couldn't tell which.
				 */
				if (pd_idx != pinfo->nparts)
					elog(ERROR, "could not match partition child tables to plan elements");
			}

			/* present_parts is also subject to later modification */
			pprune->present_parts = bms_copy(pinfo->present_parts);

			/*
			 * Initialize pruning contexts as needed.
			 */
			pprune->initial_pruning_steps = pinfo->initial_pruning_steps;
			if (pinfo->initial_pruning_steps)
			{
				InitPartitionPruneContext(&pprune->initial_context,
										  pinfo->initial_pruning_steps,
										  partdesc, partkey, planstate,
										  econtext);
				/* Record whether initial pruning is needed at any level */
				prunestate->do_initial_prune = true;
			}
			pprune->exec_pruning_steps = pinfo->exec_pruning_steps;
			if (pinfo->exec_pruning_steps)
			{
				InitPartitionPruneContext(&pprune->exec_context,
										  pinfo->exec_pruning_steps,
										  partdesc, partkey, planstate,
										  econtext);
				/* Record whether exec pruning is needed at any level */
				prunestate->do_exec_prune = true;
			}

			/*
			 * Accumulate the IDs of all PARAM_EXEC Params affecting the
			 * partitioning decisions at this plan node.
			 */
			prunestate->execparamids = bms_add_members(prunestate->execparamids,
													   pinfo->execparamids);

			j++;
		}
		i++;
	}

	return prunestate;
}

/*
 * Initialize a PartitionPruneContext for the given list of pruning steps.
 */
static void
InitPartitionPruneContext(PartitionPruneContext *context,
						  List *pruning_steps,
						  PartitionDesc partdesc,
						  PartitionKey partkey,
						  PlanState *planstate,
						  ExprContext *econtext)
{
	int			n_steps;
	int			partnatts;
	ListCell   *lc;

	n_steps = list_length(pruning_steps);

	context->strategy = partkey->strategy;
	context->partnatts = partnatts = partkey->partnatts;
	context->nparts = partdesc->nparts;
	context->boundinfo = partdesc->boundinfo;
	context->partcollation = partkey->partcollation;
	context->partsupfunc = partkey->partsupfunc;

	/* We'll look up type-specific support functions as needed */
	context->stepcmpfuncs = (FmgrInfo *)
		palloc0(sizeof(FmgrInfo) * n_steps * partnatts);

	context->ppccontext = GetCurrentMemoryContext();
	context->planstate = planstate;
	context->exprcontext = econtext;

	/* Initialize expression state for each expression we need */
	context->exprstates = (ExprState **)
		palloc0(sizeof(ExprState *) * n_steps * partnatts);
	foreach(lc, pruning_steps)
	{
		PartitionPruneStepOp *step = (PartitionPruneStepOp *) lfirst(lc);
		ListCell   *lc2;
		int			keyno;

		/* not needed for other step kinds */
		if (!IsA(step, PartitionPruneStepOp))
			continue;

		Assert(list_length(step->exprs) <= partnatts);

		keyno = 0;
		foreach(lc2, step->exprs)
		{
			Expr	   *expr = (Expr *) lfirst(lc2);

			/* not needed for Consts */
			if (!IsA(expr, Const))
			{
				int			stateidx = PruneCxtStateIdx(partnatts,
														step->step.step_id,
														keyno);

				/*
				 * When planstate is NULL, pruning_steps is known not to
				 * contain any expressions that depend on the parent plan.
				 * Information of any available EXTERN parameters must be
				 * passed explicitly in that case, which the caller must have
				 * made available via econtext.
				 */
				if (planstate == NULL)
					context->exprstates[stateidx] =
						ExecInitExprWithParams(expr,
											   econtext->ecxt_param_list_info);
				else
					context->exprstates[stateidx] =
						ExecInitExpr(expr, context->planstate);
			}
			keyno++;
		}
	}
}

/*
 * PartitionPruneFixSubPlanMap
 *		Fix mapping of partition indexes to subplan indexes contained in
 *		prunestate by considering the new list of subplans that survived
 *		initial pruning
 *
 * Current values of the indexes present in PartitionPruneState count all the
 * subplans that would be present before initial pruning was done.  If initial
 * pruning got rid of some of the subplans, any subsequent pruning passes will
 * will be looking at a different set of target subplans to choose from than
 * those in the pre-initial-pruning set, so the maps in PartitionPruneState
 * containing those indexes must be updated to reflect the new indexes of
 * subplans in the post-initial-pruning set.
 */
static void
PartitionPruneFixSubPlanMap(PartitionPruneState *prunestate,
							Bitmapset *initially_valid_subplans,
							int n_total_subplans)
{
	int		   *new_subplan_indexes;
	Bitmapset  *new_other_subplans;
	int			i;
	int			newidx;

	/*
	 * First we must build a temporary array which maps old subplan indexes to
	 * new ones.  For convenience of initialization, we use 1-based indexes in
	 * this array and leave pruned items as 0.
	 */
	new_subplan_indexes = (int *) palloc0(sizeof(int) * n_total_subplans);
	newidx = 1;
	i = -1;
	while ((i = bms_next_member(initially_valid_subplans, i)) >= 0)
	{
		Assert(i < n_total_subplans);
		new_subplan_indexes[i] = newidx++;
	}

	/*
	 * Now we can update each PartitionedRelPruneInfo's subplan_map with new
	 * subplan indexes.  We must also recompute its present_parts bitmap.
	 */
	for (i = 0; i < prunestate->num_partprunedata; i++)
	{
		PartitionPruningData *prunedata = prunestate->partprunedata[i];
		int			j;

		/*
		 * Within each hierarchy, we perform this loop in back-to-front order
		 * so that we determine present_parts for the lowest-level partitioned
		 * tables first.  This way we can tell whether a sub-partitioned
		 * table's partitions were entirely pruned so we can exclude it from
		 * the current level's present_parts.
		 */
		for (j = prunedata->num_partrelprunedata - 1; j >= 0; j--)
		{
			PartitionedRelPruningData *pprune = &prunedata->partrelprunedata[j];
			int			nparts = pprune->nparts;
			int			k;

			/* We just rebuild present_parts from scratch */
			bms_free(pprune->present_parts);
			pprune->present_parts = NULL;

			for (k = 0; k < nparts; k++)
			{
				int			oldidx = pprune->subplan_map[k];
				int			subidx;

				/*
				 * If this partition existed as a subplan then change the old
				 * subplan index to the new subplan index.  The new index may
				 * become -1 if the partition was pruned above, or it may just
				 * come earlier in the subplan list due to some subplans being
				 * removed earlier in the list.  If it's a subpartition, add
				 * it to present_parts unless it's entirely pruned.
				 */
				if (oldidx >= 0)
				{
					Assert(oldidx < n_total_subplans);
					pprune->subplan_map[k] = new_subplan_indexes[oldidx] - 1;

					if (new_subplan_indexes[oldidx] > 0)
						pprune->present_parts =
							bms_add_member(pprune->present_parts, k);
				}
				else if ((subidx = pprune->subpart_map[k]) >= 0)
				{
					PartitionedRelPruningData *subprune;

					subprune = &prunedata->partrelprunedata[subidx];

					if (!bms_is_empty(subprune->present_parts))
						pprune->present_parts =
							bms_add_member(pprune->present_parts, k);
				}
			}
		}
	}

	/*
	 * We must also recompute the other_subplans set, since indexes in it may
	 * change.
	 */
	new_other_subplans = NULL;
	i = -1;
	while ((i = bms_next_member(prunestate->other_subplans, i)) >= 0)
		new_other_subplans = bms_add_member(new_other_subplans,
											new_subplan_indexes[i] - 1);

	bms_free(prunestate->other_subplans);
	prunestate->other_subplans = new_other_subplans;

	pfree(new_subplan_indexes);
}

/*
 * ExecFindMatchingSubPlans
 *		Determine which subplans match the pruning steps detailed in
 *		'prunestate' for the current comparison expression values.
 *
 * Pass initial_prune if PARAM_EXEC Params cannot yet be evaluated.  This
 * differentiates the initial executor-time pruning step from later
 * runtime pruning.
 */
Bitmapset *
ExecFindMatchingSubPlans(PartitionPruneState *prunestate,
						 bool initial_prune)
{
	Bitmapset  *result = NULL;
	MemoryContext oldcontext;
	int			i;

	/*
	 * Either we're here on the initial prune done during pruning
	 * initialization, or we're at a point where PARAM_EXEC Params can be
	 * evaluated *and* there are steps in which to do so.
	 */
	Assert(initial_prune || prunestate->do_exec_prune);

	/*
	 * Switch to a temp context to avoid leaking memory in the executor's
	 * query-lifespan memory context.
	 */
	oldcontext = MemoryContextSwitchTo(prunestate->prune_context);

	/*
	 * For each hierarchy, do the pruning tests, and add nondeletable
	 * subplans' indexes to "result".
	 */
	for (i = 0; i < prunestate->num_partprunedata; i++)
	{
		PartitionPruningData *prunedata = prunestate->partprunedata[i];
		PartitionedRelPruningData *pprune;

		/*
		 * We pass the zeroth item, belonging to the root table of the
		 * hierarchy, and find_matching_subplans_recurse() takes care of
		 * recursing to other (lower-level) parents as needed.
		 */
		pprune = &prunedata->partrelprunedata[0];
		find_matching_subplans_recurse(prunedata, pprune, initial_prune,
									   &result);

		/* Expression eval may have used space in ExprContext too */
		if (pprune->exec_pruning_steps)
			ResetExprContext(pprune->exec_context.exprcontext);
	}

	/* Add in any subplans that partition pruning didn't account for */
	result = bms_add_members(result, prunestate->other_subplans);

	MemoryContextSwitchTo(oldcontext);

	/* Copy result out of the temp context before we reset it */
	result = bms_copy(result);

	MemoryContextReset(prunestate->prune_context);

	return result;
}

/*
 * find_matching_subplans_recurse
 *		Recursive worker function for ExecFindMatchingSubPlans
 *
 * Adds valid (non-prunable) subplan IDs to *validsubplans
 */
static void
find_matching_subplans_recurse(PartitionPruningData *prunedata,
							   PartitionedRelPruningData *pprune,
							   bool initial_prune,
							   Bitmapset **validsubplans)
{
	Bitmapset  *partset;
	int			i;

	/* Guard against stack overflow due to overly deep partition hierarchy. */
	check_stack_depth();

	/*
	 * Prune as appropriate, if we have pruning steps matching the current
	 * execution context.  Otherwise just include all partitions at this
	 * level.
	 */
	if (initial_prune && pprune->initial_pruning_steps)
		partset = get_matching_partitions(&pprune->initial_context,
										  pprune->initial_pruning_steps);
	else if (!initial_prune && pprune->exec_pruning_steps)
		partset = get_matching_partitions(&pprune->exec_context,
										  pprune->exec_pruning_steps);
	else
		partset = pprune->present_parts;

	/* Translate partset into subplan indexes */
	i = -1;
	while ((i = bms_next_member(partset, i)) >= 0)
	{
		if (pprune->subplan_map[i] >= 0)
			*validsubplans = bms_add_member(*validsubplans,
											pprune->subplan_map[i]);
		else
		{
			int			partidx = pprune->subpart_map[i];

			if (partidx >= 0)
				find_matching_subplans_recurse(prunedata,
											   &prunedata->partrelprunedata[partidx],
											   initial_prune, validsubplans);
			else
			{
				/*
				 * We get here if the planner already pruned all the sub-
				 * partitions for this partition.  Silently ignore this
				 * partition in this case.  The end result is the same: we
				 * would have pruned all partitions just the same, but we
				 * don't have any pruning steps to execute to verify this.
				 */
			}
		}
	}
}

Oid
YbPartitionTupleRoutingRootRelid(PartitionTupleRouting *proute)
{
	return RelationGetRelid(proute->partition_root);
}
