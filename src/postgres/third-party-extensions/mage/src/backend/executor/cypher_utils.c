/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its documentation for any purpose,
 * without fee, and without a written agreement is hereby granted, provided that the above copyright notice
 * and this paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT,
 * INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS,
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY
 * OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA
 * HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "postgres.h"

#include "nodes/makefuncs.h"
#include "parser/parse_relation.h"

#include "catalog/ag_label.h"
#include "commands/label_commands.h"
#include "executor/cypher_utils.h"
#include "utils/ag_cache.h"

/* YB includes */
#include "executor/ybModifyTable.h"
#include "fmgr.h"
#include "pg_yb_utils.h"
#include "utils/builtins.h"
#include "utils/uuid.h"

extern Datum uuid_in(PG_FUNCTION_ARGS); /* YB: for yb_extract_meko_columns_from_properties */

/*
 * Given the graph name and the label name, create a ResultRelInfo for the table
 * those to variables represent. Open the Indices too.
 */
ResultRelInfo *create_entity_result_rel_info(EState *estate, char *graph_name,
                                             char *label_name)
{
    RangeVar *rv;
    Relation label_relation;
    ResultRelInfo *resultRelInfo;

    ParseState *pstate = make_parsestate(NULL);

    resultRelInfo = palloc(sizeof(ResultRelInfo));

    if (strlen(label_name) == 0)
    {
        rv = makeRangeVar(graph_name, AG_DEFAULT_LABEL_VERTEX, -1);
    }
    else
    {
        rv = makeRangeVar(graph_name, label_name, -1);
    }

    label_relation = parserOpenTable(pstate, rv, RowExclusiveLock);

    /* initialize the resultRelInfo */
    InitResultRelInfo(resultRelInfo, label_relation,
                      list_length(estate->es_range_table), NULL,
                      estate->es_instrument);

    /* open the parse state */
    ExecOpenIndices(resultRelInfo, false);

    free_parsestate(pstate);

    return resultRelInfo;
}

/* close the result_rel_info and close all the indices */
void destroy_entity_result_rel_info(ResultRelInfo *result_rel_info)
{
    /* close the indices */
    ExecCloseIndices(result_rel_info);

    /* close the rel */
    table_close(result_rel_info->ri_RelationDesc, RowExclusiveLock);
}

/*
 * YB: Copy meko_* tenant column values from `meko` into the four
 * corresponding slot offsets on a vertex/edge tuple. is_edge selects the
 * vertex or edge column layout. Caller is expected to gate the call with
 * IsYugaByteEnabled().
 */
void yb_populate_meko_columns(TupleTableSlot *slot, YbMekoDp meko,
                              bool is_edge)
{
    int dp_off    = is_edge ? edge_tuple_meko_datapack_id
                            : vertex_tuple_meko_datapack_id;
    int user_off  = is_edge ? edge_tuple_meko_user_id
                            : vertex_tuple_meko_user_id;
    int ag_off    = is_edge ? edge_tuple_meko_agent_id
                            : vertex_tuple_meko_agent_id;
    int conv_off  = is_edge ? edge_tuple_meko_conversation_id
                            : vertex_tuple_meko_conversation_id;

    slot->tts_values[dp_off]   = meko.datapack_id;
    slot->tts_isnull[dp_off]   = false;
    slot->tts_values[user_off] = meko.user_id;
    slot->tts_isnull[user_off] = false;
    slot->tts_values[ag_off]   = meko.agent_id;
    slot->tts_isnull[ag_off]   = false;
    slot->tts_values[conv_off] = meko.conversation_id;
    slot->tts_isnull[conv_off] = meko.conversation_id_isnull;
}

/*
 * YB: Helper for the SET path: copy the four meko_* columns from an
 * on-disk tuple into a fresh YbMekoDp and hand it off to
 * yb_populate_meko_columns() so the slot-population logic stays in one
 * place. The first three columns are NOT NULL on the table so non-null
 * is asserted; conversation is nullable and its flag is propagated.
 */
