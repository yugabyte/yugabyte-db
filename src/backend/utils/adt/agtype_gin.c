/*
 * For PostgreSQL Database Management System:
 * (formerly known as Postgres, then as Postgres95)
 *
 * Portions Copyright (c) 1996-2010, The PostgreSQL Global Development Group
 *
 * Portions Copyright (c) 1994, The Regents of the University of California
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without a written agreement
 * is hereby granted, provided that the above copyright notice and this
 * paragraph and the following two paragraphs appear in all copies.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING
 * LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION,
 * EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF
 * CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES,
 * ENHANCEMENTS, OR MODIFICATIONS.
 */

#include "postgres.h"

#include "access/gin.h"
#include "access/hash.h"
#include "access/stratnum.h"
#include "catalog/pg_collation.h"
#include "catalog/pg_type.h"
#include "utils/float.h"
#include "utils/builtins.h"
#include "utils/varlena.h"

#include "utils/agtype.h"

typedef struct PathHashStack
{
    uint32 hash;
    struct PathHashStack *parent;
} PathHashStack;

static Datum make_text_key(char flag, const char *str, int len);
static Datum make_scalar_key(const agtype_value *scalar_val, bool is_key);


/*
 *
 * agtype_ops GIN opclass support functions
 *
 */
/*
 * Compares two keys (not indexed items!) and returns an integer less than zero,
 * zero, or greater than zero, indicating whether the first key is less than,
 * equal to, or greater than the second. NULL keys are never passed to this
 * function.
 */
PG_FUNCTION_INFO_V1(gin_compare_agtype);
Datum gin_compare_agtype(PG_FUNCTION_ARGS)
{
    text *arg1, *arg2;
    int32 result;
    char *a1p, *a2p;
    int len1, len2;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
    {
        PG_RETURN_NULL();
    }

    arg1 = PG_GETARG_TEXT_PP(0);
    arg2 = PG_GETARG_TEXT_PP(1);

    a1p = VARDATA_ANY(arg1);
    a2p = VARDATA_ANY(arg2);

    len1 = VARSIZE_ANY_EXHDR(arg1);
    len2 = VARSIZE_ANY_EXHDR(arg2);

    /* Compare text as bttextcmp does, but always using C collation */
    result = varstr_cmp(a1p, len1, a2p, len2, C_COLLATION_OID);

    PG_FREE_IF_COPY(arg1, 0);
    PG_FREE_IF_COPY(arg2, 1);

    PG_RETURN_INT32(result);
}

/*
 * Returns a palloc'd array of keys given an item to be indexed. The number of
 * returned keys must be stored into *nkeys. The return value can be NULL if the
 * item contains no keys.
 */
PG_FUNCTION_INFO_V1(gin_extract_agtype);
Datum gin_extract_agtype(PG_FUNCTION_ARGS)
{
    agtype *agt;
    int32 *nentries;
    int total;
    agtype_iterator *it;
    agtype_value v;
    agtype_iterator_token r;
    int i = 0;
    Datum *entries;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
    {
        PG_RETURN_POINTER(NULL);
    }

    agt = (agtype *) AG_GET_ARG_AGTYPE_P(0);
    nentries = (int32 *) PG_GETARG_POINTER(1);
    total = 2 * AGT_ROOT_COUNT(agt);

    /* If the root level is empty, we certainly have no keys */
    if (total == 0)
    {
        *nentries = 0;
        PG_RETURN_POINTER(NULL);
    }

    /* Otherwise, use 2 * root count as initial estimate of result size */
    entries = (Datum *) palloc(sizeof(Datum) * total);

    it = agtype_iterator_init(&agt->root);

    while ((r = agtype_iterator_next(&it, &v, false)) != WAGT_DONE)
    {
        /* Since we recurse into the object, we might need more space */
        if (i >= total)
        {
            total *= 2;
            entries = (Datum *) repalloc(entries, sizeof(Datum) * total);
        }

        switch (r)
        {
            case WAGT_KEY:
                entries[i++] = make_scalar_key(&v, true);
                break;
            case WAGT_ELEM:
                /* Pretend string array elements are keys */
                entries[i++] = make_scalar_key(&v, (v.type == AGTV_STRING));
                break;
            case WAGT_VALUE:
                entries[i++] = make_scalar_key(&v, false);
                break;
            default:
                /* we can ignore structural items */
                break;
        }
    }

    *nentries = i;

    PG_RETURN_POINTER(entries);
}

