/*
 * Copyright 2020 Bitnine Co., Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "postgres.h"

#include "access/heapam.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "catalog/indexing.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/rel.h"
#include "utils/relcache.h"

#include "catalog/ag_label.h"
#include "utils/ag_cache.h"

// INSERT INTO ag_catalog.ag_label
// VALUES (label_name, label_graph, label_id, label_kind, label_relation)
Oid insert_label(const char *label_name, Oid label_graph, int32 label_id,
                 char label_kind, Oid label_relation)
{
    NameData label_name_data;
    Datum values[Natts_ag_label];
    bool nulls[Natts_ag_label];
    Relation ag_label;
    HeapTuple tuple;
    Oid label_oid;

    /*
     * NOTE: Is it better to make use of label_id and label_kind domain types
     *       than to use assert to check label_id and label_kind are valid?
     */
    AssertArg(label_name);
    AssertArg(OidIsValid(label_graph));
    AssertArg(label_id_is_valid(label_id));
    AssertArg(label_kind == LABEL_KIND_VERTEX ||
              label_kind == LABEL_KIND_EDGE);
    AssertArg(OidIsValid(label_relation));

    namestrcpy(&label_name_data, label_name);
    values[Anum_ag_label_name - 1] = NameGetDatum(&label_name_data);
    nulls[Anum_ag_label_name - 1] = false;

    values[Anum_ag_label_graph - 1] = ObjectIdGetDatum(label_graph);
    nulls[Anum_ag_label_graph - 1] = false;

    values[Anum_ag_label_id - 1] = Int32GetDatum(label_id);
    nulls[Anum_ag_label_id - 1] = false;

    values[Anum_ag_label_kind - 1] = CharGetDatum(label_kind);
    nulls[Anum_ag_label_kind - 1] = false;

    values[Anum_ag_label_relation - 1] = ObjectIdGetDatum(label_relation);
    nulls[Anum_ag_label_relation - 1] = false;

    ag_label = heap_open(ag_label_relation_id(), RowExclusiveLock);

    tuple = heap_form_tuple(RelationGetDescr(ag_label), values, nulls);

    /*
     * CatalogTupleInsert() is originally for PostgreSQL's catalog. However,
     * it is used at here for convenience.
     */
    label_oid = CatalogTupleInsert(ag_label, tuple);

    heap_close(ag_label, RowExclusiveLock);

    return label_oid;
}

Oid get_label_oid(const char *label_name, Oid label_graph)
{
    label_cache_data *cache_data;

    cache_data = search_label_name_graph_cache(label_name, label_graph);
    if (cache_data)
        return cache_data->oid;
    else
        return InvalidOid;
}

bool label_id_exists(Oid label_graph, int32 label_id)
{
    label_cache_data *cache_data;

    cache_data = search_label_graph_id_cache(label_graph, label_id);
    if (cache_data)
        return true;
    else
        return false;
}