void yb_copy_meko_columns_from_tuple(TupleTableSlot *slot,
                                     HeapTuple heap_tuple,
                                     TupleDesc tupdesc,
                                     bool is_edge)
{
    YbMekoDp meko;
    bool isnull;
    int dp_anum   = is_edge ? Anum_ag_label_edge_table_meko_datapack_id
                            : Anum_ag_label_vertex_table_meko_datapack_id;
    int user_anum = is_edge ? Anum_ag_label_edge_table_meko_user_id
                            : Anum_ag_label_vertex_table_meko_user_id;
    int ag_anum   = is_edge ? Anum_ag_label_edge_table_meko_agent_id
                            : Anum_ag_label_vertex_table_meko_agent_id;
    int conv_anum = is_edge ? Anum_ag_label_edge_table_meko_conversation_id
                            : Anum_ag_label_vertex_table_meko_conversation_id;

    meko.datapack_id = heap_getattr(heap_tuple, dp_anum, tupdesc, &isnull);
    Assert(!isnull);
    meko.user_id = heap_getattr(heap_tuple, user_anum, tupdesc, &isnull);
    Assert(!isnull);
    meko.agent_id = heap_getattr(heap_tuple, ag_anum, tupdesc, &isnull);
    Assert(!isnull);
    meko.conversation_id =
        heap_getattr(heap_tuple, conv_anum, tupdesc, &isnull);
    meko.conversation_id_isnull = isnull;

    yb_populate_meko_columns(slot, meko, is_edge);
}

TupleTableSlot *populate_vertex_tts(TupleTableSlot *elemTupleSlot,
                                    agtype_value *id,
                                    agtype_value *properties,
                                    YbMekoDp meko)
{
    bool properties_isnull;

    if (id == NULL)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("vertex id field cannot be NULL")));
    }

    properties_isnull = properties == NULL;

    elemTupleSlot->tts_values[vertex_tuple_id] = GRAPHID_GET_DATUM(id->val.int_value);
    elemTupleSlot->tts_isnull[vertex_tuple_id] = false;

    elemTupleSlot->tts_values[vertex_tuple_properties] =
        AGTYPE_P_GET_DATUM(agtype_value_to_agtype(properties));
    elemTupleSlot->tts_isnull[vertex_tuple_properties] = properties_isnull;

    if (IsYugaByteEnabled())
        yb_populate_meko_columns(elemTupleSlot, meko, false /* is_edge */);

    return elemTupleSlot;
}

TupleTableSlot *populate_edge_tts(
    TupleTableSlot *elemTupleSlot, agtype_value *id, agtype_value *startid,
    agtype_value *endid, agtype_value *properties, YbMekoDp meko)
{
    bool properties_isnull;

    if (id == NULL)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edge id field cannot be NULL")));
    }
    if (startid == NULL)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edge start_id field cannot be NULL")));
    }

    if (endid == NULL)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("edge end_id field cannot be NULL")));
    }

    properties_isnull = properties == NULL;

    elemTupleSlot->tts_values[edge_tuple_id] =
        GRAPHID_GET_DATUM(id->val.int_value);
    elemTupleSlot->tts_isnull[edge_tuple_id] = false;

    elemTupleSlot->tts_values[edge_tuple_start_id] =
        GRAPHID_GET_DATUM(startid->val.int_value);
    elemTupleSlot->tts_isnull[edge_tuple_start_id] = false;

    elemTupleSlot->tts_values[edge_tuple_end_id] =
        GRAPHID_GET_DATUM(endid->val.int_value);
    elemTupleSlot->tts_isnull[edge_tuple_end_id] = false;

    elemTupleSlot->tts_values[edge_tuple_properties] =
        AGTYPE_P_GET_DATUM(agtype_value_to_agtype(properties));
    elemTupleSlot->tts_isnull[edge_tuple_properties] = properties_isnull;

    if (IsYugaByteEnabled())
        yb_populate_meko_columns(elemTupleSlot, meko, true /* is_edge */);

    return elemTupleSlot;
}


/*
 * Find out if the entity still exists. This is for 'implicit' deletion
 * of an entity.
 */
bool entity_exists(EState *estate, Oid graph_oid, graphid id)
{
    label_cache_data *label;
    ScanKeyData scan_keys[1];
    TableScanDesc scan_desc;
    HeapTuple tuple;
    Relation rel;
    bool result = true;

    /*
     * Extract the label id from the graph id and get the table name
     * the entity is part of.
     */
    label = search_label_graph_oid_cache(graph_oid, GET_LABEL_ID(id));

    /* Setup the scan key to be the graphid */
    ScanKeyInit(&scan_keys[0], 1, BTEqualStrategyNumber,
                F_GRAPHIDEQ, GRAPHID_GET_DATUM(id));

    rel = table_open(label->relation, RowExclusiveLock);
    scan_desc = table_beginscan(rel, estate->es_snapshot, 1, scan_keys);

    tuple = heap_getnext(scan_desc, ForwardScanDirection);

    /*
     * If a single tuple was returned, the tuple is still valid, otherwise'
     * set to false.
     */
    if (!HeapTupleIsValid(tuple))
    {
        result = false;
    }

    table_endscan(scan_desc);
    table_close(rel, RowExclusiveLock);

    return result;
}

/*
 * Insert the edge/vertex tuple into the table and indices. Check that the
 * table's constraints have not been violated.
 *
 * This function defaults to, and flags for update, the currentCommandId. If
 * you need to pass a specific cid and avoid using the currentCommandId, use
 * insert_entity_tuple_cid instead.
 */
