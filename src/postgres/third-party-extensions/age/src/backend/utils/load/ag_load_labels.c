/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
#include "postgres.h"
#include "executor/spi.h"
#include "catalog/namespace.h"
#include "executor/executor.h"

#include "utils/load/ag_load_labels.h"
#include "utils/load/csv.h"

static void setup_temp_table_for_vertex_ids(char *graph_name);
static void insert_batch_in_temp_table(batch_insert_state *batch_state,
                                       Oid graph_oid, Oid relid);
static void init_vertex_batch_insert(batch_insert_state **batch_state,
                                     char *label_name, Oid graph_oid,
                                     Oid temp_table_relid);
static void finish_vertex_batch_insert(batch_insert_state **batch_state,
                                       char *label_name, Oid graph_oid,
                                       Oid temp_table_relid);
static void insert_vertex_batch(batch_insert_state *batch_state, char *label_name,
                                Oid graph_oid, Oid temp_table_relid);

void vertex_field_cb(void *field, size_t field_len, void *data)
{

    csv_vertex_reader *cr = (csv_vertex_reader *) data;

    if (cr->error)
    {
        cr->error = 1;
        ereport(NOTICE,(errmsg("There is some unknown error")));
    }

    /* check for space to store this field */
    if (cr->cur_field == cr->alloc)
    {
        cr->alloc *= 2;
        cr->fields = realloc(cr->fields, sizeof(char *) * cr->alloc);
        cr->fields_len = realloc(cr->header, sizeof(size_t *) * cr->alloc);
        if (cr->fields == NULL)
        {
            cr->error = 1;
            ereport(ERROR,
                    (errmsg("field_cb: failed to reallocate %zu bytes\n",
                            sizeof(char *) * cr->alloc)));
        }
    }
    cr->fields_len[cr->cur_field] = field_len;
    cr->curr_row_length += field_len;
    cr->fields[cr->cur_field] = strndup((char *) field, field_len);
    cr->cur_field += 1;
}

void vertex_row_cb(int delim __attribute__((unused)), void *data)
{
    csv_vertex_reader *cr = (csv_vertex_reader*)data;
    batch_insert_state *batch_state = cr->batch_state;
    size_t i, n_fields;
    graphid vertex_id;
    int64 entry_id;
    TupleTableSlot *slot;
    TupleTableSlot *temp_id_slot;

    n_fields = cr->cur_field;

    if (cr->row == 0)
    {
        cr->header_num = cr->cur_field;
        cr->header_row_length = cr->curr_row_length;
        cr->header_len = (size_t* )malloc(sizeof(size_t *) * cr->cur_field);
        cr->header = malloc((sizeof (char*) * cr->cur_field));

        for (i = 0; i<cr->cur_field; i++)
        {
            cr->header_len[i] = cr->fields_len[i];
            cr->header[i] = strndup(cr->fields[i], cr->header_len[i]);
        }
    }
    else
    {
        if (cr->id_field_exists)
        {
            entry_id = strtol(cr->fields[0], NULL, 10);
            if (entry_id > cr->curr_seq_num)
            {
                DirectFunctionCall2(setval_oid,
                                    ObjectIdGetDatum(cr->label_seq_relid),
                                    Int64GetDatum(entry_id));
                cr->curr_seq_num = entry_id;
            }
        }
        else
        {
            entry_id = nextval_internal(cr->label_seq_relid, true);
        }

        vertex_id = make_graphid(cr->label_id, entry_id);

        /* Get the appropriate slot from the batch state */
        slot = batch_state->slots[batch_state->num_tuples];
        temp_id_slot = batch_state->temp_id_slots[batch_state->num_tuples];

        /* Clear the slots contents */
        ExecClearTuple(slot);
        ExecClearTuple(temp_id_slot);

        /* Fill the values in the slot */
        slot->tts_values[0] = GRAPHID_GET_DATUM(vertex_id);
        slot->tts_values[1] = AGTYPE_P_GET_DATUM(
                                create_agtype_from_list(cr->header, cr->fields,
                                                        n_fields, entry_id,
                                                        cr->load_as_agtype));
        slot->tts_isnull[0] = false;
        slot->tts_isnull[1] = false;

        temp_id_slot->tts_values[0] = GRAPHID_GET_DATUM(vertex_id);
        temp_id_slot->tts_isnull[0] = false;

        /* Make the slot as containing virtual tuple */
        ExecStoreVirtualTuple(slot);
        ExecStoreVirtualTuple(temp_id_slot);

        batch_state->num_tuples++;

        if (batch_state->num_tuples >= batch_state->max_tuples)
        {
            /* Insert the batch when it is full (i.e. BATCH_SIZE) */
            insert_vertex_batch(batch_state, cr->label_name, cr->graph_oid,
                                cr->temp_table_relid);
            batch_state->num_tuples = 0;
        }
    }

    for (i = 0; i < n_fields; ++i)
    {
        free(cr->fields[i]);
    }

    if (cr->error)
    {
        ereport(NOTICE,(errmsg("THere is some error")));
    }

    cr->cur_field = 0;
    cr->curr_row_length = 0;
    cr->row += 1;
}

