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

/*
 * I/O routines for agtype type
 *
 * Portions Copyright (c) 2014-2018, PostgreSQL Global Development Group
 */

#include "postgres.h"
#include "utils/jsonfuncs.h"

#include <float.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/namespace.h"
#include "catalog/pg_collation_d.h"
#include "catalog/pg_operator_d.h"
#include "funcapi.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "parser/parse_coerce.h"
#include "utils/builtins.h"
#include "utils/float.h"
#include "utils/lsyscache.h"
#include "utils/snapmgr.h"
#include "utils/typcache.h"
#include "utils/age_vle.h"
#include "utils/agtype_parser.h"
#include "utils/ag_float8_supp.h"
#include "utils/agtype_raw.h"
#include "catalog/ag_graph.h"
#include "catalog/ag_label.h"

/* State structure for Percentile aggregate functions */
typedef struct PercentileGroupAggState
{
    /* percentile value */
    float8 percentile;
    /* Sort object we're accumulating data in: */
    Tuplesortstate *sortstate;
    /* Number of normal rows inserted into sortstate: */
    int64 number_of_rows;
    /* Have we already done tuplesort_performsort? */
    bool sort_done;
} PercentileGroupAggState;

typedef enum /* type categories for datum_to_agtype */
{
    AGT_TYPE_NULL, /* null, so we didn't bother to identify */
    AGT_TYPE_BOOL, /* boolean (built-in types only) */
    AGT_TYPE_INTEGER, /* Cypher Integer type */
    AGT_TYPE_FLOAT, /* Cypher Float type */
    AGT_TYPE_NUMERIC, /* numeric (ditto) */
    AGT_TYPE_DATE, /* we use special formatting for datetimes */
    AGT_TYPE_TIMESTAMP, /* we use special formatting for timestamp */
    AGT_TYPE_TIMESTAMPTZ, /* ... and timestamptz */
    AGT_TYPE_AGTYPE, /* AGTYPE */
    AGT_TYPE_JSON, /* JSON */
    AGT_TYPE_JSONB, /* JSONB */
    AGT_TYPE_ARRAY, /* array */
    AGT_TYPE_COMPOSITE, /* composite */
    AGT_TYPE_JSONCAST, /* something with an explicit cast to JSON */
    AGT_TYPE_VERTEX,
    AGT_TYPE_OTHER /* all else */
} agt_type_category;

static inline Datum agtype_from_cstring(char *str, int len);
size_t check_string_length(size_t len);
static void agtype_in_agtype_annotation(void *pstate, char *annotation);
static void agtype_in_object_start(void *pstate);
static void agtype_in_object_end(void *pstate);
static void agtype_in_array_start(void *pstate);
static void agtype_in_array_end(void *pstate);
static void agtype_in_object_field_start(void *pstate, char *fname,
                                         bool isnull);
static void agtype_put_escaped_value(StringInfo out, agtype_value *scalar_val,
                                     bool extend);
static void escape_agtype(StringInfo buf, const char *str);
bool is_decimal_needed(char *numstr);
static void agtype_in_scalar(void *pstate, char *token,
                             agtype_token_type tokentype,
                             char *annotation);
static void agtype_categorize_type(Oid typoid, agt_type_category *tcategory,
                                   Oid *outfuncoid);
static void composite_to_agtype(Datum composite, agtype_in_state *result);
static void array_dim_to_agtype(agtype_in_state *result, int dim, int ndims,
                                int *dims, Datum *vals, bool *nulls,
                                int *valcount, agt_type_category tcategory,
                                Oid outfuncoid);
static void array_to_agtype_internal(Datum array, agtype_in_state *result);
static void datum_to_agtype(Datum val, bool is_null, agtype_in_state *result,
                            agt_type_category tcategory, Oid outfuncoid,
                            bool key_scalar);
static char *agtype_to_cstring_worker(StringInfo out, agtype_container *in,
                                      int estimated_len, bool indent,
                                      bool extend);
static text *agtype_value_to_text(agtype_value *scalar_val,
                                  bool err_not_scalar);
static void add_indent(StringInfo out, bool indent, int level);
static void cannot_cast_agtype_value(enum agtype_value_type type,
                                     const char *sqltype);
static bool agtype_extract_scalar(agtype_container *agtc, agtype_value *res);
static agtype_value *execute_array_access_operator(agtype *array,
                                                   agtype_value *array_value,
                                                   agtype *array_index);
static agtype_value *execute_array_access_operator_internal(agtype *array,
                                                            agtype_value *array_value,
                                                            int64 array_index);
static agtype_value *execute_map_access_operator(agtype *map,
                                                 agtype_value* map_value,
                                                 agtype *key);
static agtype_value *execute_map_access_operator_internal(agtype *map,
                                                          agtype_value *map_value,
                                                          char *key,
                                                          int key_len);
static Datum agtype_object_field_impl(FunctionCallInfo fcinfo,
                                      agtype *agtype_in,
                                      char *key, int key_len, bool as_text);
static Datum agtype_array_element_impl(FunctionCallInfo fcinfo,
                                       agtype *agtype_in, int element,
                                       bool as_text);
static Datum process_access_operator_result(FunctionCallInfo fcinfo,
                                            agtype_value *agtv,
                                            bool as_text);
/* typecast functions */
static void agtype_typecast_object(agtype_in_state *state, char *annotation);
static void agtype_typecast_array(agtype_in_state *state, char *annotation);
/* validation functions */
static bool is_object_vertex(agtype_value *agtv);
static bool is_object_edge(agtype_value *agtv);
static bool is_array_path(agtype_value *agtv);
/* graph entity retrieval */
static Datum get_vertex(const char *graph, const char *vertex_label,
                        int64 graphid);
static char *get_label_name(const char *graph_name, graphid element_graphid);
static float8 get_float_compatible_arg(Datum arg, Oid type, char *funcname,
                                       bool *is_null);
static Numeric get_numeric_compatible_arg(Datum arg, Oid type, char *funcname,
                                          bool *is_null,
                                          enum agtype_value_type *ag_type);
agtype *get_one_agtype_from_variadic_args(FunctionCallInfo fcinfo,
                                          int variadic_offset,
                                          int expected_nargs);

static int64 get_int64_from_int_datums(Datum d, Oid type, char *funcname,
                                       bool *is_agnull);

static agtype_iterator *get_next_object_key(agtype_iterator *it,
                                            agtype_container *agtc,
                                            agtype_value *key);
static int extract_variadic_args_min(FunctionCallInfo fcinfo,
                                     int variadic_start, bool convert_unknown,
                                     Datum **args, Oid **types, bool **nulls,
                                     int min_num_args);
static agtype_value *agtype_build_map_as_agtype_value(FunctionCallInfo fcinfo);
agtype_value *agtype_composite_to_agtype_value_binary(agtype *a);
static agtype_value *tostring_helper(Datum arg, Oid type, char *msghdr);

/*
 * Due to how pfree can be implemented, it may not check for a passed NULL. This
 * wrapper does just that, it will only call pfree is the pointer passed is not
 * NULL.
 */
void pfree_if_not_null(void *ptr)
{
    if (ptr != NULL)
    {
        pfree(ptr);
    }
}

/* global storage of  OID for agtype and _agtype */
static Oid g_AGTYPEOID = InvalidOid;
static Oid g_AGTYPEARRAYOID = InvalidOid;

/* helper function to quickly set, if necessary, and retrieve AGTYPEOID */
Oid get_AGTYPEOID(void)
{
    if (g_AGTYPEOID == InvalidOid)
    {
        g_AGTYPEOID = GetSysCacheOid2(TYPENAMENSP, Anum_pg_type_oid,
                                      CStringGetDatum("agtype"),
                                      ObjectIdGetDatum(ag_catalog_namespace_id()));
    }

    return g_AGTYPEOID;
}

/* helper function to quickly set, if necessary, and retrieve AGTYPEARRAYOID */
Oid get_AGTYPEARRAYOID(void)
{
    if (g_AGTYPEARRAYOID == InvalidOid)
    {
        g_AGTYPEARRAYOID = GetSysCacheOid2(TYPENAMENSP,Anum_pg_type_oid,
                                           CStringGetDatum("_agtype"),
                                           ObjectIdGetDatum(ag_catalog_namespace_id()));
    }

    return g_AGTYPEARRAYOID;
}

/* helper function to clear the AGTYPEOIDs after a drop extension */
void clear_global_Oids_AGTYPE(void)
{
    g_AGTYPEOID = InvalidOid;
    g_AGTYPEARRAYOID = InvalidOid;
}

/* fast helper function to test for AGTV_NULL in an agtype */
bool is_agtype_null(agtype *agt_arg)
{
    agtype_container *agtc = &agt_arg->root;

    if (AGTYPE_CONTAINER_IS_SCALAR(agtc) &&
            AGTE_IS_NULL(agtc->children[0]))
    {
        return true;
    }
    return false;
}

/*
 * graphid_recv - converts external binary format to a graphid.
 *
 * Copied from PGs int8recv as a graphid is an int64.
 */

PG_FUNCTION_INFO_V1(graphid_recv);

Datum graphid_recv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo) PG_GETARG_POINTER(0);
    int64 result = pq_getmsgint64(buf);

    PG_FREE_IF_COPY(buf, 0);

    PG_RETURN_INT64(result);
}

/*
 * graphid_send - converts a graphid to binary format.
 *
 * Copied from PGs int8send as a graphid is an int64.
 */

PG_FUNCTION_INFO_V1(graphid_send);

Datum graphid_send(PG_FUNCTION_ARGS)
{
    int64 arg1 = PG_GETARG_INT64(0);
    StringInfoData buf;

    pq_begintypsend(&buf);
    pq_sendint64(&buf, arg1);
    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

/*
 * agtype recv function copied from PGs jsonb_recv as agtype is based
 * off of jsonb
 *
 * The type is sent as text in binary mode, so this is almost the same
 * as the input function, but it's prefixed with a version number so we
 * can change the binary format sent in future if necessary. For now,
 * only version 1 is supported.
 */
PG_FUNCTION_INFO_V1(agtype_recv);

Datum agtype_recv(PG_FUNCTION_ARGS)
{
    StringInfo buf = (StringInfo) PG_GETARG_POINTER(0);
    int version = pq_getmsgint(buf, 1);
    char *str = NULL;
    int nbytes = 0;
    Datum result;

    if (version == 1)
    {
        str = pq_getmsgtext(buf, buf->len - buf->cursor, &nbytes);
    }
    else
    {
        elog(ERROR, "unsupported agtype version number %d", version);
    }

    result = agtype_from_cstring(str, nbytes);

    PG_FREE_IF_COPY(buf, 0);
    pfree_if_not_null(str);

    return result;
}

/*
 * agtype send function copied from PGs jsonb_send as agtype is based
 * off of jsonb
 *
 * Just send agtype as a version number, then a string of text
 */
PG_FUNCTION_INFO_V1(agtype_send);

Datum agtype_send(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    StringInfoData buf;
    StringInfo agtype_text = makeStringInfo();
    int version = 1;

    (void) agtype_to_cstring(agtype_text, &agt->root, VARSIZE(agt));

    pq_begintypsend(&buf);
    pq_sendint8(&buf, version);
    pq_sendtext(&buf, agtype_text->data, agtype_text->len);
    pfree_if_not_null(agtype_text->data);
    pfree_if_not_null(agtype_text);

    PG_FREE_IF_COPY(agt, 0);

    PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}

PG_FUNCTION_INFO_V1(agtype_in);

/*
 * agtype type input function
 */
Datum agtype_in(PG_FUNCTION_ARGS)
{
    char *str = PG_GETARG_CSTRING(0);
    Datum result = agtype_from_cstring(str, strlen(str));

    PG_FREE_IF_COPY(str, 0);

    return result;
}

PG_FUNCTION_INFO_V1(agtype_out);

/*
 * agtype type output function
 */
Datum agtype_out(PG_FUNCTION_ARGS)
{
    agtype *agt = NULL;
    char *out = NULL;

    agt = AG_GET_ARG_AGTYPE_P(0);

    out = agtype_to_cstring(NULL, &agt->root, VARSIZE(agt));

    PG_FREE_IF_COPY(agt, 0);

    PG_RETURN_CSTRING(out);
}

/*
 * agtype_value_from_cstring
 *
 * Helper function to turn an agtype string into an agtype_value.
 *
 * Uses the agtype parser (with hooks) to construct an agtype.
 */

agtype_value *agtype_value_from_cstring(char *str, int len)
{
    agtype_lex_context *lex;
    agtype_in_state state;
    agtype_sem_action sem;

    memset(&state, 0, sizeof(state));
    memset(&sem, 0, sizeof(sem));
    lex = make_agtype_lex_context_cstring_len(str, len, true);

    sem.semstate = (void *)&state;

    sem.object_start = agtype_in_object_start;
    sem.array_start = agtype_in_array_start;
    sem.object_end = agtype_in_object_end;
    sem.array_end = agtype_in_array_end;
    sem.scalar = agtype_in_scalar;
    sem.object_field_start = agtype_in_object_field_start;
    /* callback for annotation (typecasts) */
    sem.agtype_annotation = agtype_in_agtype_annotation;

    parse_agtype(lex, &sem);

    /* after parsing, the item member has the composed agtype structure */
    return state.res;
}

/*
 * agtype_from_cstring
 *
 * Turns agtype string into a Datum of agtype.
 *
 * Calls helper function
 */
static inline Datum agtype_from_cstring(char *str, int len)
{
    agtype_value *agtv = agtype_value_from_cstring(str, len);
    agtype *agt = agtype_value_to_agtype(agtv);

    pfree_agtype_value(agtv);
    PG_RETURN_POINTER(agt);
}

size_t check_string_length(size_t len)
{
    if (len > AGTENTRY_OFFLENMASK)
    {
        ereport(ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg("string too long to represent as agtype string"),
                 errdetail("Due to an implementation restriction, agtype strings cannot exceed %d bytes.",
                           AGTENTRY_OFFLENMASK)));
    }

    return len;
}

static void agtype_in_object_start(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_BEGIN_OBJECT,
                                    NULL);
}

static void agtype_in_object_end(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_END_OBJECT,
                                    NULL);
}

static void agtype_in_array_start(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_BEGIN_ARRAY,
                                    NULL);
}

static void agtype_in_array_end(void *pstate)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_END_ARRAY,
                                    NULL);
}

static void agtype_in_object_field_start(void *pstate, char *fname,
                                         bool isnull)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;
    agtype_value v;

    Assert(fname != NULL);
    v.type = AGTV_STRING;
    v.val.string.len = check_string_length(strlen(fname));
    v.val.string.val = fname;

    _state->res = push_agtype_value(&_state->parse_state, WAGT_KEY, &v);
}

/* main in function to process annotations */
static void agtype_in_agtype_annotation(void *pstate, char *annotation)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;

    /* verify that our required params are not null */
    Assert(pstate != NULL);
    Assert(annotation != NULL);

    /* pass to the appropriate typecast routine */
    switch (_state->res->type)
    {
    case AGTV_OBJECT:
        agtype_typecast_object(_state, annotation);
        break;
    case AGTV_ARRAY:
        agtype_typecast_array(_state, annotation);
        break;

    /*
     * Maybe we need to eventually move scalar annotations here. However,
     * we need to think about how an actual scalar value may be incorporated
     * into another object. Remember, the scalar is copied in on close, before
     * we would apply the annotation.
     */
    default:
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("unsupported type to annotate")));
        break;
    }
}

/* function to handle object typecasts */
static void agtype_typecast_object(agtype_in_state *state, char *annotation)
{
    agtype_value *agtv = NULL;
    agtype_value *last_updated_value = NULL;
    int len;
    bool top = true;

    /* verify that our required params are not null */
    Assert(annotation != NULL);
    Assert(state != NULL);

    len = strlen(annotation);
    agtv = state->res;

    /*
     * If the parse_state is not NULL, then we are not at the top level
     * and the following must be valid for a nested object with a typecast
     * at the end.
     */
    if (state->parse_state != NULL)
    {
        top = false;
        last_updated_value = state->parse_state->last_updated_value;
        /* make sure there is a value just copied in */
        Assert(last_updated_value != NULL);
        /* and that it is of type object */
        Assert(last_updated_value->type == AGTV_OBJECT);
    }

    /* check for a cast to a vertex */
    if (len == 6 && pg_strncasecmp(annotation, "vertex", len) == 0)
    {
        /* verify that the structure conforms to a valid vertex */
        if (is_object_vertex(agtv))
        {
            agtv->type = AGTV_VERTEX;
            /* if it isn't the top, we need to adjust the copied value */
            if (!top)
                last_updated_value->type = AGTV_VERTEX;
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("object is not a vertex")));
        }

    }
    /* check for a cast to an edge */
    else if (len == 4 && pg_strncasecmp(annotation, "edge", len) == 0)
    {
        /* verify that the structure conforms to a valid edge */
        if (is_object_edge(agtv))
        {
            agtv->type = AGTV_EDGE;
            /* if it isn't the top, we need to adjust the copied value */
            if (!top)
                last_updated_value->type = AGTV_EDGE;
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("object is not a edge")));
        }
    }
    /* otherwise this isn't a supported typecast */
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid annotation value for object")));
    }
}

/* function to handle array typecasts */
static void agtype_typecast_array(agtype_in_state *state, char *annotation)
{
    agtype_value *agtv = NULL;
    agtype_value *last_updated_value = NULL;
    int len;
    bool top = true;

    /* verify that our required params are not null */
    Assert(annotation != NULL);
    Assert(state != NULL);

    len = strlen(annotation);
    agtv = state->res;

    /*
     * If the parse_state is not NULL, then we are not at the top level
     * and the following must be valid for a nested array with a typecast
     * at the end.
     */
    if (state->parse_state != NULL)
    {
        top = false;
        last_updated_value = state->parse_state->last_updated_value;
        /* make sure there is a value just copied in */
        Assert(last_updated_value != NULL);
        /* and that it is of type object */
        Assert(last_updated_value->type == AGTV_ARRAY);
    }

    /* check for a cast to a path */
    if (len == 4 && pg_strncasecmp(annotation, "path", len) == 0)
    {
        /* verify that the array conforms to a valid path */
        if (is_array_path(agtv))
        {
            agtv->type = AGTV_PATH;
            /* if it isn't the top, we need to adjust the copied value */
            if (!top)
                last_updated_value->type = AGTV_PATH;
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("array is not a valid path")));
        }
    }
    /* otherwise this isn't a supported typecast */
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid annotation value for object")));
    }
}

/* helper function to check if an object fits a vertex */
static bool is_object_vertex(agtype_value *agtv)
{
    bool has_id = false;
    bool has_label = false;
    bool has_properties = false;
    int i;

    /* we require a valid object */
    Assert(agtv != NULL);
    Assert(agtv->type == AGTV_OBJECT);

    /* we need 3 pairs for a vertex */
    if (agtv->val.object.num_pairs != 3)
    {
        return false;
    }

    /* iterate through all pairs */
    for (i = 0; i < agtv->val.object.num_pairs; i++)
    {
        agtype_value *key = &agtv->val.object.pairs[i].key;
        agtype_value *value = &agtv->val.object.pairs[i].value;

        char *key_val = key->val.string.val;
        int key_len = key->val.string.len;

        Assert(key->type == AGTV_STRING);

        /* check for an id of type integer */
        if (key_len == 2 &&
            pg_strncasecmp(key_val, "id", key_len) == 0 &&
            value->type == AGTV_INTEGER)
        {
            has_id = true;
        }
        /* check for a label of type string */
        else if (key_len == 5 &&
                 pg_strncasecmp(key_val, "label", key_len) == 0 &&
                 value->type == AGTV_STRING)
        {
            has_label = true;
        }
        /* check for properties of type object */
        else if (key_len == 10 &&
                 pg_strncasecmp(key_val, "properties", key_len) == 0 &&
                 value->type == AGTV_OBJECT)
        {
            has_properties = true;
        }
        /* if it gets to this point, it can't be a vertex */
        else
        {
            return false;
        }
    }
    return (has_id && has_label && has_properties);
}

/* helper function to check if an object fits an edge */
static bool is_object_edge(agtype_value *agtv)
{
    bool has_id = false;
    bool has_label = false;
    bool has_properties = false;
    bool has_start_id = false;
    bool has_end_id = false;
    int i;

    /* we require a valid object */
    Assert(agtv != NULL);
    Assert(agtv->type == AGTV_OBJECT);

    /* we need 5 pairs for an edge */
    if (agtv->val.object.num_pairs != 5)
    {
        return false;
    }

    /* iterate through the pairs */
    for (i = 0; i < agtv->val.object.num_pairs; i++)
    {
        agtype_value *key = &agtv->val.object.pairs[i].key;
        agtype_value *value = &agtv->val.object.pairs[i].value;

        char *key_val = key->val.string.val;
        int key_len = key->val.string.len;

        Assert(key->type == AGTV_STRING);

        /* check for an id of type integer */
        if (key_len == 2 &&
            pg_strncasecmp(key_val, "id", key_len) == 0 &&
            value->type == AGTV_INTEGER)
        {
            has_id = true;
        }
        /* check for a label of type string */
        else if (key_len == 5 &&
                 pg_strncasecmp(key_val, "label", key_len) == 0 &&
                 value->type == AGTV_STRING)
        {
            has_label = true;
        }
        /* check for properties of type object */
        else if (key_len == 10 &&
                 pg_strncasecmp(key_val, "properties", key_len) == 0 &&
                 value->type == AGTV_OBJECT)
        {
            has_properties = true;
        }
        /* check for a start_id of type integer */
        else if (key_len == 8 &&
                 pg_strncasecmp(key_val, "start_id", key_len) == 0 &&
                 value->type == AGTV_INTEGER)
        {
            has_start_id = true;
        }
        /* check for an end_id of type integer */
        else if (key_len == 6 &&
                 pg_strncasecmp(key_val, "end_id", key_len) == 0 &&
                 value->type == AGTV_INTEGER)
        {
            has_end_id = true;
        }
        /* if it gets to this point, it can't be an edge */
        else
        {
            return false;
        }
    }
    return (has_id && has_label && has_properties &&
            has_start_id && has_end_id);
}

/* helper function to check if an array fits a path */
static bool is_array_path(agtype_value *agtv)
{
    agtype_value *element = NULL;
    int i;

    /* we require a valid array */
    Assert(agtv != NULL);
    Assert(agtv->type == AGTV_ARRAY);

    /* the array needs to have an odd number of elements */
    if (agtv->val.array.num_elems < 1 ||
        (agtv->val.array.num_elems - 1) % 2 != 0)
        return false;

    /* iterate through all elements */
    for (i = 0; (i + 1) < agtv->val.array.num_elems; i+=2)
    {
        element = &agtv->val.array.elems[i];
        if (element->type != AGTV_VERTEX)
            return false;

        element = &agtv->val.array.elems[i+1];
        if (element->type != AGTV_EDGE)
            return false;
    }

    /* check the last element */
    element = &agtv->val.array.elems[i];
    if (element->type != AGTV_VERTEX)
        return false;

    return true;
}

static void agtype_put_escaped_value(StringInfo out, agtype_value *scalar_val,
                                     bool extend)
{
    char *numstr;

    switch (scalar_val->type)
    {
    case AGTV_NULL:
        appendBinaryStringInfo(out, "null", 4);
        break;
    case AGTV_STRING:
        escape_agtype(out, pnstrdup(scalar_val->val.string.val,
                                    scalar_val->val.string.len));
        break;
    case AGTV_NUMERIC:
        appendStringInfoString(
            out, DatumGetCString(DirectFunctionCall1(
                     numeric_out, PointerGetDatum(scalar_val->val.numeric))));
        if (extend)
        {
            appendBinaryStringInfo(out, "::numeric", 9);
        }
        break;
    case AGTV_INTEGER:
        appendStringInfoString(
            out, DatumGetCString(DirectFunctionCall1(
                     int8out, Int64GetDatum(scalar_val->val.int_value))));
        break;
    case AGTV_FLOAT:
        numstr = DatumGetCString(DirectFunctionCall1(
            float8out, Float8GetDatum(scalar_val->val.float_value)));
        appendStringInfoString(out, numstr);

        if (is_decimal_needed(numstr))
            appendBinaryStringInfo(out, ".0", 2);
        break;
    case AGTV_BOOL:
        if (scalar_val->val.boolean)
            appendBinaryStringInfo(out, "true", 4);
        else
            appendBinaryStringInfo(out, "false", 5);
        break;
    case AGTV_VERTEX:
    {
        agtype *prop;
        scalar_val->type = AGTV_OBJECT;
        prop = agtype_value_to_agtype(scalar_val);
        agtype_to_cstring_worker(out, &prop->root, prop->vl_len_,
                                 false, extend);
        if (extend)
        {
            appendBinaryStringInfo(out, "::vertex", 8);
        }
        break;
    }
    case AGTV_EDGE:
    {
        agtype *prop;
        scalar_val->type = AGTV_OBJECT;
        prop = agtype_value_to_agtype(scalar_val);
        agtype_to_cstring_worker(out, &prop->root, prop->vl_len_,
                                 false, extend);
        if (extend)
        {
            appendBinaryStringInfo(out, "::edge", 6);
        }
        break;
    }
    case AGTV_PATH:
    {
        agtype *prop;
        scalar_val->type = AGTV_ARRAY;
        prop = agtype_value_to_agtype(scalar_val);
        agtype_to_cstring_worker(out, &prop->root, prop->vl_len_,
                                 false, extend);
        if (extend)
        {
            appendBinaryStringInfo(out, "::path", 6);
        }
        break;
    }

    default:
        elog(ERROR, "unknown agtype scalar type");
    }
}

/*
 * Produce an agtype string literal, properly escaping characters in the text.
 */
static void escape_agtype(StringInfo buf, const char *str)
{
    const char *p;

    appendStringInfoCharMacro(buf, '"');
    for (p = str; *p; p++)
    {
        switch (*p)
        {
        case '\b':
            appendStringInfoString(buf, "\\b");
            break;
        case '\f':
            appendStringInfoString(buf, "\\f");
            break;
        case '\n':
            appendStringInfoString(buf, "\\n");
            break;
        case '\r':
            appendStringInfoString(buf, "\\r");
            break;
        case '\t':
            appendStringInfoString(buf, "\\t");
            break;
        case '"':
            appendStringInfoString(buf, "\\\"");
            break;
        case '\\':
            appendStringInfoString(buf, "\\\\");
            break;
        default:
            if ((unsigned char)*p < ' ')
                appendStringInfo(buf, "\\u%04x", (int)*p);
            else
                appendStringInfoCharMacro(buf, *p);
            break;
        }
    }
    appendStringInfoCharMacro(buf, '"');
}

bool is_decimal_needed(char *numstr)
{
    int i;

    Assert(numstr);

    i = (numstr[0] == '-') ? 1 : 0;

    while (numstr[i] != '\0')
    {
        if (numstr[i] < '0' || numstr[i] > '9')
            return false;

        i++;
    }

    return true;
}

/*
 * For agtype we always want the de-escaped value - that's what's in token
 */
static void agtype_in_scalar(void *pstate, char *token,
                             agtype_token_type tokentype,
                             char *annotation)
{
    agtype_in_state *_state = (agtype_in_state *)pstate;
    agtype_value v;
    Datum numd;

    /*
     * Process the scalar typecast annotations, if present, but not if the
     * argument is a null. Typecasting a null is a null.
     */
    if (annotation != NULL && tokentype != AGTYPE_TOKEN_NULL)
    {
        int len = strlen(annotation);

        if (len == 7 && pg_strcasecmp(annotation, "numeric") == 0)
            tokentype = AGTYPE_TOKEN_NUMERIC;
        else if (len == 7 && pg_strcasecmp(annotation, "integer") == 0)
            tokentype = AGTYPE_TOKEN_INTEGER;
        else if (len == 5 && pg_strcasecmp(annotation, "float") == 0)
            tokentype = AGTYPE_TOKEN_FLOAT;
        else
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("invalid annotation value for scalar")));
    }

    switch (tokentype)
    {
    case AGTYPE_TOKEN_STRING:
        Assert(token != NULL);
        v.type = AGTV_STRING;
        v.val.string.len = check_string_length(strlen(token));
        v.val.string.val = token;
        break;
    case AGTYPE_TOKEN_INTEGER:
        Assert(token != NULL);
        v.type = AGTV_INTEGER;
        v.val.int_value = pg_strtoint64(token);
        break;
    case AGTYPE_TOKEN_FLOAT:
        Assert(token != NULL);
        v.type = AGTV_FLOAT;
        v.val.float_value = float8in_internal(token, NULL, "double precision",
                                              token);
        break;
    case AGTYPE_TOKEN_NUMERIC:
        Assert(token != NULL);
        v.type = AGTV_NUMERIC;
        numd = DirectFunctionCall3(numeric_in,
                                   CStringGetDatum(token),
                                   ObjectIdGetDatum(InvalidOid),
                                   Int32GetDatum(-1));
        v.val.numeric = DatumGetNumeric(numd);
        break;

    case AGTYPE_TOKEN_TRUE:
        v.type = AGTV_BOOL;
        v.val.boolean = true;
        break;
    case AGTYPE_TOKEN_FALSE:
        v.type = AGTV_BOOL;
        v.val.boolean = false;
        break;
    case AGTYPE_TOKEN_NULL:
        v.type = AGTV_NULL;
        break;
    default:
        /* should not be possible */
        elog(ERROR, "invalid agtype token type");
        break;
    }

    if (_state->parse_state == NULL)
    {
        /* single scalar */
        agtype_value va;

        va.type = AGTV_ARRAY;
        va.val.array.raw_scalar = true;
        va.val.array.num_elems = 1;

        _state->res = push_agtype_value(&_state->parse_state, WAGT_BEGIN_ARRAY,
                                        &va);
        _state->res = push_agtype_value(&_state->parse_state, WAGT_ELEM, &v);
        _state->res = push_agtype_value(&_state->parse_state, WAGT_END_ARRAY,
                                        NULL);
    }
    else
    {
        agtype_value *o = &_state->parse_state->cont_val;

        switch (o->type)
        {
        case AGTV_ARRAY:
            _state->res = push_agtype_value(&_state->parse_state, WAGT_ELEM,
                                            &v);
            break;
        case AGTV_OBJECT:
            _state->res = push_agtype_value(&_state->parse_state, WAGT_VALUE,
                                            &v);
            break;
        default:
            elog(ERROR, "unexpected parent of nested structure");
        }
    }
}

/*
 * agtype_to_cstring
 *     Converts agtype value to a C-string.
 *
 * If 'out' argument is non-null, the resulting C-string is stored inside the
 * StringBuffer.  The resulting string is always returned.
 *
 * A typical case for passing the StringInfo in rather than NULL is where the
 * caller wants access to the len attribute without having to call strlen, e.g.
 * if they are converting it to a text* object.
 */
char *agtype_to_cstring(StringInfo out, agtype_container *in,
                        int estimated_len)
{
    return agtype_to_cstring_worker(out, in, estimated_len, false,
                                    true);
}

/*
 * same thing but with indentation turned on
 */
char *agtype_to_cstring_indent(StringInfo out, agtype_container *in,
                               int estimated_len)
{
    return agtype_to_cstring_worker(out, in, estimated_len, true,
                                    true);
}

/*
 * common worker for above two functions
 */
static char *agtype_to_cstring_worker(StringInfo out, agtype_container *in,
                                      int estimated_len, bool indent,
                                      bool extend)
{
    bool first = true;
    agtype_iterator *it;
    agtype_value v;
    agtype_iterator_token type = WAGT_DONE;
    int level = 0;
    bool redo_switch = false;

    /* If we are indenting, don't add a space after a comma */
    int ispaces = indent ? 1 : 2;

    /*
     * Don't indent the very first item. This gets set to the indent flag at
     * the bottom of the loop.
     */
    bool use_indent = false;
    bool raw_scalar = false;
    bool last_was_key = false;

    if (out == NULL)
        out = makeStringInfo();

    enlargeStringInfo(out, (estimated_len >= 0) ? estimated_len : 64);

    it = agtype_iterator_init(in);

    while (redo_switch ||
           ((type = agtype_iterator_next(&it, &v, false)) != WAGT_DONE))
    {
        redo_switch = false;
        switch (type)
        {
        case WAGT_BEGIN_ARRAY:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);

            if (!v.val.array.raw_scalar)
            {
                add_indent(out, use_indent && !last_was_key, level);
                appendStringInfoCharMacro(out, '[');
            }
            else
            {
                raw_scalar = true;
            }

            first = true;
            level++;
            break;
        case WAGT_BEGIN_OBJECT:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);

            add_indent(out, use_indent && !last_was_key, level);
            appendStringInfoCharMacro(out, '{');

            first = true;
            level++;
            break;
        case WAGT_KEY:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);
            first = true;

            add_indent(out, use_indent, level);

            /* agtype rules guarantee this is a string */
            agtype_put_escaped_value(out, &v, extend);
            appendBinaryStringInfo(out, ": ", 2);

            type = agtype_iterator_next(&it, &v, false);
            if (type == WAGT_VALUE)
            {
                first = false;
                agtype_put_escaped_value(out, &v, extend);
            }
            else
            {
                Assert(type == WAGT_BEGIN_OBJECT || type == WAGT_BEGIN_ARRAY);

                /*
                 * We need to rerun the current switch() since we need to
                 * output the object which we just got from the iterator
                 * before calling the iterator again.
                 */
                redo_switch = true;
            }
            break;
        case WAGT_ELEM:
            if (!first)
                appendBinaryStringInfo(out, ", ", ispaces);
            first = false;

            if (!raw_scalar)
                add_indent(out, use_indent, level);
            agtype_put_escaped_value(out, &v, extend);
            break;
        case WAGT_END_ARRAY:
            level--;
            if (!raw_scalar)
            {
                add_indent(out, use_indent, level);
                appendStringInfoCharMacro(out, ']');
            }
            first = false;
            break;
        case WAGT_END_OBJECT:
            level--;
            add_indent(out, use_indent, level);
            appendStringInfoCharMacro(out, '}');
            first = false;
            break;
        default:
            elog(ERROR, "unknown agtype iterator token type");
        }
        use_indent = indent;
        last_was_key = redo_switch;
    }

    Assert(level == 0);

    return out->data;
}

/*
 * Convert agtype_value(scalar) to text
 */
static text *agtype_value_to_text(agtype_value *scalar_val,
                                  bool err_not_scalar)
{
    text *result = NULL;
    switch (scalar_val->type)
    {
    case AGTV_INTEGER:
        result = cstring_to_text(DatumGetCString(DirectFunctionCall1(
            int8out, Int64GetDatum(scalar_val->val.int_value))));
        break;
    case AGTV_FLOAT:
        result = cstring_to_text(DatumGetCString(DirectFunctionCall1(
            float8out, Float8GetDatum(scalar_val->val.float_value))));
        break;
    case AGTV_STRING:
        result = cstring_to_text_with_len(scalar_val->val.string.val,
                                          scalar_val->val.string.len);
        break;
    case AGTV_NUMERIC:
        result = cstring_to_text(DatumGetCString(DirectFunctionCall1(
            numeric_out, PointerGetDatum(scalar_val->val.numeric))));
        break;
    case AGTV_BOOL:
        result = cstring_to_text((scalar_val->val.boolean) ? "true" : "false");
        break;
    case AGTV_NULL:
        result = NULL;
        break;
    default:
        if (err_not_scalar)
        {
            ereport(
                ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("agtype_value_to_text: unsupported argument agtype %d",
                        scalar_val->type)));
        }
    }
    return result;
}