HeapTuple insert_entity_tuple(ResultRelInfo *resultRelInfo,
                              TupleTableSlot *elemTupleSlot,
                              EState *estate)
{
    return insert_entity_tuple_cid(resultRelInfo, elemTupleSlot, estate,
                                   GetCurrentCommandId(true));
}

static HeapTuple yb_insert_entity_tuple(ResultRelInfo *resultRelInfo,
                                        TupleTableSlot *elemTupleSlot,
                                        EState *estate)
{
    HeapTuple tuple = NULL;
    Relation rel = resultRelInfo->ri_RelationDesc;

    ExecStoreVirtualTuple(elemTupleSlot);
    tuple = ExecFetchSlotHeapTuple(elemTupleSlot, true, NULL);

    tuple->t_tableOid = RelationGetRelid(rel);
    if (rel->rd_att->constr != NULL)
    {
        ExecConstraints(resultRelInfo, elemTupleSlot, estate,
                        NULL /* mtstate */);
    }

    YBCHeapInsert(resultRelInfo, elemTupleSlot, NULL /* blockInsertStmt */,
                  estate, ONCONFLICT_NONE);

    if (YBCRelInfoHasSecondaryIndices(resultRelInfo))
    {
        ExecInsertIndexTuples(resultRelInfo, elemTupleSlot, estate, false,
                              true, NULL, NIL);
    }

    return tuple;
}

/*
 * Insert the edge/vertex tuple into the table and indices. Check that the
 * table's constraints have not been violated.
 *
 * This function uses the passed cid for updates.
 */
HeapTuple insert_entity_tuple_cid(ResultRelInfo *resultRelInfo,
                                  TupleTableSlot *elemTupleSlot,
                                  EState *estate, CommandId cid)
{
    HeapTuple tuple = NULL;

    if (IsYBRelation(resultRelInfo->ri_RelationDesc))
        return yb_insert_entity_tuple(resultRelInfo, elemTupleSlot, estate);

    ExecStoreVirtualTuple(elemTupleSlot);
    tuple = ExecFetchSlotHeapTuple(elemTupleSlot, true, NULL);

    /* Check the constraints of the tuple */
    tuple->t_tableOid = RelationGetRelid(resultRelInfo->ri_RelationDesc);
    if (resultRelInfo->ri_RelationDesc->rd_att->constr != NULL)
    {
        ExecConstraints(resultRelInfo, elemTupleSlot, estate, NULL /* YB: mtstate */);
    }

    /* Insert the tuple normally */
    table_tuple_insert(resultRelInfo->ri_RelationDesc, elemTupleSlot, cid, 0,
                       NULL);

    /* Insert index entries for the tuple */
    if (resultRelInfo->ri_NumIndices > 0)
    {
        ExecInsertIndexTuples(resultRelInfo, elemTupleSlot, estate, false,
                              false, NULL, NIL);
    }

    return tuple;
}

/*
 * YB: Require `key` to be present in `props` as a string and palloc a
 * NUL-terminated copy. Distinguishes "missing" (ERRCODE_NOT_NULL_VIOLATION)
 * from "present but not a string" (ERRCODE_INVALID_TEXT_REPRESENTATION).
 * Caller pfrees the returned cstring.
 */
static char *yb_require_meko_string(agtype *props, const char *key)
{
    agtype_value search_key;
    agtype_value *val;

    search_key.type = AGTV_STRING;
    search_key.val.string.val = (char *) key;
    search_key.val.string.len = strlen(key);
    val = find_agtype_value_from_container(&props->root, AGT_FOBJECT,
                                           &search_key);
    if (val == NULL || val->type == AGTV_NULL)
        ereport(ERROR,
                (errcode(ERRCODE_NOT_NULL_VIOLATION),
                 errmsg("missing required tenant property \"%s\"", key)));
    if (val->type != AGTV_STRING)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("tenant property \"%s\" must be a string", key)));
    return pnstrdup(val->val.string.val, val->val.string.len);
}

/*
 * YB: Look up an optional `key` in `props`. Returns true and sets *out_str
 * to a palloc'd copy when the key is present and string-typed (caller
 * pfrees). Returns false when the key is absent or set to JSON null. A
 * present-but-non-string value raises ERRCODE_INVALID_TEXT_REPRESENTATION.
 */
static bool yb_optional_meko_string(agtype *props, const char *key,
                                    char **out_str)
{
    agtype_value search_key;
    agtype_value *val;

    search_key.type = AGTV_STRING;
    search_key.val.string.val = (char *) key;
    search_key.val.string.len = strlen(key);
    val = find_agtype_value_from_container(&props->root, AGT_FOBJECT,
                                           &search_key);
    if (val == NULL || val->type == AGTV_NULL)
        return false;
    if (val->type != AGTV_STRING)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
                 errmsg("tenant property \"%s\" must be a string", key)));
    *out_str = pnstrdup(val->val.string.val, val->val.string.len);
    return true;
}