/*
 * Returns a palloc'd array of keys given a value to be queried; that is, query
 * is the value on the right-hand side of an indexable operator whose left-hand
 * side is the indexed column. The number of returned keys must be stored into
 * *nkeys. If any of the keys can be null, also palloc an array of *nkeys bool
 * fields, store its address at *nullFlags, and set these null flags as needed.
 * *nullFlags can be left NULL (its initial value) if all keys are non-null.
 * The return value can be NULL if the query contains no keys.
 *
 * searchMode is an output argument that allows extractQuery to specify details
 * about how the search will be done. If *searchMode is set to
 * GIN_SEARCH_MODE_DEFAULT (which is the value it is initialized to before
 * call), only items that match at least one of the returned keys are considered
 * candidate matches. If *searchMode is set to GIN_SEARCH_MODE_ALL, then all
 * non-null items in the index are considered candidate matches, whether they
 * match any of the returned keys or not. This is only done when the contains
 * or exists all strategy are used and the passed map is empty.
 */
PG_FUNCTION_INFO_V1(gin_extract_agtype_query);
Datum gin_extract_agtype_query(PG_FUNCTION_ARGS)
{
    int32 *nentries;
    StrategyNumber strategy;
    int32 *searchMode;
    Datum *entries;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1) ||
        PG_ARGISNULL(2) || PG_ARGISNULL(6))
    {
        PG_RETURN_NULL();
    }

    nentries = (int32 *) PG_GETARG_POINTER(1);
    strategy = PG_GETARG_UINT16(2);
    searchMode = (int32 *) PG_GETARG_POINTER(6);

    if (strategy == AGTYPE_CONTAINS_STRATEGY_NUMBER)
    {
        /* Query is a agtype, so just apply gin_extract_agtype... */
        entries = (Datum *)
            DatumGetPointer(DirectFunctionCall2(gin_extract_agtype,
                                                PG_GETARG_DATUM(0),
                                                PointerGetDatum(nentries)));
        /* ...although "contains {}" requires a full index scan */
        if (*nentries == 0)
        {
            *searchMode = GIN_SEARCH_MODE_ALL;
        }
    }
    else if (strategy == AGTYPE_EXISTS_STRATEGY_NUMBER)
    {
        /* Query is a text string, which we treat as a key */
        text *query = PG_GETARG_TEXT_PP(0);

        *nentries = 1;
        entries = (Datum *)palloc(sizeof(Datum));
        entries[0] = make_text_key(AGT_GIN_FLAG_KEY, VARDATA_ANY(query),
                                   VARSIZE_ANY_EXHDR(query));
    }
    else if (strategy == AGTYPE_EXISTS_ANY_STRATEGY_NUMBER ||
             strategy == AGTYPE_EXISTS_ALL_STRATEGY_NUMBER)
    {
        /* Query is a text array; each element is treated as a key */
        agtype *agt = AG_GET_ARG_AGTYPE_P(0);
        agtype_iterator *it = NULL;
        agtype_value elem;
        agtype_iterator_token itok;
        int key_count = AGTYPE_CONTAINER_SIZE(&agt->root);
        int index = 0;

        if (AGTYPE_CONTAINER_IS_SCALAR(&agt->root) ||
            !AGTYPE_CONTAINER_IS_ARRAY(&agt->root))
        {
            elog(ERROR, "GIN query requires an agtype array");
        }

        entries = (Datum *) palloc(sizeof(Datum) * key_count);
        it = agtype_iterator_init(&agt->root);

        /* it should be WAGT_BEGIN_ARRAY */
        itok = agtype_iterator_next(&it, &elem, true);
        Assert(itok == WAGT_BEGIN_ARRAY);

        while (WAGT_END_ARRAY != agtype_iterator_next(&it, &elem, true))
        {
            if (elem.type != AGTV_STRING)
            {
                elog(ERROR, "unsupport agtype for GIN lookup: %d", elem.type);
            }

            entries[index++] = make_text_key(AGT_GIN_FLAG_KEY,
                                         elem.val.string.val,
                                         elem.val.string.len);
        }

        *nentries = index;

        /* ExistsAll with no keys should match everything */
        if (index == 0 && strategy == AGTYPE_EXISTS_ALL_STRATEGY_NUMBER)
        {
            *searchMode = GIN_SEARCH_MODE_ALL;
        }
    }
    else
    {
        elog(ERROR, "unrecognized strategy number: %d", strategy);
        entries = NULL;            /* keep compiler quiet */
    }

    PG_RETURN_POINTER(entries);
}