static void add_indent(StringInfo out, bool indent, int level)
{
    if (indent)
    {
        int i;

        appendStringInfoCharMacro(out, '\n');
        for (i = 0; i < level; i++)
            appendBinaryStringInfo(out, "    ", 4);
    }
}

Datum integer_to_agtype(int64 i)
{
    agtype_value agtv;
    agtype *agt;

    agtv.type = AGTV_INTEGER;
    agtv.val.int_value = i;
    agt = agtype_value_to_agtype(&agtv);

    return AGTYPE_P_GET_DATUM(agt);
}

Datum float_to_agtype(float8 f)
{
    agtype_value agtv;
    agtype *agt;

    agtv.type = AGTV_FLOAT;
    agtv.val.float_value = f;
    agt = agtype_value_to_agtype(&agtv);

    return AGTYPE_P_GET_DATUM(agt);
}

/*
 * s must be a UTF-8 encoded, unescaped, and null-terminated string which is
 * a valid string for internal storage of agtype.
 */
Datum string_to_agtype(char *s)
{
    agtype_value agtv;
    agtype *agt;

    agtv.type = AGTV_STRING;
    agtv.val.string.len = check_string_length(strlen(s));
    agtv.val.string.val = s;
    agt = agtype_value_to_agtype(&agtv);

    return AGTYPE_P_GET_DATUM(agt);
}

Datum boolean_to_agtype(bool b)
{
    agtype_value agtv;
    agtype *agt;

    agtv.type = AGTV_BOOL;
    agtv.val.boolean = b;
    agt = agtype_value_to_agtype(&agtv);

    return AGTYPE_P_GET_DATUM(agt);
}

/*
 * Determine how we want to render values of a given type in datum_to_agtype.
 *
 * Given the datatype OID, return its agt_type_category, as well as the type's
 * output function OID.  If the returned category is AGT_TYPE_JSONCAST,
 * we return the OID of the relevant cast function instead.
 */
static void agtype_categorize_type(Oid typoid, agt_type_category *tcategory,
                                   Oid *outfuncoid)
{
    bool typisvarlena;

    /* Look through any domain */
    typoid = getBaseType(typoid);

    *outfuncoid = InvalidOid;

    /*
     * We need to get the output function for everything except date and
     * timestamp types, booleans, array and composite types, json and jsonb,
     * and non-builtin types where there's a cast to json. In this last case
     * we return the oid of the cast function instead.
     */

    switch (typoid)
    {
    case BOOLOID:
        *tcategory = AGT_TYPE_BOOL;
        break;

    case INT2OID:
    case INT4OID:
    case INT8OID:
        getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
        *tcategory = AGT_TYPE_INTEGER;
        break;

    case FLOAT8OID:
        getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
        *tcategory = AGT_TYPE_FLOAT;
        break;

    case FLOAT4OID:
    case NUMERICOID:
        getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
        *tcategory = AGT_TYPE_NUMERIC;
        break;

    case DATEOID:
        *tcategory = AGT_TYPE_DATE;
        break;

    case TIMESTAMPOID:
        *tcategory = AGT_TYPE_TIMESTAMP;
        break;

    case TIMESTAMPTZOID:
        *tcategory = AGT_TYPE_TIMESTAMPTZ;
        break;

    case JSONBOID:
        *tcategory = AGT_TYPE_JSONB;
        break;

    case JSONOID:
        *tcategory = AGT_TYPE_JSON;
        break;

    default:
        /* Check for arrays and composites */
        if (typoid == AGTYPEOID)
        {
            *tcategory = AGT_TYPE_AGTYPE;
        }
        else if (OidIsValid(get_element_type(typoid)) ||
                 typoid == ANYARRAYOID || typoid == RECORDARRAYOID)
        {
            *tcategory = AGT_TYPE_ARRAY;
        }
        /* includes RECORDOID */
        else if (type_is_rowtype(typoid))
        {
            *tcategory = AGT_TYPE_COMPOSITE;
        }
        else if (typoid == GRAPHIDOID)
        {
            getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
            *tcategory = AGT_TYPE_INTEGER;
        }
        else
        {
            /* It's probably the general case ... */
            *tcategory = AGT_TYPE_OTHER;

            /*
             * but first let's look for a cast to json (note: not to
             * jsonb) if it's not built-in.
             */
            if (typoid >= FirstNormalObjectId)
            {
                Oid castfunc;
                CoercionPathType ctype;

                ctype = find_coercion_pathway(JSONOID, typoid,
                                              COERCION_EXPLICIT, &castfunc);
                if (ctype == COERCION_PATH_FUNC && OidIsValid(castfunc))
                {
                    *tcategory = AGT_TYPE_JSONCAST;
                    *outfuncoid = castfunc;
                }
                else
                {
                    /* not a cast type, so just get the usual output func */
                    getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
                }
            }
            else
            {
                /* any other builtin type */
                getTypeOutputInfo(typoid, outfuncoid, &typisvarlena);
            }
            break;
        }
    }
}

/*
 * Turn a Datum into agtype, adding it to the result agtype_in_state.
 *
 * tcategory and outfuncoid are from a previous call to agtype_categorize_type,
 * except that if is_null is true then they can be invalid.
 *
 * If key_scalar is true, the value is stored as a key, so insist
 * it's of an acceptable type, and force it to be a AGTV_STRING.
 */
static void datum_to_agtype(Datum val, bool is_null, agtype_in_state *result,
                            agt_type_category tcategory, Oid outfuncoid,
                            bool key_scalar)
{
    char *outputstr;
    bool numeric_error;
    agtype_value agtv;
    bool scalar_agtype = false;

    check_stack_depth();

    /* Convert val to an agtype_value in agtv (in most cases) */
    if (is_null)
    {
        Assert(!key_scalar);
        agtv.type = AGTV_NULL;
    }
    else if (key_scalar &&
             (tcategory == AGT_TYPE_ARRAY || tcategory == AGT_TYPE_COMPOSITE ||
              tcategory == AGT_TYPE_JSON || tcategory == AGT_TYPE_JSONB ||
              tcategory == AGT_TYPE_AGTYPE || tcategory == AGT_TYPE_JSONCAST))
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg(
                 "key value must be scalar, not array, composite, or json")));
    }
    else
    {
        if (tcategory == AGT_TYPE_JSONCAST)
            val = OidFunctionCall1(outfuncoid, val);

        switch (tcategory)
        {
        case AGT_TYPE_ARRAY:
            array_to_agtype_internal(val, result);
            break;
        case AGT_TYPE_COMPOSITE:
            composite_to_agtype(val, result);
            break;
        case AGT_TYPE_BOOL:
            if (key_scalar)
            {
                outputstr = DatumGetBool(val) ? "true" : "false";
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                agtv.type = AGTV_BOOL;
                agtv.val.boolean = DatumGetBool(val);
            }
            break;
        case AGT_TYPE_INTEGER:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            if (key_scalar)
            {
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                Datum intd;

                intd = DirectFunctionCall1(int8in, CStringGetDatum(outputstr));
                agtv.type = AGTV_INTEGER;
                agtv.val.int_value = DatumGetInt64(intd);
                pfree_if_not_null(outputstr);
            }
            break;
        case AGT_TYPE_FLOAT:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            if (key_scalar)
            {
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                agtv.type = AGTV_FLOAT;
                agtv.val.float_value = DatumGetFloat8(val);
            }
            break;
        case AGT_TYPE_NUMERIC:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            if (key_scalar)
            {
                /* always quote keys */
                agtv.type = AGTV_STRING;
                agtv.val.string.len = strlen(outputstr);
                agtv.val.string.val = outputstr;
            }
            else
            {
                /*
                 * Make it numeric if it's a valid agtype number, otherwise
                 * a string. Invalid numeric output will always have an
                 * 'N' or 'n' in it (I think).
                 */
                numeric_error = (strchr(outputstr, 'N') != NULL ||
                                 strchr(outputstr, 'n') != NULL);
                if (!numeric_error)
                {
                    Datum numd;

                    agtv.type = AGTV_NUMERIC;
                    numd = DirectFunctionCall3(numeric_in,
                                               CStringGetDatum(outputstr),
                                               ObjectIdGetDatum(InvalidOid),
                                               Int32GetDatum(-1));
                    agtv.val.numeric = DatumGetNumeric(numd);
                    pfree_if_not_null(outputstr);
                }
                else
                {
                    agtv.type = AGTV_STRING;
                    agtv.val.string.len = strlen(outputstr);
                    agtv.val.string.val = outputstr;
                }
            }
            break;
        case AGT_TYPE_DATE:
            agtv.type = AGTV_STRING;
            agtv.val.string.val = agtype_encode_date_time(NULL, val, DATEOID);
            agtv.val.string.len = strlen(agtv.val.string.val);
            break;
        case AGT_TYPE_TIMESTAMP:
            agtv.type = AGTV_STRING;
            agtv.val.string.val = agtype_encode_date_time(NULL, val,
                                                          TIMESTAMPOID);
            agtv.val.string.len = strlen(agtv.val.string.val);
            break;
        case AGT_TYPE_TIMESTAMPTZ:
            agtv.type = AGTV_STRING;
            agtv.val.string.val = agtype_encode_date_time(NULL, val,
                                                          TIMESTAMPTZOID);
            agtv.val.string.len = strlen(agtv.val.string.val);
            break;
        case AGT_TYPE_JSONCAST:
        case AGT_TYPE_JSON:
        {
            /*
             * Parse the json right into the existing result object.
             * We can handle it as an agtype because agtype is currently an
             * extension of json.
             * Unlike AGT_TYPE_JSONB, numbers will be stored as either
             * an integer or a float, not a numeric.
             */
            agtype_lex_context *lex;
            agtype_sem_action sem;
            text *json = DatumGetTextPP(val);

            lex = make_agtype_lex_context(json, true);

            memset(&sem, 0, sizeof(sem));

            sem.semstate = (void *)result;

            sem.object_start = agtype_in_object_start;
            sem.array_start = agtype_in_array_start;
            sem.object_end = agtype_in_object_end;
            sem.array_end = agtype_in_array_end;
            sem.scalar = agtype_in_scalar;
            sem.object_field_start = agtype_in_object_field_start;

            parse_agtype(lex, &sem);
        }
        break;
        case AGT_TYPE_AGTYPE:
        case AGT_TYPE_JSONB:
        {
            agtype *jsonb = DATUM_GET_AGTYPE_P(val);
            agtype_iterator *it;

            /*
             * val is actually jsonb datum but we can handle it as an agtype
             * datum because agtype is currently an extension of jsonb.
             */

            it = agtype_iterator_init(&jsonb->root);

            if (AGT_ROOT_IS_SCALAR(jsonb))
            {
                agtype_iterator_next(&it, &agtv, true);
                Assert(agtv.type == AGTV_ARRAY);
                agtype_iterator_next(&it, &agtv, true);
                scalar_agtype = true;
            }
            else
            {
                agtype_iterator_token type;

                while ((type = agtype_iterator_next(&it, &agtv, false)) !=
                       WAGT_DONE)
                {
                    if (type == WAGT_END_ARRAY || type == WAGT_END_OBJECT ||
                        type == WAGT_BEGIN_ARRAY || type == WAGT_BEGIN_OBJECT)
                    {
                        result->res = push_agtype_value(&result->parse_state,
                                                        type, NULL);
                    }
                    else
                    {
                        result->res = push_agtype_value(&result->parse_state,
                                                        type, &agtv);
                    }
                }
            }
        }
        break;
        default:
            outputstr = OidOutputFunctionCall(outfuncoid, val);
            agtv.type = AGTV_STRING;
            agtv.val.string.len = check_string_length(strlen(outputstr));
            agtv.val.string.val = outputstr;
            break;
        }
    }

    /* Now insert agtv into result, unless we did it recursively */
    if (!is_null && !scalar_agtype && tcategory >= AGT_TYPE_AGTYPE &&
        tcategory <= AGT_TYPE_JSONCAST)
    {
        /* work has been done recursively */
        return;
    }
    else if (result->parse_state == NULL)
    {
        /* single root scalar */
        agtype_value va;

        va.type = AGTV_ARRAY;
        va.val.array.raw_scalar = true;
        va.val.array.num_elems = 1;

        result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_ARRAY,
                                        &va);
        result->res = push_agtype_value(&result->parse_state, WAGT_ELEM,
                                        &agtv);
        result->res = push_agtype_value(&result->parse_state, WAGT_END_ARRAY,
                                        NULL);
    }
    else
    {
        agtype_value *o = &result->parse_state->cont_val;

        switch (o->type)
        {
        case AGTV_ARRAY:
            result->res = push_agtype_value(&result->parse_state, WAGT_ELEM,
                                            &agtv);
            break;
        case AGTV_OBJECT:
            result->res = push_agtype_value(&result->parse_state,
                                            key_scalar ? WAGT_KEY : WAGT_VALUE,
                                            &agtv);
            break;
        default:
            elog(ERROR, "unexpected parent of nested structure");
        }
    }
}

/*
 * Process a single dimension of an array.
 * If it's the innermost dimension, output the values, otherwise call
 * ourselves recursively to process the next dimension.
 */
static void array_dim_to_agtype(agtype_in_state *result, int dim, int ndims,
                                int *dims, Datum *vals, bool *nulls,
                                int *valcount, agt_type_category tcategory,
                                Oid outfuncoid)
{
    int i;

    Assert(dim < ndims);

    result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_ARRAY,
                                    NULL);

    for (i = 1; i <= dims[dim]; i++)
    {
        if (dim + 1 == ndims)
        {
            datum_to_agtype(vals[*valcount], nulls[*valcount], result,
                            tcategory, outfuncoid, false);
            (*valcount)++;
        }
        else
        {
            array_dim_to_agtype(result, dim + 1, ndims, dims, vals, nulls,
                                valcount, tcategory, outfuncoid);
        }
    }

    result->res = push_agtype_value(&result->parse_state, WAGT_END_ARRAY,
                                    NULL);
}

/*
 * Turn an array into agtype.
 */
static void array_to_agtype_internal(Datum array, agtype_in_state *result)
{
    ArrayType *v = DatumGetArrayTypeP(array);
    Oid element_type = ARR_ELEMTYPE(v);
    int *dim;
    int ndim;
    int nitems;
    int count = 0;
    Datum *elements;
    bool *nulls;
    int16 typlen;
    bool typbyval;
    char typalign;
    agt_type_category tcategory;
    Oid outfuncoid;

    ndim = ARR_NDIM(v);
    dim = ARR_DIMS(v);
    nitems = ArrayGetNItems(ndim, dim);

    if (nitems <= 0)
    {
        result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_ARRAY,
                                        NULL);
        result->res = push_agtype_value(&result->parse_state, WAGT_END_ARRAY,
                                        NULL);
        return;
    }

    get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);

    agtype_categorize_type(element_type, &tcategory, &outfuncoid);

    deconstruct_array(v, element_type, typlen, typbyval, typalign, &elements,
                      &nulls, &nitems);

    array_dim_to_agtype(result, 0, ndim, dim, elements, nulls, &count,
                        tcategory, outfuncoid);

    pfree_if_not_null(elements);
    pfree_if_not_null(nulls);
}

/*
 * Turn a composite / record into agtype.
 */
static void composite_to_agtype(Datum composite, agtype_in_state *result)
{
    HeapTupleHeader td;
    Oid tup_type;
    int32 tup_typmod;
    TupleDesc tupdesc;
    HeapTupleData tmptup, *tuple;
    int i;

    td = DatumGetHeapTupleHeader(composite);

    /* Extract rowtype info and find a tupdesc */
    tup_type = HeapTupleHeaderGetTypeId(td);
    tup_typmod = HeapTupleHeaderGetTypMod(td);
    tupdesc = lookup_rowtype_tupdesc(tup_type, tup_typmod);

    /* Build a temporary HeapTuple control structure */
    tmptup.t_len = HeapTupleHeaderGetDatumLength(td);
    tmptup.t_data = td;
    tuple = &tmptup;

    result->res = push_agtype_value(&result->parse_state, WAGT_BEGIN_OBJECT,
                                    NULL);

    for (i = 0; i < tupdesc->natts; i++)
    {
        Datum val;
        bool isnull;
        char *attname;
        agt_type_category tcategory;
        Oid outfuncoid;
        agtype_value v;
        Form_pg_attribute att = TupleDescAttr(tupdesc, i);

        if (att->attisdropped)
            continue;

        attname = NameStr(att->attname);

        v.type = AGTV_STRING;
        /*
         * don't need check_string_length here
         * - can't exceed maximum name length
         */
        v.val.string.len = strlen(attname);
        v.val.string.val = attname;

        result->res = push_agtype_value(&result->parse_state, WAGT_KEY, &v);

        val = heap_getattr(tuple, i + 1, tupdesc, &isnull);

        if (isnull)
        {
            tcategory = AGT_TYPE_NULL;
            outfuncoid = InvalidOid;
        }
        else
        {
            agtype_categorize_type(att->atttypid, &tcategory, &outfuncoid);
        }

        datum_to_agtype(val, isnull, result, tcategory, outfuncoid, false);
    }

    result->res = push_agtype_value(&result->parse_state, WAGT_END_OBJECT,
                                    NULL);
    ReleaseTupleDesc(tupdesc);
}

/*
 * Removes properties with null value from the given agtype object.
 */
void remove_null_from_agtype_object(agtype_value *object)
{
     /* next available position */
    agtype_pair *avail;
    agtype_pair *ptr;

    if (object->type != AGTV_OBJECT)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("a map is expected")));
    }

    avail = object->val.object.pairs;
    ptr = object->val.object.pairs;

    while (ptr - object->val.object.pairs < object->val.object.num_pairs)
    {
        if (ptr->value.type != AGTV_NULL)
        {
            if (ptr != avail)
            {
                memcpy(avail, ptr, sizeof(agtype_pair));
            }
            avail++;
        }
        ptr++;
    }

    object->val.object.num_pairs = avail - object->val.object.pairs;
}

/*
 * Append agtype text for "val" to "result".
 *
 * This is just a thin wrapper around datum_to_agtype.  If the same type
 * will be printed many times, avoid using this; better to do the
 * agtype_categorize_type lookups only once.
 */
void add_agtype(Datum val, bool is_null, agtype_in_state *result,
                       Oid val_type, bool key_scalar)
{
    agt_type_category tcategory;
    Oid outfuncoid;

    if (val_type == InvalidOid)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("could not determine input data type")));
    }

    if (is_null)
    {
        tcategory = AGT_TYPE_NULL;
        outfuncoid = InvalidOid;
    }
    else
    {
        agtype_categorize_type(val_type, &tcategory, &outfuncoid);
    }

    datum_to_agtype(val, is_null, result, tcategory, outfuncoid, key_scalar);
}

agtype_value *string_to_agtype_value(char *s)
{
    agtype_value *agtv = palloc0(sizeof(agtype_value));

    agtv->type = AGTV_STRING;
    agtv->val.string.len = check_string_length(strlen(s));
    agtv->val.string.val = pnstrdup(s, agtv->val.string.len);

    return agtv;
}

/* helper function to create an agtype_value integer from an integer */
agtype_value *integer_to_agtype_value(int64 int_value)
{
    agtype_value *agtv = palloc0(sizeof(agtype_value));

    agtv->type = AGTV_INTEGER;
    agtv->val.int_value = int_value;

    return agtv;
}

PG_FUNCTION_INFO_V1(_agtype_build_path);

/*
 * SQL function agtype_build_path(VARIADIC agtype)
 */
Datum _agtype_build_path(PG_FUNCTION_ARGS)
{
    agtype_in_state result;
    agtype *agt_result;
    Datum *args = NULL;
    bool *nulls = NULL;
    Oid *types = NULL;
    int nargs = 0;
    int i = 0;
    bool is_zero_boundary_case = false;

    /* build argument values to build the object */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    if (nargs < 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("paths require at least 1 vertex")));
    }

    /*
     * If this path is only 1 to 3 elements in length, check to see if the
     * contained edge is actually a path (made by the VLE). If so, just
     * materialize the vle path because it already contains the two outside
     * vertices.
     */
    if (nargs >= 1 && nargs <= 3)
    {
        int i = 0;

        for (i = 0; i < nargs; i++)
        {
            agtype *agt = NULL;

            if (nulls[i] || types[i] != AGTYPEOID)
            {
                break;
            }

            agt = DATUM_GET_AGTYPE_P(args[i]);

            if (AGT_ROOT_IS_BINARY(agt) &&
                AGT_ROOT_BINARY_FLAGS(agt) == AGT_FBINARY_TYPE_VLE_PATH)
            {
                agtype *path = agt_materialize_vle_path(agt);
                PG_FREE_IF_COPY(agt, i);
                PG_RETURN_POINTER(path);
            }
            PG_FREE_IF_COPY(agt, i);
        }
    }

    if (nargs % 2 == 0)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("a path is of the form: [vertex, (edge, vertex)*i] where i >= 0")));
    }

    /* initialize the result */
    memset(&result, 0, sizeof(agtype_in_state));

    /* push in the beginning of the agtype array */
    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY, NULL);

    /* loop through the path components */
    for (i = 0; i < nargs; i++)
    {
        agtype *agt = NULL;

        if (nulls[i])
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument %d must not be null", i + 1)));
        }
        else if (types[i] != AGTYPEOID)
        {

            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument %d must be an agtype", i + 1)));
        }

        /* get the agtype pointer */
        agt = DATUM_GET_AGTYPE_P(args[i]);

        /* is this a VLE path edge */
        if (i % 2 == 1 &&
            AGT_ROOT_IS_BINARY(agt) &&
            AGT_ROOT_BINARY_FLAGS(agt) == AGT_FBINARY_TYPE_VLE_PATH)
        {
            agtype_value *agtv_path = NULL;
            int j = 0;

            /* get the VLE path from the container as an agtype_value */
            agtv_path = agtv_materialize_vle_path(agt);

            PG_FREE_IF_COPY(agt, i);

            /* it better be an AGTV_PATH */
            Assert(agtv_path->type == AGTV_PATH);

            /*
             * If the VLE path is the zero boundary case, there isn't an edge to
             * process. Additionally, the start and end vertices are the same.
             * We need to flag this condition so that we can skip processing the
             * following vertex.
             */
            if (agtv_path->val.array.num_elems == 1)
            {
                is_zero_boundary_case = true;
                continue;
            }

            /*
             * Add in the interior path - excluding the start and end vertices.
             * The other iterations of the for loop has handled start and will
             * handle end.
             */
            for (j = 1; j <= agtv_path->val.array.num_elems - 2; j++)
            {
                result.res = push_agtype_value(&result.parse_state, WAGT_ELEM,
                                               &agtv_path->val.array.elems[j]);
            }
        }
        else if (i % 2 == 1 && (!AGTE_IS_AGTYPE(agt->root.children[0]) ||
                                agt->root.children[1] != AGT_HEADER_EDGE))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("paths consist of alternating vertices and edges"),
                     errhint("argument %d must be an edge", i + 1)));
        }
        else if (i % 2 == 0 && (!AGTE_IS_AGTYPE(agt->root.children[0]) ||
                                agt->root.children[1] != AGT_HEADER_VERTEX))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("paths consist of alternating vertices and edges"),
                     errhint("argument %d must be an vertex", i + 1)));
        }
        /*
         * This will always add in vertices or edges depending on the loop
         * iteration. However, when it is a vertex, there is the possibility
         * that the previous iteration flagged a zero boundary case. We can only
         * add it if this is not the case. If this is an edge, it is not
         * possible to be a zero boundary case.
         */
        else if (is_zero_boundary_case == false)
        {
            add_agtype(AGTYPE_P_GET_DATUM(agt), false, &result, types[i],
                       false);
            PG_FREE_IF_COPY(agt, i);
        }
        /* If we got here, we had a zero boundary case. So, clear it */
        else
        {
            is_zero_boundary_case = false;
        }
    }

    /* push the end of the array */
    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

    /* set it to a path type */
    result.res->type = AGTV_PATH;

    agt_result = agtype_value_to_agtype(result.res);

    pfree_agtype_in_state(&result);

    PG_RETURN_POINTER(agt_result);
}

Datum make_path(List *path)
{
    agtype *agt_result;
    ListCell *lc;
    agtype_in_state result;
    int i = 1;

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY, NULL);

    if (list_length(path) < 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("paths require at least 1 vertex")));
    }

    if (list_length(path) % 2 != 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("a path is of the form: [vertex, (edge, vertex)*i] where i >= 0")));
    }


    foreach (lc, path)
    {
        agtype *agt = DATUM_GET_AGTYPE_P(PointerGetDatum(lfirst(lc)));
        agtype_value *elem;
        elem = get_ith_agtype_value_from_container(&agt->root, 0);

        if (!agt)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument must not be null")));
        }
        else if (i % 2 == 1 && elem->type != AGTV_VERTEX)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument %i must be a vertex", i)));
        }
        else if (i % 2 == 0 && elem->type != AGTV_EDGE)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument %i must be an edge", i)));
        }

        add_agtype((Datum)agt, false, &result, AGTYPEOID, false);

        if ((Pointer) (agt) != lfirst(lc))
        {
            pfree_if_not_null(agt);
        }
        pfree_agtype_value(elem);

        i++;
    }

    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

    result.res->type = AGTV_PATH;

    agt_result = agtype_value_to_agtype(result.res);

    pfree_agtype_in_state(&result);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(_agtype_build_vertex);

/*
 * SQL function agtype_build_vertex(graphid, cstring, agtype)
 */
Datum _agtype_build_vertex(PG_FUNCTION_ARGS)
{
    graphid id;
    char *label;
    agtype *properties;
    agtype_build_state *bstate;
    agtype *rawscalar;
    agtype *vertex;

    /* handles null */
    if (fcinfo->args[0].isnull)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("_agtype_build_vertex() graphid cannot be NULL")));
    }

    if (fcinfo->args[1].isnull)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("_agtype_build_vertex() label cannot be NULL")));
    }

    id = AG_GETARG_GRAPHID(0);
    label = PG_GETARG_CSTRING(1);

    if (fcinfo->args[2].isnull)
    {
        agtype_build_state *bstate = init_agtype_build_state(0, AGT_FOBJECT);
        properties = build_agtype(bstate);
        pfree_agtype_build_state(bstate);
    }
    else
    {
        properties = AG_GET_ARG_AGTYPE_P(2);

        if (!AGT_ROOT_IS_OBJECT(properties))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("_agtype_build_vertex() properties argument must be an object")));
        }
    }

    bstate = init_agtype_build_state(3, AGT_FOBJECT);
    write_string(bstate, "id");
    write_string(bstate, "label");
    write_string(bstate, "properties");
    write_graphid(bstate, id);
    write_string(bstate, label);
    write_container(bstate, properties);
    vertex = build_agtype(bstate);
    pfree_agtype_build_state(bstate);

    bstate = init_agtype_build_state(1, AGT_FARRAY | AGT_FSCALAR);
    write_extended(bstate, vertex, AGT_HEADER_VERTEX);
    rawscalar = build_agtype(bstate);
    pfree_agtype_build_state(bstate);

    PG_FREE_IF_COPY(label, 1);
    PG_FREE_IF_COPY(properties, 2);

    PG_RETURN_POINTER(rawscalar);
}

Datum make_vertex(Datum id, Datum label, Datum properties)
{
    return DirectFunctionCall3(_agtype_build_vertex, id, label, properties);
}

PG_FUNCTION_INFO_V1(_agtype_build_edge);

/*
 * SQL function agtype_build_edge(graphid, graphid, graphid, cstring, agtype)
 */
Datum _agtype_build_edge(PG_FUNCTION_ARGS)
{
    agtype_build_state *bstate;
    agtype *edge, *rawscalar;
    graphid id, start_id, end_id;
    char *label;
    agtype *properties;

    /* process graph id */
    if (fcinfo->args[0].isnull)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("_agtype_build_edge() graphid cannot be NULL")));
    }

    id = AG_GETARG_GRAPHID(0);

    /* process label */
    if (fcinfo->args[3].isnull)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("_agtype_build_vertex() label cannot be NULL")));
    }

    label = PG_GETARG_CSTRING(3);

    /* process end_id */
    if (fcinfo->args[2].isnull)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("_agtype_build_edge() endid cannot be NULL")));
    }

    end_id = AG_GETARG_GRAPHID(2);

    /* process start_id */
    if (fcinfo->args[1].isnull)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("_agtype_build_edge() startid cannot be NULL")));
    }

    start_id = AG_GETARG_GRAPHID(1);

    /* process properties */

    /* if the properties object is null, push an empty object */
    if (fcinfo->args[4].isnull)
    {
        agtype_build_state *bstate = init_agtype_build_state(0, AGT_FOBJECT);
        properties = build_agtype(bstate);
        pfree_agtype_build_state(bstate);
    }
    else
    {
        properties = AG_GET_ARG_AGTYPE_P(4);

        if (!AGT_ROOT_IS_OBJECT(properties))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("_agtype_build_edge() properties argument must be an object")));
        }
    }

    bstate = init_agtype_build_state(5, AGT_FOBJECT);
    write_string(bstate, "id");
    write_string(bstate, "label");
    write_string(bstate, "end_id");
    write_string(bstate, "start_id");
    write_string(bstate, "properties");
    write_graphid(bstate, id);
    write_string(bstate, label);
    write_graphid(bstate, end_id);
    write_graphid(bstate, start_id);
    write_container(bstate, properties);
    edge = build_agtype(bstate);
    pfree_agtype_build_state(bstate);

    bstate = init_agtype_build_state(1, AGT_FARRAY | AGT_FSCALAR);
    write_extended(bstate, edge, AGT_HEADER_EDGE);
    rawscalar = build_agtype(bstate);
    pfree_agtype_build_state(bstate);

    PG_FREE_IF_COPY(label, 3);
    PG_FREE_IF_COPY(properties, 4);

    PG_RETURN_POINTER(rawscalar);
}

Datum make_edge(Datum id, Datum startid, Datum endid, Datum label,
                Datum properties)
{
    return DirectFunctionCall5(_agtype_build_edge, id, startid, endid, label,
                               properties);
}

static agtype_value *agtype_build_map_as_agtype_value(FunctionCallInfo fcinfo)
{
    int nargs;
    int i;
    agtype_in_state result;
    Datum *args;
    bool *nulls;
    Oid *types;

    /* build argument values to build the object */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    if (nargs < 0)
    {
        return NULL;
    }

    if (nargs % 2 != 0)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
             errmsg("argument list must have been even number of elements"),
             errhint("The arguments of agtype_build_map() must consist of alternating keys and values.")));
    }

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);

    /* iterate through the arguments and build the object */
    for (i = 0; i < nargs; i += 2)
    {
        /* process key */
        if (nulls[i])
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument %d: key must not be null", i + 1)));
        }

        /*
         * If the key is agtype, we need to extract it as an agtype string and
         * push the value.
         */
        if (types[i] == AGTYPEOID)
        {
            agtype_value *agtv = NULL;

            agtv = tostring_helper(args[i], types[i],
                                   "agtype_build_map_as_agtype_value");
            if (agtv == NULL)
            {
                ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("argument %d: key must not be null", i + 1)));

            }

            result.res = push_agtype_value(&result.parse_state, WAGT_KEY, agtv);

            /* free the agtype_value from tostring_helper */
            pfree_if_not_null(agtv);
        }
        else
        {
            add_agtype(args[i], false, &result, types[i], true);
        }

        /* process value */
        add_agtype(args[i + 1], nulls[i + 1], &result, types[i + 1], false);
    }

    result.res = push_agtype_value(&result.parse_state, WAGT_END_OBJECT, NULL);
    return result.res;
}

PG_FUNCTION_INFO_V1(agtype_build_map);

/*
 * SQL function agtype_build_map(variadic "any")
 */
Datum agtype_build_map(PG_FUNCTION_ARGS)
{
    agtype_value *result = NULL;
    agtype *agt_result = NULL;

    result = agtype_build_map_as_agtype_value(fcinfo);
    if (result == NULL)
    {
        PG_RETURN_NULL();
    }

    agt_result = agtype_value_to_agtype(result);
    pfree_agtype_value(result);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(agtype_build_map_noargs);

/*
 * degenerate case of agtype_build_map where it gets 0 arguments.
 */
Datum agtype_build_map_noargs(PG_FUNCTION_ARGS)
{
    agtype_in_state result;
    agtype *agt_result;

    memset(&result, 0, sizeof(agtype_in_state));

    push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT, NULL);
    result.res = push_agtype_value(&result.parse_state, WAGT_END_OBJECT, NULL);

    agt_result = agtype_value_to_agtype(result.res);

    pfree_agtype_in_state(&result);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(agtype_build_map_nonull);

/*
 * Similar to agtype_build_map except null properties are removed.
 */
Datum agtype_build_map_nonull(PG_FUNCTION_ARGS)
{
    agtype_value *result = NULL;
    agtype *agt_result;

    result = agtype_build_map_as_agtype_value(fcinfo);
    if (result == NULL)
    {
        PG_RETURN_NULL();
    }

    remove_null_from_agtype_object(result);
    agt_result = agtype_value_to_agtype(result);

    pfree_agtype_value(result);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(agtype_build_list);

/*
 * SQL function agtype_build_list(variadic "any")
 */
Datum agtype_build_list(PG_FUNCTION_ARGS)
{
    int nargs;
    int i;
    agtype_in_state result;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype *agt_result;

    /*build argument values to build the array */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    if (nargs < 0)
    {
        PG_RETURN_NULL();
    }

    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY,
                                   NULL);

    for (i = 0; i < nargs; i++)
    {
        add_agtype(args[i], nulls[i], &result, types[i], false);
        PG_FREE_IF_COPY(DatumGetPointer(args[i]), i);
    }

    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);
    agt_result = agtype_value_to_agtype(result.res);

    pfree_agtype_in_state(&result);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(agtype_build_list_noargs);

/*
 * degenerate case of agtype_build_list where it gets 0 arguments.
 */
Datum agtype_build_list_noargs(PG_FUNCTION_ARGS)
{
    agtype_in_state result;
    agtype *agt_result;

    memset(&result, 0, sizeof(agtype_in_state));

    push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY, NULL);
    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

    agt_result = agtype_value_to_agtype(result.res);

    pfree_agtype_in_state(&result);

    PG_RETURN_POINTER(agt_result);
}

/*
 * Extract scalar value from raw-scalar pseudo-array agtype.
 */