/*
 * YB: Run an input function on `str`, but on failure rewrite the error to
 * name the meko_* property that produced it. Without this, uuid_in /
 * textin failures bubble up with no hint about which tenant column was
 * malformed.
 */
static Datum yb_call_meko_input(PGFunction input_func, const char *str,
                                const char *key)
{
    Datum result;

    PG_TRY();
    {
        result = DirectFunctionCall1(input_func, CStringGetDatum(str));
    }
    PG_CATCH();
    {
        ErrorData *edata;
        int sqlerrcode;
        char *original_message;

        MemoryContextSwitchTo(ErrorContext);
        edata = CopyErrorData();
        FlushErrorState();
        sqlerrcode = edata->sqlerrcode;
        original_message = pstrdup(edata->message);
        FreeErrorData(edata);
        ereport(ERROR,
                (errcode(sqlerrcode),
                 errmsg("invalid value for tenant property \"%s\"", key),
                 errdetail("%s", original_message)));
    }
    PG_END_TRY();
    return result;
}

/*
 * Read meko_datapack_id, meko_user_id, meko_agent_id, meko_conversation_id
 * from a vertex or edge properties map and materialize them as column
 * Datums for the caller.
 *
 * The meko_* keys are intentionally left inside the properties map so that
 * Cypher MATCH/MERGE patterns can match on those keys without parser-level
 * changes. Callers that mutate the map (for example SET) are responsible
 * for keeping the map and the columns in sync.
 *
 * meko_datapack_id, meko_user_id and meko_agent_id are NOT NULL on the
 * underlying tables and so must be present in the properties map; this
 * function raises ERRCODE_NOT_NULL_VIOLATION if any of them is missing or
 * if the properties map itself is null/non-object. meko_conversation_id is
 * nullable and reported via conversation_id_isnull.
 */
YbMekoDp yb_extract_meko_columns_from_properties(Datum props_datum,
                                                 bool props_isnull)
{
    YbMekoDp result;
    agtype *props_agtype;
    char *str;

    result.conversation_id = (Datum) 0;
    result.conversation_id_isnull = true;

    if (props_isnull)
        ereport(ERROR,
                (errcode(ERRCODE_NOT_NULL_VIOLATION),
                 errmsg("missing required tenant properties on vertex/edge")));

    props_agtype = DATUM_GET_AGTYPE_P(props_datum);
    if (!AGT_ROOT_IS_OBJECT(props_agtype))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex/edge tenant properties must be an object")));

    /*
     * The three NOT NULL keys must be present and string-typed; the helper
     * raises an appropriate error otherwise. yb_call_meko_input wraps the
     * input function so a malformed value (for example a non-UUID string)
     * fails with the meko_* property name in the message instead of a
     * bare uuid_in() error.
     */
    str = yb_require_meko_string(props_agtype, AG_COLNAME_MEKO_DATAPACK_ID);
    result.datapack_id = yb_call_meko_input(uuid_in, str,
                                            AG_COLNAME_MEKO_DATAPACK_ID);
    pfree(str);

    str = yb_require_meko_string(props_agtype, AG_COLNAME_MEKO_USER_ID);
    result.user_id = yb_call_meko_input(uuid_in, str,
                                        AG_COLNAME_MEKO_USER_ID);
    pfree(str);

    str = yb_require_meko_string(props_agtype, AG_COLNAME_MEKO_AGENT_ID);
    result.agent_id = CStringGetTextDatum(str);
    pfree(str);

    /*
     * meko_conversation_id is nullable. If the key is absent or set to JSON
     * null, leave conversation_id_isnull = true. If it is present, require
     * a string value (anything else is a clear user error).
     */
    if (yb_optional_meko_string(props_agtype,
                                AG_COLNAME_MEKO_CONVERSATION_ID, &str))
    {
        result.conversation_id =
            yb_call_meko_input(uuid_in, str,
                               AG_COLNAME_MEKO_CONVERSATION_ID);
        result.conversation_id_isnull = false;
        pfree(str);
    }

    return result;
}

/*
 * YB has incomplete support for Postgres command counters.
 * This function increments/resets the read time on the tserver as a proxy for
 * command counter increment. This has the side effect of flushing buffered
 * writes in pggate and can potentially cause performance degradation.
 */
void YbCommandCounterIncrement()
{
	(void) YbResetTransactionReadPoint(false /* is_catalog_snapshot */ );

	CommandCounterIncrement(); /* YB */
}