/*
 * Returns true if an indexed item satisfies the query operator for the given
 * strategy (or might satisfy it, if the recheck indication is returned). This
 * function does not have direct access to the indexed item's value, since GIN
 * does not store items explicitly. Rather, what is available is knowledge about
 * which key values extracted from the query appear in a given indexed item. The
 * check array has length nkeys, which is the same as the number of keys
 * previously returned by gin_extract_agtype_query for this query datum. Each
 * element of the check array is true if the indexed item contains the
 * corresponding query key, i.e., if (check[i] == true) the i-th key of the
 * gin_extract_agtype_query result array is present in the indexed item. The
 * original query datum is passed in case the consistent method needs to consult
 * it, and so are the queryKeys[] and nullFlags[] arrays previously returned by
 * gin_extract_agtype_query.
 *
 * When extractQuery returns a null key in queryKeys[], the corresponding
 * check[] element is true if the indexed item contains a null key; that is, the
 * semantics of check[] are like IS NOT DISTINCT FROM. The consistent function
 * can examine the corresponding nullFlags[] element if it needs to tell the
 * difference between a regular value match and a null match.
 *
 * On success, *recheck should be set to true if the heap tuple needs to be
 * rechecked against the query operator, or false if the index test is exact.
 * That is, a false return value guarantees that the heap tuple does not match
 * the query; a true return value with *recheck set to false guarantees that the
 * heap tuple does match the query; and a true return value with *recheck set to
 * true means that the heap tuple might match the query, so it needs to be
 * fetched and rechecked by evaluating the query operator directly against the
 * originally indexed item.
 */