static bool agtype_extract_scalar(agtype_container *agtc, agtype_value *res)
{
    agtype_iterator *it;
    agtype_iterator_token tok PG_USED_FOR_ASSERTS_ONLY;
    agtype_value tmp;

    if (!AGTYPE_CONTAINER_IS_ARRAY(agtc) || !AGTYPE_CONTAINER_IS_SCALAR(agtc))
    {
        /* inform caller about actual type of container */
        res->type = AGTYPE_CONTAINER_IS_ARRAY(agtc) ? AGTV_ARRAY : AGTV_OBJECT;
        return false;
    }

    /*
     * A root scalar is stored as an array of one element, so we get the array
     * and then its first (and only) member.
     */
    it = agtype_iterator_init(agtc);

    tok = agtype_iterator_next(&it, &tmp, true);
    Assert(tok == WAGT_BEGIN_ARRAY);
    Assert(tmp.val.array.num_elems == 1 && tmp.val.array.raw_scalar);

    tok = agtype_iterator_next(&it, res, true);
    Assert(tok == WAGT_ELEM);
    Assert(IS_A_AGTYPE_SCALAR(res));

    tok = agtype_iterator_next(&it, &tmp, true);
    Assert(tok == WAGT_END_ARRAY);

    tok = agtype_iterator_next(&it, &tmp, true);
    Assert(tok == WAGT_DONE);

    return true;
}

/*
 * Emit correct, translatable cast error message
 */
static void cannot_cast_agtype_value(enum agtype_value_type type,
                                     const char *sqltype)
{
    static const struct
    {
        enum agtype_value_type type;
        const char *msg;
    } messages[] = {
        {AGTV_NULL, gettext_noop("cannot cast agtype null to type %s")},
        {AGTV_STRING, gettext_noop("cannot cast agtype string to type %s")},
        {AGTV_NUMERIC, gettext_noop("cannot cast agtype numeric to type %s")},
        {AGTV_INTEGER, gettext_noop("cannot cast agtype integer to type %s")},
        {AGTV_FLOAT, gettext_noop("cannot cast agtype float to type %s")},
        {AGTV_BOOL, gettext_noop("cannot cast agtype boolean to type %s")},
        {AGTV_ARRAY, gettext_noop("cannot cast agtype array to type %s")},
        {AGTV_OBJECT, gettext_noop("cannot cast agtype object to type %s")},
        {AGTV_VERTEX, gettext_noop("cannot cast agtype vertex to type %s")},
        {AGTV_EDGE, gettext_noop("cannot cast agtype edge to type %s")},
        {AGTV_PATH, gettext_noop("cannot cast agtype path to type %s")},
        {AGTV_BINARY,
         gettext_noop("cannot cast agtype array or object to type %s")}};
    int i;

    for (i = 0; i < lengthof(messages); i++)
    {
        if (messages[i].type == type)
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg(messages[i].msg, sqltype)));
        }
    }

    /* should be unreachable */
    elog(ERROR, "unknown agtype type: %d", (int)type);
}

PG_FUNCTION_INFO_V1(agtype_to_bool);

/*
 * Cast agtype to boolean. From jsonb_bool().
 */
Datum agtype_to_bool(PG_FUNCTION_ARGS)
{
    agtype *agtype_in = AG_GET_ARG_AGTYPE_P(0);
    agtype_value agtv;

    if (!agtype_extract_scalar(&agtype_in->root, &agtv) ||
        agtv.type != AGTV_BOOL)
    {
        cannot_cast_agtype_value(agtv.type, "boolean");
    }

    PG_FREE_IF_COPY(agtype_in, 0);

    PG_RETURN_BOOL(agtv.val.boolean);
}

PG_FUNCTION_INFO_V1(agtype_to_int8);
/*
 * Cast agtype to int8.
 */
Datum agtype_to_int8(PG_FUNCTION_ARGS)
{
    agtype_value agtv;
    agtype_value *agtv_p = NULL;
    agtype_value *container = NULL;
    int64 result = 0x0;
    agtype *arg_agt = NULL;
    bool is_scalar = false;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    /* get the scalar value if it is one and set the flag accordingly */
    is_scalar = agtype_extract_scalar(&arg_agt->root, &agtv);

    /* if it isn't something that can be cast error out */
    if (!is_scalar ||
        (agtv.type != AGTV_FLOAT &&
         agtv.type != AGTV_INTEGER &&
         agtv.type != AGTV_NUMERIC &&
         agtv.type != AGTV_STRING &&
         agtv.type != AGTV_BOOL))
    {
        cannot_cast_agtype_value(agtv.type, "int");
    }

    agtv_p = &agtv;

    /*
     * If it is an agtype string, we need to convert the string component first.
     * We need to do this because the string could be any type of value. Fx,
     * integer, float, boolean, numeric, object, or array. Once converted, we
     * need to remember scalar values are returned as a scalar array. We only
     * care about scalar arrays.
     */
    if (agtv_p->type == AGTV_STRING)
    {
        agtype_value *temp = NULL;

        /*
         * Convert the string to an agtype_value. Remember that a returned
         * scalar value is returned in a one element array.
         */
        temp = agtype_value_from_cstring(agtv_p->val.string.val,
                                         agtv_p->val.string.len);

        /* this will catch anything that isn't an array and isn't a scalar */
        if (temp->type != AGTV_ARRAY ||
            !temp->val.array.raw_scalar)
        {
            ereport(ERROR,
                   (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("invalid agtype string to int8 type: %d",
                           temp->type)));
        }

        /* save the top agtype_value */
        container = temp;
        /* get the wrapped agtype_value */
        temp = &temp->val.array.elems[0];

        /* these we expect */
        if (temp->type == AGTV_FLOAT ||
            temp->type == AGTV_INTEGER ||
            temp->type == AGTV_NUMERIC ||
            temp->type == AGTV_BOOL)
        {
            agtv_p = temp;
        }
        else
        {
            elog(ERROR, "unexpected string type: %d in agtype_to_int8",
                        temp->type);
        }
    }

    /* now check the rest */
    if (agtv_p->type == AGTV_INTEGER)
    {
        result = agtv_p->val.int_value;
    }
    else if (agtv_p->type == AGTV_FLOAT)
    {
        result = DatumGetInt64(DirectFunctionCall1(dtoi8,
                     Float8GetDatum(agtv_p->val.float_value)));
    }
    else if (agtv_p->type == AGTV_NUMERIC)
    {
        result = DatumGetInt64(DirectFunctionCall1(numeric_int8,
                     NumericGetDatum(agtv_p->val.numeric)));
    }
    else if(agtv_p->type == AGTV_BOOL)
    {
        result = (agtv_p->val.boolean) ? 1 : 0;
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid conversion type in agtype_to_int8: %d",
                        agtv_p->type)));
    }

    /* free the container, if it was used */
    if (container)
    {
        pfree_if_not_null(container);
    }

    PG_FREE_IF_COPY(arg_agt, 0);

    PG_RETURN_INT64(result);
}

PG_FUNCTION_INFO_V1(agtype_to_int4);

/*
 * Cast agtype to int4.
 */
Datum agtype_to_int4(PG_FUNCTION_ARGS)
{
    agtype_value agtv;
    agtype_value *agtv_p = NULL;
    agtype_value *container = NULL;
    int32 result = 0x0;
    agtype *arg_agt = NULL;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    if (!agtype_extract_scalar(&arg_agt->root, &agtv) ||
        (agtv.type != AGTV_FLOAT &&
         agtv.type != AGTV_INTEGER &&
         agtv.type != AGTV_NUMERIC &&
         agtv.type != AGTV_STRING &&
         agtv.type != AGTV_BOOL))
    {
        cannot_cast_agtype_value(agtv.type, "int");
    }

    agtv_p = &agtv;

    /*
     * If it is an agtype string, we need to convert the string component first.
     * We need to do this because the string could be any type of value. Fx,
     * integer, float, boolean, numeric, object, or array. Once converted, we
     * need to remember scalar values are returned as a scalar array. We only
     * care about scalar arrays.
     */
    if (agtv_p->type == AGTV_STRING)
    {
        agtype_value *temp = NULL;

        /*
         * Convert the string to an agtype_value. Remember that a returned
         * scalar value is returned in a one element array.
         */
        temp = agtype_value_from_cstring(agtv_p->val.string.val,
                                         agtv_p->val.string.len);

        /* this will catch anything that isn't an array and isn't a scalar */
        if (temp->type != AGTV_ARRAY ||
            !temp->val.array.raw_scalar)
        {
            ereport(ERROR,
                   (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("invalid agtype string to int4 type: %d",
                           (int)temp->type)));
        }

        /* save the top agtype_value */
        container = temp;
        /* get the wrapped agtype_value */
        temp = &temp->val.array.elems[0];

        /* these we expect */
        if (temp->type == AGTV_FLOAT ||
            temp->type == AGTV_INTEGER ||
            temp->type == AGTV_NUMERIC ||
            temp->type == AGTV_BOOL)
        {
            agtv_p = temp;
        }
        else
        {
            elog(ERROR, "unexpected string type: %d in agtype_to_int4",
                        (int)temp->type);
        }
    }

    /* now check the rest */
    if (agtv_p->type == AGTV_INTEGER)
    {
        result = DatumGetInt32(DirectFunctionCall1(int84,
                     Int64GetDatum(agtv_p->val.int_value)));
    }
    else if (agtv_p->type == AGTV_FLOAT)
    {
        result = DatumGetInt32(DirectFunctionCall1(dtoi4,
                     Float8GetDatum(agtv_p->val.float_value)));
    }
    else if (agtv_p->type == AGTV_NUMERIC)
    {
        result = DatumGetInt32(DirectFunctionCall1(numeric_int4,
                     NumericGetDatum(agtv_p->val.numeric)));
    }
    else if (agtv_p->type == AGTV_BOOL)
    {
        result = (agtv_p->val.boolean) ? 1 : 0;
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid conversion type in agtype_to_int4: %d",
                        (int)agtv_p->type)));
    }

    /* free the container, if it was used */
    if (container)
    {
        pfree_if_not_null(container);
    }

    PG_FREE_IF_COPY(arg_agt, 0);

    PG_RETURN_INT32(result);
}

PG_FUNCTION_INFO_V1(agtype_to_int2);

/*
 * Cast agtype to int2.
 */
Datum agtype_to_int2(PG_FUNCTION_ARGS)
{
    agtype_value agtv;
    agtype_value *agtv_p = NULL;
    agtype_value *container = NULL;
    int16 result = 0x0;
    agtype *arg_agt = NULL;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    if (!agtype_extract_scalar(&arg_agt->root, &agtv) ||
        (agtv.type != AGTV_FLOAT &&
         agtv.type != AGTV_INTEGER &&
         agtv.type != AGTV_NUMERIC &&
         agtv.type != AGTV_STRING &&
         agtv.type != AGTV_BOOL))
    {
        cannot_cast_agtype_value(agtv.type, "int");
    }

    agtv_p = &agtv;

    /*
     * If it is an agtype string, we need to convert the string component first.
     * We need to do this because the string could be any type of value. Fx,
     * integer, float, boolean, numeric, object, or array. Once converted, we
     * need to remember scalar values are returned as a scalar array. We only
     * care about scalar arrays.
     */
    if (agtv_p->type == AGTV_STRING)
    {
        agtype_value *temp = NULL;

        /*
         * Convert the string to an agtype_value. Remember that a returned
         * scalar value is returned in a one element array.
         */
        temp = agtype_value_from_cstring(agtv_p->val.string.val,
                                         agtv_p->val.string.len);

        /* this will catch anything that isn't an array and isn't a scalar */
        if (temp->type != AGTV_ARRAY ||
            !temp->val.array.raw_scalar)
        {
            ereport(ERROR,
                   (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("invalid agtype string to int2 type: %d",
                           (int)temp->type)));
        }

        /* save the top agtype_value */
        container = temp;
        /* get the wrapped agtype_value */
        temp = &temp->val.array.elems[0];

        /* these we expect */
        if (temp->type == AGTV_FLOAT ||
            temp->type == AGTV_INTEGER ||
            temp->type == AGTV_NUMERIC ||
            temp->type == AGTV_BOOL)
        {
            agtv_p = temp;
        }
        else
        {
            elog(ERROR, "unexpected string type: %d in agtype_to_int2",
                        (int)temp->type);
        }
    }

    /* now check the rest */
    if (agtv_p->type == AGTV_INTEGER)
    {
        result = DatumGetInt16(DirectFunctionCall1(int82,
                     Int64GetDatum(agtv_p->val.int_value)));
    }
    else if (agtv_p->type == AGTV_FLOAT)
    {
        result = DatumGetInt16(DirectFunctionCall1(dtoi2,
                     Float8GetDatum(agtv_p->val.float_value)));
    }
    else if (agtv_p->type == AGTV_NUMERIC)
    {
        result = DatumGetInt16(DirectFunctionCall1(numeric_int2,
                     NumericGetDatum(agtv_p->val.numeric)));
    }
    else if (agtv_p->type == AGTV_BOOL)
    {
        result = (agtv_p->val.boolean) ? 1 : 0;
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid conversion type in agtype_to_int2: %d",
                        (int)agtv_p->type)));

    }

    /* free the container, if it was used */
    if (container)
    {
        pfree_if_not_null(container);
    }

    PG_FREE_IF_COPY(arg_agt, 0);

    PG_RETURN_INT16(result);
}

PG_FUNCTION_INFO_V1(agtype_to_float8);

/*
 * Cast agtype to float8.
 */
Datum agtype_to_float8(PG_FUNCTION_ARGS)
{
    agtype *agtype_in = AG_GET_ARG_AGTYPE_P(0);
    agtype_value agtv;
    float8 result;

    if (!agtype_extract_scalar(&agtype_in->root, &agtv) ||
        (agtv.type != AGTV_FLOAT &&
         agtv.type != AGTV_INTEGER &&
         agtv.type != AGTV_NUMERIC &&
         agtv.type != AGTV_STRING))
    {
        cannot_cast_agtype_value(agtv.type, "float");
    }

    PG_FREE_IF_COPY(agtype_in, 0);

    if (agtv.type == AGTV_FLOAT)
    {
        result = agtv.val.float_value;
    }
    else if (agtv.type == AGTV_INTEGER)
    {
        /*
         * Get the string representation of the integer because it could be
         * too large to fit in a float. Let the float routine determine
         * what to do with it.
         */
        char *string = DatumGetCString(DirectFunctionCall1(int8out,
                           Int64GetDatum(agtv.val.int_value)));
        bool is_valid = false;
        /* turn it into a float */
        result = float8in_internal_null(string, NULL, "double precision",
                                     string, &is_valid);

        /* return null if it was not a invalid float */
        if (!is_valid)
            ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                            errmsg("cannot cast to float8, integer value out of range")));
    }
    else if (agtv.type == AGTV_NUMERIC)
    {
        result = DatumGetFloat8(DirectFunctionCall1(numeric_float8,
                     NumericGetDatum(agtv.val.numeric)));
    }
    else if (agtv.type == AGTV_STRING)
    {
        result = DatumGetFloat8(DirectFunctionCall1(float8in,
                                                    CStringGetDatum(agtv.val.string.val)));
    }
    else
    {
        elog(ERROR, "invalid agtype type: %d", (int)agtv.type);
    }

    PG_RETURN_FLOAT8(result);
}

PG_FUNCTION_INFO_V1(agtype_to_text);

/*
 * Cast agtype to text.
 */
Datum agtype_to_text(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value *arg_value;
    text *text_value;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    /* check that we have a scalar value */
    if (!AGT_ROOT_IS_SCALAR(arg_agt))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("agtype argument must resolve to a scalar value")));
    }

    /* get the arg parameter */
    arg_value = get_ith_agtype_value_from_container(&arg_agt->root, 0);
    PG_FREE_IF_COPY(arg_agt, 0);

    text_value = agtype_value_to_text(arg_value, true);
    pfree_agtype_value(arg_value);

    if (text_value == NULL)
    {
        PG_RETURN_NULL();
    }

    PG_RETURN_TEXT_P(text_value);
}

PG_FUNCTION_INFO_V1(text_to_agtype);

/*
 * Cast text to agtype.
 */
Datum text_to_agtype(PG_FUNCTION_ARGS)
{
    agtype *result = NULL;
    agtype_value agtv;
    text *text_value = NULL;
    char *string = NULL;
    int len = 0;

    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    /* get the text value */
    text_value = PG_GETARG_TEXT_PP(0);
    /* convert it to a string */
    string = text_to_cstring(text_value);
    /* get the length */
    len = strlen(string);

    /* create a temporary agtype string */
    agtv.type = AGTV_STRING;
    agtv.val.string.len = len;
    agtv.val.string.val = pstrdup(string);

    /* free the string */
    pfree_if_not_null(string);

    /* convert to agtype */
    result = agtype_value_to_agtype(&agtv);

    /* free the input arg if necessary */
    PG_FREE_IF_COPY(text_value, 0);

    /* return our result */
    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(agtype_to_json);

/*
 * Cast agtype to json.
 *
 * If the input agtype is vertex, edge or path, the trailing
 * type(::vertex, ::edge, ::path) is removed.
 */
Datum agtype_to_json(PG_FUNCTION_ARGS)
{
    Datum result;
    char *json_str;
    agtype *agt;

    agt = AG_GET_ARG_AGTYPE_P(0);

    if (AGT_ROOT_IS_SCALAR(agt))
    {
        enum agtype_value_type type;

        type = get_ith_agtype_value_type(&agt->root, 0);
        if (type >= AGTV_NUMERIC && type <= AGTV_BOOL)
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("cannot cast agtype %s to json",
                             agtype_value_type_to_string(type))));
        }
    }

    json_str = agtype_to_cstring_worker(NULL, &agt->root, VARSIZE(agt),
                                        false, false);

    result = DirectFunctionCall1(json_in, CStringGetDatum(json_str));

    PG_FREE_IF_COPY(agt, 0);
    pfree_if_not_null(json_str);

    PG_RETURN_DATUM(result);
}

PG_FUNCTION_INFO_V1(bool_to_agtype);

/*
 * Cast boolean to agtype.
 */
Datum bool_to_agtype(PG_FUNCTION_ARGS)
{
    return boolean_to_agtype(PG_GETARG_BOOL(0));
}

PG_FUNCTION_INFO_V1(float8_to_agtype);

/*
 * Cast float8 to agtype.
 */
Datum float8_to_agtype(PG_FUNCTION_ARGS)
{
    return float_to_agtype(PG_GETARG_FLOAT8(0));
}

PG_FUNCTION_INFO_V1(int8_to_agtype);

/*
 * Cast int8 to agtype.
 */
Datum int8_to_agtype(PG_FUNCTION_ARGS)
{
    return integer_to_agtype(PG_GETARG_INT64(0));
}

PG_FUNCTION_INFO_V1(int4_to_agtype);
/*
 * Cast int to agtype.
 */
Datum int4_to_agtype(PG_FUNCTION_ARGS)
{
    return integer_to_agtype((int64)PG_GETARG_INT32(0));
}

PG_FUNCTION_INFO_V1(agtype_to_int4_array);

/*
 * Cast agtype to int4[].
 *
 * TODO:
 *
 * We either need to change the function definition in age--x.x.x.sql
 * to something like agtype[] or we need to make this function work
 * for "any" type input. Right now it only works for an agtype array but
 * it takes "any" input. Hence the additional code added to block anything
 * other than agtype.
 */
Datum agtype_to_int4_array(PG_FUNCTION_ARGS)
{
    agtype_iterator *agtype_iterator = NULL;
    agtype *agtype_in = NULL;
    agtype_value agtv;
    agtype_iterator_token agtv_token;
    Datum *array_value;
    ArrayType *result;
    Oid arg_type = InvalidOid;
    int element_size;
    int i;

    /* get the input data type */
    arg_type = get_fn_expr_argtype(fcinfo->flinfo, 0);

    /* verify the input is agtype */
    if (arg_type != AGTYPEOID)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("argument must resolve to agtype")));
    }

    agtype_in = AG_GET_ARG_AGTYPE_P(0);

    agtype_iterator = agtype_iterator_init(&agtype_in->root);
    agtv_token = agtype_iterator_next(&agtype_iterator, &agtv, false);

    if (agtv.type != AGTV_ARRAY)
    {
        cannot_cast_agtype_value(agtv.type, "int4[]");
    }

    element_size = agtv.val.array.num_elems;
    array_value = (Datum *) palloc(sizeof(Datum) * element_size);

    i = 0;
    while ((agtv_token = agtype_iterator_next(&agtype_iterator, &agtv, true)) != WAGT_END_ARRAY)
    {
        int32 element_value = 0;
        if (agtv.type == AGTV_INTEGER)
            element_value = DatumGetInt32(DirectFunctionCall1(int84,
                                                              Int64GetDatum(agtv.val.int_value)));
        else if (agtv.type == AGTV_FLOAT)
            element_value = DatumGetInt32(DirectFunctionCall1(dtoi4,
                                                              Float8GetDatum(agtv.val.float_value)));
        else if (agtv.type == AGTV_NUMERIC)
            element_value = DatumGetInt32(DirectFunctionCall1(numeric_int4,
                                                              NumericGetDatum(agtv.val.numeric)));
        else if (agtv.type == AGTV_STRING)
            element_value = DatumGetInt32(DirectFunctionCall1(int4in,
                                                              CStringGetDatum(agtv.val.string.val)));
        array_value[i++] = element_value;
    }

    result = construct_array(array_value, element_size, INT4OID, 4, true, 'i');

    PG_RETURN_ARRAYTYPE_P(result);
}

/*
 * Helper function for agtype_access_operator map access.
 * Note: This function expects that a map and a scalar key are being passed.
 */
static agtype_value *execute_map_access_operator(agtype *map,
                                                 agtype_value *map_value,
                                                 agtype *key)
{
    agtype_value *key_value;
    char *key_str;
    int key_len = 0;

    /* get the key from the container */
    key_value = get_ith_agtype_value_from_container(&key->root, 0);

    switch (key_value->type)
    {
    case AGTV_NULL:
        return NULL;

    case AGTV_INTEGER:
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("AGTV_INTEGER is not a valid key type")));
        break;
    case AGTV_FLOAT:
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("AGTV_FLOAT is not a valid key type")));
        break;
    case AGTV_NUMERIC:
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("AGTV_NUMERIC is not a valid key type")));
        break;
    case AGTV_BOOL:
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("AGTV_BOOL is not a valid key type")));
        break;
    case AGTV_STRING:
        key_str = key_value->val.string.val;
        key_len = key_value->val.string.len;
        break;
    default:
        ereport(ERROR, (errmsg("unknown agtype scalar type")));
        break;
    }

    return execute_map_access_operator_internal(map, map_value, key_str,
                                                key_len);
}

static agtype_value *execute_map_access_operator_internal(
    agtype *map, agtype_value *map_value, char *key, int key_len)
{
    agtype_value new_key_value;
    new_key_value.type = AGTV_STRING;
    new_key_value.val.string.val = key;
    new_key_value.val.string.len = key_len;
    /* if we were passed an agtype */
    if (map_value == NULL)
    {
        map_value = find_agtype_value_from_container(&map->root, AGT_FOBJECT,
                                                     &new_key_value);
    }
    /* if we were passed an agtype_value OBJECT (BINARY) */
    else if (map_value != NULL && map_value->type == AGTV_BINARY)
    {
        map_value = find_agtype_value_from_container(map_value->val.binary.data,
                                                     AGT_FOBJECT,
                                                     &new_key_value);
    }
    /* if we were passed an agtype_value OBJECT */
    else if (map_value != NULL && map_value->type == AGTV_OBJECT)
    {
        map_value = get_agtype_value_object_value(map_value, key, key_len);
    }
    /* otherwise, we don't know how to process it */
    else
    {
        ereport(ERROR, (errmsg("unknown map_value type")));
    }

    /* return the agtype_value */
    return map_value;
}

/*
 * Helper function for agtype_access_operator array access.
 * Note: This function expects that an array and a scalar int are being passed.
 */
static agtype_value *execute_array_access_operator(agtype *array,
                                                   agtype_value *array_value,
                                                   agtype *array_index)
{
    agtype_value *array_index_value = NULL;
    agtype_value *result = NULL;

    /* unpack the array index value */
    array_index_value = get_ith_agtype_value_from_container(&array_index->root,
                                                            0);

    /* if AGTV_NULL return NULL */
    if (array_index_value->type == AGTV_NULL)
    {
        pfree_agtype_value(array_index_value);
        return NULL;
    }

    /* index must be an integer */
    if (array_index_value->type != AGTV_INTEGER)
    {
        ereport(ERROR,
                (errmsg("array index must resolve to an integer value")));
    }

    result =  execute_array_access_operator_internal(array, array_value,
                                                     array_index_value->val.int_value);

    pfree_agtype_value(array_index_value);
    return result;
}

static agtype_value *execute_array_access_operator_internal(agtype *array,
                                                            agtype_value *array_value,
                                                            int64 array_index)
{
    agtype_value *array_element_value = NULL;
    uint32 size = 0;

    /* get the size of the array, given the type of the input */
    if (array_value == NULL)
    {
        size = AGT_ROOT_COUNT(array);
    }
    else if (array_value->type == AGTV_ARRAY)
    {
        size = array_value->val.array.num_elems;
    }
    else if (array_value->type == AGTV_BINARY)
    {
        size = AGTYPE_CONTAINER_SIZE(array_value->val.binary.data);
    }
    else
    {
        elog(ERROR, "execute_array_access_operator_internal: unexpected type");
    }

    /* adjust for negative index values */
    if (array_index < 0)
    {
        array_index = size + array_index;
    }

    /* check array bounds */
    if ((array_index >= size) || (array_index < 0))
    {
        return NULL;
    }

    /* if we were passed an agtype */
    if (array_value == NULL)
    {
        array_element_value = get_ith_agtype_value_from_container(&array->root,
                                                                  array_index);
    }
    /* if we were passed an agtype_value ARRAY (BINARY) */
    else if (array_value != NULL && array_value->type == AGTV_BINARY)
    {
        array_element_value = get_ith_agtype_value_from_container(
            array_value->val.binary.data, array_index);
    }
    /* if we were passed an agtype_value ARRAY */
    else if (array_value != NULL && array_value->type == AGTV_ARRAY)
    {
        array_element_value = &array_value->val.array.elems[array_index];
    }
    /* otherwise, we don't know how to process it */
    else
    {
        ereport(ERROR, (errmsg("unknown array_value type")));
    }

    return array_element_value;
}

/*
 * Helper function to do a binary search through an object's key/value pairs,
 * looking for a specific key. It will return the key or NULL if not found.
 */
agtype_value *get_agtype_value_object_value(const agtype_value *agtv_object,
                                            char *search_key,
                                            int search_key_length)
{
    agtype_value *agtv_key = NULL;
    int current_key_length = 0;
    int middle = 0;
    int num_pairs = 0;
    int left = 0;
    int right = 0;
    int result = 0;

    if (agtv_object == NULL || search_key == NULL || search_key_length <= 0)
    {
        return NULL;
    }

    /* get the number of object pairs */
    num_pairs = agtv_object->val.object.num_pairs;

    /* do a binary search through the pairs */
    right = num_pairs - 1;
    middle = num_pairs / 2;

    /* while middle is within the constraints */
    while (middle >= left && middle <= right)
    {
        /* get the current key length */
        agtv_key = &agtv_object->val.object.pairs[middle].key;
        current_key_length = agtv_key->val.string.len;

        /* if not the same length, divide the search space and continue */
        if (current_key_length != search_key_length)
        {
            /* if we need to search in the lower half */
            if (search_key_length < current_key_length)
            {
                middle -= 1;
                right = middle;
                middle = ((middle - left) / 2) + left;
            }
            /* else we need to search in the upper half */
            else
            {
                middle += 1;
                left = middle;
                middle = ((right - middle) / 2) + left;
            }
            continue;
        }

        /* they are the same length so compare the keys */
        result = strncmp(search_key, agtv_key->val.string.val,
                         search_key_length);

        /* if they don't match */
        if (result != 0)
        {
            /* if smaller */
            if (result < 0)
            {
                middle -= 1;
                right = middle;
                middle = ((middle - left) / 2) + left;
            }
            /* if larger */
            else
            {
                middle += 1;
                left = middle;
                middle = ((right - middle) / 2) + left;
            }
            continue;
        }

        /* they match */
        return (&agtv_object->val.object.pairs[middle].value);
    }

    /* they don't match */
    return NULL;
}

/*
 * From PG's extract_variadic_args
 *
 * This function allows you to pass the minimum number of required arguments
 * so that you can have it bail out early (without allocating and building
 * the output arrays). In this case, the returned number of args will be 0
 * and the args array will be NULL.
 */
static int extract_variadic_args_min(FunctionCallInfo fcinfo,
                                     int variadic_start, bool convert_unknown,
                                     Datum **args, Oid **types, bool **nulls,
                                     int min_num_args)
{
    bool variadic = get_fn_expr_variadic(fcinfo->flinfo);
    Datum *args_res = NULL;
    bool *nulls_res = NULL;
    Oid *types_res = NULL;
    int nargs = 0;
    int i = 0;

    *args = NULL;
    *types = NULL;
    *nulls = NULL;

    if (variadic)
    {
        ArrayType *array_in = NULL;
        Oid element_type = InvalidOid;
        bool typbyval = false;
        char typalign = 0;
        int16 typlen = 0;

        Assert(PG_NARGS() == variadic_start + 1);

        if (PG_ARGISNULL(variadic_start))
        {
            return -1;
        }

        /* get the array */
        array_in = PG_GETARG_ARRAYTYPE_P(variadic_start);

        /* verify that we have the minimum number of args */
        if (ArrayGetNItems(ARR_NDIM(array_in), ARR_DIMS(array_in)) <
            min_num_args)
        {
            return 0;
        }

        /* get the element type */
        element_type = ARR_ELEMTYPE(array_in);

        get_typlenbyvalalign(element_type, &typlen, &typbyval, &typalign);
        deconstruct_array(array_in, element_type, typlen, typbyval, typalign,
                          &args_res, &nulls_res, &nargs);

        /* All the elements of the array have the same type */
        types_res = (Oid *) palloc0(nargs * sizeof(Oid));
        for (i = 0; i < nargs; i++)
        {
            types_res[i] = element_type;
        }
    }
    else
    {
        /* get the number of arguments */
        nargs = PG_NARGS() - variadic_start;
        Assert(nargs > 0);

        /* verify that we have the minimum number of args */
        if (nargs < min_num_args)
        {
            return 0;
        }

        /* allocate result memory */
        nulls_res = (bool *) palloc0(nargs * sizeof(bool));
        args_res = (Datum *) palloc0(nargs * sizeof(Datum));
        types_res = (Oid *) palloc0(nargs * sizeof(Oid));

        for (i = 0; i < nargs; i++)
        {
            nulls_res[i] = PG_ARGISNULL(i + variadic_start);
            types_res[i] = get_fn_expr_argtype(fcinfo->flinfo, i + variadic_start);

            /*
             * Turn a constant (more or less literal) value that's of unknown
             * type into text if required. Unknowns come in as a cstring
             * pointer. Note: for functions declared as taking type "any", the
             * parser will not do any type conversion on unknown-type literals
             * (that is, undecorated strings or NULLs).
             */
            if (convert_unknown &&
                types_res[i] == UNKNOWNOID &&
                get_fn_expr_arg_stable(fcinfo->flinfo, i + variadic_start))
            {
                types_res[i] = TEXTOID;

                if (PG_ARGISNULL(i + variadic_start))
                {
                    args_res[i] = (Datum) 0;
                }
                else
                {
                    args_res[i] = CStringGetTextDatum(PG_GETARG_POINTER(i + variadic_start));
                }
            }
            else
            {
                /* no conversion needed, just take the datum as given */
                args_res[i] = PG_GETARG_DATUM(i + variadic_start);
            }

            if (!OidIsValid(types_res[i]) ||
                (convert_unknown && types_res[i] == UNKNOWNOID))
            {
                ereport(ERROR,
                        (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                         errmsg("could not determine data type for argument %d",
                                i + 1)));
            }
        }
    }

    /* Fill in results */
    *args = args_res;
    *nulls = nulls_res;
    *types = types_res;

    return nargs;
}

static Datum process_access_operator_result(FunctionCallInfo fcinfo,
                                            agtype_value *agtv,
                                            bool as_text)
{
    if (agtv != NULL)
    {
        if (as_text)
        {
            text *result;

            if (agtv->type == AGTV_BINARY)
            {
                StringInfo out = makeStringInfo();
                agtype_container *agtc =
                    (agtype_container *)agtv->val.binary.data;
                char *str;

                str = agtype_to_cstring_worker(out, agtc,
                                               agtv->val.binary.len,
                                               false, true);
                result = cstring_to_text(str);
            }
            else
            {
                result = agtype_value_to_text(agtv, false);
            }

            if (result)
            {
                PG_RETURN_TEXT_P(result);
            }
        }
        else
        {
            AG_RETURN_AGTYPE_P(agtype_value_to_agtype(agtv));
        }
    }

    PG_RETURN_NULL();
}

Datum agtype_array_element_impl(FunctionCallInfo fcinfo, agtype *agtype_in,
                                int element, bool as_text)
{
    agtype_value *v;

    if (!AGT_ROOT_IS_ARRAY(agtype_in))
    {
        PG_RETURN_NULL();
    }

    v = execute_array_access_operator_internal(agtype_in, NULL, element);

    return process_access_operator_result(fcinfo, v, as_text);
}

Datum agtype_object_field_impl(FunctionCallInfo fcinfo, agtype *agtype_in,
                               char *key, int key_len, bool as_text)
{
    agtype_value *v;
    agtype* process_agtype;

    if (AGT_ROOT_IS_SCALAR(agtype_in))
    {
        agtype_value *process_agtv = extract_entity_properties(agtype_in,
                                                               false);
        if (!process_agtv)
        {
            PG_RETURN_NULL();
        }

        process_agtype = agtype_value_to_agtype(process_agtv);
    }
    else
    {
        process_agtype = agtype_in;
    }

    if (!AGT_ROOT_IS_OBJECT(process_agtype))
    {
        PG_RETURN_NULL();
    }

    v = execute_map_access_operator_internal(process_agtype, NULL,
                                             key, key_len);

    return process_access_operator_result(fcinfo, v, as_text);
}

PG_FUNCTION_INFO_V1(agtype_object_field_agtype);

Datum agtype_object_field_agtype(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    agtype *key = AG_GET_ARG_AGTYPE_P(1);

    if (AGT_ROOT_IS_SCALAR(key))
    {
        agtype_value *key_value;

        key_value = get_ith_agtype_value_from_container(&key->root, 0);

        if (key_value->type == AGTV_INTEGER ||
            key_value->type == AGTV_STRING)
        {
            Datum retval = 0;

            if (key_value->type == AGTV_INTEGER)
            {
                retval = agtype_array_element_impl(fcinfo, agt,
                                                   key_value->val.int_value,
                                                   false);
            }
            else if (key_value->type == AGTV_STRING)
            {
                retval = agtype_object_field_impl(fcinfo, agt,
                                                  key_value->val.string.val,
                                                  key_value->val.string.len,
                                                  false);
            }

            pfree_agtype_value(key_value);
            PG_FREE_IF_COPY(agt, 0);
            PG_FREE_IF_COPY(key, 1);

            PG_RETURN_POINTER((const void*) retval);
        }
        pfree_agtype_value(key_value);
    }

    PG_FREE_IF_COPY(agt, 0);
    PG_FREE_IF_COPY(key, 1);
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_object_field_text_agtype);

