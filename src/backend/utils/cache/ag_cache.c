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
#include "access/tupdesc.h"
#include "fmgr.h"
#include "storage/lockdefs.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"
#include "utils/syscache.h"

#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"
#include "utils/ag_cache.h"

typedef struct graph_name_cache_entry
{
    NameData name; // hash key
    graph_cache_data data;
} graph_name_cache_entry;

typedef struct label_name_graph_cache_key
{
    NameData name;
    Oid graph;
} label_name_graph_cache_key;

typedef struct label_name_graph_cache_entry
{
    label_name_graph_cache_key key; // hash key
    label_cache_data data;
} label_name_graph_cache_entry;

// ag_graph.name
static HTAB *graph_name_cache_hash = NULL;
static ScanKeyData graph_name_scan_keys[1];

// ag_label.name
static HTAB *label_name_graph_cache_hash = NULL;
static ScanKeyData label_name_graph_scan_keys[2];

// initialize all caches
static void initialize_caches(void);

// common
static int name_hash_compare(const void *key1, const void *key2, Size keysize);

// ag_graph
static void initialize_graph_caches(void);
static void create_graph_caches(void);
static void create_graph_name_cache(void);
static void invalidate_graph_caches(Datum arg, int cache_id,
                                    uint32 hash_value);
static void flush_graph_name_cache(void);
static graph_cache_data *search_graph_name_cache_miss(Name name);
static void fill_graph_cache_data(graph_cache_data *cache_data,
                                  HeapTuple tuple, TupleDesc tuple_desc);

// ag_label
static void initialize_label_caches(void);
static void create_label_caches(void);
static void create_label_name_graph_cache(void);
static void invalidate_label_caches(Datum arg, Oid relid);
static void invalidate_label_graph_name_cache(Oid relid);
static void flush_label_name_graph_cache(void);
static label_cache_data *search_label_name_graph_cache_miss(Name name,
                                                            Oid graph);
static void *label_name_graph_cache_hash_search(Name name, Oid graph,
                                                HASHACTION action,
                                                bool *found);
static void fill_label_cache_data(label_cache_data *cache_data,
                                  HeapTuple tuple, TupleDesc tuple_desc);

static void initialize_caches(void)
{
    static bool initialized = false;

    if (initialized)
        return;

    if (!CacheMemoryContext)
        CreateCacheMemoryContext();

    initialize_graph_caches();
    initialize_label_caches();

    initialized = true;
}

static int name_hash_compare(const void *key1, const void *key2, Size keysize)
{
    Name name1 = (Name)key1;
    Name name2 = (Name)key2;

    // keysize parameter is superfluous here
    AssertArg(keysize == NAMEDATALEN);

    return strncmp(NameStr(*name1), NameStr(*name2), NAMEDATALEN);
}

static void initialize_graph_caches(void)
{
    graph_name_scan_keys[0].sk_flags = 0;
    graph_name_scan_keys[0].sk_attno = Anum_ag_graph_name;
    graph_name_scan_keys[0].sk_strategy = BTEqualStrategyNumber;
    graph_name_scan_keys[0].sk_collation = InvalidOid;
    fmgr_info_cxt(F_NAMEEQ, &graph_name_scan_keys[0].sk_func,
                  CacheMemoryContext);
    graph_name_scan_keys[0].sk_collation = InvalidOid;

    create_graph_caches();

    /*
     * A graph is backed by the bound namespace. So, register the invalidation
     * logic of the graph caches for invalidation events of NAMESPACEOID cache.
     */
    CacheRegisterSyscacheCallback(NAMESPACEOID, invalidate_graph_caches,
                                  (Datum)0);
}

static void create_graph_caches(void)
{
    /*
     * All the hash tables are created using their dedicated memory contexts
     * which are under TopMemoryContext.
     */
    create_graph_name_cache();
}

static void create_graph_name_cache(void)
{
    HASHCTL hash_ctl;

    MemSet(&hash_ctl, 0, sizeof(hash_ctl));
    hash_ctl.keysize = sizeof(NameData);
    hash_ctl.entrysize = sizeof(graph_name_cache_entry);
    hash_ctl.match = name_hash_compare;

    /*
     * Please see the comment of hash_create() for the nelem value 16 here.
     * HASH_BLOBS flag is set because the key for this hash is fixed-size.
     */
    graph_name_cache_hash = hash_create("ag_graph (name) cache", 16, &hash_ctl,
                                        HASH_ELEM | HASH_BLOBS | HASH_COMPARE);
}

static void invalidate_graph_caches(Datum arg, int cache_id, uint32 hash_value)
{
    Assert(graph_name_cache_hash);

    /*
     * Currently, all entries in the graph caches are flushed because
     * hash_value is for an entry in NAMESPACEOID cache. Since the caches
     * are not currently used in performance-critical paths, this seems OK.
     */
    flush_graph_name_cache();
}

