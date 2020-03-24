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

#include "catalog/dependency.h"
#include "catalog/objectaccess.h"
#include "catalog/pg_class_d.h"
#include "catalog/pg_namespace_d.h"
#include "utils/lsyscache.h"

#include "catalog/ag_catalog.h"
#include "catalog/ag_namespace.h"
#include "utils/ag_cache.h"

static object_access_hook_type prev_object_access_hook;

static void object_access(ObjectAccessType access, Oid class_id, Oid object_id,
                          int sub_id, void *arg);

void object_access_hook_init(void)
{
    prev_object_access_hook = object_access_hook;
    object_access_hook = object_access;
}

void object_access_hook_fini(void)
{
    object_access_hook = prev_object_access_hook;
}

/*
 * object_access_hook is called before actual deletion. So, looking up ag_cache
 * is still valid at this point. For labels, once a backed table is deleted,
 * its corresponding ag_label cache entry will be removed by cache
 * invalidation.
 */
static void object_access(ObjectAccessType access, Oid class_id, Oid object_id,
                          int sub_id, void *arg)
{
    ObjectAccessDrop *drop_arg;

    // We are interested in DROP SCHEMA and DROP TABLE commands.
    if (access != OAT_DROP)
        return;

    drop_arg = arg;

    /*
     * PERFORM_DELETION_INTERNAL flag will be set when remove_schema() calls
     * performDeletion(). However, if PostgreSQL does performDeletion() with
     * PERFORM_DELETION_INTERNAL flag over backed schemas of graphs due to
     * side effects of other commands run by user, it is impossible to
     * distinguish between this and drop_graph().
     *
     * The above applies to DROP TABLE command too.
     */

    if (class_id == NamespaceRelationId)
    {
        graph_cache_data *cache_data;

        if (drop_arg->dropflags & PERFORM_DELETION_INTERNAL)
            return;

        cache_data = search_graph_namespace_cache(object_id);
        if (cache_data)
        {
            char *nspname = get_namespace_name(object_id);

            ereport(ERROR, (errcode(ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST),
                            errmsg("schema \"%s\" is for graph \"%s\"",
                                   nspname, NameStr(cache_data->name))));
        }

        return;
    }

    if (class_id == RelationRelationId)
    {
        label_cache_data *cache_data;

        if (drop_arg->dropflags & PERFORM_DELETION_INTERNAL)
            return;

        cache_data = search_label_relation_cache(object_id);
        if (cache_data)
        {
            char *relname = get_rel_name(object_id);

            ereport(ERROR, (errcode(ERRCODE_DEPENDENT_OBJECTS_STILL_EXIST),
                            errmsg("table \"%s\" is for label \"%s\"",
                                   relname, NameStr(cache_data->name))));
        }

        return;
    }
}

Oid ag_relation_id(const char *name, const char *kind)
{
    Oid id;

    id = get_relname_relid(name, ag_catalog_namespace_id());
    if (!OidIsValid(id))
    {
        ereport(ERROR, (errcode(ERRCODE_UNDEFINED_TABLE),
                        errmsg("%s \"%s\" does not exist", kind, name)));
    }

    return id;
}
