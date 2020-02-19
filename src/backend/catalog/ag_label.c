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

#include "access/genam.h"
#include "access/heapam.h"
#include "access/htup.h"
#include "access/htup_details.h"
#include "access/skey.h"
#include "access/stratnum.h"
#include "access/sysattr.h"
#include "catalog/indexing.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/rel.h"
#include "utils/relcache.h"

#include "catalog/ag_catalog.h"
#include "catalog/ag_label.h"

#define Anum_ag_label_name 1
#define Anum_ag_label_graph 2
#define Anum_ag_label_id 3
#define Anum_ag_label_kind 4
#define Anum_ag_label_relation 5

#define Natts_ag_label 5

#define ag_label_relation_id() ag_relation_id("ag_label", "table")
#define ag_label_name_graph_index_id() \
    ag_relation_id("ag_label_name_graph_index", "index")

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

    AssertArg(label_name);
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
    NameData label_name_key;
    ScanKeyData scan_keys[2];
    Relation ag_label;
    SysScanDesc scan_desc;
    HeapTuple tuple;
    Oid label_oid;

    AssertArg(label_name);
    AssertArg(OidIsValid(label_graph));

    namestrcpy(&label_name_key, label_name);
    ScanKeyInit(&scan_keys[0], Anum_ag_label_name, BTEqualStrategyNumber,
                F_NAMEEQ, NameGetDatum(&label_name_key));
    ScanKeyInit(&scan_keys[1], Anum_ag_label_graph, BTEqualStrategyNumber,
                F_OIDEQ, ObjectIdGetDatum(label_graph));

    ag_label = heap_open(ag_label_relation_id(), AccessShareLock);
    scan_desc = systable_beginscan(ag_label, ag_label_name_graph_index_id(),
                                   true, NULL, 2, scan_keys);

    tuple = systable_getnext(scan_desc);
    if (HeapTupleIsValid(tuple))
    {
        bool is_null;
        Datum value;

        value = heap_getsysattr(tuple, ObjectIdAttributeNumber,
                                RelationGetDescr(ag_label), &is_null);
        Assert(!is_null);

        label_oid = DatumGetObjectId(value);
    }
    else
    {
        label_oid = InvalidOid;
    }

    systable_endscan(scan_desc);
    heap_close(ag_label, AccessShareLock);

    return label_oid;
}