Datum agtype_object_field_text_agtype(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    agtype *key = AG_GET_ARG_AGTYPE_P(1);

    if (AGT_ROOT_IS_SCALAR(key))
    {
        agtype_value *key_value;

        key_value = get_ith_agtype_value_from_container(&key->root, 0);

        if (key_value->type == AGTV_INTEGER || key_value->type == AGTV_STRING)
        {
            Datum retval = 0;

            if (key_value->type == AGTV_INTEGER)
            {
                retval = agtype_array_element_impl(fcinfo, agt,
                                                   key_value->val.int_value,
                                                   true);
            }
            else if (key_value->type == AGTV_STRING)
            {
                retval = agtype_object_field_impl(fcinfo, agt,
                                                  key_value->val.string.val,
                                                  key_value->val.string.len,
                                                  true);
            }

            pfree_agtype_value(key_value);
            PG_FREE_IF_COPY(agt, 0);
            PG_FREE_IF_COPY(key, 1);

            PG_RETURN_POINTER((const void*) retval);
        }
        pfree_agtype_value(key_value);
    }

    PG_FREE_IF_COPY(agt, 0);
    PG_FREE_IF_COPY(key, 1);
    PG_RETURN_NULL();
}

PG_FUNCTION_INFO_V1(agtype_object_field);

Datum agtype_object_field(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    text *key = PG_GETARG_TEXT_PP(1);
    Datum retval;

    retval = agtype_object_field_impl(fcinfo, agt, VARDATA_ANY(key),
                                      VARSIZE_ANY_EXHDR(key), false);
    PG_FREE_IF_COPY(agt, 0);
    PG_FREE_IF_COPY(key, 1);

    AG_RETURN_AGTYPE_P((const void*) retval);
}

PG_FUNCTION_INFO_V1(agtype_object_field_text);

Datum agtype_object_field_text(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    text *key = PG_GETARG_TEXT_PP(1);
    Datum retval;

    retval = agtype_object_field_impl(fcinfo, agt, VARDATA_ANY(key),
                                      VARSIZE_ANY_EXHDR(key), true);
    PG_FREE_IF_COPY(agt, 0);
    PG_FREE_IF_COPY(key, 1);

    PG_RETURN_TEXT_P((const void*) retval);
}

PG_FUNCTION_INFO_V1(agtype_array_element);

Datum agtype_array_element(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    int elem = PG_GETARG_INT32(1);
    Datum retval;

    retval = agtype_array_element_impl(fcinfo, agt, elem, false);

    PG_FREE_IF_COPY(agt, 0);

    AG_RETURN_AGTYPE_P((const void*) retval);
}

PG_FUNCTION_INFO_V1(agtype_array_element_text);

Datum agtype_array_element_text(PG_FUNCTION_ARGS)
{
    agtype *agt = AG_GET_ARG_AGTYPE_P(0);
    int elem = PG_GETARG_INT32(1);
    Datum retval;

    retval = agtype_array_element_impl(fcinfo, agt, elem, true);

    PG_FREE_IF_COPY(agt, 0);

    PG_RETURN_TEXT_P((const void*) retval);
}

PG_FUNCTION_INFO_V1(agtype_access_operator);
/*
 * Execution function for object.property, object["property"],
 * and array[element]
 */
Datum agtype_access_operator(PG_FUNCTION_ARGS)
{
    Datum *args = NULL;
    bool *nulls = NULL;
    Oid *types = NULL;
    int nargs = 0;
    agtype *container = NULL;
    agtype_value *container_value = NULL;
    agtype *result = NULL;
    int i = 0;

    /* extract our args, we need at least 2 */
    nargs = extract_variadic_args_min(fcinfo, 0, true, &args, &types, &nulls,
                                      2);
    /*
     * Return NULL if -
     *
     *     1) Our args are all null - nothing passed at all.
     *     2) We don't have the minimum number of args. We require an object or
     *        an array along with either a key or element number. Note that the
     *        function extract_variadic_args_min will return 0 (nargs) if we
     *        don't have at least 2 args.
     *
     */
    if (args == NULL || nargs == 0 || nulls[0] == true)
    {
        pfree_if_not_null(args);
        pfree_if_not_null(types);
        pfree_if_not_null(nulls);

        PG_RETURN_NULL();
    }

    /* check for individual NULLs */
    for (i = 0; i < nargs; i++)
    {
        /* if we have a NULL, return NULL */
        if (nulls[i] == true)
        {
            pfree_if_not_null(args);
            pfree_if_not_null(types);
            pfree_if_not_null(nulls);
            PG_RETURN_NULL();
        }
    }

    /* get the container argument. It could be an object or array */
    container = DATUM_GET_AGTYPE_P(args[0]);

    /* if it is a binary container, check for a VLE vpc */
    if (AGT_ROOT_IS_BINARY(container))
    {
        if (AGT_ROOT_BINARY_FLAGS(container) == AGT_FBINARY_TYPE_VLE_PATH)
        {
            /* retrieve an array of edges from the vpc */
            container_value = agtv_materialize_vle_edges(container);
            /* clear the container reference */

            container = NULL;
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("binary container must be a VLE vpc")));
        }
    }
    /* if it is a scalar, open it and pull out the value */
    else if (AGT_ROOT_IS_SCALAR(container))
    {
        container_value = get_ith_agtype_value_from_container(&container->root,
                                                              0);

        /* it must be either a vertex or an edge */
        if (container_value->type != AGTV_EDGE &&
            container_value->type != AGTV_VERTEX)
        {
                ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                               errmsg("scalar object must be a vertex or edge")));
        }

        /* clear the container reference */
        container = NULL;
    }

    /* iterate through the keys (object fields or array elements) */
    for (i = 1; i < nargs; i++)
    {
        agtype *key = NULL;

        /* get the key */
        key = DATUM_GET_AGTYPE_P(args[i]);

        /* the key must be a scalar */
        if (!(AGT_ROOT_IS_SCALAR(key)))
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("key must resolve to a scalar value")));
        }

        /*
         * Check for a vertex or edge container_value and extract the properties
         * object.
         */
        if ((container_value != NULL &&
             (container_value->type == AGTV_EDGE ||
              container_value->type == AGTV_VERTEX)))
        {
            /* both are objects, get the properties object */
            container_value = (container_value->type == AGTV_EDGE)
                ? &container_value->val.object.pairs[4].value
                : &container_value->val.object.pairs[2].value;
        }

        /*
         * If we are dealing with a type of object, which can be an -
         * agtype OBJECT, an agtype_value OBJECT serialized (BINARY), or an
         * agtype_value OBJECT deserialized.
         */
        if ((container_value != NULL &&
             (container_value->type == AGTV_OBJECT ||
              (container_value->type == AGTV_BINARY &&
               AGTYPE_CONTAINER_IS_OBJECT(container_value->val.binary.data)))) ||
            (container != NULL && AGT_ROOT_IS_OBJECT(container)))
        {
            container_value = execute_map_access_operator(container,
                                                          container_value, key);
        }
        /*
         * If we are dealing with a type of array, which can be an -
         * agtype ARRAY, an agtype_value ARRAY serialized (BINARY), or an
         * agtype_value ARRAY deserialized.
         */
        else if ((container_value != NULL &&
                  (container_value->type == AGTV_ARRAY ||
                   (container_value->type == AGTV_BINARY &&
                    AGTYPE_CONTAINER_IS_ARRAY(container_value->val.binary.data)))) ||
                 (container != NULL && AGT_ROOT_IS_ARRAY(container)))
        {
            container_value = execute_array_access_operator(container,
                                                            container_value,
                                                            key);
        }
        else
        {
            /* this is unexpected */
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("container must be an array or object")));
        }

        /* for NULL values return NULL */
        if (container_value == NULL || container_value->type == AGTV_NULL)
        {
            PG_RETURN_NULL();
        }

        /* clear the container reference */
        container = NULL;
    }

    pfree_if_not_null(args);
    pfree_if_not_null(types);
    pfree_if_not_null(nulls);

    /* serialize and return the result */
    result = agtype_value_to_agtype(container_value);

    return AGTYPE_P_GET_DATUM(result);
}

PG_FUNCTION_INFO_V1(agtype_access_slice);
/*
 * Execution function for list slices
 */
Datum agtype_access_slice(PG_FUNCTION_ARGS)
{
    agtype_value *lidx_value = NULL;
    agtype_value *uidx_value = NULL;
    agtype_in_state result;
    agtype *agt_result = NULL;
    agtype *agt_array = NULL;
    agtype *agt_lidx = NULL;
    agtype *agt_uidx = NULL;
    agtype_value *agtv_array = NULL;
    int64 upper_index = 0;
    int64 lower_index = 0;
    uint32 array_size = 0;
    int64 i = 0;

    /* return null if the array to slice is null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    /* return an error if both indices are NULL */
    if (PG_ARGISNULL(1) && PG_ARGISNULL(2))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("slice start and/or end is required")));
    }

    /* get the array parameter and verify that it is a list */
    agt_array = AG_GET_ARG_AGTYPE_P(0);

    if ((!AGT_ROOT_IS_ARRAY(agt_array) && !AGT_ROOT_IS_VPC(agt_array)) || AGT_ROOT_IS_SCALAR(agt_array))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("slice must access a list")));
    }

    /* If we have a vpc, decode it and get AGTV_ARRAY agtype_value */
    if (AGT_ROOT_IS_VPC(agt_array))
    {
        agtv_array = agtv_materialize_vle_edges(agt_array);

        /* get the size of array */
        array_size = agtv_array->val.array.num_elems;
    }
    else
    {
        array_size = AGT_ROOT_COUNT(agt_array);
    }

    /* if we don't have a lower bound, make it 0 */
    if (PG_ARGISNULL(1))
    {
        lower_index = 0;
    }
    else
    {
        agt_lidx = AG_GET_ARG_AGTYPE_P(1);
        lidx_value = get_ith_agtype_value_from_container(&agt_lidx->root, 0);
        /* adjust for AGTV_NULL */
        if (lidx_value->type == AGTV_NULL)
        {
            lower_index = 0;
            lidx_value = NULL;
        }
    }

    /* if we don't have an upper bound, make it the size of the array */
    if (PG_ARGISNULL(2))
    {
        upper_index = array_size;
    }
    else
    {
        agt_uidx = AG_GET_ARG_AGTYPE_P(2);
        uidx_value = get_ith_agtype_value_from_container(&agt_uidx->root, 0);
        /* adjust for AGTV_NULL */
        if (uidx_value->type == AGTV_NULL)
        {
            upper_index = array_size;
            uidx_value = NULL;
        }
    }

    /* if both indices are NULL (AGTV_NULL) return an error */
    if (lidx_value == NULL && uidx_value == NULL)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("slice start and/or end is required")));
    }

    /* key must be an integer or NULL */
    if ((lidx_value != NULL && lidx_value->type != AGTV_INTEGER) ||
        (uidx_value != NULL && uidx_value->type != AGTV_INTEGER))
    {
        ereport(ERROR,
                (errmsg("array slices must resolve to an integer value")));
    }

    /* set indices if not already set */
    if (lidx_value)
    {
        lower_index = lidx_value->val.int_value;
        pfree_agtype_value(lidx_value);
    }
    if (uidx_value)
    {
        upper_index = uidx_value->val.int_value;
        pfree_agtype_value(uidx_value);
    }

    /* adjust for negative and out of bounds index values */
    if (lower_index < 0)
    {
        lower_index = array_size + lower_index;
    }
    if (lower_index < 0)
    {
        lower_index = 0;
    }
    if (lower_index > array_size)
    {
        lower_index = array_size;
    }
    if (upper_index < 0)
    {
        upper_index = array_size + upper_index;
    }
    if (upper_index < 0)
    {
        upper_index = 0;
    }
    if (upper_index > array_size)
    {
        upper_index = array_size;
    }

    /* build our result array */
    memset(&result, 0, sizeof(agtype_in_state));

    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY,
                                   NULL);

    /* if we have agtype_value, we need to iterate through the array */
    if (agtv_array)
    {
        for (i = lower_index; i < upper_index; i++)
        {
            result.res = push_agtype_value(&result.parse_state, WAGT_ELEM,
                                           &agtv_array->val.array.elems[i]);
        }
    }
    else
    {
        /* get array elements from agtype_container */
        for (i = lower_index; i < upper_index; i++)
        {
            result.res = push_agtype_value(&result.parse_state, WAGT_ELEM,
                get_ith_agtype_value_from_container(&agt_array->root, i));
        }
    }

    result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

    agt_result = agtype_value_to_agtype(result.res);

    pfree_agtype_in_state(&result);
    PG_FREE_IF_COPY(agt_array, 0);
    PG_FREE_IF_COPY(agt_lidx, 1);
    PG_FREE_IF_COPY(agt_uidx, 2);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(agtype_in_operator);
/*
 * Execute function for IN operator
 */
Datum agtype_in_operator(PG_FUNCTION_ARGS)
{
    agtype *agt_arg, *agt_item;
    agtype_iterator *it_array, *it_item;
    agtype_value *agtv_arg, agtv_item, agtv_elem;
    uint32 array_size = 0;
    bool result = false;
    uint32 i = 0;

    /* return null if the array is null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    /* get the array parameter and verify that it is a list */
    agt_arg = AG_GET_ARG_AGTYPE_P(0);

    if ((!AGT_ROOT_IS_ARRAY(agt_arg) && !AGT_ROOT_IS_VPC(agt_arg)) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("object of IN must be a list")));
    }
    /* If we have vpc as arg, get the agtype_value AGTV_ARRAY of edges */
    if (AGT_ROOT_IS_VPC(agt_arg))
    {
        agtv_arg = agtv_materialize_vle_edges(agt_arg);
        array_size = agtv_arg->val.array.num_elems;

        /* return null if the item to find is null */
        if (PG_ARGISNULL(1))
        {
            PG_RETURN_NULL();
        }
        /* get the item to search for */
        agt_item = AG_GET_ARG_AGTYPE_P(1);

        /* init item iterator */
        it_item = agtype_iterator_init(&agt_item->root);

        /* get value of item */
        agtype_iterator_next(&it_item, &agtv_item, false);
        if (agtv_item.type == AGTV_ARRAY && agtv_item.val.array.raw_scalar)
        {
            agtype_iterator_next(&it_item, &agtv_item, false);
            /* check for AGTYPE NULL */
            if (agtv_item.type == AGTV_NULL)
            {
                PG_RETURN_NULL();
            }
        }

        /* iterate through the array, but stop if we find it */
        for (i = 0; i < array_size && !result; i++)
        {
            agtv_elem = agtv_arg->val.array.elems[i];

            /* if both are containers, compare containers */
            if (!IS_A_AGTYPE_SCALAR(&agtv_item) && !IS_A_AGTYPE_SCALAR(&agtv_elem))
            {
                result = (compare_agtype_containers_orderability(
                            &agt_item->root, agtv_elem.val.binary.data) == 0);
            }
            /* if both are scalars and of the same type, compare scalars */
            else if (IS_A_AGTYPE_SCALAR(&agtv_item) &&
                    IS_A_AGTYPE_SCALAR(&agtv_elem) &&
                    agtv_item.type == agtv_elem.type)
            {
                result = (compare_agtype_scalar_values(&agtv_item, &agtv_elem) ==
                        0);
            }
        }
    }
    /* Else we need to iterate agtype_container */
    else
    {
        /* init array iterator */
        it_array = agtype_iterator_init(&agt_arg->root);
        /* open array container */
        agtype_iterator_next(&it_array, &agtv_elem, false);
        /* check for an array scalar value */
        if (agtv_elem.type == AGTV_ARRAY && agtv_elem.val.array.raw_scalar)
        {
            agtype_iterator_next(&it_array, &agtv_elem, false);
            /* check for AGTYPE NULL */
            if (agtv_elem.type == AGTV_NULL)
            {
                PG_RETURN_NULL();
            }
            /* if it is a scalar, but not AGTV_NULL, error out */
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("object of IN must be a list")));
        }

        array_size = AGT_ROOT_COUNT(agt_arg);

        /* return null if the item to find is null */
        if (PG_ARGISNULL(1))
        {
            PG_RETURN_NULL();
        }
        /* get the item to search for */
        agt_item = AG_GET_ARG_AGTYPE_P(1);

        /* init item iterator */
        it_item = agtype_iterator_init(&agt_item->root);

        /* get value of item */
        agtype_iterator_next(&it_item, &agtv_item, false);
        if (agtv_item.type == AGTV_ARRAY && agtv_item.val.array.raw_scalar)
        {
            agtype_iterator_next(&it_item, &agtv_item, false);
            /* check for AGTYPE NULL */
            if (agtv_item.type == AGTV_NULL)
            {
                PG_RETURN_NULL();
            }
        }

        /* iterate through the array, but stop if we find it */
        for (i = 0; i < array_size && !result; i++)
        {
            /* get next element */
            agtype_iterator_next(&it_array, &agtv_elem, true);
            /* if both are containers, compare containers */
            if (!IS_A_AGTYPE_SCALAR(&agtv_item) && !IS_A_AGTYPE_SCALAR(&agtv_elem))
            {
                result = (compare_agtype_containers_orderability(
                            &agt_item->root, agtv_elem.val.binary.data) == 0);
            }
            /* if both are scalars and of the same type, compare scalars */
            else if (IS_A_AGTYPE_SCALAR(&agtv_item) &&
                    IS_A_AGTYPE_SCALAR(&agtv_elem) &&
                    agtv_item.type == agtv_elem.type)
            {
                result = (compare_agtype_scalar_values(&agtv_item, &agtv_elem) ==
                        0);
            }
        }
    }

    return boolean_to_agtype(result);
}

PG_FUNCTION_INFO_V1(agtype_string_match_starts_with);
/*
 * Execution function for STARTS WITH
 */
Datum agtype_string_match_starts_with(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result = false;

    if (AGT_ROOT_IS_SCALAR(lhs) && AGT_ROOT_IS_SCALAR(rhs))
    {
        agtype_value *lhs_value;
        agtype_value *rhs_value;

        lhs_value = get_ith_agtype_value_from_container(&lhs->root, 0);
        rhs_value = get_ith_agtype_value_from_container(&rhs->root, 0);

        if (lhs_value->type == AGTV_STRING && rhs_value->type == AGTV_STRING)
        {
            if (lhs_value->val.string.len < rhs_value->val.string.len)
            {
                result = false;
            }
            else if (strncmp(lhs_value->val.string.val,
                             rhs_value->val.string.val,
                             rhs_value->val.string.len) == 0)
            {
                result = true;
            }
            else
            {
                result = false;
            }
        }
        pfree_agtype_value(lhs_value);
        pfree_agtype_value(rhs_value);
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("agtype string values expected")));
    }

    PG_FREE_IF_COPY(lhs, 0);
    PG_FREE_IF_COPY(rhs, 1);

    return boolean_to_agtype(result);
}

PG_FUNCTION_INFO_V1(agtype_string_match_ends_with);
/*
 * Execution function for ENDS WITH
 */
Datum agtype_string_match_ends_with(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result = false;

    if (AGT_ROOT_IS_SCALAR(lhs) && AGT_ROOT_IS_SCALAR(rhs))
    {
        agtype_value *lhs_value;
        agtype_value *rhs_value;

        lhs_value = get_ith_agtype_value_from_container(&lhs->root, 0);
        rhs_value = get_ith_agtype_value_from_container(&rhs->root, 0);

        if (lhs_value->type == AGTV_STRING && rhs_value->type == AGTV_STRING)
        {
            if (lhs_value->val.string.len < rhs_value->val.string.len)
            {
                result = false;
            }
            else if (strncmp((lhs_value->val.string.val +
                              lhs_value->val.string.len -
                              rhs_value->val.string.len),
                             rhs_value->val.string.val,
                             rhs_value->val.string.len) == 0)
            {
                result = true;
            }
            else
            {
                result = false;
            }
        }
        pfree_agtype_value(lhs_value);
        pfree_agtype_value(rhs_value);
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("agtype string values expected")));
    }

    PG_FREE_IF_COPY(lhs, 0);
    PG_FREE_IF_COPY(rhs, 1);

    return boolean_to_agtype(result);
}

PG_FUNCTION_INFO_V1(agtype_string_match_contains);
/*
 * Execution function for CONTAINS
 */
Datum agtype_string_match_contains(PG_FUNCTION_ARGS)
{
    agtype *lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype *rhs = AG_GET_ARG_AGTYPE_P(1);
    bool result = false;

    if (AGT_ROOT_IS_SCALAR(lhs) && AGT_ROOT_IS_SCALAR(rhs))
    {
        agtype_value *lhs_value;
        agtype_value *rhs_value;

        lhs_value = get_ith_agtype_value_from_container(&lhs->root, 0);
        rhs_value = get_ith_agtype_value_from_container(&rhs->root, 0);

        if (lhs_value->type == AGTV_STRING && rhs_value->type == AGTV_STRING)
        {
            char *l;
            char *r;

            if (lhs_value->val.string.len < rhs_value->val.string.len)
            {
                result = false;
            }

            l = pnstrdup(lhs_value->val.string.val, lhs_value->val.string.len);
            r = pnstrdup(rhs_value->val.string.val, rhs_value->val.string.len);

            if (strstr(l, r) == NULL)
            {
                result = false;
            }
            else
            {
                result = true;
            }
            pfree_if_not_null(l);
            pfree_if_not_null(r);
        }
        pfree_agtype_value(lhs_value);
        pfree_agtype_value(rhs_value);
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("agtype string values expected")));
    }

    PG_FREE_IF_COPY(lhs, 0);
    PG_FREE_IF_COPY(rhs, 1);

    return boolean_to_agtype(result);
}

#define LEFT_ROTATE(n, i) ((n << i) | (n >> (64 - i)))
#define RIGHT_ROTATE(n, i)  ((n >> i) | (n << (64 - i)))

/* Hashing Function for Hash Indexes */
PG_FUNCTION_INFO_V1(agtype_hash_cmp);

Datum agtype_hash_cmp(PG_FUNCTION_ARGS)
{
    uint64 hash = 0;
    agtype *agt;
    agtype_iterator *it;
    agtype_iterator_token tok;
    agtype_value *r;
    uint64 seed = 0xF0F0F0F0;

    if (PG_ARGISNULL(0))
        PG_RETURN_INT16(0);

    agt = AG_GET_ARG_AGTYPE_P(0);

    r = palloc0(sizeof(agtype_value));

    it = agtype_iterator_init(&agt->root);
    while ((tok = agtype_iterator_next(&it, r, false)) != WAGT_DONE)
    {
        if (IS_A_AGTYPE_SCALAR(r) && AGTYPE_ITERATOR_TOKEN_IS_HASHABLE(tok))
            agtype_hash_scalar_value_extended(r, &hash, seed);
        else if (tok == WAGT_BEGIN_ARRAY && !r->val.array.raw_scalar)
            seed = LEFT_ROTATE(seed, 4);
        else if (tok == WAGT_BEGIN_OBJECT)
            seed = LEFT_ROTATE(seed, 6);
        else if (tok == WAGT_END_ARRAY && !r->val.array.raw_scalar)
            seed = RIGHT_ROTATE(seed, 4);
        else if (tok == WAGT_END_OBJECT)
            seed = RIGHT_ROTATE(seed, 4);

        seed = LEFT_ROTATE(seed, 1);
    }

    pfree_if_not_null(r);
    PG_FREE_IF_COPY(agt, 0);

    PG_RETURN_INT32(hash);
}

/* Comparison function for btree Indexes */
PG_FUNCTION_INFO_V1(agtype_btree_cmp);

Datum agtype_btree_cmp(PG_FUNCTION_ARGS)
{
    agtype *agtype_lhs;
    agtype *agtype_rhs;
    int32 result;

    if (PG_ARGISNULL(0) && PG_ARGISNULL(1))
    {
        PG_RETURN_INT32(0);
    }
    else if (PG_ARGISNULL(0))
    {
        PG_RETURN_INT32(1);
    }
    else if (PG_ARGISNULL(1))
    {
        PG_RETURN_INT32(-1);
    }

    agtype_lhs = AG_GET_ARG_AGTYPE_P(0);
    agtype_rhs = AG_GET_ARG_AGTYPE_P(1);

    result = compare_agtype_containers_orderability(&agtype_lhs->root,
                                                    &agtype_rhs->root);

    PG_FREE_IF_COPY(agtype_lhs, 0);
    PG_FREE_IF_COPY(agtype_rhs, 1);

    PG_RETURN_INT32(result);
}

PG_FUNCTION_INFO_V1(agtype_typecast_numeric);
/*
 * Execute function to typecast an agtype to an agtype numeric
 */
Datum agtype_typecast_numeric(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value *arg_value;
    agtype_value result_value;
    Datum numd;
    char *string = NULL;
    agtype *result = NULL;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    /* check that we have a scalar value */
    if (!AGT_ROOT_IS_SCALAR(arg_agt))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast argument must resolve to a scalar value")));
    }

    /* get the arg parameter */
    arg_value = get_ith_agtype_value_from_container(&arg_agt->root, 0);

    /* we don't need to agtype arg anymore */
    PG_FREE_IF_COPY(arg_agt, 0);

    /* the input type drives the casting */
    switch(arg_value->type)
    {
    case AGTV_INTEGER:
        numd = DirectFunctionCall1(int8_numeric,
                                   Int64GetDatum(arg_value->val.int_value));
        break;
    case AGTV_FLOAT:
        numd = DirectFunctionCall1(float8_numeric,
                                   Float8GetDatum(arg_value->val.float_value));
        break;
    case AGTV_NUMERIC:
        /* it is already a numeric so just return it */
        result = agtype_value_to_agtype(arg_value);
        pfree_agtype_value(arg_value);
        PG_RETURN_POINTER(result);
        break;
    /* this allows string numbers and NaN */
    case AGTV_STRING:
        /* we need a null terminated string */
        string = (char *) palloc0(sizeof(char)*arg_value->val.string.len + 1);
        string = strncpy(string, arg_value->val.string.val,
                         arg_value->val.string.len);
        string[arg_value->val.string.len] = '\0';
        /* pass the string to the numeric in function for conversion */
        numd = DirectFunctionCall3(numeric_in,
                                   CStringGetDatum(string),
                                   ObjectIdGetDatum(InvalidOid),
                                   Int32GetDatum(-1));
        /* free the string */
        pfree_if_not_null(string);
        string = NULL;
        break;
    /* what was given doesn't cast to a numeric */
    default:
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast expression must be a number or a string")));
        break;
    }

    pfree_agtype_value(arg_value);

    /* fill in and return our result */
    result_value.type = AGTV_NUMERIC;
    result_value.val.numeric = DatumGetNumeric(numd);

    result = agtype_value_to_agtype(&result_value);
    pfree_agtype_value_content(&result_value);

    PG_RETURN_POINTER(result);
}

PG_FUNCTION_INFO_V1(agtype_typecast_int);
/*
 * Execute function to typecast an agtype to an agtype int
 */
Datum agtype_typecast_int(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value *arg_value;
    agtype_value result_value;
    Datum d;
    char *string = NULL;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    /* check that we have a scalar value */
    if (!AGT_ROOT_IS_SCALAR(arg_agt))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast argument must be a scalar value")));
    }

    /* get the arg parameter */
    arg_value = get_ith_agtype_value_from_container(&arg_agt->root, 0);

    /* check for agtype null */
    if (arg_value->type == AGTV_NULL)
    {
        PG_RETURN_NULL();
    }

    /* the input type drives the casting */
    switch(arg_value->type)
    {
    case AGTV_INTEGER:
        PG_RETURN_POINTER(agtype_value_to_agtype(arg_value));
        break;
    case AGTV_FLOAT:
        d = DirectFunctionCall1(dtoi8,
                                Float8GetDatum(arg_value->val.float_value));
        break;
    case AGTV_NUMERIC:
        d = DirectFunctionCall1(numeric_int8,
                                NumericGetDatum(arg_value->val.numeric));
        break;
    case AGTV_BOOL:
        d = DirectFunctionCall1(bool_int4,
                                BoolGetDatum(arg_value->val.boolean));
        break;
    case AGTV_STRING:
        /* we need a null terminated string */
        string = (char *) palloc0(sizeof(char)*arg_value->val.string.len + 1);
        string = strncpy(string, arg_value->val.string.val,
                         arg_value->val.string.len);
        string[arg_value->val.string.len] = '\0';

        d = DirectFunctionCall1(int8in, CStringGetDatum(string));
        /* free the string */
        pfree_if_not_null(string);
        string = NULL;
        break;
    /* what was given doesn't cast to an int */
    default:
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast expression must be a number or a string")));
        break;
    }

    /* set the result type and return our result */
    result_value.type = AGTV_INTEGER;
    result_value.val.int_value = DatumGetInt64(d);

    PG_RETURN_POINTER(agtype_value_to_agtype(&result_value));
}

PG_FUNCTION_INFO_V1(agtype_typecast_bool);
/*
 * Execute function to typecast an agtype to an agtype bool
 */
Datum agtype_typecast_bool(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value *arg_value;
    agtype_value result_value;
    Datum d;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
    {
        PG_RETURN_NULL();
    }

    /* check that we have a scalar value */
    if (!AGT_ROOT_IS_SCALAR(arg_agt))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast argument must be a scalar value")));
    }

    /* get the arg parameter */
    arg_value = get_ith_agtype_value_from_container(&arg_agt->root, 0);

    /* check for agtype null */
    if (arg_value->type == AGTV_NULL)
    {
        PG_RETURN_NULL();
    }

    /* the input type drives the casting */
    switch(arg_value->type)
    {
    case AGTV_BOOL:
        PG_RETURN_POINTER(agtype_value_to_agtype(arg_value));
        break;
    case AGTV_INTEGER:
        d = DirectFunctionCall1(int4_bool,
                                Int64GetDatum(arg_value->val.int_value));
        break;
    /* what was given doesn't cast to a bool */
    default:
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast expression must be an integer or a boolean")));
        break;
    }

    /* set the result type and return our result */
    result_value.type = AGTV_BOOL;
    result_value.val.boolean = DatumGetBool(d);

    PG_RETURN_POINTER(agtype_value_to_agtype(&result_value));
}

PG_FUNCTION_INFO_V1(agtype_typecast_float);
/*
 * Execute function to typecast an agtype to an agtype float
 */
Datum agtype_typecast_float(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value *arg_value;
    agtype_value result_value;
    Datum d;
    char *string = NULL;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
        PG_RETURN_NULL();

    /* check that we have a scalar value */
    if (!AGT_ROOT_IS_SCALAR(arg_agt))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast argument must be a scalar value")));

    /* get the arg parameter */
    arg_value = get_ith_agtype_value_from_container(&arg_agt->root, 0);
    /* check for agtype null */
    if (arg_value->type == AGTV_NULL)
        PG_RETURN_NULL();

    /* the input type drives the casting */
    switch(arg_value->type)
    {
    case AGTV_INTEGER:
        d = DirectFunctionCall1(int8out,
                                Int64GetDatum(arg_value->val.int_value));
        d = DirectFunctionCall1(float8in, d);
        break;
    case AGTV_FLOAT:
        /* it is already a float so just return it */
        PG_RETURN_POINTER(agtype_value_to_agtype(arg_value));
        break;
    case AGTV_NUMERIC:
        d = DirectFunctionCall1(numeric_float8,
                                NumericGetDatum(arg_value->val.numeric));
        break;
    /* this allows string numbers, NaN, Infinity, and -Infinity */
    case AGTV_STRING:
        /* we need a null terminated string */
        string = (char *) palloc0(sizeof(char)*arg_value->val.string.len + 1);
        string = strncpy(string, arg_value->val.string.val,
                         arg_value->val.string.len);
        string[arg_value->val.string.len] = '\0';

        d = DirectFunctionCall1(float8in, CStringGetDatum(string));
        /* free the string */
        pfree_if_not_null(string);
        string = NULL;
        break;
    /* what was given doesn't cast to a float */
    default:
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast expression must be a number or a string")));
        break;
    }

    /* set the result type and return our result */
    result_value.type = AGTV_FLOAT;
    result_value.val.float_value = DatumGetFloat8(d);

    PG_RETURN_POINTER(agtype_value_to_agtype(&result_value));
}

PG_FUNCTION_INFO_V1(agtype_typecast_vertex);
/*
 * Execute function for typecast to vertex
 */
Datum agtype_typecast_vertex(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value agtv_key;
    agtype_value *agtv_graphid, *agtv_label, *agtv_properties;
    Datum result;
    int count;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
        PG_RETURN_NULL();

    /* A vertex is an object so the arg needs to be one too */
    if (!AGT_ROOT_IS_OBJECT(arg_agt))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex typecast argument must resolve to an object")));

    /* A vertex object has 3 key/value pairs */
    count = AGTYPE_CONTAINER_SIZE(&arg_agt->root);
    if (count != 3)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast object is not a vertex")));

    /*
     * The 3 key/value pairs need to each exist and their names need to match
     * the names used for a vertex.
     */
    agtv_key.type = AGTV_STRING;
    agtv_key.val.string.val = "id";
    agtv_key.val.string.len = 2;
    agtv_graphid = find_agtype_value_from_container(&arg_agt->root,
                                                    AGT_FOBJECT, &agtv_key);
    if (agtv_graphid == NULL || agtv_graphid->type != AGTV_INTEGER)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex typecast object has invalid or missing id")));

    agtv_key.val.string.val = "label";
    agtv_key.val.string.len = 5;
    agtv_label = find_agtype_value_from_container(&arg_agt->root,
                                                  AGT_FOBJECT, &agtv_key);
    if (agtv_label == NULL || agtv_label->type != AGTV_STRING)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex typecast object has invalid or missing label")));

    agtv_key.val.string.val = "properties";
    agtv_key.val.string.len = 10;
    agtv_properties = find_agtype_value_from_container(&arg_agt->root,
                                                       AGT_FOBJECT, &agtv_key);
    if (agtv_properties == NULL ||
        (agtv_properties->type != AGTV_OBJECT &&
         agtv_properties->type != AGTV_BINARY))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("vertex typecast object has invalid or missing properties")));

    /* Hand it off to the build vertex routine */
    result = DirectFunctionCall3(_agtype_build_vertex,
                 Int64GetDatum(agtv_graphid->val.int_value),
                 CStringGetDatum(agtv_label->val.string.val),
                 PointerGetDatum(agtype_value_to_agtype(agtv_properties)));
    return result;
}

PG_FUNCTION_INFO_V1(agtype_typecast_edge);
/*
 * Execute function for typecast to edge
 */