PG_FUNCTION_INFO_V1(gin_consistent_agtype);
Datum gin_consistent_agtype(PG_FUNCTION_ARGS)
{
    bool *check;
    StrategyNumber strategy;
    int32 nkeys;
    bool *recheck;
    bool res = true;
    int32 i;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1) ||
        PG_ARGISNULL(3) || PG_ARGISNULL(5))
    {
        PG_RETURN_NULL();
    }

    check = (bool *) PG_GETARG_POINTER(0);
    strategy = PG_GETARG_UINT16(1);
    nkeys = PG_GETARG_INT32(3);
    recheck = (bool *) PG_GETARG_POINTER(5);

    if (strategy == AGTYPE_CONTAINS_STRATEGY_NUMBER)
    {
        /*
         * We must always recheck, since we can't tell from the index whether
         * the positions of the matched items match the structure of the query
         * object.  (Even if we could, we'd also have to worry about hashed
         * keys and the index's failure to distinguish keys from string array
         * elements.)  However, the tuple certainly doesn't match unless it
         * contains all the query keys.
         */
        *recheck = true;
        for (i = 0; i < nkeys; i++)
        {
            if (!check[i])
            {
                res = false;
                break;
            }
        }
    }
    else if (strategy == AGTYPE_EXISTS_STRATEGY_NUMBER)
    {
        /*
         * Although the key is certainly present in the index, we must recheck
         * because (1) the key might be hashed, and (2) the index match might
         * be for a key that's not at top level of the JSON object.  For (1),
         * we could look at the query key to see if it's hashed and not
         * recheck if not, but the index lacks enough info to tell about (2).
         */
        *recheck = true;
        res = true;
    }
    else if (strategy == AGTYPE_EXISTS_ANY_STRATEGY_NUMBER)
    {
        /* As for plain exists, we must recheck */
        *recheck = true;
        res = true;
    }
    else if (strategy == AGTYPE_EXISTS_ALL_STRATEGY_NUMBER)
    {
        /* As for plain exists, we must recheck */
        *recheck = true;
        /* ... but unless all the keys are present, we can say "false" */
        for (i = 0; i < nkeys; i++)
        {
            if (!check[i])
            {
                res = false;
                break;
            }
        }
    }
    else
    {
        elog(ERROR, "unrecognized strategy number: %d", strategy);
    }

    PG_RETURN_BOOL(res);
}

/*
 * gin_triconsistent_agtype is similar to gin_consistent_agtype, but instead of
 * booleans in the check vector, there are three possible values for each key:
 * GIN_TRUE, GIN_FALSE and GIN_MAYBE. GIN_FALSE and GIN_TRUE have the same
 * meaning as regular boolean values, while GIN_MAYBE means that the presence of
 * that key is not known. When GIN_MAYBE values are present, the function should
 * only return GIN_TRUE if the item certainly matches whether or not the index
 * item contains the corresponding query keys. Likewise, the function must
 * return GIN_FALSE only if the item certainly does not match, whether or not it
 * contains the GIN_MAYBE keys. If the result depends on the GIN_MAYBE entries,
 * i.e., the match cannot be confirmed or refuted based on the known query keys,
 * the function must return GIN_MAYBE.
 *
 * When there are no GIN_MAYBE values in the check vector, a GIN_MAYBE return
 * value is the equivalent of setting the recheck flag in the boolean consistent
 * function.
 */
PG_FUNCTION_INFO_V1(gin_triconsistent_agtype);
Datum gin_triconsistent_agtype(PG_FUNCTION_ARGS)
{
    GinTernaryValue *check;
    StrategyNumber strategy;
    int32 nkeys;
    GinTernaryValue res = GIN_MAYBE;
    int32 i;

    if (PG_ARGISNULL(0) || PG_ARGISNULL(1) || PG_ARGISNULL(3))
    {
        PG_RETURN_NULL();
    }

    check = (GinTernaryValue *)PG_GETARG_POINTER(0);
    strategy = PG_GETARG_UINT16(1);
    nkeys = PG_GETARG_INT32(3);

    /*
     * Note that we never return GIN_TRUE, only GIN_MAYBE or GIN_FALSE; this
     * corresponds to always forcing recheck in the regular consistent
     * function, for the reasons listed there.
     */
    if (strategy == AGTYPE_CONTAINS_STRATEGY_NUMBER ||
        strategy == AGTYPE_EXISTS_ALL_STRATEGY_NUMBER)
    {
        /* All extracted keys must be present */
        for (i = 0; i < nkeys; i++)
        {
            if (check[i] == GIN_FALSE)
            {
                res = GIN_FALSE;
                break;
            }
        }
    }
    else if (strategy == AGTYPE_EXISTS_STRATEGY_NUMBER ||
             strategy == AGTYPE_EXISTS_ANY_STRATEGY_NUMBER)
    {
        /* At least one extracted key must be present */
        res = GIN_FALSE;
        for (i = 0; i < nkeys; i++)
        {
            if (check[i] == GIN_TRUE || check[i] == GIN_MAYBE)
            {
                res = GIN_MAYBE;
                break;
            }
        }
    }
    else
    {
        elog(ERROR, "unrecognized strategy number: %d", strategy);
    }

    PG_RETURN_GIN_TERNARY_VALUE(res);
}