static void flush_graph_name_cache(void)
{
    HASH_SEQ_STATUS hash_seq;

    hash_seq_init(&hash_seq, graph_name_cache_hash);
    for (;;)
    {
        graph_name_cache_entry *entry;
        void *removed;

        entry = (graph_name_cache_entry *)hash_seq_search(&hash_seq);
        if (!entry)
            break;

        removed = hash_search(graph_name_cache_hash, &entry->name, HASH_REMOVE,
                              NULL);
        if (!removed)
            ereport(ERROR, (errmsg_internal("graph (name) cache corrupted")));
    }
}

graph_cache_data *search_graph_name_cache(Name name)
{
    graph_name_cache_entry *entry;

    initialize_caches();

    entry = hash_search(graph_name_cache_hash, name, HASH_FIND, NULL);
    if (entry)
        return &entry->data;

    return search_graph_name_cache_miss(name);
}

static graph_cache_data *search_graph_name_cache_miss(Name name)
{
    ScanKeyData scan_keys[1];
    Relation ag_graph;
    SysScanDesc scan_desc;
    HeapTuple tuple;
    bool found;
    graph_name_cache_entry *entry;

    memcpy(scan_keys, graph_name_scan_keys, sizeof(graph_name_scan_keys));
    scan_keys[0].sk_argument = NameGetDatum(name);

    /*
     * Calling heap_open() might call AcceptInvalidationMessage() and that
     * might flush the graph caches. This is OK because this function is called
     * when the desired entry is not in the cache.
     */
    ag_graph = heap_open(ag_graph_relation_id(), AccessShareLock);
    scan_desc = systable_beginscan(ag_graph, ag_graph_name_index_id(), true,
                                   NULL, 1, scan_keys);

    // don't need to loop over scan_desc because ag_graph_name_index is UNIQUE
    tuple = systable_getnext(scan_desc);
    if (!HeapTupleIsValid(tuple))
    {
        systable_endscan(scan_desc);
        heap_close(ag_graph, AccessShareLock);

        return NULL;
    }

    // get a new entry
    entry = hash_search(graph_name_cache_hash, name, HASH_ENTER, &found);
    Assert(!found); // no concurrent update on graph_name_cache_hash

    // fill the new entry with the retrieved tuple
    fill_graph_cache_data(&entry->data, tuple, RelationGetDescr(ag_graph));

    systable_endscan(scan_desc);
    heap_close(ag_graph, AccessShareLock);

    return &entry->data;
}