Datum agtype_typecast_edge(PG_FUNCTION_ARGS)
{
    agtype *arg_agt;
    agtype_value agtv_key;
    agtype_value *agtv_graphid, *agtv_label, *agtv_properties,
                 *agtv_startid, *agtv_endid;
    Datum result;
    int count;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
        PG_RETURN_NULL();

    /* An edge is an object, so the arg needs to be one too */
    if (!AGT_ROOT_IS_OBJECT(arg_agt))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("edge typecast argument must resolve to an object")));

    /* An edge has 5 key/value pairs */
    count = AGTYPE_CONTAINER_SIZE(&arg_agt->root);
    if (count != 5)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast object is not an edge")));

    /*
     * The 5 key/value pairs need to each exist and their names need to match
     * the names used for an edge.
     */
    agtv_key.type = AGTV_STRING;
    agtv_key.val.string.val = "id";
    agtv_key.val.string.len = 2;
    agtv_graphid = find_agtype_value_from_container(&arg_agt->root,
                                                    AGT_FOBJECT, &agtv_key);
    if (agtv_graphid == NULL || agtv_graphid->type != AGTV_INTEGER)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("edge typecast object has an invalid or missing id")));

    agtv_key.val.string.val = "label";
    agtv_key.val.string.len = 5;
    agtv_label = find_agtype_value_from_container(&arg_agt->root,
                                                  AGT_FOBJECT, &agtv_key);
    if (agtv_label == NULL || agtv_label->type != AGTV_STRING)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("edge typecast object has an invalid or missing label")));

    agtv_key.val.string.val = "properties";
    agtv_key.val.string.len = 10;
    agtv_properties = find_agtype_value_from_container(&arg_agt->root,
                                                 AGT_FOBJECT, &agtv_key);
    if (agtv_properties == NULL ||
        (agtv_properties->type != AGTV_OBJECT &&
         agtv_properties->type != AGTV_BINARY))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("edge typecast object has invalid or missing properties")));

    agtv_key.val.string.val = "start_id";
    agtv_key.val.string.len = 8;
    agtv_startid = find_agtype_value_from_container(&arg_agt->root,
                                                    AGT_FOBJECT, &agtv_key);
    if (agtv_startid == NULL || agtv_startid->type != AGTV_INTEGER)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("edge typecast object has an invalid or missing start_id")));

    agtv_key.val.string.val = "end_id";
    agtv_key.val.string.len = 6;
    agtv_endid = find_agtype_value_from_container(&arg_agt->root,
                                                    AGT_FOBJECT, &agtv_key);
    if (agtv_endid == NULL || agtv_endid->type != AGTV_INTEGER)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("edge typecast object has an invalid or missing end_id")));

    /* Hand it off to the build edge routine */
    result = DirectFunctionCall5(_agtype_build_edge,
                 Int64GetDatum(agtv_graphid->val.int_value),
                 Int64GetDatum(agtv_startid->val.int_value),
                 Int64GetDatum(agtv_endid->val.int_value),
                 CStringGetDatum(agtv_label->val.string.val),
                 PointerGetDatum(agtype_value_to_agtype(agtv_properties)));
    return result;
}

PG_FUNCTION_INFO_V1(agtype_typecast_path);
/*
 * Execute function for typecast to path
 */
Datum agtype_typecast_path(PG_FUNCTION_ARGS)
{
    agtype *arg_agt = NULL;
    agtype_in_state path;
    agtype_value *agtv_element = NULL;
    int count = 0;
    int i = 0;

    /* get the agtype equivalence of any convertable input type */
    arg_agt = get_one_agtype_from_variadic_args(fcinfo, 0, 1);

    /* Return null if arg_agt is null. This covers SQL and Agtype NULLS */
    if (arg_agt == NULL)
        PG_RETURN_NULL();

    /* path needs to be an array */
    if (!AGT_ROOT_IS_ARRAY(arg_agt))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("path typecast argument must resolve to an array")));

    count = AGT_ROOT_COUNT(arg_agt);

    /* quick check for valid path lengths */
    if (count < 3 || (count-1) % 2 != 0)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast argument is not a valid path")));

    /* create an agtype array */
    memset(&path, 0, sizeof(agtype_in_state));
    path.res = push_agtype_value(&path.parse_state, WAGT_BEGIN_ARRAY, NULL);

    /*
     * Iterate through the provided list, check that each value conforms, and
     * then add it if it does. Otherwise error out.
     */
    for (i = 0; i+1 < count; i+=2)
    {
        /* get a potential vertex, check it, then add it */
        agtv_element = get_ith_agtype_value_from_container(&arg_agt->root, i);
        if (agtv_element == NULL || agtv_element->type != AGTV_VERTEX)
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("typecast argument is not a valid path")));
        push_agtype_value(&path.parse_state, WAGT_ELEM, agtv_element);

        /* get a potential edge, check it, then add it */
        agtv_element = get_ith_agtype_value_from_container(&arg_agt->root, i+1);
        if (agtv_element == NULL || agtv_element->type != AGTV_EDGE)
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("typecast argument is not a valid path")));
        push_agtype_value(&path.parse_state, WAGT_ELEM, agtv_element);
    }

    /* validate the last element is a vertex, add it if it is, fail otherwise */
    agtv_element = get_ith_agtype_value_from_container(&arg_agt->root, i);
    if (agtv_element == NULL || agtv_element->type != AGTV_VERTEX)
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("typecast argument is not a valid path")));
    push_agtype_value(&path.parse_state, WAGT_ELEM, agtv_element);

    /* close the array */
    path.res = push_agtype_value(&path.parse_state, WAGT_END_ARRAY, NULL);
    /* set it to a path */
    path.res->type = AGTV_PATH;

    PG_RETURN_POINTER(agtype_value_to_agtype(path.res));
}

PG_FUNCTION_INFO_V1(age_id);

Datum age_id(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_result = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("id() argument must resolve to a scalar value")));

    /* get the object out of the array */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_object->type == AGTV_NULL)
            PG_RETURN_NULL();

    /* check for proper agtype */
    if (agtv_object->type != AGTV_VERTEX && agtv_object->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("id() argument must be a vertex, an edge or null")));

    agtv_result = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "id");

    Assert(agtv_result != NULL);
    Assert(agtv_result->type = AGTV_INTEGER);

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_start_id);

Datum age_start_id(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_result = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("start_id() argument must resolve to a scalar value")));

    /* get the object out of the array */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_object->type == AGTV_NULL)
            PG_RETURN_NULL();

    /* check for proper agtype */
    if (agtv_object->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("start_id() argument must be an edge or null")));

    agtv_result = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "start_id");

    Assert(agtv_result != NULL);
    Assert(agtv_result->type = AGTV_INTEGER);

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_end_id);

Datum age_end_id(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_result = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("end_id() argument must resolve to a scalar value")));

    /* get the object out of the array */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_object->type == AGTV_NULL)
            PG_RETURN_NULL();

    /* check for proper agtype */
    if (agtv_object->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("end_id() argument must be an edge or null")));

    agtv_result = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "end_id");

    Assert(agtv_result != NULL);
    Assert(agtv_result->type = AGTV_INTEGER);

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

/*
 * Helper function to return the Datum value of a column (attribute) in a heap
 * tuple (row) given the column number (starting from 0), attribute name, typid,
 * and whether it can be null. The function is designed to extract and validate
 * that the data (attribute) is what is expected. The function will error on any
 * issues.
 */
Datum column_get_datum(TupleDesc tupdesc, HeapTuple tuple, int column,
                       const char *attname, Oid typid, bool isnull)
{
    Form_pg_attribute att;
    HeapTupleHeader hth;
    HeapTupleData tmptup, *htd;
    Datum result;
    bool _isnull = true;

    /* build the heap tuple data */
    hth = tuple->t_data;
    tmptup.t_len = HeapTupleHeaderGetDatumLength(hth);
    tmptup.t_data = hth;
    htd = &tmptup;

    /* get the description for the column from the tuple descriptor */
    att = TupleDescAttr(tupdesc, column);
    /* get the datum (attribute) for that column*/
    result = heap_getattr(htd, column + 1, tupdesc, &_isnull);
    /* verify that the attribute typid is as expected */
    if (att->atttypid != typid)
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("Invalid attribute typid. Expected %d, found %d", typid,
                        att->atttypid)));
    /* verify that the attribute name is as expected */
    if (strcmp(att->attname.data, attname) != 0)
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("Invalid attribute name. Expected %s, found %s",
                        attname, att->attname.data)));
    /* verify that if it is null, it is allowed to be null */
    if (isnull == false && _isnull == true)
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("Attribute was found to be null when null is not allowed.")));

    return result;
}

/*
 * Function to retrieve a label name, given the graph name and graphid of the
 * node or edge. The function returns a pointer to a duplicated string that
 * needs to be freed when you are finished using it.
 */
static char *get_label_name(const char *graph_name, graphid element_graphid)
{
    ScanKeyData scan_keys[2];
    Relation ag_label;
    SysScanDesc scan_desc;
    HeapTuple tuple;
    TupleDesc tupdesc;
    char *result = NULL;
    bool column_is_null = false;
    Oid graph_oid = get_graph_oid(graph_name);
    int32 label_id = get_graphid_label_id(element_graphid);

    /* scankey for first match in ag_label, column 2, graphoid, BTEQ, OidEQ */
    ScanKeyInit(&scan_keys[0], Anum_ag_label_graph, BTEqualStrategyNumber,
                F_OIDEQ, ObjectIdGetDatum(graph_oid));
    /* scankey for second match in ag_label, column 3, label id, BTEQ, Int4EQ */
    ScanKeyInit(&scan_keys[1], Anum_ag_label_id, BTEqualStrategyNumber,
                F_INT4EQ, Int32GetDatum(label_id));

    ag_label = table_open(ag_label_relation_id(), ShareLock);
    scan_desc = systable_beginscan(ag_label, ag_label_graph_oid_index_id(), true,
                                   NULL, 2, scan_keys);

    tuple = systable_getnext(scan_desc);
    if (!HeapTupleIsValid(tuple))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_SCHEMA),
                 errmsg("graphid %lu does not exist", element_graphid)));
    }

    /* get the tupdesc - we don't need to release this one */
    tupdesc = RelationGetDescr(ag_label);

    /* bail if the number of columns differs */
    if (tupdesc->natts != Natts_ag_label)
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("Invalid number of attributes for ag_catalog.ag_label")));
    }

    /* get the label name */
    result = NameStr(*DatumGetName(heap_getattr(tuple, Anum_ag_label_name,
                                                tupdesc, &column_is_null)));
    /* duplicate it */
    result = strdup(result);

    /* end the scan and close the relation */
    systable_endscan(scan_desc);
    table_close(ag_label, ShareLock);

    return result;
}

static Datum get_vertex(const char *graph, const char *vertex_label,
                        int64 graphid)
{
    ScanKeyData scan_keys[1];
    Relation graph_vertex_label;
    TableScanDesc scan_desc;
    HeapTuple tuple;
    TupleDesc tupdesc;
    Datum id, properties, result;

    /* get the specific graph namespace (schema) */
    Oid graph_namespace_oid = get_namespace_oid(graph, false);
    /* get the specific vertex label table (schema.vertex_label) */
    Oid vertex_label_table_oid = get_relname_relid(vertex_label,
                                                 graph_namespace_oid);
    /* get the active snapshot */
    Snapshot snapshot = GetActiveSnapshot();

    /* initialize the scan key */
    ScanKeyInit(&scan_keys[0], 1, BTEqualStrategyNumber, F_OIDEQ,
                Int64GetDatum(graphid));

    /* open the relation (table), begin the scan, and get the tuple  */
    graph_vertex_label = table_open(vertex_label_table_oid, ShareLock);
    scan_desc = table_beginscan(graph_vertex_label, snapshot, 1, scan_keys);
    tuple = heap_getnext(scan_desc, ForwardScanDirection);

    /* bail if the tuple isn't valid */
    if (!HeapTupleIsValid(tuple))
    {
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("graphid %lu does not exist", graphid)));
    }

    /* get the tupdesc - we don't need to release this one */
    tupdesc = RelationGetDescr(graph_vertex_label);
    /* bail if the number of columns differs */
    if (tupdesc->natts != 2)
        ereport(ERROR,
                (errcode(ERRCODE_UNDEFINED_TABLE),
                 errmsg("Invalid number of attributes for %s.%s", graph,
                        vertex_label )));

    /* get the id */
    id = column_get_datum(tupdesc, tuple, 0, "id", GRAPHIDOID, true);
    /* get the properties */
    properties = column_get_datum(tupdesc, tuple, 1, "properties",
                                  AGTYPEOID, true);
    /* reconstruct the vertex */
    result = DirectFunctionCall3(_agtype_build_vertex, id,
                                 CStringGetDatum(vertex_label), properties);
    /* end the scan and close the relation */
    table_endscan(scan_desc);
    table_close(graph_vertex_label, ShareLock);
    /* return the vertex datum */
    return result;
}

PG_FUNCTION_INFO_V1(age_startnode);

Datum age_startnode(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_value = NULL;
    char *graph_name = NULL;
    char *label_name = NULL;
    graphid start_id;
    Datum result;

    /* we need the graph name */
    Assert(PG_ARGISNULL(0) == false);

    /* check for null */
    if (PG_ARGISNULL(1))
        PG_RETURN_NULL();

    /* get the graph name */
    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* it must be a scalar and must be a string */
    Assert(AGT_ROOT_IS_SCALAR(agt_arg));
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);
    Assert(agtv_object->type == AGTV_STRING);
    graph_name = strndup(agtv_object->val.string.val,
                         agtv_object->val.string.len);

    /* get the edge */
    agt_arg = AG_GET_ARG_AGTYPE_P(1);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("startNode() argument must resolve to a scalar value")));
    /* get the object */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null, return null if it is */
    if (agtv_object->type == AGTV_NULL)
            PG_RETURN_NULL();

    /* check for proper agtype */
    if (agtv_object->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("startNode() argument must be an edge or null")));

    /* get the graphid for start_id */
    agtv_value = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "start_id");
    /* it must not be null and must be an integer */
    Assert(agtv_value != NULL);
    Assert(agtv_value->type = AGTV_INTEGER);
    start_id = agtv_value->val.int_value;

    /* get the label */
    label_name = get_label_name(graph_name, start_id);
    /* it must not be null and must be a string */
    Assert(label_name != NULL);

    result = get_vertex(graph_name, label_name, start_id);

    free(label_name);

    return result;
}

PG_FUNCTION_INFO_V1(age_endnode);

Datum age_endnode(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_value = NULL;
    char *graph_name = NULL;
    char *label_name = NULL;
    graphid end_id;
    Datum result;

    /* we need the graph name */
    Assert(PG_ARGISNULL(0) == false);

    /* check for null */
    if (PG_ARGISNULL(1))
        PG_RETURN_NULL();

    /* get the graph name */
    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* it must be a scalar and must be a string */
    Assert(AGT_ROOT_IS_SCALAR(agt_arg));
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);
    Assert(agtv_object->type == AGTV_STRING);
    graph_name = strndup(agtv_object->val.string.val,
                         agtv_object->val.string.len);

    /* get the edge */
    agt_arg = AG_GET_ARG_AGTYPE_P(1);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("endNode() argument must resolve to a scalar value")));
    /* get the object */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null, return null if it is */
    if (agtv_object->type == AGTV_NULL)
            PG_RETURN_NULL();

    /* check for proper agtype */
    if (agtv_object->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("endNode() argument must be an edge or null")));

    /* get the graphid for the end_id */
    agtv_value = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "end_id");
    /* it must not be null and must be an integer */
    Assert(agtv_value != NULL);
    Assert(agtv_value->type = AGTV_INTEGER);
    end_id = agtv_value->val.int_value;

    /* get the label */
    label_name = get_label_name(graph_name, end_id);
    /* it must not be null and must be a string */
    Assert(label_name != NULL);

    result = get_vertex(graph_name, label_name, end_id);

    free(label_name);

    return result;
}

PG_FUNCTION_INFO_V1(age_head);

Datum age_head(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_arg = NULL;
    agtype_value *agtv_result = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
 
    /* check for an array */
    if ((!AGT_ROOT_IS_ARRAY(agt_arg) && !AGT_ROOT_IS_VPC(agt_arg)) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("head() argument must resolve to a list or null")));
    }

    /*
     * If we have a vpc, materialize the edges to get AGTV_ARRAY
     * agtype_value, process it and return the result.
     */
    if (AGT_ROOT_IS_VPC(agt_arg))
    {
        agtv_arg = agtv_materialize_vle_edges(agt_arg);

        /* if we have an empty list, return a null */
        if (agtv_arg->val.array.num_elems == 0)
        {
            PG_RETURN_NULL();
        }

        /* get the first element of the array */
        agtv_result = &agtv_arg->val.array.elems[0];
    }
    else
    {
        /* if we have an empty list, return a null */
        if (AGT_ROOT_COUNT(agt_arg) == 0)
        {
            PG_RETURN_NULL();
        }

        /* get the first element of the array */
        agtv_result = get_ith_agtype_value_from_container(&agt_arg->root, 0);
    }

    /* if it is AGTV_NULL, return null */
    if (agtv_result->type == AGTV_NULL)
    {
        PG_RETURN_NULL();
    }

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_last);

Datum age_last(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_arg = NULL;
    agtype_value *agtv_result = NULL;
    int size;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    agt_arg = AG_GET_ARG_AGTYPE_P(0);

    /* check for an array */
    if ((!AGT_ROOT_IS_ARRAY(agt_arg) && !AGT_ROOT_IS_VPC(agt_arg)) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("last() argument must resolve to a list or null")));
    }

    /*
     * If we have a vpc, materialize the edges to get AGTV_ARRAY
     * agtype_value, process it and return the result.
     */
    if (AGT_ROOT_IS_VPC(agt_arg))
    {
        agtv_arg = agtv_materialize_vle_edges(agt_arg);

        size = agtv_arg->val.array.num_elems;

        /* if we have an empty list, return a null */
        if (size == 0)
        {
            PG_RETURN_NULL();
        }

        /* get the first element of the array */
        agtv_result = &agtv_arg->val.array.elems[size-1];
    }
    else
    {
        size = AGT_ROOT_COUNT(agt_arg);

        /* if we have an empty list, return a null */
        if (size == 0)
        {
            PG_RETURN_NULL();
        }

        /* get the first element of the array */
        agtv_result = get_ith_agtype_value_from_container(&agt_arg->root, size-1);
    }

    /* if it is AGTV_NULL, return null */
    if (agtv_result->type == AGTV_NULL)
    {
        PG_RETURN_NULL();
    }

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}


PG_FUNCTION_INFO_V1(age_tail);
/*
 * Returns a list  containing all the elements, excluding the first one, from a list.
 */
Datum age_tail(PG_FUNCTION_ARGS)
{
    Oid arg_type;
    agtype *agt_arg = NULL;
    agtype *agt_result = NULL;
    agtype_in_state agis_result;
    int count;
    int i;

    /* check number of arguments */
    if (PG_NARGS() < 1 || PG_NARGS() > 1)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("age_tail() requires only one argument")));
    }

    /* get the data type */
    arg_type = get_fn_expr_argtype(fcinfo->flinfo, 0);

    /* check the data type */
    if (arg_type != AGTYPEOID)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("age_tail() argument must be of type agtype")));
    }

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for an array */
    if (!AGT_ROOT_IS_ARRAY(agt_arg) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("tail() argument must resolve to a list or null")));
    }

    count = AGT_ROOT_COUNT(agt_arg);

    /* if we have an empty list or only one element in the list, return null */
    if (count <= 1)
    {
        PG_RETURN_NULL();
    }

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* iterate through the list beginning with the second item */
    for (i = 1; i < count; i++)
    {
        agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM,
                                            get_ith_agtype_value_from_container(&agt_arg->root, i));
    }

    /* push the end of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    agt_result = agtype_value_to_agtype(agis_result.res);
    pfree_agtype_in_state(&agis_result);

    PG_RETURN_POINTER(agt_result);
}

PG_FUNCTION_INFO_V1(age_properties);

Datum age_properties(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_result = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar or regular object */

    if (!AGT_ROOT_IS_SCALAR(agt_arg) && !AGT_ROOT_IS_OBJECT(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("properties() argument must resolve to an object")));
    }

    /*
     * If it isn't an array (wrapped scalar) and is an object, just return it.
     * This is necessary for some cases where an object may be passed in. For
     * example, SET v={blah}.
     */
    if (!AGT_ROOT_IS_ARRAY(agt_arg) && AGT_ROOT_IS_OBJECT(agt_arg))
    {
        PG_RETURN_POINTER(agt_arg);
    }

    /* get the object out of the array */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_object->type == AGTV_NULL)
    {
        PG_RETURN_NULL();
    }

    /* check for proper agtype */
    if (agtv_object->type != AGTV_VERTEX && agtv_object->type != AGTV_EDGE)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("properties() argument must be a vertex, an edge or null")));
    }

    agtv_result = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "properties");

    Assert(agtv_result != NULL);
    Assert(agtv_result->type = AGTV_OBJECT);

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_length);

Datum age_length(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_path = NULL;
    agtype_value agtv_result;

    /* check for null */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("length() argument must resolve to a scalar")));

    /* get the path array */
    agtv_path = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* if it is AGTV_NULL, return null */
    if (agtv_path->type == AGTV_NULL)
        PG_RETURN_NULL();

    /* check for a path */
    if (agtv_path ->type != AGTV_PATH)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("length() argument must resolve to a path or null")));

    agtv_result.type = AGTV_INTEGER;
    agtv_result.val.int_value = (agtv_path->val.array.num_elems - 1) /2;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_toboolean);

Datum age_toboolean(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    Oid type;
    agtype_value agtv_result;
    char *string = NULL;
    bool result = false;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toBoolean() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * toBoolean() supports bool, text, cstring, integer or the agtype bool,
     * string and integer input.
     */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == BOOLOID)
            result = DatumGetBool(arg);
        else if (type == CSTRINGOID || type == TEXTOID)
        {
            if (type == CSTRINGOID)
                string = DatumGetCString(arg);
            else
                string = text_to_cstring(DatumGetTextPP(arg));

            if (pg_strcasecmp(string, "true") == 0)
                result = true;
            else if (pg_strcasecmp(string, "false") == 0)
                result = false;
            else
                PG_RETURN_NULL();
        }
        else if (type == INT2OID || type == INT4OID || type == INT8OID)
        {
            result = DatumGetBool(DirectFunctionCall1(int4_bool, arg));
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toBoolean() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toBoolean() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        if (agtv_value->type == AGTV_BOOL)
            result = agtv_value->val.boolean;
        else if (agtv_value->type == AGTV_STRING)
        {
            int len = agtv_value->val.string.len;

            string = agtv_value->val.string.val;

            if (len == 4 && pg_strncasecmp(string, "true", len) == 0)
                result = true;
            else if (len == 5 && pg_strncasecmp(string, "false", len) == 0)
                result = false;
            else
                PG_RETURN_NULL();
        }
        else if (agtv_value->type == AGTV_INTEGER)
        {
            result = DatumGetBool(DirectFunctionCall1(int4_bool,
                                                      Int64GetDatum(agtv_value->val.int_value)));
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toBoolean() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /* build the result */
    agtv_result.type = AGTV_BOOL;
    agtv_result.val.boolean = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_tobooleanlist);
/*
 * Converts a list of values and returns a list of boolean values. 
 * If any values are not convertible to boolean they will be null in the list returned.
 */
Datum age_tobooleanlist(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_in_state agis_result;
    agtype_value *elem;
    agtype_value bool_elem;
    char *string = NULL;
    int count;
    int i;

    /* check for null */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for an array */
    if (!AGT_ROOT_IS_ARRAY(agt_arg) || AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toBooleanList() argument must resolve to a list or null")));

    count = AGT_ROOT_COUNT(agt_arg);

    /* if we have an empty list or only one element in the list, return null */
    if (count == 0)
        PG_RETURN_NULL();
    
    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* iterate through the list */
    for (i = 0; i < count; i++)
    {
        /* TODO: check element's type, it's value, and convert it to boolean if possible. */
        elem = get_ith_agtype_value_from_container(&agt_arg->root, i);
        bool_elem.type = AGTV_BOOL;

        switch (elem->type)
        {
        case AGTV_STRING:
            
            string = elem->val.string.val;

            if (pg_strcasecmp(string, "true") == 0)
            {
                bool_elem.val.boolean = true;
                agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                    WAGT_ELEM, &bool_elem);
            }
            else if (pg_strcasecmp(string, "false") == 0)
            {
                bool_elem.val.boolean = false;
                agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                    WAGT_ELEM, &bool_elem);
            }
            else
            {
                bool_elem.type = AGTV_NULL;
                agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                    WAGT_ELEM, &bool_elem);
            }
            
            break;
        
        case AGTV_BOOL:
            
            bool_elem.val.boolean = elem->val.boolean;
            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &bool_elem);

            break;

        case AGTV_INTEGER:

            bool_elem.val.boolean = DatumGetBool(DirectFunctionCall1(int4_bool,
                                                                     Int64GetDatum(elem->val.int_value)));
            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &bool_elem);

            break;
        
        default:
            
            bool_elem.type = AGTV_NULL;
            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &bool_elem);
            
            break;
        }
    }

    /* push the end of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

PG_FUNCTION_INFO_V1(age_tofloat);

Datum age_tofloat(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    char *string = NULL;
    bool is_valid = false;
    Oid type;
    float8 result;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toFloat() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * toFloat() supports integer, float, numeric, text, cstring, or the
     * agtype integer, float, numeric, and string input
     */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == INT2OID)
            result = (float8) DatumGetInt16(arg);
        else if (type == INT4OID)
            result = (float8) DatumGetInt32(arg);
        else if (type == INT8OID)
        {
            /*
             * Get the string representation of the integer because it could be
             * too large to fit in a float. Let the float routine determine
             * what to do with it.
             */
            string = DatumGetCString(DirectFunctionCall1(int8out, arg));
            /* turn it into a float */
            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);
            /* return null if it was not a invalid float */
            if (!is_valid)
                PG_RETURN_NULL();
        }
        else if (type == FLOAT4OID)
            result = (float8) DatumGetFloat4(arg);
        else if (type == FLOAT8OID)
            result = DatumGetFloat8(arg);
        else if (type == NUMERICOID)
            result = DatumGetFloat8(DirectFunctionCall1(
                numeric_float8_no_overflow, arg));
        else if (type == CSTRINGOID || type == TEXTOID)
        {
            if (type == CSTRINGOID)
                string = DatumGetCString(arg);
            else
                string = text_to_cstring(DatumGetTextPP(arg));

            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);
            if (!is_valid)
                PG_RETURN_NULL();
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toFloat() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toFloat() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        if (agtv_value->type == AGTV_INTEGER)
        {
            /* get the string representation of the integer */
            string = DatumGetCString(DirectFunctionCall1(int8out,
                         Int64GetDatum(agtv_value->val.int_value)));
            /* turn it into a float */
            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);
            /* return null if it was an invalid float */
            if (!is_valid)
                PG_RETURN_NULL();
        }
        else if (agtv_value->type == AGTV_FLOAT)
            result = agtv_value->val.float_value;
        else if (agtv_value->type == AGTV_NUMERIC)
            result = DatumGetFloat8(DirectFunctionCall1(
                numeric_float8_no_overflow,
                NumericGetDatum(agtv_value->val.numeric)));
        else if (agtv_value->type == AGTV_STRING)
        {
            string = strndup(agtv_value->val.string.val,
                             agtv_value->val.string.len);
            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);
            free(string);
            if (!is_valid)
                PG_RETURN_NULL();
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toFloat() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_tofloatlist);
/*
 * toFloatList() converts a list of values and returns a list of floating point values.
 * If any values are not convertible to floating point they will be null in the list returned.
 */
Datum age_tofloatlist(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_in_state agis_result;
    agtype_value *elem;
    agtype_value float_elem;
    char *string = NULL;
    int count;
    int i;
    bool is_valid = false;
    float8 float_num;
    char buffer[64];

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }
    agt_arg = AG_GET_ARG_AGTYPE_P(0);

    /* check for an array */
    if (!AGT_ROOT_IS_ARRAY(agt_arg) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toFloatList() argument must resolve to a list or null")));
    }

    count = AGT_ROOT_COUNT(agt_arg);

    /* if we have an empty list or only one element in the list, return null */
    if (count == 0)
    {
        PG_RETURN_NULL();
    }

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* iterate through the list */
    for (i = 0; i < count; i++)
    {
        /* TODO: check element's type, it's value, and convert it to float if possible. */
        elem = get_ith_agtype_value_from_container(&agt_arg->root, i);
        float_elem.type = AGTV_FLOAT;

        switch (elem->type)
        {
        case AGTV_STRING:

            string = elem->val.string.val;
            if (atof(string))
            {
                float_elem.type = AGTV_FLOAT;
                float_elem.val.float_value = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);
                agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &float_elem);
            }
            else
            {
                float_elem.type = AGTV_NULL;
                agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &float_elem);
            }

            break;

        case AGTV_FLOAT:

            float_elem.type = AGTV_FLOAT;
            float_num = elem->val.float_value;
            sprintf(buffer, "%f", float_num);
            string = buffer;
            float_elem.val.float_value = float8in_internal_null(string, NULL, "double precision", string, &is_valid);
            agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &float_elem);

            break;

        default:

            float_elem.type = AGTV_NULL;
            agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &float_elem);

            break;
        }
    }
    agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_END_ARRAY, NULL);

    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

PG_FUNCTION_INFO_V1(age_tointeger);

Datum age_tointeger(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    char *string = NULL;
    bool is_valid = false;
    Oid type;
    int64 result;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toInteger() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * toInteger() supports integer, float, numeric, text, cstring, or the
     * agtype integer, float, numeric, and string input
     */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == INT2OID)
        {
            result = (int64) DatumGetInt16(arg);
        }
        else if (type == INT4OID)
        {
            result = (int64) DatumGetInt32(arg);
        }
        else if (type == INT8OID)
        {
            result = (int64) DatumGetInt64(arg);
        }
        else if (type == FLOAT4OID)
        {
            float4 f = DatumGetFloat4(arg);

            if (isnan(f) || isinf(f) ||
                f < (float4)PG_INT64_MIN || f > (float4)PG_INT64_MAX)
            {
                PG_RETURN_NULL();
            }

            result = (int64) f;
        }
        else if (type == FLOAT8OID)
        {
            float8 f = DatumGetFloat8(arg);

            if (isnan(f) || isinf(f) ||
                f < (float8)PG_INT64_MIN || f > (float8)PG_INT64_MAX)
            {
                PG_RETURN_NULL();
            }

            result = (int64) f;
        }
        else if (type == NUMERICOID)
        {
            float8 f;

            f = DatumGetFloat8(DirectFunctionCall1(
                numeric_float8_no_overflow, arg));

            if (isnan(f) || isinf(f) ||
                f < (float8)PG_INT64_MIN || f > (float8)PG_INT64_MAX)
            {
                PG_RETURN_NULL();
            }

            result = (int64) f;
        }
        else if (type == CSTRINGOID || type == TEXTOID)
        {
            char *endptr;
            if (type == CSTRINGOID)
            {
                string = DatumGetCString(arg);
            }
            else
            {
                string = text_to_cstring(DatumGetTextPP(arg));
            }

            /* convert it if it is a regular integer string */
            result = strtoi64(string, &endptr, 10);

            /*
             * If it isn't an integer string, try converting it as a float
             * string.
             */
            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);

            if (*endptr != '\0')
            {
                float8 f;

                f = float8in_internal_null(string, NULL, "double precision",
                                           string, &is_valid);
                /*
                 * If the conversions failed or it's a special float value,
                 * return null.
                 */
                if (!is_valid || isnan(f) || isinf(f) ||
                    f < PG_INT64_MIN || f > (double)PG_INT64_MAX)
                {
                    PG_RETURN_NULL();
                }

                result = (int64) f;
            }
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toInteger() unsupported argument type %d",
                                   type)));
        }
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toInteger() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        if (agtv_value->type == AGTV_INTEGER)
            result = agtv_value->val.int_value;
        else if (agtv_value->type == AGTV_FLOAT)
        {
            float8 f = agtv_value->val.float_value;

            if (isnan(f) || isinf(f) ||
                f < (float8)PG_INT64_MIN || f > (float8)PG_INT64_MAX)
            {
                PG_RETURN_NULL();
            }

            result = (int64) f;
        }
        else if (agtv_value->type == AGTV_NUMERIC)
        {
            float8 f;
            Datum num = NumericGetDatum(agtv_value->val.numeric);

            f = DatumGetFloat8(DirectFunctionCall1(
                numeric_float8_no_overflow, num));

            if (isnan(f) || isinf(f) ||
                f < (float8)PG_INT64_MIN || f > (float8)PG_INT64_MAX)
            {
                PG_RETURN_NULL();
            }

            result = (int64) f;
        }
        else if (agtv_value->type == AGTV_STRING)
        {
            char *endptr;
            /* we need a null terminated cstring */
            string = strndup(agtv_value->val.string.val,
                             agtv_value->val.string.len);
            /* convert it if it is a regular integer string */
            result = strtoi64(string, &endptr, 10);

            /*
             * If it isn't an integer string, try converting it as a float
             * string.
             */
            if (*endptr != '\0')
            {
                float8 f;

                f = float8in_internal_null(string, NULL, "double precision",
                                           string, &is_valid);
                free(string);
                /*
                 * If the conversions failed or it's a special float value,
                 * return null.
                 */
                if (!is_valid || isnan(f) || isinf(f) ||
                    f < (float8)PG_INT64_MIN || f > (float8)PG_INT64_MAX)
                {
                    PG_RETURN_NULL();
                }

                result = (int64) f;
            }
            else
            {
                free(string);
            }
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toInteger() unsupported argument agtype %d",
                                   agtv_value->type)));
        }
    }

    /* build the result */
    agtv_result.type = AGTV_INTEGER;
    agtv_result.val.int_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_tointegerlist);
/*
 * toIntegerList() converts a list of values and returns a list of integers point values.
 * If any values are not convertible to integer they will be null in the list returned.
 */
Datum age_tointegerlist(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_in_state agis_result;
    agtype_value *elem;
    agtype_value integer_elem;
    int count;
    int i;
    char *string = NULL;
    int integer_num;
    float float_num;
    int is_float;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }
    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for an array */
    if (!AGT_ROOT_IS_ARRAY(agt_arg) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toIntegerList() argument must resolve to a list or null")));
    }

    count = AGT_ROOT_COUNT(agt_arg);

    /* if we have an empty list or only one element in the list, return null */
    if (count == 0)
    {
        PG_RETURN_NULL();
    }

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* iterate through the list */
    for (i = 0; i < count; i++)
    {
        /* TODO: check element's type, it's value, and convert it to integer if possible. */
        elem = get_ith_agtype_value_from_container(&agt_arg->root, i);
        integer_elem.type = AGTV_INTEGER;

        switch (elem->type)
        {
        case AGTV_STRING:

            string = elem->val.string.val;
            integer_elem.type = AGTV_INTEGER;
            integer_elem.val.int_value = atoi(string);

            if (*string == '+' || *string == '-' || (*string >= '0' && *string <= '9'))
            {
                is_float = 1;
                while (*(++string))
                {
                    if(!(*string >= '0' && *string <= '9'))
                    {
                        if(*string == '.' && is_float)
                        {
                            is_float--;
                        }
                        else
                        {
                            integer_elem.type = AGTV_NULL;
                            break;
                        }
                    }
                }
            }
            else
            {

                integer_elem.type = AGTV_NULL;
            }

            agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &integer_elem);

            break;

        case AGTV_FLOAT:

            integer_elem.type = AGTV_INTEGER;
            float_num = elem->val.float_value;
            integer_elem.val.int_value = (int)float_num;
            agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &integer_elem);

            break;

        case AGTV_INTEGER:

            integer_elem.type = AGTV_INTEGER;
            integer_num = elem->val.int_value;
            integer_elem.val.int_value = integer_num;
            agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &integer_elem);

            break;

        default:

            integer_elem.type = AGTV_NULL;
            agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM, &integer_elem);

            break;
        }
    }
    agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_END_ARRAY, NULL);

    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