/*
 * Construct a agtype_ops GIN key from a flag byte and a textual representation
 * (which need not be null-terminated).  This function is responsible
 * for hashing overlength text representations; it will add the
 * AGT_GIN_FLAG_HASHED bit to the flag value if it does that.
 */
static Datum make_text_key(char flag, const char *str, int len)
{
    text *item;
    char hashbuf[10];

    if (len > AGT_GIN_MAX_LENGTH)
    {
        uint32 hashval;

        hashval = DatumGetUInt32(hash_any((const unsigned char *) str, len));
        snprintf(hashbuf, sizeof(hashbuf), "%08x", hashval);
        str = hashbuf;
        len = 8;
        flag |= AGT_GIN_FLAG_HASHED;
    }

    /*
     * Now build the text Datum.  For simplicity we build a 4-byte-header
     * varlena text Datum here, but we expect it will get converted to short
     * header format when stored in the index.
     */
    item = (text *)palloc(VARHDRSZ + len + 1);
    SET_VARSIZE(item, VARHDRSZ + len + 1);

    *VARDATA(item) = flag;

    memcpy(VARDATA(item) + 1, str, len);

    return PointerGetDatum(item);
}

/*
 * Create a textual representation of a agtype_value that will serve as a GIN
 * key in a agtype_ops index.  is_key is true if the JsonbValue is a key,
 * or if it is a string array element (since we pretend those are keys,
 * see jsonb.h).
 */
static Datum make_scalar_key(const agtype_value *scalarVal, bool is_key)
{
    Datum item = 0;
    char *cstr = NULL;
    char buf[MAXINT8LEN + 1];
    switch (scalarVal->type)
    {
    case AGTV_NULL:
        Assert(!is_key);
        item = make_text_key(AGT_GIN_FLAG_NULL, "", 0);
        break;
    case AGTV_INTEGER:
    {
        char *result;

        Assert(!is_key);

        pg_lltoa(scalarVal->val.int_value, buf);

        result = pstrdup(buf);
        item = make_text_key(AGT_GIN_FLAG_NUM, result, strlen(result));
        break;
    }
    case AGTV_FLOAT:
        Assert(!is_key);
        cstr = float8out_internal(scalarVal->val.float_value);
        item = make_text_key(AGT_GIN_FLAG_NUM, cstr, strlen(cstr));
        break;
    case AGTV_BOOL:
        Assert(!is_key);
        item = make_text_key(AGT_GIN_FLAG_BOOL,
                             scalarVal->val.boolean ? "t" : "f", 1);
        break;
    case AGTV_NUMERIC:
        Assert(!is_key);
        /*
         * A normalized textual representation, free of trailing zeroes,
         * is required so that numerically equal values will produce equal
         * strings.
         *
         * It isn't ideal that numerics are stored in a relatively bulky
         * textual format.  However, it's a notationally convenient way of
         * storing a "union" type in the GIN B-Tree, and indexing Jsonb
         * strings takes precedence.
         */
        cstr = numeric_normalize(scalarVal->val.numeric);
        item = make_text_key(AGT_GIN_FLAG_NUM, cstr, strlen(cstr));
        pfree(cstr);
        break;
    case AGTV_STRING:
        item = make_text_key(is_key ? AGT_GIN_FLAG_KEY : AGT_GIN_FLAG_STR,
                             scalarVal->val.string.val,
                             scalarVal->val.string.len);
        break;
    case AGTV_VERTEX:
    case AGTV_EDGE:
    case AGTV_PATH:
        elog(ERROR, "agtype type: %d is not a scalar", scalarVal->type);
        break;
    default:
        elog(ERROR, "unrecognized agtype type: %d", scalarVal->type);
        break;
    }

    return item;
}