static int is_space(unsigned char c)
{
    if (c == CSV_SPACE || c == CSV_TAB)
    {
        return 1;
    }
    else
    {
        return 0;
    }

}
static int is_term(unsigned char c)
{
    if (c == CSV_CR || c == CSV_LF)
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

int create_labels_from_csv_file(char *file_path,
                                char *graph_name,
                                Oid graph_oid,
                                char *label_name,
                                int label_id,
                                bool id_field_exists,
                                bool load_as_agtype)
{

    FILE *fp;
    struct csv_parser p;
    char buf[1024];
    size_t bytes_read;
    unsigned char options = 0;
    csv_vertex_reader cr;
    char *label_seq_name;
    Oid temp_table_relid;

    if (csv_init(&p, options) != 0)
    {
        ereport(ERROR,
                (errmsg("Failed to initialize csv parser\n")));
    }

    temp_table_relid = RelnameGetRelid(GET_TEMP_VERTEX_ID_TABLE(graph_name));
    if (!OidIsValid(temp_table_relid))
    {
        setup_temp_table_for_vertex_ids(graph_name);
        temp_table_relid = RelnameGetRelid(GET_TEMP_VERTEX_ID_TABLE(graph_name));
    }

    csv_set_space_func(&p, is_space);
    csv_set_term_func(&p, is_term);

    fp = fopen(file_path, "rb");
    if (!fp)
    {
        ereport(ERROR,
                (errmsg("Failed to open %s\n", file_path)));
    }

    label_seq_name = get_label_seq_relation_name(label_name);

    memset((void*)&cr, 0, sizeof(csv_vertex_reader));

    cr.alloc = 2048;
    cr.fields = malloc(sizeof(char *) * cr.alloc);
    cr.fields_len = malloc(sizeof(size_t *) * cr.alloc);
    cr.header_row_length = 0;
    cr.curr_row_length = 0;
    cr.graph_name = graph_name;
    cr.graph_oid = graph_oid;
    cr.label_name = label_name;
    cr.label_id = label_id;
    cr.id_field_exists = id_field_exists;
    cr.label_seq_relid = get_relname_relid(label_seq_name, graph_oid);
    cr.load_as_agtype = load_as_agtype;
    cr.temp_table_relid = temp_table_relid;
    
    if (cr.id_field_exists)
    {
        /*
         * Set the curr_seq_num since we will need it to compare with
         * incoming entry_id.
         * 
         * We cant use currval because it will error out if nextval was
         * not called before in the session.
         */
        cr.curr_seq_num = nextval_internal(cr.label_seq_relid, true);
    }

    /* Initialize the batch insert state */
    init_vertex_batch_insert(&cr.batch_state, label_name, graph_oid,
                             cr.temp_table_relid);

    while ((bytes_read=fread(buf, 1, 1024, fp)) > 0)
    {
        if (csv_parse(&p, buf, bytes_read, vertex_field_cb,
                      vertex_row_cb, &cr) != bytes_read)
        {
            ereport(ERROR, (errmsg("Error while parsing file: %s\n",
                                   csv_strerror(csv_error(&p)))));
        }
    }

    csv_fini(&p, vertex_field_cb, vertex_row_cb, &cr);

    /* Finish any remaining batch inserts */
    finish_vertex_batch_insert(&cr.batch_state, label_name, graph_oid,
                               cr.temp_table_relid);

    if (ferror(fp))
    {
        ereport(ERROR, (errmsg("Error while reading file %s\n",
                               file_path)));
    }

    fclose(fp);

    free(cr.fields);
    csv_free(&p);
    return EXIT_SUCCESS;
}

static void insert_vertex_batch(batch_insert_state *batch_state, char *label_name,
                                Oid graph_oid, Oid temp_table_relid)
{
    insert_batch_in_temp_table(batch_state, graph_oid, temp_table_relid);
    insert_batch(batch_state, label_name, graph_oid);
}

/*
 * Create and populate a temporary table with vertex ids that are already
 * present in the graph. This table will be used to check if the new vertex
 * id generated by loader is a duplicate.
 * Unique index is created to enforce uniqueness of the ids.
 * 
 * We dont need this for loading edges since the ids are generated using
 * sequence and are unique.
 */ 
static void setup_temp_table_for_vertex_ids(char *graph_name)
{
    char *create_as_query;
    char *index_query;

    create_as_query = psprintf("CREATE TEMP TABLE IF NOT EXISTS %s AS "
                               "SELECT DISTINCT id FROM \"%s\".%s",
                                GET_TEMP_VERTEX_ID_TABLE(graph_name), graph_name,
                                AG_DEFAULT_LABEL_VERTEX);

    index_query = psprintf("CREATE UNIQUE INDEX ON %s (id)",
                           GET_TEMP_VERTEX_ID_TABLE(graph_name));
    SPI_connect();
    SPI_execute(create_as_query, false, 0);
    SPI_execute(index_query, false, 0);

    SPI_finish();
}

/*
 * Inserts batch of tuples into the temporary table.
 * This function also updates the index to check for
 * uniqueness of the ids.
 */
static void insert_batch_in_temp_table(batch_insert_state *batch_state,
                                       Oid graph_oid, Oid relid)
{
    int i;
    EState *estate;
    ResultRelInfo *resultRelInfo;
    Relation rel;
    List *result;

    rel = table_open(relid, RowExclusiveLock);

    /* Initialize executor state */
    estate = CreateExecutorState();

    /* Initialize result relation information */
    resultRelInfo = makeNode(ResultRelInfo);
    InitResultRelInfo(resultRelInfo, rel, 1, NULL, estate->es_instrument);
    estate->es_result_relations = &resultRelInfo;

    /* Open the indices */
    ExecOpenIndices(resultRelInfo, false);

    /* Insert the batch into the temporary table */
    heap_multi_insert(rel, batch_state->temp_id_slots, batch_state->num_tuples,
                      GetCurrentCommandId(true), 0, NULL);

    for (i = 0; i < batch_state->num_tuples; i++)
    {
        result = ExecInsertIndexTuples(resultRelInfo, batch_state->temp_id_slots[i],
                                       estate, false, true, NULL, NIL);
        /* Check if the unique cnstraint is violated */
        if (list_length(result) != 0)
        {
            Datum id;
            bool isnull;

            id = slot_getattr(batch_state->temp_id_slots[i], 1, &isnull);
            ereport(ERROR, (errmsg("Cannot insert duplicate vertex id: %ld",
                                    DATUM_GET_GRAPHID(id)),
                            errhint("Entry id %ld is already used",
                                    get_graphid_entry_id(id))));
        }
    }
    /* Clean up and close the indices */
    ExecCloseIndices(resultRelInfo);

    FreeExecutorState(estate);
    table_close(rel, RowExclusiveLock);

    CommandCounterIncrement();
}

/*
 * Initialize the batch insert state for vertices.
 */
static void init_vertex_batch_insert(batch_insert_state **batch_state,
                                     char *label_name, Oid graph_oid,
                                     Oid temp_table_relid)
{
    Relation relation;
    Oid relid;

    Relation temp_table_relation;
    int i;

    /* Open a temporary relation to get the tuple descriptor */
    relid = get_label_relation(label_name, graph_oid);
    relation = table_open(relid, AccessShareLock);

    temp_table_relation = table_open(temp_table_relid, AccessShareLock);

    /* Initialize the batch insert state */
    *batch_state = (batch_insert_state *) palloc0(sizeof(batch_insert_state));
    (*batch_state)->max_tuples = BATCH_SIZE;
    (*batch_state)->slots = palloc(sizeof(TupleTableSlot *) * BATCH_SIZE);
    (*batch_state)->temp_id_slots = palloc(sizeof(TupleTableSlot *) * BATCH_SIZE);
    (*batch_state)->num_tuples = 0;

    /* Create slots */
    for (i = 0; i < BATCH_SIZE; i++)
    {
        (*batch_state)->slots[i] = MakeSingleTupleTableSlot(
                                            RelationGetDescr(relation),
                                            &TTSOpsHeapTuple);
        (*batch_state)->temp_id_slots[i] = MakeSingleTupleTableSlot(
                                        RelationGetDescr(temp_table_relation),
                                        &TTSOpsHeapTuple);
    }

    table_close(relation, AccessShareLock);
    table_close(temp_table_relation, AccessShareLock);
}

/*
 * Finish the batch insert for vertices. Insert the
 * remaining tuples in the batch state and clean up.
 */
static void finish_vertex_batch_insert(batch_insert_state **batch_state,
                                       char *label_name, Oid graph_oid,
                                       Oid temp_table_relid)
{
    Relation relation;
    Oid relid;

    Relation temp_table_relation;
    int i;

    if ((*batch_state)->num_tuples > 0)
    {
        insert_vertex_batch(*batch_state, label_name, graph_oid, temp_table_relid);
        (*batch_state)->num_tuples = 0;
    }

    /* Open a temporary relation to ensure resources are properly cleaned up */
    relid = get_label_relation(label_name, graph_oid);
    relation = table_open(relid, AccessShareLock);

    temp_table_relation = table_open(temp_table_relid, AccessShareLock);

    /* Free slots */
    for (i = 0; i < BATCH_SIZE; i++)
    {
        ExecDropSingleTupleTableSlot((*batch_state)->slots[i]);
        ExecDropSingleTupleTableSlot((*batch_state)->temp_id_slots[i]);
    }

    /* Clean up batch state */
    pfree_if_not_null((*batch_state)->slots);
    pfree_if_not_null((*batch_state)->temp_id_slots);
    pfree_if_not_null(*batch_state);
    *batch_state = NULL;

    table_close(relation, AccessShareLock);
    table_close(temp_table_relation, AccessShareLock);
}