PG_FUNCTION_INFO_V1(age_size);

Datum age_size(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    char *string = NULL;
    Oid type;
    int64 result;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("size() only supports one argument")));
    }

    /* check for null */
    if (nargs < 0 || nulls[0])
    {
        PG_RETURN_NULL();
    }

    /*
     * size() supports cstring, text, or the agtype string or list input
     */
    arg = args[0];
    type = types[0];

    if (type == CSTRINGOID)
    {
        string = DatumGetCString(arg);
        result = strlen(string);
    }
    else if (type == TEXTOID)
    {
        string = text_to_cstring(DatumGetTextPP(arg));
        result = strlen(string);
    }
    else if (type == AGTYPEOID)
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (AGT_ROOT_IS_SCALAR(agt_arg))
        {
            agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

            if (agtv_value->type == AGTV_STRING)
            {
                result = agtv_value->val.string.len;
            }
            else
            {
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                        errmsg("size() unsupported argument")));
            }
        }
        else if (AGT_ROOT_IS_VPC(agt_arg))
        {
            agtv_value = agtv_materialize_vle_edges(agt_arg);
            result = agtv_value->val.array.num_elems;
        }
        else if (AGT_ROOT_IS_ARRAY(agt_arg))
        {
            result = AGT_ROOT_COUNT(agt_arg);
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("size() unsupported argument")));
        }
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("size() unsupported argument")));
    }

    /* build the result */
    agtv_result.type = AGTV_INTEGER;
    agtv_result.val.int_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(graphid_to_agtype);

Datum graphid_to_agtype(PG_FUNCTION_ARGS)
{
    PG_RETURN_POINTER(integer_to_agtype(AG_GETARG_GRAPHID(0)));
}

PG_FUNCTION_INFO_V1(agtype_to_graphid);

Datum agtype_to_graphid(PG_FUNCTION_ARGS)
{
    agtype *agtype_in = AG_GET_ARG_AGTYPE_P(0);
    agtype_value agtv;

    if (!agtype_extract_scalar(&agtype_in->root, &agtv) ||
        agtv.type != AGTV_INTEGER)
        cannot_cast_agtype_value(agtv.type, "graphid");

    PG_FREE_IF_COPY(agtype_in, 0);

    PG_RETURN_INT16(agtv.val.int_value);
}

PG_FUNCTION_INFO_V1(age_type);

Datum age_type(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_object = NULL;
    agtype_value *agtv_result = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("type() argument must resolve to a scalar value")));

    /* get the object out of the array */
    agtv_object = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_object->type == AGTV_NULL)
            PG_RETURN_NULL();

    /* check for proper agtype */
    if (agtv_object->type != AGTV_EDGE)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("type() argument must be an edge or null")));

    agtv_result = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, "label");

    Assert(agtv_result != NULL);
    Assert(agtv_result->type = AGTV_STRING);

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_exists);
/*
 * Executor function for EXISTS(property).
 *
 * Note: For most executor functions we want to return SQL NULL for NULL input.
 *       However, in this case, NULL means false - it was not found.
 */
Datum age_exists(PG_FUNCTION_ARGS)
{
    /* check for NULL, NULL is FALSE */
    if (PG_ARGISNULL(0))
        PG_RETURN_BOOL(false);

    /* otherwise, we have something, and something is TRUE */
    PG_RETURN_BOOL(true);
}

PG_FUNCTION_INFO_V1(age_isempty);
/*
 * Executor function for isEmpty(property).
 */

Datum age_isempty(PG_FUNCTION_ARGS)
{
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    char *string = NULL;
    Oid type;
    int64 result;

    /* extract argument values */
    extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /*
     * isEmpty() supports cstring, text, or the agtype string or list input
     */
    arg = args[0];
    type = types[0];

    if (type == CSTRINGOID)
    {
        string = DatumGetCString(arg);
        result = strlen(string);
    }
    else if (type == TEXTOID)
    {
        string = text_to_cstring(DatumGetTextPP(arg));
        result = strlen(string);
    }
    else if (type == AGTYPEOID)
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (AGT_ROOT_IS_SCALAR(agt_arg))
        {
            agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

            if (agtv_value->type == AGTV_STRING)
            {
                result = agtv_value->val.string.len;
            }
            else
            {
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                        errmsg("isEmpty() unsupported argument, expected a List, Map, or String")));
            }
        }
        else if (AGT_ROOT_IS_VPC(agt_arg))
        {
            agtv_value = agtv_materialize_vle_edges(agt_arg);
            result = agtv_value->val.array.num_elems;
        }
        else if (AGT_ROOT_IS_ARRAY(agt_arg))
        {
            result = AGT_ROOT_COUNT(agt_arg);
        }
        else if (AGT_ROOT_IS_OBJECT(agt_arg))
        {
            result = AGT_ROOT_COUNT(agt_arg);
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("isEmpty() unsupported argument, expected a List, Map, or String")));
        }
    }
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("isEmpty() unsupported argument, expected a List, Map, or String")));
    }

    /* build the result */
    PG_RETURN_BOOL(result == 0);
}

PG_FUNCTION_INFO_V1(age_label);
/*
 * Executor function for label(edge/vertex).
 */
Datum age_label(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_value = NULL;
    agtype_value *label = NULL;

    /* check for NULL, NULL is FALSE */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    /* get the argument */
    agt_arg = AG_GET_ARG_AGTYPE_P(0);

    /* edges and vertices are considered scalars */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
    {
        if (AGTE_IS_NULL(agt_arg->root.children[0]))
            PG_RETURN_NULL();

        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("label() argument must resolve to an edge or vertex")));

    }

    agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* fail if agtype value isn't an edge or vertex */
    if (agtv_value->type != AGTV_VERTEX && agtv_value->type != AGTV_EDGE)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("label() argument must resolve to an edge or vertex")));

    }

    /* extract the label agtype value from the vertex or edge */
    label = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_value, "label");

    PG_RETURN_POINTER(agtype_value_to_agtype(label));
}

PG_FUNCTION_INFO_V1(age_tostring);

Datum age_tostring(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum arg;
    Oid type = InvalidOid;
    agtype *agt = NULL;
    agtype_value *agtv = NULL;

    nargs = PG_NARGS();

    /* check number of args */
    if (nargs > 1)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toString() only supports one argument")));
    }

    /* check for null */
    if (nargs < 1 || PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    /* get the argument and type */
    arg = PG_GETARG_DATUM(0);
    type = get_fn_expr_argtype(fcinfo->flinfo, 0);

    /* verify that if the type is UNKNOWNOID it can be converted */
    if (type == UNKNOWNOID && !get_fn_expr_arg_stable(fcinfo->flinfo, 0))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toString() UNKNOWNOID and not stable")));
    }

    /*
     * toString() supports integer, float, numeric, text, cstring, boolean,
     * regtype or the agtypes: integer, float, numeric, string, boolean input
     */
    agtv = tostring_helper(arg, type, "toString()");

    /* if we get a NULL back we need to return NULL */
    if (agtv == NULL)
    {
        PG_RETURN_NULL();
    }

    /* convert to agtype and free the agtype_value */
    agt = agtype_value_to_agtype(agtv);
    pfree_if_not_null(agtv);

    PG_RETURN_POINTER(agt);
}

/*
 * Helper function to take any valid type and convert it to an agtype string.
 * Returns NULL for NULL input.
 */
static agtype_value *tostring_helper(Datum arg, Oid type, char *msghdr)
{
    agtype_value *agtv_result = NULL;
    char *string = NULL;

    agtv_result = palloc0(sizeof(agtype_value));

    /*
     * toString() supports: unknown, integer, float, numeric, text, cstring,
     * boolean, regtype or the agtypes: integer, float, numeric, string, and
     * boolean input.
     */

    /*
     * If the type is UNKNOWNOID convert it to a cstring. Prior to passing an
     * UNKNOWNOID it should be verified to be stable.
     */
    if (type == UNKNOWNOID)
    {
        char *str = DatumGetPointer(arg);

        string = pnstrdup(str, strlen(str));
    }
    /* if it is not an AGTYPEOID */
    else if (type != AGTYPEOID)
    {
        if (type == INT2OID)
        {
            string = DatumGetCString(DirectFunctionCall1(int8out,
                Int64GetDatum((int64) DatumGetInt16(arg))));
        }
        else if (type == INT4OID)
        {
            string = DatumGetCString(DirectFunctionCall1(int8out,
                Int64GetDatum((int64) DatumGetInt32(arg))));
        }
        else if (type == INT8OID)
        {
            string = DatumGetCString(DirectFunctionCall1(int8out, arg));
        }
        else if (type == FLOAT4OID)
        {
            string = DatumGetCString(DirectFunctionCall1(float8out, arg));
        }
        else if (type == FLOAT8OID)
        {
            string = DatumGetCString(DirectFunctionCall1(float8out, arg));
        }
        else if (type == NUMERICOID)
        {
            string = DatumGetCString(DirectFunctionCall1(numeric_out, arg));
        }
        else if (type == CSTRINGOID)
        {
            string = DatumGetCString(arg);
        }
        else if (type == TEXTOID)
        {
            string = text_to_cstring(DatumGetTextPP(arg));
        }
        else if (type == BOOLOID)
        {
            string = DatumGetBool(arg) ? "true" : "false";
        }
        else if (type == REGTYPEOID)
        {
            string = DatumGetCString(DirectFunctionCall1(regtypeout, arg));
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s unsupported argument type %d",
                                   msghdr, type)));
        }
    }
    /* if it is an AGTYPEOID */
    else if (type == AGTYPEOID)
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s only supports scalar arguments",
                                   msghdr)));
        }

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        if (agtv_value->type == AGTV_NULL)
        {
            return NULL;
        }
        else if (agtv_value->type == AGTV_INTEGER)
        {
            string = DatumGetCString(DirectFunctionCall1(int8out,
                Int64GetDatum(agtv_value->val.int_value)));
        }
        else if (agtv_value->type == AGTV_FLOAT)
        {
            string = DatumGetCString(DirectFunctionCall1(float8out,
                Float8GetDatum(agtv_value->val.float_value)));
        }
        else if (agtv_value->type == AGTV_STRING)
        {
            string = pnstrdup(agtv_value->val.string.val,
                              agtv_value->val.string.len);
        }
        else if (agtv_value->type == AGTV_NUMERIC)
        {
            string = DatumGetCString(DirectFunctionCall1(numeric_out,
                PointerGetDatum(agtv_value->val.numeric)));
        }
        else if (agtv_value->type == AGTV_BOOL)
        {
            string = (agtv_value->val.boolean) ? "true" : "false";
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s unsupported argument agtype %d",
                                   msghdr, agtv_value->type)));
        }
    }
    /* it is an unknown type */
    else
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s unknown argument agtype %d",
                                   msghdr, type)));
    }

    /* build the result */
    agtv_result->type = AGTV_STRING;
    agtv_result->val.string.val = string;
    agtv_result->val.string.len = strlen(string);

    return agtv_result;
}

PG_FUNCTION_INFO_V1(age_tostringlist);
/*
 * toStringList() converts a list of values and returns a list of String values.
 * If any values are not convertible to string point they will be null in the list returned.
 */
Datum age_tostringlist(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_in_state agis_result;
    agtype_value *elem;
    agtype_value string_elem;
    int count;
    int i;
    char buffer[64];

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }
    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for an array */
    if (!AGT_ROOT_IS_ARRAY(agt_arg) || AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toStringList() argument must resolve to a list or null")));
    }

    count = AGT_ROOT_COUNT(agt_arg);

    /* if we have an empty list or only one element in the list, return null */
    if (count == 0)
    {
        PG_RETURN_NULL();
    }

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* iterate through the list */
    for (i = 0; i < count; i++)
    {
        /* TODO: check element's type, it's value, and convert it to string if possible. */
        elem = get_ith_agtype_value_from_container(&agt_arg->root, i);
        string_elem.type = AGTV_STRING;

        switch (elem->type)
        {
        case AGTV_STRING:

            if(!elem)
            {
                string_elem.type = AGTV_NULL;

                agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                    WAGT_ELEM, &string_elem);
            }

            string_elem.val.string.val = elem->val.string.val;
            string_elem.val.string.len = elem->val.string.len;

            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &string_elem);

            break;

        case AGTV_FLOAT:

            sprintf(buffer, "%.*g", DBL_DIG, elem->val.float_value);
            string_elem.val.string.val = pstrdup(buffer);
            string_elem.val.string.len = strlen(buffer);

            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &string_elem);

            break;

        case AGTV_INTEGER:

            sprintf(buffer, "%ld", elem->val.int_value);
            string_elem.val.string.val = pstrdup(buffer);
            string_elem.val.string.len = strlen(buffer);

            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &string_elem);

            break;

        default:

            string_elem.type = AGTV_NULL;
            agis_result.res = push_agtype_value(&agis_result.parse_state,
                                                WAGT_ELEM, &string_elem);

            break;
        }
    }

    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

agtype_iterator *get_next_list_element(agtype_iterator *it,
                           agtype_container *agtc, agtype_value *elem)
{
    agtype_iterator_token itok;
    agtype_value tmp;

    /* verify input params */
    Assert(agtc != NULL);
    Assert(elem != NULL);

    /* check to see if the container is empty */
    if (AGTYPE_CONTAINER_SIZE(agtc) == 0)
    {
       return NULL;
    }

    /* if the passed iterator is NULL, this is the first time, create it */
    if (it == NULL)
    {
        /* initial the iterator */
        it = agtype_iterator_init(agtc);
        /* get the first token */
        itok = agtype_iterator_next(&it, &tmp, true);
        /* it should be WAGT_BEGIN_ARRAY */
        Assert(itok == WAGT_BEGIN_ARRAY);
    }

    /* the next token should be an element or the end of the array */
    itok = agtype_iterator_next(&it, &tmp, true);
    Assert(itok == WAGT_ELEM || itok == WAGT_END_ARRAY);

    /* if this is the end of the array return NULL */
    if (itok == WAGT_END_ARRAY)
    {
        return NULL;
    }

    /* this should be the element, copy it */
    if (itok == WAGT_ELEM)
    {
        *elem = tmp;
    }

    return it;
}

PG_FUNCTION_INFO_V1(age_reverse);

Datum age_reverse(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int string_len;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("reverse() only supports one argument")));
    }

    /* check for null */
    if (nargs < 0 || nulls[0])
    {
        PG_RETURN_NULL();
    }

    /* reverse() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
        {
            text_string = cstring_to_text(DatumGetCString(arg));
        }
        else if (type == TEXTOID)
        {
            text_string = DatumGetTextPP(arg);
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("reverse() unsupported argument type %d",
                                   type)));
        }
    }
    else
    {
        agtype *agt_arg = NULL;
        agtype_value *agtv_value = NULL;
        agtype_in_state result;
        agtype_parse_state *parse_state = NULL;
        agtype_value elem = {0};
        agtype_iterator *it = NULL;
        agtype_value tmp;
        agtype_value *elems = NULL;
        int num_elems;
        int i;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (AGT_ROOT_IS_SCALAR(agt_arg))
        {
            agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

            /* check for agtype null */
            if (agtv_value->type == AGTV_NULL)
            {
                PG_RETURN_NULL();
            }
            if (agtv_value->type == AGTV_STRING)
            {
                text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                    agtv_value->val.string.len);
            }
            else
            {
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("reverse() unsupported argument agtype %d",
                                    agtv_value->type)));
            }
        }
        else if (AGT_ROOT_IS_ARRAY(agt_arg))
        {
            agtv_value = push_agtype_value(&parse_state, WAGT_BEGIN_ARRAY, NULL);

            while ((it = get_next_list_element(it, &agt_arg->root, &elem)))
            {
                agtv_value = push_agtype_value(&parse_state, WAGT_ELEM, &elem);
            }

            /* now reverse the list */
            elems = parse_state->cont_val.val.array.elems;
            num_elems = parse_state->cont_val.val.array.num_elems;

            for(i = 0; i < num_elems/2; i++)
            {
                tmp = elems[i];
                elems[i] = elems[num_elems - 1 - i];
                elems[num_elems - 1 - i] = tmp;
            }
            /* reverse done*/

            elems = NULL;

            agtv_value = push_agtype_value(&parse_state, WAGT_END_ARRAY, NULL);

            Assert(agtv_value != NULL);
            Assert(agtv_value->type = AGTV_ARRAY);

            PG_RETURN_POINTER(agtype_value_to_agtype(agtv_value));

        }
        else if (AGT_ROOT_IS_VPC(agt_arg))
        {
            elems = agtv_materialize_vle_edges(agt_arg);
            num_elems = elems->val.array.num_elems;

            /* build our result array */
            memset(&result, 0, sizeof(agtype_in_state));

            result.res = push_agtype_value(&result.parse_state,
                                            WAGT_BEGIN_ARRAY, NULL);

            for (i = num_elems-1; i >= 0; i--)
            {
                result.res = push_agtype_value(&result.parse_state, WAGT_ELEM,
                                               &elems->val.array.elems[i]);
            }

            result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

            PG_RETURN_POINTER(agtype_value_to_agtype(result.res));
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("reverse() unsupported argument agtype")));
        }
    }

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in reversing the string.
     */
    text_string = DatumGetTextPP(DirectFunctionCall1(text_reverse,
                                                     PointerGetDatum(text_string)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
    {
        PG_RETURN_NULL();
    }

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_toupper);

Datum age_toupper(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    char *string = NULL;
    char *result = NULL;
    int string_len;
    Oid type;
    int i;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toUpper() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* toUpper() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];
    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            string = DatumGetCString(arg);
        else if (type == TEXTOID)
            string = text_to_cstring(DatumGetTextPP(arg));
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toUpper() unsupported argument type %d",
                                   type)));
        string_len = strlen(string);
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toUpper() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
        {
            string = agtv_value->val.string.val;
            string_len = agtv_value->val.string.len;
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toUpper() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* allocate the new string */
    result = palloc0(string_len);

    /* upcase the string */
    for (i = 0; i < string_len; i++)
        result[i] = pg_toupper(string[i]);

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = result;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_tolower);

Datum age_tolower(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    char *string = NULL;
    char *result = NULL;
    int string_len;
    Oid type;
    int i;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("toLower() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* toLower() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];
    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            string = DatumGetCString(arg);
        else if (type == TEXTOID)
            string = text_to_cstring(DatumGetTextPP(arg));
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toLower() unsupported argument type %d",
                                   type)));
        string_len = strlen(string);
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toLower() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
        {
            string = agtv_value->val.string.val;
            string_len = agtv_value->val.string.len;
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("toLower() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* allocate the new string */
    result = palloc0(string_len);

    /* downcase the string */
    for (i = 0; i < string_len; i++)
        result[i] = pg_tolower(string[i]);

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = result;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_rtrim);

Datum age_rtrim(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int string_len;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("rTrim() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* rTrim() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            text_string = cstring_to_text(DatumGetCString(arg));
        else if (type == TEXTOID)
            text_string = DatumGetTextPP(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("rTrim() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("rTrim() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
            text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                   agtv_value->val.string.len);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("rTrim() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in trimming the string.
     */
    text_string = DatumGetTextPP(DirectFunctionCall1(rtrim1,
                                                     PointerGetDatum(text_string)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_ltrim);

Datum age_ltrim(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int string_len;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("lTrim() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* rTrim() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            text_string = cstring_to_text(DatumGetCString(arg));
        else if (type == TEXTOID)
            text_string = DatumGetTextPP(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("lTrim() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("lTrim() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
            text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                   agtv_value->val.string.len);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("lTrim() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in trimming the string.
     */
    text_string = DatumGetTextPP(DirectFunctionCall1(ltrim1,
                                                     PointerGetDatum(text_string)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_trim);

Datum age_trim(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int string_len;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs > 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("trim() only supports one argument")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* trim() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            text_string = cstring_to_text(DatumGetCString(arg));
        else if (type == TEXTOID)
            text_string = DatumGetTextPP(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("trim() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("trim() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
            text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                   agtv_value->val.string.len);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("trim() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in trimming the string.
     */
    text_string = DatumGetTextPP(DirectFunctionCall1(btrim1,
                                                     PointerGetDatum(text_string)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_right);

Datum age_right(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int string_len;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 2)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("right() invalid number of arguments")));

    /* check for a null string */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* check for a null length */
    if (nulls[1])
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() length parameter cannot be null")));

    /* right() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            text_string = cstring_to_text(DatumGetCString(arg));
        else if (type == TEXTOID)
            text_string = DatumGetTextPP(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
            text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                   agtv_value->val.string.len);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /* right() only supports integer and agtype integer for the second parameter. */
    arg = args[1];
    type = types[1];

    if (type != AGTYPEOID)
    {
        if (type == INT2OID)
            string_len = (int64) DatumGetInt16(arg);
        else if (type == INT4OID)
            string_len = (int64) DatumGetInt32(arg);
        else if (type == INT8OID)
            string_len = (int64) DatumGetInt64(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() unsupported argument type %d", type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* no need to check for agtype null because it is an error if found */
        if (agtv_value->type != AGTV_INTEGER)
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("right() unsupported argument agtype %d",
                                   agtv_value->type)));

        string_len = agtv_value->val.int_value;
    }

    /* negative values are not supported in the opencypher spec */
    if (string_len < 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("right() negative values are not supported for length")));

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in the string.
     */
    text_string = DatumGetTextPP(DirectFunctionCall2(text_right,
                                                     PointerGetDatum(text_string),
                                                     Int64GetDatum(string_len)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_left);

Datum age_left(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int string_len;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 2)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("left() invalid number of arguments")));

    /* check for a null string */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* check for a null length */
    if (nulls[1])
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() length parameter cannot be null")));

    /* left() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            text_string = cstring_to_text(DatumGetCString(arg));
        else if (type == TEXTOID)
            text_string = DatumGetTextPP(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
            text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                   agtv_value->val.string.len);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /* left() only supports integer and agtype integer for the second parameter. */
    arg = args[1];
    type = types[1];

    if (type != AGTYPEOID)
    {
        if (type == INT2OID)
            string_len = (int64) DatumGetInt16(arg);
        else if (type == INT4OID)
            string_len = (int64) DatumGetInt32(arg);
        else if (type == INT8OID)
            string_len = (int64) DatumGetInt64(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() unsupported argument type %d", type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* no need to check for agtype null because it is an error if found */
        if (agtv_value->type != AGTV_INTEGER)
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("left() unsupported argument agtype %d",
                                   agtv_value->type)));

        string_len = agtv_value->val.int_value;
    }

    /* negative values are not supported in the opencypher spec */
    if (string_len < 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("left() negative values are not supported for length")));

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in the string.
     */
    text_string = DatumGetTextPP(DirectFunctionCall2(text_left,
                                                     PointerGetDatum(text_string),
                                                     Int64GetDatum(string_len)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_substring);

Datum age_substring(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *text_string = NULL;
    char *string = NULL;
    int param;
    int string_start = 0;
    int string_len = 0;
    int i;
    Oid type;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs < 2 || nargs > 3)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("substring() invalid number of arguments")));

    /* check for null */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /* neither offset or length can be null if there is a valid string */
    if ((nargs == 2 && nulls[1]) ||
        (nargs == 3 && nulls[2]))
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("substring() offset or length cannot be null")));

    /* substring() supports text, cstring, or the agtype string input */
    arg = args[0];
    type = types[0];

    if (type != AGTYPEOID)
    {
        if (type == CSTRINGOID)
            text_string = cstring_to_text(DatumGetCString(arg));
        else if (type == TEXTOID)
            text_string = DatumGetTextPP(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("substring() unsupported argument type %d",
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("substring() only supports scalar arguments")));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            PG_RETURN_NULL();
        if (agtv_value->type == AGTV_STRING)
            text_string = cstring_to_text_with_len(agtv_value->val.string.val,
                                                   agtv_value->val.string.len);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("substring() unsupported argument agtype %d",
                                   agtv_value->type)));
    }

    /*
     * substring() only supports integer and agtype integer for the second and
     * third parameters values.
     */
    for (i = 1; i < nargs; i++)
    {
        arg = args[i];
        type = types[i];

        if (type != AGTYPEOID)
        {
            if (type == INT2OID)
                param = (int64) DatumGetInt16(arg);
            else if (type == INT4OID)
                param = (int64) DatumGetInt32(arg);
            else if (type == INT8OID)
                param = (int64) DatumGetInt64(arg);
            else
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("substring() unsupported argument type %d",
                                       type)));
        }
        else
        {
            agtype *agt_arg;
            agtype_value *agtv_value;

            /* get the agtype argument */
            agt_arg = DATUM_GET_AGTYPE_P(arg);

            if (!AGT_ROOT_IS_SCALAR(agt_arg))
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("substring() only supports scalar arguments")));

            agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

            /* no need to check for agtype null because it is an error if found */
            if (agtv_value->type != AGTV_INTEGER)
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("substring() unsupported argument agtype %d",
                                       agtv_value->type)));

            param = agtv_value->val.int_value;
        }

        if (i == 1)
            string_start = param;
        if (i == 2)
            string_len = param;
    }

    /* negative values are not supported in the opencypher spec */
    if (string_start < 0 || string_len < 0)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("substring() negative values are not supported for offset or length")));

    /* cypher substring is 0 based while PG's is 1 based */
    string_start += 1;

    /*
     * We need the string as a text string so that we can let PG deal with
     * multibyte characters in the string.
     */

    /* if optional length is left out */
    if (nargs == 2)
         text_string = DatumGetTextPP(DirectFunctionCall2(text_substr_no_len,
                                                          PointerGetDatum(text_string),
                                                          Int64GetDatum(string_start)));
    /* if length is given */
    else
        text_string = DatumGetTextPP(DirectFunctionCall3(text_substr,
                                                         PointerGetDatum(text_string),
                                                         Int64GetDatum(string_start),
                                                         Int64GetDatum(string_len)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_string);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_split);

Datum age_split(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value *agtv_result;
    text *param = NULL;
    text *text_string = NULL;
    text *text_delimiter = NULL;
    Datum text_array;
    Oid type;
    int i;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 2)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("split() invalid number of arguments")));

    /* check for a null string and delimiter */
    if (nargs < 0 || nulls[0] || nulls[1])
        PG_RETURN_NULL();

    /*
     * split() supports text, cstring, or the agtype string input for the
     * string and delimiter values
     */

    for (i = 0; i < 2; i++)
    {
        arg = args[i];
        type = types[i];

        if (type != AGTYPEOID)
        {
            if (type == CSTRINGOID)
                param = cstring_to_text(DatumGetCString(arg));
            else if (type == TEXTOID)
                param = DatumGetTextPP(arg);
            else
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("split() unsupported argument type %d",
                                       type)));
        }
        else
        {
            agtype *agt_arg;
            agtype_value *agtv_value;

            /* get the agtype argument */
            agt_arg = DATUM_GET_AGTYPE_P(arg);

            if (!AGT_ROOT_IS_SCALAR(agt_arg))
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("split() only supports scalar arguments")));

            agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

            /* check for agtype null */
            if (agtv_value->type == AGTV_NULL)
                PG_RETURN_NULL();
            if (agtv_value->type == AGTV_STRING)
                param = cstring_to_text_with_len(agtv_value->val.string.val,
                                                 agtv_value->val.string.len);
            else
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("split() unsupported argument agtype %d",
                                       agtv_value->type)));
        }
        if (i == 0)
            text_string = param;
        if (i == 1)
            text_delimiter = param;
    }

    /*
     * We need the strings as a text strings so that we can let PG deal with
     * multibyte characters in the string. The result is an ArrayType
     */
    text_array = DirectFunctionCall2Coll(regexp_split_to_array,
                                         DEFAULT_COLLATION_OID,
                                         PointerGetDatum(text_string),
                                         PointerGetDatum(text_delimiter));

    /* now build an agtype array of strings */
    if (PointerIsValid(DatumGetPointer(text_array)))
    {
        ArrayType *array = DatumGetArrayTypeP(text_array);
        agtype_in_state result;
        Datum *elements;
        int nelements;

        /* zero the state and deconstruct the ArrayType to TEXTOID */
        memset(&result, 0, sizeof(agtype_in_state));
        deconstruct_array(array, TEXTOID, -1, false, 'i', &elements, NULL,
                          &nelements);

        /* open the agtype array */
        result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_ARRAY,
                                       NULL);
        /* add the values */
        for (i = 0; i < nelements; i++)
        {
            char *string;
            int string_len;
            char *string_copy;
            agtype_value agtv_string;
            Datum d;

            /* get the string element from the array */
            string = VARDATA(elements[i]);
            string_len = VARSIZE(elements[i]) - VARHDRSZ;

            /* make a copy */
            string_copy = palloc0(string_len);
            memcpy(string_copy, string, string_len);

            /* build the agtype string */
            agtv_string.type = AGTV_STRING;
            agtv_string.val.string.val = string_copy;
            agtv_string.val.string.len = string_len;

            /* get the datum */
            d = PointerGetDatum(agtype_value_to_agtype(&agtv_string));

            /* add the value */
            add_agtype(d, false, &result, AGTYPEOID, false);
        }

        /* close the array */
        result.res = push_agtype_value(&result.parse_state, WAGT_END_ARRAY, NULL);

        agtv_result = result.res;
    }
    else
    {
        elog(ERROR, "split() unexpected error");
    }

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_replace);

Datum age_replace(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    Datum arg;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    text *param = NULL;
    text *text_string = NULL;
    text *text_search = NULL;
    text *text_replace = NULL;
    text *text_result = NULL;
    char *string = NULL;
    int string_len;
    Oid type;
    int i;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 3)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("replace() invalid number of arguments")));

    /* check for a null string, search, and replace */
    if (nargs < 0 || nulls[0] || nulls[1] || nulls[2])
        PG_RETURN_NULL();

    /*
     * replace() supports text, cstring, or the agtype string input for the
     * string and delimiter values
     */

    for (i = 0; i < 3; i++)
    {
        arg = args[i];
        type = types[i];

        if (type != AGTYPEOID)
        {
            if (type == CSTRINGOID)
                param = cstring_to_text(DatumGetCString(arg));
            else if (type == TEXTOID)
                param = DatumGetTextPP(arg);
            else
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("replace() unsupported argument type %d",
                                       type)));
        }
        else
        {
            agtype *agt_arg;
            agtype_value *agtv_value;

            /* get the agtype argument */
            agt_arg = DATUM_GET_AGTYPE_P(arg);

            if (!AGT_ROOT_IS_SCALAR(agt_arg))
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("replace() only supports scalar arguments")));

            agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

            /* check for agtype null */
            if (agtv_value->type == AGTV_NULL)
                PG_RETURN_NULL();
            if (agtv_value->type == AGTV_STRING)
                param = cstring_to_text_with_len(agtv_value->val.string.val,
                                                 agtv_value->val.string.len);
            else
                ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                                errmsg("replace() unsupported argument agtype %d",
                                       agtv_value->type)));
        }
        if (i == 0)
            text_string = param;
        if (i == 1)
            text_search = param;
        if (i == 2)
            text_replace = param;
    }

    /*
     * We need the strings as a text strings so that we can let PG deal with
     * multibyte characters in the string.
     */
    text_result = DatumGetTextPP(DirectFunctionCall3Coll(
        replace_text, C_COLLATION_OID, PointerGetDatum(text_string),
        PointerGetDatum(text_search), PointerGetDatum(text_replace)));

    /* convert it back to a cstring */
    string = text_to_cstring(text_result);
    string_len = strlen(string);

    /* if we have an empty string, return null */
    if (string_len == 0)
        PG_RETURN_NULL();

    /* build the result */
    agtv_result.type = AGTV_STRING;
    agtv_result.val.string.val = string;
    agtv_result.val.string.len = string_len;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

/*
 * Helper function to extract one float8 compatible value from a variadic any.
 * It supports integer2/4/8, float4/8, and numeric or the agtype integer, float,
 * and numeric for the argument. It does not support a character based float,
 * otherwise we would just use tofloat. It returns a float on success or fails
 * with a message stating the funcname that called it and a specific message
 * stating the error.
 */
static float8 get_float_compatible_arg(Datum arg, Oid type, char *funcname,
                                       bool *is_null)
{
    float8 result;

    /* Assume the value is null. Although, this is only necessary for agtypes */
    *is_null = true;

    if (type != AGTYPEOID)
    {
        if (type == INT2OID)
            result = (float8) DatumGetInt16(arg);
        else if (type == INT4OID)
            result = (float8) DatumGetInt32(arg);
        else if (type == INT8OID)
        {
            /*
             * Get the string representation of the integer because it could be
             * too large to fit in a float. Let the float routine determine
             * what to do with it.
             */
            char *string = DatumGetCString(DirectFunctionCall1(int8out, arg));
            bool is_valid = false;
            /* turn it into a float */
            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);

            /* return 0 if it was an invalid float */
            if (!is_valid)
                return 0;
        }
        else if (type == FLOAT4OID)
            result = (float8) DatumGetFloat4(arg);
        else if (type == FLOAT8OID)
            result = DatumGetFloat8(arg);
        else if (type == NUMERICOID)
            result = DatumGetFloat8(DirectFunctionCall1(
                numeric_float8_no_overflow, arg));
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s() unsupported argument type %d", funcname,
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s() only supports scalar arguments",
                                   funcname)));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            return 0;

        if (agtv_value->type == AGTV_INTEGER)
        {
            /*
             * Get the string representation of the integer because it could be
             * too large to fit in a float. Let the float routine determine
             * what to do with it.
             */
            bool is_valid = false;
            char *string = DatumGetCString(DirectFunctionCall1(int8out,
                                                               Int64GetDatum(agtv_value->val.int_value)));
            /* turn it into a float */
            result = float8in_internal_null(string, NULL, "double precision",
                                            string, &is_valid);

            /* return null if it was not a valid float */
            if (!is_valid)
                return 0;
        }
        else if (agtv_value->type == AGTV_FLOAT)
            result = agtv_value->val.float_value;
        else if (agtv_value->type == AGTV_NUMERIC)
            result = DatumGetFloat8(DirectFunctionCall1(
                numeric_float8_no_overflow,
                NumericGetDatum(agtv_value->val.numeric)));
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s() unsupported argument agtype %d",
                                   funcname, agtv_value->type)));
    }

    /* there is a valid non null value */
    *is_null = false;

    return result;
}

/*
 * Helper function to extract one numeric compatible value from a variadic any.
 * It supports integer2/4/8, float4/8, and numeric or the agtype integer, float,
 * and numeric for the argument. It does not support a character based numeric,
 * otherwise we would just cast it to numeric. It returns a numeric on success
 * or fails with a message stating the funcname that called it and a specific
 * message stating the error.
 */
static Numeric get_numeric_compatible_arg(Datum arg, Oid type, char *funcname,
                                          bool *is_null,
                                          enum agtype_value_type *ag_type)
{
    Numeric result;

    /* Assume the value is null. Although, this is only necessary for agtypes */
    *is_null = true;

    if (ag_type != NULL)
        *ag_type = AGTV_NULL;

