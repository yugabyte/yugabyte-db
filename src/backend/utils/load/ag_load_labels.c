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

#include "utils/load/ag_load_labels.h"
#include "utils/load/age_load.h"
#include "utils/load/csv.h"

void vertex_field_cb(void *field, size_t field_len, void *data)
{

    csv_vertex_reader *cr = (csv_vertex_reader *) data;

    if (cr->error)
    {
        cr->error = 1;
        ereport(NOTICE,(errmsg("There is some unknown error")));
    }

    // check for space to store this field
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
    agtype *props = NULL;
    size_t i, n_fields;
    graphid object_graph_id;
    int64 label_id_int;

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
            label_id_int = strtol(cr->fields[0], NULL, 10);
        }
        else
        {
            label_id_int = (int64)cr->row;
        }

        object_graph_id = make_graphid(cr->object_id, label_id_int);

        props = create_agtype_from_list(cr->header, cr->fields,
                                        n_fields, label_id_int);
        insert_vertex_simple(cr->graph_oid, cr->object_name,
                             object_graph_id, props);
        pfree(props);
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
                                char *object_name,
                                int object_id,
                                bool id_field_exists)
{

    FILE *fp;
    struct csv_parser p;
    char buf[1024];
    size_t bytes_read;
    unsigned char options = 0;
    csv_vertex_reader cr;

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


    memset((void*)&cr, 0, sizeof(csv_vertex_reader));

    cr.alloc = 2048;
    cr.fields = malloc(sizeof(char *) * cr.alloc);
    cr.fields_len = malloc(sizeof(size_t *) * cr.alloc);
    cr.header_row_length = 0;
    cr.curr_row_length = 0;
    cr.graph_name = graph_name;
    cr.graph_oid = graph_oid;
    cr.object_name = object_name;
    cr.object_id = object_id;
    cr.id_field_exists = id_field_exists;



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
