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

#include "utils/load/ag_load_edges.h"
#include "utils/load/csv.h"

void init_edge_batch_insert(batch_insert_state **batch_state,
                            char *label_name, Oid graph_oid);
void finish_edge_batch_insert(batch_insert_state **batch_state,
                              char *label_name, Oid graph_oid);

void edge_field_cb(void *field, size_t field_len, void *data)
{

    csv_edge_reader *cr = (csv_edge_reader*)data;
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
    cr->fields[cr->cur_field] = strndup((char*)field, field_len);
    cr->cur_field += 1;
}

/* Parser calls this function when it detects end of a row */
void edge_row_cb(int delim __attribute__((unused)), void *data)
{

    csv_edge_reader *cr = (csv_edge_reader*)data;
    batch_insert_state *batch_state = cr->batch_state;

    size_t i, n_fields;
    int64 start_id_int;
    graphid start_vertex_graph_id;
    int start_vertex_type_id;

    int64 end_id_int;
    graphid end_vertex_graph_id;
    int end_vertex_type_id;

    graphid edge_id;
    int64 entry_id;
    TupleTableSlot *slot;

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
        entry_id = nextval_internal(cr->label_seq_relid, true);
        edge_id = make_graphid(cr->label_id, entry_id);

        start_id_int = strtol(cr->fields[0], NULL, 10);
        start_vertex_type_id = get_label_id(cr->fields[1], cr->graph_oid);
        end_id_int = strtol(cr->fields[2], NULL, 10);
        end_vertex_type_id = get_label_id(cr->fields[3], cr->graph_oid);

        start_vertex_graph_id = make_graphid(start_vertex_type_id, start_id_int);
        end_vertex_graph_id = make_graphid(end_vertex_type_id, end_id_int);

        /* Get the appropriate slot from the batch state */
        slot = batch_state->slots[batch_state->num_tuples];

        /* Clear the slots contents */
        ExecClearTuple(slot);

        /* Fill the values in the slot */
        slot->tts_values[0] = GRAPHID_GET_DATUM(edge_id);
        slot->tts_values[1] = GRAPHID_GET_DATUM(start_vertex_graph_id);
        slot->tts_values[2] = GRAPHID_GET_DATUM(end_vertex_graph_id);
        slot->tts_values[3] = AGTYPE_P_GET_DATUM(
                                create_agtype_from_list_i(
                                    cr->header, cr->fields,
                                    n_fields, 4, cr->load_as_agtype));
        slot->tts_isnull[0] = false;
        slot->tts_isnull[1] = false;
        slot->tts_isnull[2] = false;
        slot->tts_isnull[3] = false;

        /* Make the slot as containing virtual tuple */
        ExecStoreVirtualTuple(slot);
        batch_state->num_tuples++;

        if (batch_state->num_tuples >= batch_state->max_tuples)
        {
            /* Insert the batch when it is full (i.e. BATCH_SIZE) */
            insert_batch(batch_state, cr->label_name, cr->graph_oid);
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

int create_edges_from_csv_file(char *file_path,
                               char *graph_name,
                               Oid graph_oid,
                               char *label_name,
                               int label_id,
                               bool load_as_agtype)
{

    FILE *fp;
    struct csv_parser p;
    char buf[1024];
    size_t bytes_read;
    unsigned char options = 0;
    csv_edge_reader cr;
    char *label_seq_name;

    if (csv_init(&p, options) != 0)
    {
        ereport(ERROR,
                (errmsg("Failed to initialize csv parser\n")));
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

    memset((void*)&cr, 0, sizeof(csv_edge_reader));
    cr.alloc = 128;
    cr.fields = malloc(sizeof(char *) * cr.alloc);
    cr.fields_len = malloc(sizeof(size_t *) * cr.alloc);
    cr.header_row_length = 0;
    cr.curr_row_length = 0;
    cr.graph_name = graph_name;
    cr.graph_oid = graph_oid;
    cr.label_name = label_name;
    cr.label_id = label_id;
    cr.label_seq_relid = get_relname_relid(label_seq_name, graph_oid);
    cr.load_as_agtype = load_as_agtype;

    /* Initialize the batch insert state */
    init_edge_batch_insert(&cr.batch_state, label_name, graph_oid);

    while ((bytes_read=fread(buf, 1, 1024, fp)) > 0)
    {
        if (csv_parse(&p, buf, bytes_read, edge_field_cb,
                      edge_row_cb, &cr) != bytes_read)
        {
            ereport(ERROR, (errmsg("Error while parsing file: %s\n",
                                   csv_strerror(csv_error(&p)))));
        }
    }

    csv_fini(&p, edge_field_cb, edge_row_cb, &cr);

    /* Finish any remaining batch inserts */
    finish_edge_batch_insert(&cr.batch_state, label_name, graph_oid);

    if (ferror(fp))
    {
        ereport(ERROR, (errmsg("Error while reading file %s\n", file_path)));
    }

    fclose(fp);

    free(cr.fields);
    csv_free(&p);
    return EXIT_SUCCESS;
}

/*
 * Initialize the batch insert state for edges.
 */
void init_edge_batch_insert(batch_insert_state **batch_state,
                            char *label_name, Oid graph_oid)
{
    Relation relation;
    int i;

    // Open a temporary relation to get the tuple descriptor
    relation = table_open(get_label_relation(label_name, graph_oid), AccessShareLock);

    // Initialize the batch insert state
    *batch_state = (batch_insert_state *) palloc0(sizeof(batch_insert_state));
    (*batch_state)->max_tuples = BATCH_SIZE;
    (*batch_state)->slots = palloc(sizeof(TupleTableSlot *) * BATCH_SIZE);
    (*batch_state)->num_tuples = 0;

    // Create slots
    for (i = 0; i < BATCH_SIZE; i++)
    {
        (*batch_state)->slots[i] = MakeSingleTupleTableSlot(
                                            RelationGetDescr(relation),
                                            &TTSOpsHeapTuple);
    }

    table_close(relation, AccessShareLock);
}

/*
 * Finish the batch insert for edges. Insert the
 * remaining tuples in the batch state and clean up.
 */
void finish_edge_batch_insert(batch_insert_state **batch_state,
                              char *label_name, Oid graph_oid)
{
    int i;
    Relation relation;

    if ((*batch_state)->num_tuples > 0)
    {
        insert_batch(*batch_state, label_name, graph_oid);
        (*batch_state)->num_tuples = 0;
    }

    // Open a temporary relation to ensure resources are properly cleaned up
    relation = table_open(get_label_relation(label_name, graph_oid), AccessShareLock);

    // Free slots
    for (i = 0; i < BATCH_SIZE; i++)
    {
        ExecDropSingleTupleTableSlot((*batch_state)->slots[i]);
    }

    // Clean up batch state
    pfree((*batch_state)->slots);
    pfree(*batch_state);
    *batch_state = NULL;

    table_close(relation, AccessShareLock);
}