    if (type != AGTYPEOID)
    {
        if (type == INT2OID)
            result = DatumGetNumeric(DirectFunctionCall1(int2_numeric, arg));
        else if (type == INT4OID)
            result = DatumGetNumeric(DirectFunctionCall1(int4_numeric, arg));
        else if (type == INT8OID)
            result = DatumGetNumeric(DirectFunctionCall1(int8_numeric, arg));
        else if (type == FLOAT4OID)
            result = DatumGetNumeric(DirectFunctionCall1(float4_numeric, arg));
        else if (type == FLOAT8OID)
            result = DatumGetNumeric(DirectFunctionCall1(float8_numeric, arg));
        else if (type == NUMERICOID)
            result = DatumGetNumeric(arg);
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s() unsupported argument type %d", funcname,
                                   type)));
    }
    else
    {
        agtype *agt_arg;
        agtype_value *agtv_value;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(arg);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s() only supports scalar arguments",
                                   funcname)));

        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype null */
        if (agtv_value->type == AGTV_NULL)
            return 0;

        if (agtv_value->type == AGTV_INTEGER)
        {
            result = DatumGetNumeric(DirectFunctionCall1(
                int8_numeric, Int64GetDatum(agtv_value->val.int_value)));
            if (ag_type != NULL)
                *ag_type = AGTV_INTEGER;
        }
        else if (agtv_value->type == AGTV_FLOAT)
        {
            result = DatumGetNumeric(DirectFunctionCall1(
                float8_numeric, Float8GetDatum(agtv_value->val.float_value)));
            if (ag_type != NULL)
                *ag_type = AGTV_FLOAT;
        }
        else if (agtv_value->type == AGTV_NUMERIC)
        {
            result = agtv_value->val.numeric;
            if (ag_type != NULL)
                *ag_type = AGTV_NUMERIC;
        }
        else
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("%s() unsupported argument agtype %d",
                                   funcname, agtv_value->type)));
    }

    /* there is a valid non null value */
    *is_null = false;

    return result;
}

PG_FUNCTION_INFO_V1(age_sin);

Datum age_sin(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 angle;
    float8 result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("sin() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * sin() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the angle
     */

    angle = get_float_compatible_arg(args[0], types[0], "sin", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    result = DatumGetFloat8(DirectFunctionCall1(dsin,
                                                Float8GetDatum(angle)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_cos);

Datum age_cos(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 angle;
    float8 result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("cos() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * cos() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the angle
     */

    angle = get_float_compatible_arg(args[0], types[0], "cos", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    result = DatumGetFloat8(DirectFunctionCall1(dcos,
                                                Float8GetDatum(angle)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_tan);

Datum age_tan(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 angle;
    float8 result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("tan() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * tan() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the angle
     */

    angle = get_float_compatible_arg(args[0], types[0], "tan", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    result = DatumGetFloat8(DirectFunctionCall1(dtan,
                                                Float8GetDatum(angle)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_cot);

Datum age_cot(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 angle;
    float8 result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("cot() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * cot() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the angle
     */

    angle = get_float_compatible_arg(args[0], types[0], "cot", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    result = DatumGetFloat8(DirectFunctionCall1(dcot,
                                                Float8GetDatum(angle)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_asin);

Datum age_asin(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 x;
    float8 angle;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("asin() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * asin() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the input expression.
     */

    x = get_float_compatible_arg(args[0], types[0], "asin", &is_null);

    /* verify that x is within range */
    if (x < -1 || x > 1)
        PG_RETURN_NULL();

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    angle = DatumGetFloat8(DirectFunctionCall1(dasin,
                                               Float8GetDatum(x)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = angle;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_acos);

Datum age_acos(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 x;
    float8 angle;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("acos() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * acos() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the input expression.
     */

    x = get_float_compatible_arg(args[0], types[0], "acos", &is_null);

    /* verify that x is within range */
    if (x < -1 || x > 1)
        PG_RETURN_NULL();

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    angle = DatumGetFloat8(DirectFunctionCall1(dacos,
                                               Float8GetDatum(x)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = angle;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_atan);

Datum age_atan(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 x;
    float8 angle;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("atan() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * atan() supports integer, float, and numeric or the agtype integer, float,
     * and numeric for the input expression.
     */

    x = get_float_compatible_arg(args[0], types[0], "atan", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    angle = DatumGetFloat8(DirectFunctionCall1(datan,
                                               Float8GetDatum(x)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = angle;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_atan2);

Datum age_atan2(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 x, y;
    float8 angle;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 2)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("atan2() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0] || nulls[1])
        PG_RETURN_NULL();

    /*
     * atan2() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expressions.
     */

    y = get_float_compatible_arg(args[0], types[0], "atan2", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    x = get_float_compatible_arg(args[1], types[1], "atan2", &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    angle = DatumGetFloat8(DirectFunctionCall2(datan2,
                                               Float8GetDatum(y),
                                               Float8GetDatum(x)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = angle;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_degrees);

Datum age_degrees(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 angle_degrees;
    float8 angle_radians;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("degrees() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * degrees_from_radians() supports integer, float, and numeric or the agtype
     * integer, float, and numeric for the input expression.
     */

    angle_radians = get_float_compatible_arg(args[0], types[0], "degrees",
                                             &is_null);
    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    angle_degrees = DatumGetFloat8(DirectFunctionCall1(degrees,
                                                       Float8GetDatum(angle_radians)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = angle_degrees;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_radians);

Datum age_radians(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    float8 angle_degrees;
    float8 angle_radians;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("radians() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * radians_from_degrees() supports integer, float, and numeric or the agtype
     * integer, float, and numeric for the input expression.
     */

    angle_degrees = get_float_compatible_arg(args[0], types[0], "radians",
                                             &is_null);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the numeric input as a float8 so that we can pass it off to PG */
    angle_radians = DatumGetFloat8(DirectFunctionCall1(radians,
                                                       Float8GetDatum(angle_degrees)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = angle_radians;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_round);

Datum age_round(PG_FUNCTION_ARGS)
{
    Datum *args = NULL;
    bool *nulls = NULL;
    Oid *types = NULL;
    int nargs = 0;
    agtype_value agtv_result;
    Numeric arg, arg_precision;
    Numeric numeric_result;
    float8 float_result;
    bool is_null = true;
    int precision = 0;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1 && nargs != 2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("round() invalid number of arguments")));
    }

    /* check for a null input */
    if (nargs < 0 || nulls[0])
    {
        PG_RETURN_NULL();
    }

    /*
     * round() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "round", &is_null,
                                     NULL);

    /* check for a agtype null input */
    if (is_null)
    {
        PG_RETURN_NULL();
    }

    /* We need the input as a numeric so that we can pass it off to PG */
    if (nargs == 2 && !nulls[1])
    {
        arg_precision = get_numeric_compatible_arg(args[1], types[1], "round",
                                                   &is_null, NULL);
        if (!is_null)
        {
            precision = DatumGetInt64(DirectFunctionCall1(numeric_int8,
                                      NumericGetDatum(arg_precision)));
            numeric_result = DatumGetNumeric(DirectFunctionCall2(numeric_round,
                                             NumericGetDatum(arg),
                                             Int32GetDatum(precision)));
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("round() invalid NULL precision value")));
        }
    }
    else
    {
        numeric_result = DatumGetNumeric(DirectFunctionCall2(numeric_round,
                                         NumericGetDatum(arg),
                                         Int32GetDatum(0)));
    }

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                               NumericGetDatum(numeric_result)));
    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_ceil);

Datum age_ceil(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric numeric_result;
    float8 float_result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("ceil() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * ceil() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "ceil", &is_null, NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_ceil,
                                                         NumericGetDatum(arg)));

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                                      NumericGetDatum(numeric_result)));
    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_floor);

Datum age_floor(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric numeric_result;
    float8 float_result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("floor() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * floor() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "floor", &is_null,
                                     NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_floor,
                                                         NumericGetDatum(arg)));

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                                      NumericGetDatum(numeric_result)));
    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_abs);

Datum age_abs(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric numeric_result;
    bool is_null = true;
    enum agtype_value_type type = AGTV_NULL;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("abs() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * abs() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "abs", &is_null, &type);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_abs,
                                                         NumericGetDatum(arg)));

    /* build the result, based on the type */
    if (types[0] == INT2OID || types[0] == INT4OID || types[0] == INT8OID ||
        (types[0] == AGTYPEOID && type == AGTV_INTEGER))
    {
        int64 int_result;

        int_result = DatumGetInt64(DirectFunctionCall1(numeric_int8,
                                                       NumericGetDatum(numeric_result)));

        agtv_result.type = AGTV_INTEGER;
        agtv_result.val.int_value = int_result;
    }
    if (types[0] == FLOAT4OID || types[0] == FLOAT8OID ||
        (types[0] == AGTYPEOID && type == AGTV_FLOAT))
    {
        float8 float_result;

        float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                           NumericGetDatum(numeric_result)));

        agtv_result.type = AGTV_FLOAT;
        agtv_result.val.float_value = float_result;
    }
    if (types[0] == NUMERICOID ||
        (types[0] == AGTYPEOID && type == AGTV_NUMERIC))
    {
        agtv_result.type = AGTV_NUMERIC;
        agtv_result.val.numeric = numeric_result;
    }

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_sign);

Datum age_sign(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric numeric_result;
    int int_result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("sign() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * sign() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "sign", &is_null, NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_sign,
                                                         NumericGetDatum(arg)));

    int_result = DatumGetInt64(DirectFunctionCall1(numeric_int8,
                                                   NumericGetDatum(numeric_result)));

    /* build the result */
    agtv_result.type = AGTV_INTEGER;
    agtv_result.val.int_value = int_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_log);

Datum age_log(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric zero;
    Numeric numeric_result;
    float8 float_result;
    bool is_null = true;
    int test;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("log() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * log() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "log", &is_null, NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* get a numeric 0 as a datum to test <= 0 log args */
    zero = DatumGetNumeric(DirectFunctionCall1(int8_numeric, Int64GetDatum(0)));

    test = DatumGetInt32(DirectFunctionCall2(numeric_cmp, NumericGetDatum(arg),
                                             NumericGetDatum(zero)));

    /* return null if the argument is <= 0; these are invalid args for logs */
    if (test <= 0)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_ln,
                                                         NumericGetDatum(arg)));

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                                      NumericGetDatum(numeric_result)));
    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_log10);

Datum age_log10(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric zero;
    Numeric numeric_result;
    float8 float_result;
    Datum base;
    bool is_null = true;
    int test;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("log() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * log10() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "log10", &is_null, NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* get a numeric 0 as a datum to test <= 0 log args */
    zero = DatumGetNumeric(DirectFunctionCall1(int8_numeric, Int64GetDatum(0)));

    test = DatumGetInt32(DirectFunctionCall2(numeric_cmp, NumericGetDatum(arg),
                                             NumericGetDatum(zero)));

    /* return null if the argument is <= 0; these are invalid args for logs */
    if (test <= 0)
        PG_RETURN_NULL();

    /* get a numeric 10 as a datum for the base */
    base = DirectFunctionCall1(float8_numeric, Float8GetDatum(10.0));

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall2(numeric_log, base,
                                                         NumericGetDatum(arg)));

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                                      NumericGetDatum(numeric_result)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_e);

Datum age_e(PG_FUNCTION_ARGS)
{
    agtype_value agtv_result;
    float8 float_result;

    /* get e by raising e to 1 - no, they don't have a constant e :/ */
    float_result = DatumGetFloat8(DirectFunctionCall1(dexp, Float8GetDatum(1)));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_pi);

Datum age_pi(PG_FUNCTION_ARGS)
{
    agtype_value agtv_result;
    float8 float_result;

    float_result = DatumGetFloat8(DirectFunctionCall1(dpi, 0));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_rand);

Datum age_rand(PG_FUNCTION_ARGS)
{
    agtype_value agtv_result;
    float8 float_result;

    float_result = DatumGetFloat8(DirectFunctionCall1(drandom, 0));

    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_exp);

Datum age_exp(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric numeric_result;
    float8 float_result;
    bool is_null = true;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("exp() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * exp() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "exp", &is_null, NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_exp,
                                                         NumericGetDatum(arg)));

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                                      NumericGetDatum(numeric_result)));
    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_sqrt);

Datum age_sqrt(PG_FUNCTION_ARGS)
{
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    agtype_value agtv_result;
    Numeric arg;
    Numeric zero;
    Numeric numeric_result;
    float8 float_result;
    bool is_null = true;
    int test;

    /* extract argument values */
    nargs = extract_variadic_args(fcinfo, 0, true, &args, &types, &nulls);

    /* check number of args */
    if (nargs != 1)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("sqrt() invalid number of arguments")));

    /* check for a null input */
    if (nargs < 0 || nulls[0])
        PG_RETURN_NULL();

    /*
     * sqrt() supports integer, float, and numeric or the agtype integer,
     * float, and numeric for the input expression.
     */
    arg = get_numeric_compatible_arg(args[0], types[0], "sqrt", &is_null, NULL);

    /* check for a agtype null input */
    if (is_null)
        PG_RETURN_NULL();

    /* get a numeric 0 as a datum to test < 0 sqrt args */
    zero = DatumGetNumeric(DirectFunctionCall1(int8_numeric, Int64GetDatum(0)));

    test = DatumGetInt32(DirectFunctionCall2(numeric_cmp, NumericGetDatum(arg),
                                             NumericGetDatum(zero)));

    /* return null if the argument is < 0; these are invalid args for sqrt */
    if (test < 0)
        PG_RETURN_NULL();

    /* We need the input as a numeric so that we can pass it off to PG */
    numeric_result = DatumGetNumeric(DirectFunctionCall1(numeric_sqrt,
                                                         NumericGetDatum(arg)));

    float_result = DatumGetFloat8(DirectFunctionCall1(numeric_float8_no_overflow,
                                                      NumericGetDatum(numeric_result)));
    /* build the result */
    agtv_result.type = AGTV_FLOAT;
    agtv_result.val.float_value = float_result;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

PG_FUNCTION_INFO_V1(age_timestamp);

Datum age_timestamp(PG_FUNCTION_ARGS)
{
    agtype_value agtv_result;
    struct timespec ts;
    long ms = 0;

    /* get the system time and convert it to milliseconds */
    clock_gettime(CLOCK_REALTIME, &ts);
    ms += (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);

    /* build the result */
    agtv_result.type = AGTV_INTEGER;
    agtv_result.val.int_value = ms;

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

/*
 * Converts an agtype object or array to a binary agtype_value.
 */
agtype_value *agtype_composite_to_agtype_value_binary(agtype *a)
{
    agtype_value *result;

    if (AGTYPE_CONTAINER_IS_SCALAR(&a->root))
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("cannot convert agtype scalar objects to binary agtype_value objects")));
    }

    result = palloc(sizeof(agtype_value));

    /* convert the agtype to a binary agtype_value */
    result->type = AGTV_BINARY;
    result->val.binary.len = AGTYPE_CONTAINER_SIZE(&a->root);
    result->val.binary.data = &a->root;

    return result;
}

/*
 * For the given properties, update the property with the key equal
 * to var_name with the value defined in new_v. If the remove_property
 * flag is set, simply remove the property with the given property
 * name instead.
 */
agtype_value *alter_property_value(agtype_value *properties, char *var_name,
                                   agtype *new_v, bool remove_property)
{
    agtype_iterator *it;
    agtype_iterator_token tok = WAGT_DONE;
    agtype_parse_state *parse_state = NULL;
    agtype_value *r;
    agtype *prop_agtype;
    agtype_value *parsed_agtype_value = NULL;
    bool found;

    /* if no properties, return NULL */
    if (properties == NULL)
    {
        return NULL;
    }

    /* if properties is not an object, throw an error */
    if (properties->type != AGTV_OBJECT)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("can only update objects")));
    }

    r = palloc0(sizeof(agtype_value));

    prop_agtype = agtype_value_to_agtype(properties);
    it = agtype_iterator_init(&prop_agtype->root);
    tok = agtype_iterator_next(&it, r, true);

    parsed_agtype_value = push_agtype_value(&parse_state, tok, tok < WAGT_BEGIN_ARRAY ? r : NULL);

    /*
     * If the new value is NULL, this is equivalent to the remove_property
     * flag set to true.
     */
    if (new_v == NULL)
    {
        remove_property = true;
    }

    found = false;
    while (true)
    {
        char *str;

        tok = agtype_iterator_next(&it, r, true);

        if (tok == WAGT_DONE || tok == WAGT_END_OBJECT)
        {
            break;
        }

        str = pnstrdup(r->val.string.val, r->val.string.len);

        /*
         * Check the key value, if it is equal to the passed in
         * var_name, replace the value for this key with the passed
         * in agtype. Otherwise pass the existing value to the
         * new properties agtype_value.
         */
        if (strcmp(str, var_name))
        {
            /* push the key */
            parsed_agtype_value = push_agtype_value(
                &parse_state, tok, tok < WAGT_BEGIN_ARRAY ? r : NULL);

            /* get the value and push the value */
            tok = agtype_iterator_next(&it, r, true);
            parsed_agtype_value = push_agtype_value(&parse_state, tok, r);
        }
        else
        {
            agtype_value *new_agtype_value_v;

            /* if the remove flag is set, don't push the key or any value */
            if(remove_property)
            {
                /* skip the value */
                tok = agtype_iterator_next(&it, r, true);
                continue;
            }

            /* push the key */
            parsed_agtype_value = push_agtype_value(
                &parse_state, tok, tok < WAGT_BEGIN_ARRAY ? r : NULL);

            /* skip the existing value for the key */
            tok = agtype_iterator_next(&it, r, true);

            /*
             * If the new agtype is scalar, push the agtype_value to the
             * parse state. If the agtype is an object or array convert the
             * agtype to a binary agtype_value to pass to the parse_state.
             * This will save unnecessary deserialization and serialization
             * logic from running.
             */
            if (AGTYPE_CONTAINER_IS_SCALAR(&new_v->root))
            {
                /* get the scalar value and push as the value */
                new_agtype_value_v = get_ith_agtype_value_from_container(&new_v->root, 0);

                parsed_agtype_value = push_agtype_value(&parse_state, WAGT_VALUE, new_agtype_value_v);
            }
            else
            {
                agtype_value *result = agtype_composite_to_agtype_value_binary(new_v);

                parsed_agtype_value = push_agtype_value(&parse_state, WAGT_VALUE, result);
            }

            found = true;
        }
    }

    /*
     * If we have not found the property and we aren't trying to remove it,
     * add the key/value pair now.
     */
    if (!found && !remove_property)
    {
        agtype_value *new_agtype_value_v;
        agtype_value *key = string_to_agtype_value(var_name);

        /* push the new key */
        parsed_agtype_value = push_agtype_value(
            &parse_state, WAGT_KEY, key);

        /*
         * If the new agtype is scalar, push the agtype_value to the
         * parse state. If the agtype is an object or array convert the
         * agtype to a binary agtype_value to pass to the parse_state.
         * This will save unnecessary deserialization and serialization
         * logic from running.
         */
        if (AGTYPE_CONTAINER_IS_SCALAR(&new_v->root))
        {
            new_agtype_value_v = get_ith_agtype_value_from_container(&new_v->root, 0);

            /* convert the agtype array or object to a binary agtype_value */
            parsed_agtype_value = push_agtype_value(&parse_state, WAGT_VALUE, new_agtype_value_v);
        }
        else
        {
            agtype_value *result = agtype_composite_to_agtype_value_binary(new_v);

            parsed_agtype_value = push_agtype_value(&parse_state, WAGT_VALUE, result);
        }
    }

    /* push the end object token to parse state */
    parsed_agtype_value = push_agtype_value(&parse_state, WAGT_END_OBJECT, NULL);

    return parsed_agtype_value;
}

/*
 * Appends new_properties into a copy of original_properties. If the
 * original_properties is NULL, returns new_properties.
 *
 * This is a helper function used by the SET clause executor for
 * updating properties with the equal, or plus-equal operator and a map.
 */
agtype_value *alter_properties(agtype_value *original_properties,
                               agtype *new_properties)
{
    agtype_iterator *it;
    agtype_iterator_token tok = WAGT_DONE;
    agtype_parse_state *parse_state = NULL;
    agtype_value *key;
    agtype_value *value;
    agtype_value *parsed_agtype_value = NULL;

    parsed_agtype_value = push_agtype_value(&parse_state, WAGT_BEGIN_OBJECT,
                                            NULL);

    /* Copy original properties. */
    if (original_properties)
    {
        if (original_properties->type != AGTV_OBJECT)
        {
            ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                            errmsg("a map is expected")));
        }

        copy_agtype_value(parse_state, original_properties,
                          &parsed_agtype_value, true);
    }

    /* Append new properties. */
    key = palloc0(sizeof(agtype_value));
    value = palloc0(sizeof(agtype_value));
    it = agtype_iterator_init(&new_properties->root);
    tok = agtype_iterator_next(&it, key, true);

    if (tok != WAGT_BEGIN_OBJECT)
    {
        ereport(ERROR, (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
                        errmsg("a map is expected")));
    }

    while (true)
    {
        tok = agtype_iterator_next(&it, key, true);

        if (tok == WAGT_DONE || tok == WAGT_END_OBJECT)
        {
            break;
        }

        agtype_iterator_next(&it, value, true);

        parsed_agtype_value = push_agtype_value(&parse_state, WAGT_KEY,
                                                key);
        parsed_agtype_value = push_agtype_value(&parse_state, WAGT_VALUE,
                                                value);
    }

    parsed_agtype_value = push_agtype_value(&parse_state, WAGT_END_OBJECT,
                                            NULL);
    return parsed_agtype_value;
}

/*
 * Helper function to extract 1 datum from a variadic "any" and convert, if
 * possible, to an agtype, if it isn't already.
 *
 * If the value is a NULL or agtype NULL, the function returns NULL.
 * If the datum cannot be converted, the function will error out in
 * extract_variadic_args.
 */
agtype *get_one_agtype_from_variadic_args(FunctionCallInfo fcinfo,
                                          int variadic_offset,
                                          int expected_nargs)
{
    int nargs;
    Datum *args = NULL;
    bool *nulls = NULL;
    Oid *types = NULL;
    agtype *agtype_result = NULL;

    nargs = extract_variadic_args(fcinfo, variadic_offset, false, &args, &types,
                                  &nulls);
    /* throw an error if the number of args is not the expected number */
    if (nargs != expected_nargs)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("number of args %d does not match expected %d",
                               nargs, expected_nargs)));
    }
    /* if null, return null */
    if (nulls[0])
    {
        pfree_if_not_null(args);
        pfree_if_not_null(nulls);
        pfree_if_not_null(types);
        return NULL;
    }

    /* if type is AGTYPEOID, we don't need to convert it */
    if (types[0] == AGTYPEOID)
    {
        agtype_container *agtc;

        agtype_result = DATUM_GET_AGTYPE_P(args[0]);
        agtc = &agtype_result->root;

        /*
         * Is this a scalar (scalars are stored as one element arrays)? If so,
         * test for agtype NULL.
         */
        if (AGTYPE_CONTAINER_IS_SCALAR(agtc) &&
            AGTE_IS_NULL(agtc->children[0]))
        {
            PG_FREE_IF_COPY(agtype_result, variadic_offset);

            pfree_if_not_null(args);
            pfree_if_not_null(nulls);
            pfree_if_not_null(types);
            return NULL;
        }
    }
    /* otherwise, try to convert it to an agtype */
    else
    {
        agtype_in_state state;
        agt_type_category tcategory;
        Oid outfuncoid;

        /* we need an empty state */
        state.parse_state = NULL;
        state.res = NULL;
        /* get the category for the datum */
        agtype_categorize_type(types[0], &tcategory, &outfuncoid);
        /* convert it to an agtype_value */
        datum_to_agtype(args[0], false, &state, tcategory, outfuncoid, false);
        /* convert it to an agtype */
        agtype_result = agtype_value_to_agtype(state.res);

        pfree_agtype_in_state(&state);
    }

    pfree_if_not_null(args);
    pfree_if_not_null(nulls);
    pfree_if_not_null(types);

    return agtype_result;
}

/*
 * Transfer function for age_sum(agtype, agtype).
 *
 * Note: that the running sum will change type depending on the
 * precision of the input. The most precise value determines the
 * result type.
 *
 * Note: The sql definition is STRICT so no input NULLs need to
 * be dealt with except for agtype.
 */
PG_FUNCTION_INFO_V1(age_agtype_sum);

Datum age_agtype_sum(PG_FUNCTION_ARGS)
{
    agtype *agt_arg0 = AG_GET_ARG_AGTYPE_P(0);
    agtype *agt_arg1 = AG_GET_ARG_AGTYPE_P(1);
    agtype_value *agtv_lhs;
    agtype_value *agtv_rhs;
    agtype_value agtv_result;

    /* get our args */
    agt_arg0 = AG_GET_ARG_AGTYPE_P(0);
    agt_arg1 = AG_GET_ARG_AGTYPE_P(1);

    /* only scalars are allowed */
    if (!AGT_ROOT_IS_SCALAR(agt_arg0) || !AGT_ROOT_IS_SCALAR(agt_arg1))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("arguments must resolve to a scalar")));

    /* get the values */
    agtv_lhs = get_ith_agtype_value_from_container(&agt_arg0->root, 0);
    agtv_rhs = get_ith_agtype_value_from_container(&agt_arg1->root, 0);

    /* only numbers are allowed */
    if ((agtv_lhs->type != AGTV_INTEGER && agtv_lhs->type != AGTV_FLOAT &&
         agtv_lhs->type != AGTV_NUMERIC) || (agtv_rhs->type != AGTV_INTEGER &&
        agtv_rhs->type != AGTV_FLOAT && agtv_rhs->type != AGTV_NUMERIC))
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("arguments must resolve to a number")));

    /* check for agtype null */
    if (agtv_lhs->type == AGTV_NULL)
        PG_RETURN_POINTER(agt_arg1);
    if (agtv_rhs->type == AGTV_NULL)
        PG_RETURN_POINTER(agt_arg0);

    /* we want to maintain the precision of the most precise input */
    if (agtv_lhs->type == AGTV_NUMERIC || agtv_rhs->type == AGTV_NUMERIC)
    {
        agtv_result.type = AGTV_NUMERIC;
    }
    else if (agtv_lhs->type == AGTV_FLOAT || agtv_rhs->type == AGTV_FLOAT)
    {
        agtv_result.type = AGTV_FLOAT;
    }
    else
    {
        agtv_result.type = AGTV_INTEGER;
    }

    /* switch on the type to perform the correct addition */
    switch(agtv_result.type)
    {
        /* if the type is integer, they are obviously both ints */
        case AGTV_INTEGER:
            agtv_result.val.int_value = DatumGetInt64(
                DirectFunctionCall2(int8pl,
                                    Int64GetDatum(agtv_lhs->val.int_value),
                                    Int64GetDatum(agtv_rhs->val.int_value)));
            break;
        /* for float it can be either, float + float or float + int */
        case AGTV_FLOAT:
        {
            Datum dfl;
            Datum dfr;
            Datum dresult;
            /* extract and convert the values as necessary */
            /* float + float */
            if (agtv_lhs->type == AGTV_FLOAT && agtv_rhs->type == AGTV_FLOAT)
            {
                dfl = Float8GetDatum(agtv_lhs->val.float_value);
                dfr = Float8GetDatum(agtv_rhs->val.float_value);
            }
            /* float + int */
            else
            {
                int64 ival;
                float8 fval;
                bool is_null;

                ival = (agtv_lhs->type == AGTV_INTEGER) ?
                    agtv_lhs->val.int_value : agtv_rhs->val.int_value;
                fval = (agtv_lhs->type == AGTV_FLOAT) ?
                    agtv_lhs->val.float_value : agtv_rhs->val.float_value;

                dfl = Float8GetDatum(get_float_compatible_arg(Int64GetDatum(ival),
                                                              INT8OID, "",
                                                              &is_null));
                dfr = Float8GetDatum(fval);
            }
            /* add the floats and set the result */
            dresult = DirectFunctionCall2(float8pl, dfl, dfr);
            agtv_result.val.float_value = DatumGetFloat8(dresult);
        }
            break;
        /*
         * For numeric it can be either, numeric + numeric or numeric + float or
         * numeric + int
         */
        case AGTV_NUMERIC:
        {
            Datum dnl;
            Datum dnr;
            Datum dresult;
            /* extract and convert the values as necessary */
            /* numeric + numeric */
            if (agtv_lhs->type == AGTV_NUMERIC && agtv_rhs->type == AGTV_NUMERIC)
            {
                dnl = NumericGetDatum(agtv_lhs->val.numeric);
                dnr = NumericGetDatum(agtv_rhs->val.numeric);
            }
            /* numeric + float */
            else if (agtv_lhs->type == AGTV_FLOAT || agtv_rhs->type == AGTV_FLOAT)
            {
                float8 fval;
                Numeric nval;

                fval = (agtv_lhs->type == AGTV_FLOAT) ?
                    agtv_lhs->val.float_value : agtv_rhs->val.float_value;
                nval = (agtv_lhs->type == AGTV_NUMERIC) ?
                    agtv_lhs->val.numeric : agtv_rhs->val.numeric;

                dnl = DirectFunctionCall1(float8_numeric, Float8GetDatum(fval));
                dnr = NumericGetDatum(nval);
            }
            /* numeric + int */
            else
            {
                int64 ival;
                Numeric nval;

                ival = (agtv_lhs->type == AGTV_INTEGER) ?
                    agtv_lhs->val.int_value : agtv_rhs->val.int_value;
                nval = (agtv_lhs->type == AGTV_NUMERIC) ?
                    agtv_lhs->val.numeric : agtv_rhs->val.numeric;

                dnl = DirectFunctionCall1(int8_numeric, Int64GetDatum(ival));
                dnr = NumericGetDatum(nval);
            }
            /* add the numerics and set the result */
            dresult = DirectFunctionCall2(numeric_add, dnl, dnr);
            agtv_result.val.numeric = DatumGetNumeric(dresult);
        }
            break;

        default:
            elog(ERROR, "unexpected agtype");
            break;
    }
    /* return the result */
    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
}

/*
 * Wrapper function for float8_accum to take an agtype input.
 * This function is defined as STRICT so it does not need to check
 * for NULL input parameters
 */
PG_FUNCTION_INFO_V1(age_agtype_float8_accum);

Datum age_agtype_float8_accum(PG_FUNCTION_ARGS)
{
    Datum dfloat;
    Datum result;

    /* convert to a float8 datum, if possible */
    dfloat = DirectFunctionCall1(agtype_to_float8, PG_GETARG_DATUM(1));
    /* pass the arguments off to float8_accum */
    result = DirectFunctionCall2(float8_accum, PG_GETARG_DATUM(0), dfloat);

    PG_RETURN_DATUM(result);
}

/* Wrapper for stdDev function. */
PG_FUNCTION_INFO_V1(age_float8_stddev_samp_aggfinalfn);

Datum age_float8_stddev_samp_aggfinalfn(PG_FUNCTION_ARGS)
{
    Datum result;
    PGFunction func;
    agtype_value agtv_float;

    /* we can't use DirectFunctionCall1 as it errors for NULL values */
    func = float8_stddev_samp;
    result = (*func) (fcinfo);

    agtv_float.type = AGTV_FLOAT;

    /*
     * Check to see if float8_stddev_samp returned null. If so, we need to
     * return a agtype float 0.
     */
    if (fcinfo->isnull)
    {
        /* we need to clear the flag */
        fcinfo->isnull = false;
        agtv_float.val.float_value = 0.0;
    }
    else
    {
        agtv_float.val.float_value = DatumGetFloat8(result);
    }

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_float));
}

PG_FUNCTION_INFO_V1(age_float8_stddev_pop_aggfinalfn);

Datum age_float8_stddev_pop_aggfinalfn(PG_FUNCTION_ARGS)
{
    Datum result;
    PGFunction func;
    agtype_value agtv_float;

    /* we can't use DirectFunctionCall1 as it errors for NULL values */
    func = float8_stddev_pop;
    result = (*func) (fcinfo);

    agtv_float.type = AGTV_FLOAT;

    /*
     * Check to see if float8_stddev_pop returned null. If so, we need to
     * return a agtype float 0.
     */
    if (fcinfo->isnull)
    {
        /* we need to clear the flag */
        fcinfo->isnull = false;
        agtv_float.val.float_value = 0.0;
    }
    else
    {
        agtv_float.val.float_value = DatumGetFloat8(result);
    }

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_float));
}

PG_FUNCTION_INFO_V1(age_agtype_larger_aggtransfn);

Datum age_agtype_larger_aggtransfn(PG_FUNCTION_ARGS)
{
    agtype *agtype_arg1;
    agtype *agtype_arg2;
    agtype *agtype_larger;
    int test;

    /* for max we need to ignore NULL values */
    /* extract the args as agtype */
    agtype_arg1 = get_one_agtype_from_variadic_args(fcinfo, 0, 2);
    agtype_arg2 = get_one_agtype_from_variadic_args(fcinfo, 1, 1);

    /* return NULL if both are NULL */
    if (agtype_arg1 == NULL && agtype_arg2 == NULL)
        PG_RETURN_NULL();
    /* if one is NULL, return the other */
    if (agtype_arg1 != NULL && agtype_arg2 == NULL)
        PG_RETURN_POINTER(agtype_arg1);
    if (agtype_arg1 == NULL && agtype_arg2 != NULL)
        PG_RETURN_POINTER(agtype_arg2);

    /* test for max value */
    test = compare_agtype_containers_orderability(&agtype_arg1->root,
                                                  &agtype_arg2->root);

    agtype_larger = (test >= 0) ? agtype_arg1 : agtype_arg2;

    PG_RETURN_POINTER(agtype_larger);
}

PG_FUNCTION_INFO_V1(age_agtype_smaller_aggtransfn);

Datum age_agtype_smaller_aggtransfn(PG_FUNCTION_ARGS)
{
    agtype *agtype_arg1 = NULL;
    agtype *agtype_arg2 = NULL;
    agtype *agtype_smaller;
    int test;

    /* for min we need to ignore NULL values */
    /* extract the args as agtype */
    agtype_arg1 = get_one_agtype_from_variadic_args(fcinfo, 0, 2);
    agtype_arg2 = get_one_agtype_from_variadic_args(fcinfo, 1, 1);

    /* return NULL if both are NULL */
    if (agtype_arg1 == NULL && agtype_arg2 == NULL)
        PG_RETURN_NULL();
    /* if one is NULL, return the other */
    if (agtype_arg1 != NULL && agtype_arg2 == NULL)
        PG_RETURN_POINTER(agtype_arg1);
    if (agtype_arg1 == NULL && agtype_arg2 != NULL)
        PG_RETURN_POINTER(agtype_arg2);

    /* test for min value */
    test = compare_agtype_containers_orderability(&agtype_arg1->root,
                                                  &agtype_arg2->root);

    agtype_smaller = (test <= 0) ? agtype_arg1 : agtype_arg2;

    PG_RETURN_POINTER(agtype_smaller);
}

/* borrowed from PGs float8 routines for percentile_cont */
static Datum float8_lerp(Datum lo, Datum hi, double pct)
{
    double loval = DatumGetFloat8(lo);
    double hival = DatumGetFloat8(hi);

    return Float8GetDatum(loval + (pct * (hival - loval)));
}

/* Code borrowed and adjusted from PG's ordered_set_transition function */
PG_FUNCTION_INFO_V1(age_percentile_aggtransfn);