static void fill_graph_cache_data(graph_cache_data *cache_data,
                                  HeapTuple tuple, TupleDesc tuple_desc)
{
    bool is_null;
    Datum value;

    // ag_graph.oid
    value = heap_getattr(tuple, ObjectIdAttributeNumber, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->oid = DatumGetObjectId(value);
    // ag_graph.name
    value = heap_getattr(tuple, Anum_ag_graph_name, tuple_desc, &is_null);
    Assert(!is_null);
    namecpy(&cache_data->name, DatumGetName(value));
    // ag_graph.namespace
    value = heap_getattr(tuple, Anum_ag_graph_namespace, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->namespace = DatumGetObjectId(value);
}

static void initialize_label_caches(void)
{
    label_name_graph_scan_keys[0].sk_flags = 0;
    label_name_graph_scan_keys[0].sk_attno = Anum_ag_label_name;
    label_name_graph_scan_keys[0].sk_strategy = BTEqualStrategyNumber;
    label_name_graph_scan_keys[0].sk_collation = InvalidOid;
    fmgr_info_cxt(F_NAMEEQ, &label_name_graph_scan_keys[0].sk_func,
                  CacheMemoryContext);
    label_name_graph_scan_keys[0].sk_collation = InvalidOid;
    label_name_graph_scan_keys[1].sk_flags = 0;
    label_name_graph_scan_keys[1].sk_attno = Anum_ag_label_graph;
    label_name_graph_scan_keys[1].sk_strategy = BTEqualStrategyNumber;
    label_name_graph_scan_keys[1].sk_collation = InvalidOid;
    fmgr_info_cxt(F_OIDEQ, &label_name_graph_scan_keys[1].sk_func,
                  CacheMemoryContext);
    label_name_graph_scan_keys[1].sk_collation = InvalidOid;

    create_label_caches();

    /*
     * A label is backed by the bound relation. So, register the invalidation
     * logic of the label caches for invalidation events of relation cache.
     */
    CacheRegisterRelcacheCallback(invalidate_label_caches, (Datum)0);
}

static void create_label_caches(void)
{
    /*
     * All the hash tables are created using their dedicated memory contexts
     * which are under TopMemoryContext.
     */
    create_label_name_graph_cache();
}

static void create_label_name_graph_cache(void)
{
    HASHCTL hash_ctl;

    MemSet(&hash_ctl, 0, sizeof(hash_ctl));
    hash_ctl.keysize = sizeof(label_name_graph_cache_key);
    hash_ctl.entrysize = sizeof(label_name_graph_cache_entry);

    /*
     * Please see the comment of hash_create() for the nelem value 16 here.
     * HASH_BLOBS flag is set because the key for this hash is fixed-size.
     */
    label_name_graph_cache_hash = hash_create("ag_label (name) cache", 16,
                                              &hash_ctl,
                                              HASH_ELEM | HASH_BLOBS);
}

static void invalidate_label_caches(Datum arg, Oid relid)
{
    Assert(label_name_graph_cache_hash);

    if (OidIsValid(relid))
        invalidate_label_graph_name_cache(relid);
    else
        flush_label_name_graph_cache();
}

static void invalidate_label_graph_name_cache(Oid relid)
{
    HASH_SEQ_STATUS hash_seq;

    hash_seq_init(&hash_seq, label_name_graph_cache_hash);
    for (;;)
    {
        label_name_graph_cache_entry *entry;
        void *removed;

        entry = (label_name_graph_cache_entry *)hash_seq_search(&hash_seq);
        if (!entry)
            break;

        if (entry->data.relation != relid)
            continue;

        removed = hash_search(label_name_graph_cache_hash, &entry->key,
                              HASH_REMOVE, NULL);
        hash_seq_term(&hash_seq);

        if (!removed)
        {
            ereport(ERROR,
                    (errmsg_internal("label (name, graph) cache corrupted")));
        }

        break;
    }
}

static void flush_label_name_graph_cache(void)
{
    HASH_SEQ_STATUS hash_seq;

    hash_seq_init(&hash_seq, label_name_graph_cache_hash);
    for (;;)
    {
        label_name_graph_cache_entry *entry;
        void *removed;

        entry = (label_name_graph_cache_entry *)hash_seq_search(&hash_seq);
        if (!entry)
            break;

        removed = hash_search(label_name_graph_cache_hash, &entry->key,
                              HASH_REMOVE, NULL);
        if (!removed)
        {
            ereport(ERROR,
                    (errmsg_internal("label (name, graph) cache corrupted")));
        }
    }
}

label_cache_data *search_label_name_graph_cache(Name name, Oid graph)
{
    label_name_graph_cache_entry *entry;

    initialize_caches();

    entry = label_name_graph_cache_hash_search(name, graph, HASH_FIND, NULL);
    if (entry)
        return &entry->data;

    return search_label_name_graph_cache_miss(name, graph);
}

static label_cache_data *search_label_name_graph_cache_miss(Name name,
                                                            Oid graph)
{
    ScanKeyData scan_keys[2];
    Relation ag_label;
    SysScanDesc scan_desc;
    HeapTuple tuple;
    bool found;
    label_name_graph_cache_entry *entry;

    memcpy(scan_keys, label_name_graph_scan_keys,
           sizeof(label_name_graph_scan_keys));
    scan_keys[0].sk_argument = NameGetDatum(name);
    scan_keys[1].sk_argument = ObjectIdGetDatum(graph);

    /*
     * Calling heap_open() might call AcceptInvalidationMessage() and that
     * might invalidate the label caches. This is OK because this function is
     * called when the desired entry is not in the cache.
     */
    ag_label = heap_open(ag_label_relation_id(), AccessShareLock);
    scan_desc = systable_beginscan(ag_label, ag_label_name_graph_index_id(),
                                   true, NULL, 2, scan_keys);

    /*
     * don't need to loop over scan_desc because ag_label_name_graph_index is
     * UNIQUE
     */
    tuple = systable_getnext(scan_desc);
    if (!HeapTupleIsValid(tuple))
    {
        systable_endscan(scan_desc);
        heap_close(ag_label, AccessShareLock);

        return NULL;
    }

    // get a new entry
    entry = label_name_graph_cache_hash_search(name, graph, HASH_ENTER,
                                               &found);
    Assert(!found); // no concurrent update on label_name_graph_cache_hash

    // fill the new entry with the retrieved tuple
    fill_label_cache_data(&entry->data, tuple, RelationGetDescr(ag_label));

    systable_endscan(scan_desc);
    heap_close(ag_label, AccessShareLock);

    return &entry->data;
}

static void *label_name_graph_cache_hash_search(Name name, Oid graph,
                                                HASHACTION action, bool *found)
{
    label_name_graph_cache_key key;

    // initialize the hash key for label_name_graph_cache_hash
    namecpy(&key.name, name);
    key.graph = graph;

    return hash_search(label_name_graph_cache_hash, &key, action, found);
}

static void fill_label_cache_data(label_cache_data *cache_data,
                                  HeapTuple tuple, TupleDesc tuple_desc)
{
    bool is_null;
    Datum value;

    // ag_label.oid
    value = heap_getattr(tuple, ObjectIdAttributeNumber, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->oid = DatumGetObjectId(value);
    // ag_label.name
    value = heap_getattr(tuple, Anum_ag_label_name, tuple_desc, &is_null);
    Assert(!is_null);
    namecpy(&cache_data->name, DatumGetName(value));
    // ag_label.graph
    value = heap_getattr(tuple, Anum_ag_label_graph, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->graph = DatumGetObjectId(value);
    // ag_label.id
    value = heap_getattr(tuple, Anum_ag_label_id, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->id = DatumGetInt32(value);
    // ag_label.kind
    value = heap_getattr(tuple, Anum_ag_label_kind, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->kind = DatumGetChar(value);
    // ag_label.relation
    value = heap_getattr(tuple, Anum_ag_label_relation, tuple_desc, &is_null);
    Assert(!is_null);
    cache_data->relation = DatumGetObjectId(value);
}