Datum age_percentile_aggtransfn(PG_FUNCTION_ARGS)
{
    PercentileGroupAggState *pgastate;

    /* verify we are in an aggregate context */
    Assert(AggCheckCallContext(fcinfo, NULL) == AGG_CONTEXT_AGGREGATE);

    /* if this is the first invocation, create the state */
    if (PG_ARGISNULL(0))
    {
        MemoryContext old_mcxt;
        float8 percentile;

        /* validate the percentile */
        if (PG_ARGISNULL(2))
            ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                errmsg("percentile value NULL is not a valid numeric value")));

        percentile = DatumGetFloat8(DirectFunctionCall1(agtype_to_float8,
                         PG_GETARG_DATUM(2)));

        if (percentile < 0 || percentile > 1 || isnan(percentile))
        ereport(ERROR, (errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
                        errmsg("percentile value %g is not between 0 and 1",
                               percentile)));

        /* switch to the correct aggregate context */
        old_mcxt = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);
        /* create and initialize the state */
        pgastate = palloc0(sizeof(PercentileGroupAggState));
        pgastate->percentile = percentile;
        /*
         * Percentiles need to be calculated from a sorted set. We are only
         * using float8 values, using the less than operator, and flagging
         * randomAccess to true - as we can potentially be reusing this
         * sort multiple times in the same query.
         */
        pgastate->sortstate = tuplesort_begin_datum(FLOAT8OID,
                                                    Float8LessOperator,
                                                    InvalidOid, false, work_mem,
                                                    NULL, true);
        pgastate->number_of_rows = 0;
        pgastate->sort_done = false;

        /* restore the old context */
        MemoryContextSwitchTo(old_mcxt);
    }
    /* otherwise, retrieve the state */
    else
        pgastate = (PercentileGroupAggState *) PG_GETARG_POINTER(0);

    /* Load the datum into the tuplesort object, but only if it's not null */
    if (!PG_ARGISNULL(1))
    {
        Datum dfloat = DirectFunctionCall1(agtype_to_float8, PG_GETARG_DATUM(1));

        tuplesort_putdatum(pgastate->sortstate, dfloat, false);
        pgastate->number_of_rows++;
    }
    /* return the state */
    PG_RETURN_POINTER(pgastate);
}

/* Code borrowed and adjusted from PG's percentile_cont_final function */
PG_FUNCTION_INFO_V1(age_percentile_cont_aggfinalfn);

Datum age_percentile_cont_aggfinalfn(PG_FUNCTION_ARGS)
{
    PercentileGroupAggState *pgastate;
    float8 percentile;
    int64 first_row = 0;
    int64 second_row = 0;
    Datum val;
    Datum first_val;
    Datum second_val;
    double proportion;
    bool isnull;
    agtype_value agtv_float;

    /* verify we are in an aggregate context */
    Assert(AggCheckCallContext(fcinfo, NULL) == AGG_CONTEXT_AGGREGATE);

    /* If there were no regular rows, the result is NULL */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    /* retrieve the state and percentile */
    pgastate = (PercentileGroupAggState *) PG_GETARG_POINTER(0);
    percentile = pgastate->percentile;

    /* number_of_rows could be zero if we only saw NULL input values */
    if (pgastate->number_of_rows == 0)
        PG_RETURN_NULL();

    /* Finish the sort, or rescan if we already did */
    if (!pgastate->sort_done)
    {
        tuplesort_performsort(pgastate->sortstate);
        pgastate->sort_done = true;
    }
    else
        tuplesort_rescan(pgastate->sortstate);

    /* calculate the percentile cont*/
    first_row = floor(percentile * (pgastate->number_of_rows - 1));
    second_row = ceil(percentile * (pgastate->number_of_rows - 1));

    Assert(first_row < pgastate->number_of_rows);

    if (!tuplesort_skiptuples(pgastate->sortstate, first_row, true))
        elog(ERROR, "missing row in percentile_cont");

    if (!tuplesort_getdatum(pgastate->sortstate, true, &first_val, &isnull, NULL))
        elog(ERROR, "missing row in percentile_cont");
    if (isnull)
        PG_RETURN_NULL();

    if (first_row == second_row)
    {
        val = first_val;
    }
    else
    {
        if (!tuplesort_getdatum(pgastate->sortstate, true, &second_val, &isnull, NULL))
            elog(ERROR, "missing row in percentile_cont");

        if (isnull)
            PG_RETURN_NULL();

        proportion = (percentile * (pgastate->number_of_rows - 1)) - first_row;
        val = float8_lerp(first_val, second_val, proportion);
    }

    /* convert to an agtype float and return the result */
    agtv_float.type = AGTV_FLOAT;
    agtv_float.val.float_value = DatumGetFloat8(val);

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_float));
}

/* Code borrowed and adjusted from PG's percentile_disc_final function */
PG_FUNCTION_INFO_V1(age_percentile_disc_aggfinalfn);

Datum age_percentile_disc_aggfinalfn(PG_FUNCTION_ARGS)
{
    PercentileGroupAggState *pgastate;
    double percentile;
    Datum val;
    bool isnull;
    int64 rownum;
    agtype_value agtv_float;

    Assert(AggCheckCallContext(fcinfo, NULL) == AGG_CONTEXT_AGGREGATE);

    /* If there were no regular rows, the result is NULL */
    if (PG_ARGISNULL(0))
        PG_RETURN_NULL();

    pgastate = (PercentileGroupAggState *) PG_GETARG_POINTER(0);
    percentile = pgastate->percentile;

    /* number_of_rows could be zero if we only saw NULL input values */
    if (pgastate->number_of_rows == 0)
        PG_RETURN_NULL();

    /* Finish the sort, or rescan if we already did */
    if (!pgastate->sort_done)
    {
        tuplesort_performsort(pgastate->sortstate);
        pgastate->sort_done = true;
    }
    else
        tuplesort_rescan(pgastate->sortstate);

    /*----------
     * We need the smallest K such that (K/N) >= percentile.
     * N>0, therefore K >= N*percentile, therefore K = ceil(N*percentile).
     * So we skip K-1 rows (if K>0) and return the next row fetched.
     *----------
     */
    rownum = (int64) ceil(percentile * pgastate->number_of_rows);
    Assert(rownum <= pgastate->number_of_rows);

    if (rownum > 1)
    {
        if (!tuplesort_skiptuples(pgastate->sortstate, rownum - 1, true))
            elog(ERROR, "missing row in percentile_disc");
    }

    if (!tuplesort_getdatum(pgastate->sortstate, true, &val, &isnull, NULL))
        elog(ERROR, "missing row in percentile_disc");

    /* We shouldn't have stored any nulls, but do the right thing anyway */
    if (isnull)
        PG_RETURN_NULL();

    /* convert to an agtype float and return the result */
    agtv_float.type = AGTV_FLOAT;
    agtv_float.val.float_value = DatumGetFloat8(val);

    PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_float));
}

/* functions to support the aggregate function COLLECT() */
PG_FUNCTION_INFO_V1(age_collect_aggtransfn);

Datum age_collect_aggtransfn(PG_FUNCTION_ARGS)
{
    agtype_in_state *castate;
    int nargs;
    Datum *args;
    bool *nulls;
    Oid *types;
    MemoryContext old_mcxt;

    /* verify we are in an aggregate context */
    Assert(AggCheckCallContext(fcinfo, NULL) == AGG_CONTEXT_AGGREGATE);

    /*
     * Switch to the correct aggregate context. Otherwise, the data added to the
     * array will be lost.
     */
    old_mcxt = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);

    /* if this is the first invocation, create the state */
    if (PG_ARGISNULL(0))
    {
        /* create and initialize the state */
        castate = palloc0(sizeof(agtype_in_state));
        memset(castate, 0, sizeof(agtype_in_state));

        /* start the array */
        castate->res = push_agtype_value(&castate->parse_state,
                                         WAGT_BEGIN_ARRAY, NULL);
    }
    /* otherwise, retrieve the state */
    else
    {
        castate = (agtype_in_state *) PG_GETARG_POINTER(0);
    }

    /*
     * Extract the variadic args, of which there should only be one.
     * Insert the arg into the array, unless it is null. Nulls are
     * skipped over.
     */
    if (PG_ARGISNULL(1))
    {
        nargs = 0;
    }
    else
    {
        nargs = extract_variadic_args(fcinfo, 1, true, &args, &types, &nulls);
    }

    if (nargs == 1)
    {
        /* only add non null values */
        if (nulls[0] == false)
        {
            agtype_value *agtv_value = NULL;

            /* we need to check for agtype null and skip it, if found */
            if (types[0] == AGTYPEOID)
            {
                agtype *agt_arg;

                /* get the agtype argument */
                agt_arg = DATUM_GET_AGTYPE_P(args[0]);

                /* get the scalar value */
                if (AGTYPE_CONTAINER_IS_SCALAR(&agt_arg->root))
                {
                    agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);
                }
            }

            /* skip the arg if agtype null */
            if (agtv_value == NULL || agtv_value->type != AGTV_NULL)
            {
                add_agtype(args[0], nulls[0], castate, types[0], false);
            }
        }
    }
    else if (nargs > 1)
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("collect() invalid number of arguments")));
    }

    /* restore the old context */
    MemoryContextSwitchTo(old_mcxt);

    /* return the state */
    PG_RETURN_POINTER(castate);
}

PG_FUNCTION_INFO_V1(age_collect_aggfinalfn);

Datum age_collect_aggfinalfn(PG_FUNCTION_ARGS)
{
    agtype_in_state *castate;
    MemoryContext old_mcxt;

    /* verify we are in an aggregate context */
    Assert(AggCheckCallContext(fcinfo, NULL) == AGG_CONTEXT_AGGREGATE);
    /*
     * Get the state. There are cases where the age_collect_aggtransfn never
     * gets called. So, check to see if this is one.
     */
    if (PG_ARGISNULL(0))
    {
        /* create and initialize the state */
        castate = palloc0(sizeof(agtype_in_state));
        memset(castate, 0, sizeof(agtype_in_state));
        /* start the array */
        castate->res = push_agtype_value(&castate->parse_state,
                                         WAGT_BEGIN_ARRAY, NULL);
    }
    else
    {
        castate = (agtype_in_state *) PG_GETARG_POINTER(0);
    }
    /* switch to the correct aggregate context */
    old_mcxt = MemoryContextSwitchTo(fcinfo->flinfo->fn_mcxt);
    /* Finish/close the array */
    castate->res = push_agtype_value(&castate->parse_state, WAGT_END_ARRAY,
                                     NULL);
    /* restore the old context */
    MemoryContextSwitchTo(old_mcxt);
    /* return the agtype array */
    PG_RETURN_POINTER(agtype_value_to_agtype(castate->res));
}

/* helper function to quickly build an agtype_value vertex */
agtype_value *agtype_value_build_vertex(graphid id, char *label,
                                        Datum properties)
{
    agtype_in_state result;

    /* the label can't be NULL */
    Assert(label != NULL);

    memset(&result, 0, sizeof(agtype_in_state));

    /* push in the object beginning */
    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);

    /* push the graph id key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("id"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE,
                                   integer_to_agtype_value(id));

    /* push the label key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("label"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE,
                                   string_to_agtype_value(label));

    /* push the properties key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("properties"));
    add_agtype((Datum)properties, false, &result, AGTYPEOID, false);

    /* push in the object end */
    result.res = push_agtype_value(&result.parse_state, WAGT_END_OBJECT, NULL);

    /* set it as an edge */
    result.res->type = AGTV_VERTEX;

    /* return the result that was build (allocated) inside the result */
    return result.res;
}

/* helper function to quickly build an agtype_value edge */
agtype_value *agtype_value_build_edge(graphid id, char *label, graphid end_id,
                                      graphid start_id, Datum properties)
{
    agtype_in_state result;

    /* the label can't be NULL */
    Assert(label != NULL);

    memset(&result, 0, sizeof(agtype_in_state));

    /* push in the object beginning */
    result.res = push_agtype_value(&result.parse_state, WAGT_BEGIN_OBJECT,
                                   NULL);
    /* push the graph id key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("id"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE,
                                   integer_to_agtype_value(id));

    /* push the label key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("label"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE,
                                   string_to_agtype_value(label));

    /* push the end_id key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("end_id"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE,
                                   integer_to_agtype_value(end_id));

    /* push the start_id key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("start_id"));
    result.res = push_agtype_value(&result.parse_state, WAGT_VALUE,
                                   integer_to_agtype_value(start_id));

    /* push the properties key/value pair */
    result.res = push_agtype_value(&result.parse_state, WAGT_KEY,
                                   string_to_agtype_value("properties"));
    add_agtype((Datum)properties, false, &result, AGTYPEOID, false);

    /* push in the object end */
    result.res = push_agtype_value(&result.parse_state, WAGT_END_OBJECT, NULL);

    /* set it as an edge */
    result.res->type = AGTV_EDGE;

    /* return the result that was build (allocated) inside the result */
    return result.res;
}

/*
 * Extract an agtype_value from an agtype and optionally verify that it is of
 * the correct type. It will always complain if the passed argument is not a
 * scalar.
 *
 * Optionally, the function will throw an error, stating the calling function
 * name, for invalid values - including AGTV_NULL
 *
 * Note: This only works for scalars wrapped in an array container, not
 * in objects.
 */
agtype_value *get_agtype_value(char *funcname, agtype *agt_arg,
                               enum agtype_value_type type, bool error)
{
    agtype_value *agtv_value = NULL;

    /* we need these */
    Assert(funcname != NULL);
    Assert(agt_arg != NULL);

    /* error if the argument is not a scalar */
    if (!AGTYPE_CONTAINER_IS_SCALAR(&agt_arg->root))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s: agtype argument must be a scalar",
                        funcname)));
    }

    /* is it AGTV_NULL? */
    if (error && is_agtype_null(agt_arg))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s: agtype argument must not be AGTV_NULL",
                        funcname)));
    }

    /* get the agtype value */
    agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it the correct type? */
    if (error && agtv_value->type != type)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s: agtype argument of wrong type",
                        funcname)));
    }
    return agtv_value;
}

/*
 * Returns properties of an entity (vertex or edge) or NULL if there are none.
 * If the object passed is not a scalar, an error is thrown.
 * If the object is a scalar and error_on_scalar is false, the scalar is
 * returned, otherwise an error is thrown.
 */
agtype_value *extract_entity_properties(agtype *object, bool error_on_scalar)
{
    agtype_value *scalar_value = NULL;
    agtype_value *return_value = NULL;

    if (!AGT_ROOT_IS_SCALAR(object))
    {
        ereport(ERROR,(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                       errmsg("expected a scalar value")));
    }

    /* unpack the scalar */
    scalar_value = get_ith_agtype_value_from_container(&object->root, 0);

    /* get the properties depending on the type or fail */
    if (scalar_value->type == AGTV_VERTEX)
    {
        return_value = &scalar_value->val.object.pairs[2].value;
    }
    else if (scalar_value->type == AGTV_EDGE)
    {
        return_value = &scalar_value->val.object.pairs[4].value;
    }
    else if (scalar_value->type == AGTV_PATH)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("cannot extract properties from an agtype path")));
    }
    else if (error_on_scalar)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("scalar object must be a vertex or edge")));
    }
    else
    {
        return_value = scalar_value;
    }

    /* if the properties are NULL, return NULL */
    if (return_value == NULL || return_value->type == AGTV_NULL)
    {
        return NULL;
    }

    /* set the object_value to the property_value. */
    return return_value;
}

PG_FUNCTION_INFO_V1(age_eq_tilde);
/*
 * Execution function for =~ aka regular expression comparisons
 *
 * Note: Everything must resolve to 2 agtype strings. All others types are
 * errors.
 */
Datum age_eq_tilde(PG_FUNCTION_ARGS)
{
    agtype *agt_string = NULL;
    agtype *agt_pattern = NULL;

    /* if either are NULL return NULL */
    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
    {
        PG_RETURN_NULL();
    }

    /* extract the input */
    agt_string = AG_GET_ARG_AGTYPE_P(0);
    agt_pattern = AG_GET_ARG_AGTYPE_P(1);

    /* they both need to scalars */
    if (AGT_ROOT_IS_SCALAR(agt_string) && AGT_ROOT_IS_SCALAR(agt_pattern))
    {
        agtype_value *agtv_string;
        agtype_value *agtv_pattern;

        /* get the contents of each container */
        agtv_string = get_ith_agtype_value_from_container(&agt_string->root, 0);
        agtv_pattern = get_ith_agtype_value_from_container(&agt_pattern->root,
                                                           0);
        /* if either are agtype null, return NULL */
        if (agtv_string->type == AGTV_NULL ||
            agtv_pattern->type == AGTV_NULL)
        {
            PG_RETURN_NULL();
        }

        /* only strings can be compared, all others are errors */
        if (agtv_string->type == AGTV_STRING &&
            agtv_pattern->type == AGTV_STRING)
        {
            text *string = NULL;
            text *pattern = NULL;
            Datum result;

            string = cstring_to_text_with_len(agtv_string->val.string.val,
                                              agtv_string->val.string.len);
            pattern = cstring_to_text_with_len(agtv_pattern->val.string.val,
                                               agtv_pattern->val.string.len);

            result = (DirectFunctionCall2Coll(textregexeq, C_COLLATION_OID,
                                              PointerGetDatum(string),
                                              PointerGetDatum(pattern)));
            return boolean_to_agtype(DatumGetBool(result));
        }
    }
    /* if we got here we have values that are invalid */
    ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("agtype string values expected")));
}

/*
 * Helper function to step through and retrieve keys from an object.
 * borrowed and modified from get_next_object_pair() in agtype_vle.c
 */
static agtype_iterator *get_next_object_key(agtype_iterator *it,
                                             agtype_container *agtc,
                                             agtype_value *key)
{
    agtype_iterator_token itok;
    agtype_value tmp;

    /* verify input params */
    Assert(agtc != NULL);
    Assert(key != NULL);

    /* check to see if the container is empty */
    if (AGTYPE_CONTAINER_SIZE(agtc) == 0)
    {
        return NULL;
    }

    /* if the passed iterator is NULL, this is the first time, create it */
    if (it == NULL)
    {
        /* initial the iterator */
        it = agtype_iterator_init(agtc);
        /* get the first token */
        itok = agtype_iterator_next(&it, &tmp, false);
        /* it should be WAGT_BEGIN_OBJECT */
        Assert(itok == WAGT_BEGIN_OBJECT);
    }

    /* the next token should be a key or the end of the object */
    itok = agtype_iterator_next(&it, &tmp, false);
    Assert(itok == WAGT_KEY || itok == WAGT_END_OBJECT);
    /* if this is the end of the object return NULL */
    if (itok == WAGT_END_OBJECT)
    {
        return NULL;
    }

    /* this should be the key, copy it */
    if (itok == WAGT_KEY)
    {
        *key = tmp;
    }

    /*
     * The next token should be a value but, it could be a begin tokens for
     * arrays or objects. For those we just return NULL to ignore them.
     */
    itok = agtype_iterator_next(&it, &tmp, true);
    Assert(itok == WAGT_VALUE);

    /* return the iterator */
    return it;
}

PG_FUNCTION_INFO_V1(age_keys);
/*
 * Execution function to implement openCypher keys() function
 */
Datum age_keys(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_result = NULL;
    agtype_value obj_key = {0};
    agtype_iterator *it = NULL;
    agtype_parse_state *parse_state = NULL;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    /* needs to be a map, node, or relationship */
    agt_arg = AG_GET_ARG_AGTYPE_P(0);

    /*
     * check for a scalar object. edges and vertexes are scalar, objects are not
     * scalar and will be handled separately
     */
    if (AGT_ROOT_IS_SCALAR(agt_arg))
    {
        agtv_result = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* is it an agtype null, return null if it is */
        if (agtv_result->type == AGTV_NULL)
            PG_RETURN_NULL();

        /* check for proper agtype and extract the properties field */
        if (agtv_result->type == AGTV_EDGE ||
            agtv_result->type == AGTV_VERTEX)
        {
            agtv_result = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_result,
                                                        "properties");

            Assert(agtv_result != NULL);
            Assert(agtv_result->type = AGTV_OBJECT);
        }
        else
        {
            ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                    errmsg("keys() argument must be a vertex, edge, object or null")));
        }

        agt_arg = agtype_value_to_agtype(agtv_result);
        agtv_result = NULL;
    }
    else if (!AGT_ROOT_IS_OBJECT(agt_arg))
    {
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                errmsg("keys() argument must be a vertex, edge, object or null")));
    }

    /* push the beginning of the array */
    agtv_result = push_agtype_value(&parse_state, WAGT_BEGIN_ARRAY, NULL);

    /* populate the array with keys */
    while ((it = get_next_object_key(it, &agt_arg->root, &obj_key)))
    {
        agtv_result = push_agtype_value(&parse_state, WAGT_ELEM, &obj_key);
    }

    /* push the end of the array*/
    agtv_result = push_agtype_value(&parse_state, WAGT_END_ARRAY, NULL);

    Assert(agtv_result != NULL);
    Assert(agtv_result->type = AGTV_ARRAY);

    PG_RETURN_POINTER(agtype_value_to_agtype(agtv_result));
}

PG_FUNCTION_INFO_V1(age_nodes);
/*
 * Execution function to implement openCypher nodes() function
 */
Datum age_nodes(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_path = NULL;
    agtype_in_state agis_result;
    int i = 0;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("nodes() argument must resolve to a scalar value")));
    }

    /* get the potential path out of the array */
    agtv_path = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_path->type == AGTV_NULL)
    {
            PG_RETURN_NULL();
    }

    /* verify that it is an agtype path */
    if (agtv_path->type != AGTV_PATH)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("nodes() argument must be a path")));

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);
    /* push in each vertex (every other entry) from the path */
    for (i = 0; i < agtv_path->val.array.num_elems; i += 2)
    {
        agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM,
                                            &agtv_path->val.array.elems[i]);
    }

    /* push the end of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    /* convert the agtype_value to a datum to return to the caller */
    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

PG_FUNCTION_INFO_V1(age_labels);
/*
 * Execution function to implement openCypher labels() function
 *
 * NOTE:
 *
 * This function is defined to return NULL on NULL input. So, no need to check
 * for SQL NULL input.
 */
Datum age_labels(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_temp = NULL;
    agtype_value *agtv_label = NULL;
    agtype_in_state agis_result;

    /* get the vertex argument */
    agt_arg = AG_GET_ARG_AGTYPE_P(0);

    /* verify it is a scalar */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("labels() argument must resolve to a scalar value")));
    }

    /* is it an agtype null? */
    if (AGTYPE_CONTAINER_IS_SCALAR(&agt_arg->root) &&
        AGTE_IS_NULL((&agt_arg->root)->children[0]))
    {
        PG_RETURN_NULL();
    }

    /* get the potential vertex */
    agtv_temp = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* verify that it is an agtype vertex */
    if (agtv_temp->type != AGTV_VERTEX)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("labels() argument must be a vertex")));
    }

    /* get the label from the vertex */
    agtv_label = GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_temp, "label");
    /* it cannot be NULL */
    Assert(agtv_label != NULL);

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* push in the label */
    agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM,
                                        agtv_label);

    /* push the end of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    /* convert the agtype_value to a datum to return to the caller */
    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

PG_FUNCTION_INFO_V1(age_relationships);
/*
 * Execution function to implement openCypher relationships() function
 */
Datum age_relationships(PG_FUNCTION_ARGS)
{
    agtype *agt_arg = NULL;
    agtype_value *agtv_path = NULL;
    agtype_in_state agis_result;
    int i = 0;

    /* check for null */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    agt_arg = AG_GET_ARG_AGTYPE_P(0);
    /* check for a scalar object */
    if (!AGT_ROOT_IS_SCALAR(agt_arg))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("relationships() argument must resolve to a scalar value")));
    }

    /* get the potential path out of the array */
    agtv_path = get_ith_agtype_value_from_container(&agt_arg->root, 0);

    /* is it an agtype null? */
    if (agtv_path->type == AGTV_NULL)
    {
            PG_RETURN_NULL();
    }

    /* verify that it is an agtype path */
    if (agtv_path->type != AGTV_PATH)
        ereport(ERROR, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("relationships() argument must be a path")));

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);
    /* push in each edge (every other entry) from the path */
    for (i = 1; i < agtv_path->val.array.num_elems; i += 2)
    {
        agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM,
                                            &agtv_path->val.array.elems[i]);
    }

    /* push the end of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    /* convert the agtype_value to a datum to return to the caller */
    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

/*
 * Helper function to convert an integer type (PostgreSQL or agtype) datum into
 * an int64. The function will flag if an agtype null was found. The function
 * will error out on invalid information, printing out the funcname passed.
 */
static int64 get_int64_from_int_datums(Datum d, Oid type, char *funcname,
                                       bool *is_agnull)
{
    int64 result = 0;

    /* test for PG integer types */
    if (type == INT2OID)
    {
        result = (int64) DatumGetInt16(d);
    }
    else if (type == INT4OID)
    {
        result = (int64) DatumGetInt32(d);
    }
    else if (type == INT8OID)
    {
        result = (int64) DatumGetInt64(d);
    }
    /* test for agtype integer */
    else if (type == AGTYPEOID)
    {
        agtype *agt_arg = NULL;
        agtype_value *agtv_value = NULL;
        agtype_container *agtc = NULL;

        /* get the agtype argument */
        agt_arg = DATUM_GET_AGTYPE_P(d);

        if (!AGT_ROOT_IS_SCALAR(agt_arg))
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("%s() only supports scalar arguments", funcname)));
        }
        /* check for agtype null*/
        agtc = &agt_arg->root;
        if (AGTE_IS_NULL(agtc->children[0]))
        {
            *is_agnull = true;
            return 0;
        }

        /* extract it from the scalar array */
        agtv_value = get_ith_agtype_value_from_container(&agt_arg->root, 0);

        /* check for agtype integer */
        if (agtv_value->type == AGTV_INTEGER)
        {
            result = agtv_value->val.int_value;
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("%s() unsupported argument type", funcname)));
        }
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("%s() unsupported argument type", funcname)));
    }

    /* return the result */
    *is_agnull = false;
    return result;
}

PG_FUNCTION_INFO_V1(age_range);
/*
 * Execution function to implement openCypher range() function
 */
Datum age_range(PG_FUNCTION_ARGS)
{
    Datum *args = NULL;
    bool *nulls = NULL;
    Oid *types = NULL;
    int nargs;
    int64 start_idx = 0;
    int64 end_idx = 0;
    /* step defaults to 1 */
    int64 step = 1;
    bool is_agnull = false;
    agtype_in_state agis_result;
    int64 i = 0;

    /* get the arguments */
    nargs = extract_variadic_args(fcinfo, 0, false, &args, &types, &nulls);

    /* throw an error if the number of args is not the expected number */
    if (nargs != 2 && nargs != 3)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("range(): invalid number of input parameters")));
    }

    /* check for NULL start and end input */
    if (nulls[0] || nulls[1])
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("range(): neither start or end can be NULL")));
    }

    /* get the start index */
    start_idx = get_int64_from_int_datums(args[0], types[0], "range",
                                          &is_agnull);
    if (is_agnull)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("range(): start cannot be NULL")));
    }

    /* get the end index */
    end_idx = get_int64_from_int_datums(args[1], types[1], "range", &is_agnull);
    if (is_agnull)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("range(): end cannot be NULL")));
    }

    /* get the step */
    if (nargs == 3 && !nulls[2])
    {
        step = get_int64_from_int_datums(args[2], types[2], "range",
                                         &is_agnull);
        if (is_agnull)
        {
            step = 1;
        }
    }

    /* the step cannot be zero */
    if (step == 0)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("range(): step cannot be zero")));
    }

    /* clear the result structure */
    MemSet(&agis_result, 0, sizeof(agtype_in_state));

    /* push the beginning of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_BEGIN_ARRAY, NULL);

    /* push in each agtype integer in the range */
    for (i = start_idx;
         (step > 0 && i <= end_idx) || (step < 0 && i >= end_idx);
         i += step)
    {
        agtype_value agtv;

        /* build the integer */
        agtv.type = AGTV_INTEGER;
        agtv.val.int_value = i;
        /* add the value to the array */
        agis_result.res = push_agtype_value(&agis_result.parse_state, WAGT_ELEM,
                                            &agtv);
    }

    /* push the end of the array */
    agis_result.res = push_agtype_value(&agis_result.parse_state,
                                        WAGT_END_ARRAY, NULL);

    /* convert the agtype_value to a datum to return to the caller */
    PG_RETURN_POINTER(agtype_value_to_agtype(agis_result.res));
}

PG_FUNCTION_INFO_V1(age_unnest);
/*
 * Function to convert the Array type of Agtype into each row. It is used for
 * Cypher `UNWIND` clause.
 */
Datum age_unnest(PG_FUNCTION_ARGS)
{
    agtype *agtype_arg = NULL;
    bool list_comprehension = false;
    ReturnSetInfo *rsi;
    Tuplestorestate *tuple_store;
    TupleDesc tupdesc;
    TupleDesc ret_tdesc;
    MemoryContext old_cxt, tmp_cxt;
    bool skipNested = false;
    agtype_iterator *it;
    agtype_value v;
    agtype_iterator_token r;

    /* verify that we have the correct number of args */
    if (PG_NARGS() != 2)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("invalid number of arguments to unnest")));
    }

    /* verify that our flags are not null */
    if (PG_ARGISNULL(1))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("invalid unnest boolean flags passed")));
    }

    /* check for a NULL expr */
    if (PG_ARGISNULL(0))
    {
        PG_RETURN_NULL();
    }

    /* get our flags */
    list_comprehension = PG_GETARG_BOOL(1);

    /* get the input expression */
    agtype_arg = AG_GET_ARG_AGTYPE_P(0);

    /* verify that it resolves to an array */
    if (!AGT_ROOT_IS_ARRAY(agtype_arg))
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                        errmsg("cannot extract elements from an object")));
    }

    rsi = (ReturnSetInfo *) fcinfo->resultinfo;

    rsi->returnMode = SFRM_Materialize;

    /* it's a simple type, so don't use get_call_result_type() */
    tupdesc = rsi->expectedDesc;

    old_cxt = MemoryContextSwitchTo(rsi->econtext->ecxt_per_query_memory);

    ret_tdesc = CreateTupleDescCopy(tupdesc);
    BlessTupleDesc(ret_tdesc);
    tuple_store =
            tuplestore_begin_heap(rsi->allowedModes & SFRM_Materialize_Random,
                                  false, work_mem);

    MemoryContextSwitchTo(old_cxt);

    tmp_cxt = AllocSetContextCreate(CurrentMemoryContext,
                                    "age_unnest temporary cxt",
                                    ALLOCSET_DEFAULT_SIZES);

    it = agtype_iterator_init(&agtype_arg->root);

    while ((r = agtype_iterator_next(&it, &v, skipNested)) != WAGT_DONE)
    {
        skipNested = true;

        if (r == WAGT_ELEM)
        {
            HeapTuple tuple;
            Datum values[1];
            bool nulls[1] = {false};
            agtype *val = agtype_value_to_agtype(&v);

            /* use the tmp context so we can clean up after each tuple is done */
            old_cxt = MemoryContextSwitchTo(tmp_cxt);

            values[0] = PointerGetDatum(val);

            tuple = heap_form_tuple(ret_tdesc, values, nulls);

            tuplestore_puttuple(tuple_store, tuple);

            /* clean up and switch back */
            MemoryContextSwitchTo(old_cxt);
            MemoryContextReset(tmp_cxt);
        }
    }

    /*
     * If this is for list_comprehension, we need to add a NULL as the last row.
     * This NULL will allow empty lists (either filtered out by where, creating
     * an empty list, or just a generic empty list) to be preserved.
     */
    if (list_comprehension)
    {
        Datum values[1] = {0};
        bool nulls[1] = {true};

        old_cxt = MemoryContextSwitchTo(tmp_cxt);

        tuplestore_puttuple(tuple_store,
                            heap_form_tuple(ret_tdesc, values, nulls));

        MemoryContextSwitchTo(old_cxt);
        MemoryContextReset(tmp_cxt);
    }

    MemoryContextDelete(tmp_cxt);

    rsi->setResult = tuple_store;
    rsi->setDesc = ret_tdesc;

    PG_RETURN_NULL();
}

/*
 * Volatile wrapper replacement. The previous version was PL/SQL
 * and could only handle AGTYPE input and returned AGTYPE output.
 * This version will create the appropriate AGTYPE based off of
 * the input type.
 */
PG_FUNCTION_INFO_V1(agtype_volatile_wrapper);

Datum agtype_volatile_wrapper(PG_FUNCTION_ARGS)
{
    int nargs = PG_NARGS();
    Oid type = InvalidOid;
    bool isnull = PG_ARGISNULL(0);

    /* check for null and pass it through */
    if (isnull)
    {
        PG_RETURN_NULL();
    }

    /* check for more than one argument */
    if (nargs > 1)
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("agtype_volatile_wrapper: too many args")));

    }

    /* get the type of the input argument */
    type = get_fn_expr_argtype(fcinfo->flinfo, 0);

    /* if it is NOT an AGTYPE, we need convert it to one, if possible */
    if (type != AGTYPEOID)
    {
        agtype_value agtv_result;
        Datum arg = PG_GETARG_DATUM(0);

        /* check for PG types that easily translate to AGTYPE */
        if (type == BOOLOID)
        {
            agtv_result.type = AGTV_BOOL;
            agtv_result.val.boolean = DatumGetBool(arg);
        }
        else if (type == INT2OID || type == INT4OID || type == INT8OID)
        {
            agtv_result.type = AGTV_INTEGER;

            if (type == INT8OID)
            {
                agtv_result.val.int_value = DatumGetInt64(arg);
            }
            else if (type == INT4OID)
            {
                agtv_result.val.int_value = (int64) DatumGetInt32(arg);
            }
            else if (type == INT2OID)
            {
                agtv_result.val.int_value = (int64) DatumGetInt16(arg);
            }
        }
        else if (type == FLOAT4OID || type == FLOAT8OID)
        {
            agtv_result.type = AGTV_FLOAT;

            if (type == FLOAT8OID)
            {
                agtv_result.val.float_value = DatumGetFloat8(arg);
            }
            else if (type == FLOAT4OID)
            {
                agtv_result.val.float_value = (float8) DatumGetFloat4(arg);
            }
        }
        else if (type == NUMERICOID)
        {
            agtv_result.type = AGTV_NUMERIC;
            agtv_result.val.numeric = DatumGetNumeric(arg);
        }
        else if (type == CSTRINGOID)
        {
            agtv_result.type = AGTV_STRING;
            agtv_result.val.string.val = DatumGetCString(arg);
            agtv_result.val.string.len = strlen(agtv_result.val.string.val);
        }
        else if (type == TEXTOID)
        {
            agtv_result.type = AGTV_STRING;
            agtv_result.val.string.val = text_to_cstring(DatumGetTextPP(arg));
            agtv_result.val.string.len = strlen(agtv_result.val.string.val);
        }
        else
        {
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("agtype_volatile_wrapper: unsupported arg type")));
        }

        /* return the built result */
        PG_RETURN_POINTER(agtype_value_to_agtype(&agtv_result));
    }

    /* otherwise, just pass it through */
    PG_RETURN_POINTER(PG_GETARG_DATUM(0));
}
