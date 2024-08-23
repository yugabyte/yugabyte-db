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
 * converting between agtype and agtype_values, and iterating.
 *
 * Portions Copyright (c) 2014-2018, PostgreSQL Global Development Group
 */

#include "postgres.h"

#include <math.h>

#include "access/hash.h"
#include "catalog/pg_collation.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/varlena.h"

#include "utils/agtype_ext.h"

/*
 * Maximum number of elements in an array (or key/value pairs in an object).
 * This is limited by two things: the size of the agtentry array must fit
 * in MaxAllocSize, and the number of elements (or pairs) must fit in the bits
 * reserved for that in the agtype_container.header field.
 *
 * (The total size of an array's or object's elements is also limited by
 * AGTENTRY_OFFLENMASK, but we're not concerned about that here.)
 */
#define AGTYPE_MAX_ELEMS (Min(MaxAllocSize / sizeof(agtype_value), AGT_CMASK))
#define AGTYPE_MAX_PAIRS (Min(MaxAllocSize / sizeof(agtype_pair), AGT_CMASK))

static void fill_agtype_value(agtype_container *container, int index,
                              char *base_addr, uint32 offset,
                              agtype_value *result);
static bool equals_agtype_scalar_value(agtype_value *a, agtype_value *b);
static agtype *convert_to_agtype(agtype_value *val);
static void convert_agtype_value(StringInfo buffer, agtentry *header,
                                 agtype_value *val, int level);
static void convert_agtype_array(StringInfo buffer, agtentry *pheader,
                                 agtype_value *val, int level);
static void convert_agtype_object(StringInfo buffer, agtentry *pheader,
                                  agtype_value *val, int level);
static void convert_agtype_scalar(StringInfo buffer, agtentry *entry,
                                  agtype_value *scalar_val);

static void append_to_buffer(StringInfo buffer, const char *data, int len);
static void copy_to_buffer(StringInfo buffer, int offset, const char *data,
                           int len);

static agtype_iterator *iterator_from_container(agtype_container *container,
                                                agtype_iterator *parent);
static agtype_iterator *free_and_get_parent(agtype_iterator *it);
static agtype_parse_state *push_state(agtype_parse_state **pstate);
static void append_key(agtype_parse_state *pstate, agtype_value *string);
static void append_value(agtype_parse_state *pstate, agtype_value *scalar_val);
static void append_element(agtype_parse_state *pstate,
                           agtype_value *scalar_val);
static int length_compare_agtype_string_value(const void *a, const void *b);
static int length_compare_agtype_pair(const void *a, const void *b,
                                      void *binequal);
static agtype_value *push_agtype_value_scalar(agtype_parse_state **pstate,
                                              agtype_iterator_token seq,
                                              agtype_value *scalar_val);
static int compare_two_floats_orderability(float8 lhs, float8 rhs);
static int get_type_sort_priority(enum agtype_value_type type);
static void pfree_agtype_value_content(agtype_value* value);
static void pfree_iterator_agtype_value_token(agtype_iterator_token token,
                                              agtype_value *agtv);

/*
 * Turn an in-memory agtype_value into an agtype for on-disk storage.
 *
 * There isn't an agtype_to_agtype_value(), because generally we find it more
 * convenient to directly iterate through the agtype representation and only
 * really convert nested scalar values.  agtype_iterator_next() does this, so
 * that clients of the iteration code don't have to directly deal with the
 * binary representation (agtype_deep_contains() is a notable exception,
 * although all exceptions are internal to this module).  In general, functions
 * that accept an agtype_value argument are concerned with the manipulation of
 * scalar values, or simple containers of scalar values, where it would be
 * inconvenient to deal with a great amount of other state.
 */
agtype *agtype_value_to_agtype(agtype_value *val)
{
    agtype *out;

    if (IS_A_AGTYPE_SCALAR(val))
    {
        /* Scalar value */
        agtype_parse_state *pstate = NULL;
        agtype_value *res;
        agtype_value scalar_array;

        scalar_array.type = AGTV_ARRAY;
        scalar_array.val.array.raw_scalar = true;
        scalar_array.val.array.num_elems = 1;

        push_agtype_value(&pstate, WAGT_BEGIN_ARRAY, &scalar_array);
        push_agtype_value(&pstate, WAGT_ELEM, val);
        res = push_agtype_value(&pstate, WAGT_END_ARRAY, NULL);

        out = convert_to_agtype(res);
    }
    else if (val->type == AGTV_OBJECT || val->type == AGTV_ARRAY)
    {
        out = convert_to_agtype(val);
    }
    else
    {
        Assert(val->type == AGTV_BINARY);
        out = palloc(VARHDRSZ + val->val.binary.len);
        SET_VARSIZE(out, VARHDRSZ + val->val.binary.len);
        memcpy(VARDATA(out), val->val.binary.data, val->val.binary.len);
    }

    return out;
}

/*
 * Get the offset of the variable-length portion of an agtype node within
 * the variable-length-data part of its container.  The node is identified
 * by index within the container's agtentry array.
 */
uint32 get_agtype_offset(const agtype_container *agtc, int index)
{
    uint32 offset = 0;
    int i;

    /*
     * Start offset of this entry is equal to the end offset of the previous
     * entry.  Walk backwards to the most recent entry stored as an end
     * offset, returning that offset plus any lengths in between.
     */
    for (i = index - 1; i >= 0; i--)
    {
        offset += AGTE_OFFLENFLD(agtc->children[i]);
        if (AGTE_HAS_OFF(agtc->children[i]))
            break;
    }

    return offset;
}

/*
 * Get the length of the variable-length portion of an agtype node.
 * The node is identified by index within the container's agtentry array.
 */
uint32 get_agtype_length(const agtype_container *agtc, int index)
{
    uint32 off;
    uint32 len;

    /*
     * If the length is stored directly in the agtentry, just return it.
     * Otherwise, get the begin offset of the entry, and subtract that from
     * the stored end+1 offset.
     */
    if (AGTE_HAS_OFF(agtc->children[index]))
    {
        off = get_agtype_offset(agtc, index);
        len = AGTE_OFFLENFLD(agtc->children[index]) - off;
    }
    else
    {
        len = AGTE_OFFLENFLD(agtc->children[index]);
    }

    return len;
}

/*
 * Helper function to generate the sort priority of a type. Larger
 * numbers have higher priority.
 */
static int get_type_sort_priority(enum agtype_value_type type)
{
    if (type == AGTV_PATH)
    {
        return 0;
    }
    if (type == AGTV_EDGE)
    {
        return 1;
    }
    if (type == AGTV_VERTEX)
    {
        return 2;
    }
    if (type == AGTV_OBJECT)
    {
        return 3;
    }
    if (type == AGTV_ARRAY)
    {
        return 4;
    }
    if (type == AGTV_STRING)
    {
        return 5;
    }
    if (type == AGTV_BOOL)
    {
        return 6;
    }
    if (type == AGTV_NUMERIC || type == AGTV_INTEGER || type == AGTV_FLOAT)
    {
        return 7;
    }
    if (type == AGTV_NULL)
    {
        return 8;
    }
    return -1;
}

static void pfree_iterator_agtype_value_token(agtype_iterator_token token,
                                              agtype_value *agtv)
{
    if (token == WAGT_KEY ||
        token == WAGT_VALUE ||
        token == WAGT_ELEM)
    {
        pfree_agtype_value_content(agtv);
    }
}

/*
 * BT comparator worker function.  Returns an integer less than, equal to, or
 * greater than zero, indicating whether a is less than, equal to, or greater
 * than b.  Consistent with the requirements for a B-Tree operator class
 *
 * Strings are compared lexically, in contrast with other places where we use a
 * much simpler comparator logic for searching through Strings.  Since this is
 * called from B-Tree support function 1, we're careful about not leaking
 * memory here.
 */
int compare_agtype_containers_orderability(agtype_container *a,
                                           agtype_container *b)
{
    agtype_iterator *ita;
    agtype_iterator *itb;
    int res = 0;

    ita = agtype_iterator_init(a);
    itb = agtype_iterator_init(b);

    do
    {
        agtype_value va;
        agtype_value vb;
        agtype_iterator_token ra;
        agtype_iterator_token rb;

        ra = agtype_iterator_next(&ita, &va, false);
        rb = agtype_iterator_next(&itb, &vb, false);

        if (ra == rb)
        {
            if (ra == WAGT_DONE)
            {
                /* Decisively equal */

                /* free the agtype_values associated with the tokens */
                pfree_iterator_agtype_value_token(ra, &va);
                pfree_iterator_agtype_value_token(rb, &vb);
                break;
            }

            if (ra == WAGT_END_ARRAY || ra == WAGT_END_OBJECT)
            {
                /*
                 * There is no array or object to compare at this stage of
                 * processing.  AGTV_ARRAY/AGTV_OBJECT values are compared
                 * initially, at the WAGT_BEGIN_ARRAY and WAGT_BEGIN_OBJECT
                 * tokens.
                 */

                /* free the agtype_values associated with the tokens */
                pfree_iterator_agtype_value_token(ra, &va);
                pfree_iterator_agtype_value_token(rb, &vb);
                continue;
            }

            if ((va.type == vb.type) ||
                ((va.type == AGTV_INTEGER || va.type == AGTV_FLOAT ||
                  va.type == AGTV_NUMERIC) &&
                 (vb.type == AGTV_INTEGER || vb.type == AGTV_FLOAT ||
                  vb.type == AGTV_NUMERIC)))
            {
                switch (va.type)
                {
                case AGTV_STRING:
                case AGTV_NULL:
                case AGTV_NUMERIC:
                case AGTV_BOOL:
                case AGTV_INTEGER:
                case AGTV_FLOAT:
                case AGTV_EDGE:
                case AGTV_VERTEX:
                case AGTV_PATH:
                    res = compare_agtype_scalar_values(&va, &vb);
                    break;
                case AGTV_ARRAY:

                    /*
                     * This could be a "raw scalar" pseudo array.  That's
                     * a special case here though, since we still want the
                     * general type-based comparisons to apply, and as far
                     * as we're concerned a pseudo array is just a scalar.
                     */
                    if (va.val.array.raw_scalar != vb.val.array.raw_scalar)
                    {
                        if (va.val.array.raw_scalar)
                        {
                            /* advance iterator ita and get contained type */
                            ra = agtype_iterator_next(&ita, &va, false);
                            res = (get_type_sort_priority(va.type) <
                                   get_type_sort_priority(vb.type)) ?
                                      -1 :
                                      1;
                        }
                        else
                        {
                            /* advance iterator itb and get contained type */
                            rb = agtype_iterator_next(&itb, &vb, false);
                            res = (get_type_sort_priority(va.type) <
                                   get_type_sort_priority(vb.type)) ?
                                      -1 :
                                      1;
                        }
                    }
                    break;
                case AGTV_OBJECT:
                    break;
                case AGTV_BINARY:
                    ereport(ERROR, (errmsg("unexpected AGTV_BINARY value")));
                }
            }
            else
            {
                /* Type-defined order */
                res = (get_type_sort_priority(va.type) <
                       get_type_sort_priority(vb.type)) ?
                          -1 :
                          1;
            }
        }
        else
        {
            /*
             * It's safe to assume that the types differed, and that the va
             * and vb values passed were set.
             *
             * If the two values were of the same container type, then there'd
             * have been a chance to observe the variation in the number of
             * elements/pairs (when processing WAGT_BEGIN_OBJECT, say). They're
             * either two heterogeneously-typed containers, or a container and
             * some scalar type.
             */

            /*
             * Check for the premature array or object end.
             * If left side is shorter, less than.
             */
            if (ra == WAGT_END_ARRAY || ra == WAGT_END_OBJECT)
            {
                res = -1;
                /* free the agtype_values associated with the tokens */
                pfree_iterator_agtype_value_token(ra, &va);
                pfree_iterator_agtype_value_token(rb, &vb);
                break;
            }
            /* If right side is shorter, greater than */
            if (rb == WAGT_END_ARRAY || rb == WAGT_END_OBJECT)
            {
                res = 1;
                /* free the agtype_values associated with the tokens */
                pfree_iterator_agtype_value_token(ra, &va);
                pfree_iterator_agtype_value_token(rb, &vb);
                break;
            }

            /* Correction step because AGTV_ARRAY might be there just because of the container type */
            /* Case 1: left side is assigned to an array, right is an object */
            if(va.type == AGTV_ARRAY && vb.type == AGTV_OBJECT)
            {
                ra = agtype_iterator_next(&ita, &va, false);
            }
            /* Case 2: left side is an object, right side is assigned to an array */
            else if(va.type == AGTV_OBJECT && vb.type == AGTV_ARRAY)
            {
                rb = agtype_iterator_next(&itb, &vb, false);
            }

            Assert(va.type != vb.type);
            Assert(va.type != AGTV_BINARY);
            Assert(vb.type != AGTV_BINARY);
            /* Type-defined order */
            res = (get_type_sort_priority(va.type) <
                   get_type_sort_priority(vb.type)) ?
                      -1 :
                      1;
        }
        /* free the agtype_values associated with the tokens */
        pfree_iterator_agtype_value_token(ra, &va);
        pfree_iterator_agtype_value_token(rb, &vb);
    } while (res == 0);

    while (ita != NULL)
    {
        agtype_iterator *i = ita->parent;

        pfree(ita);
        ita = i;
    }
    while (itb != NULL)
    {
        agtype_iterator *i = itb->parent;

        pfree(itb);
        itb = i;
    }

    return res;
}

/*
 * Find value in object (i.e. the "value" part of some key/value pair in an
 * object), or find a matching element if we're looking through an array.  Do
 * so on the basis of equality of the object keys only, or alternatively
 * element values only, with a caller-supplied value "key".  The "flags"
 * argument allows the caller to specify which container types are of interest.
 *
 * This exported utility function exists to facilitate various cases concerned
 * with "containment".  If asked to look through an object, the caller had
 * better pass an agtype String, because their keys can only be strings.
 * Otherwise, for an array, any type of agtype_value will do.
 *
 * In order to proceed with the search, it is necessary for callers to have
 * both specified an interest in exactly one particular container type with an
 * appropriate flag, as well as having the pointed-to agtype container be of
 * one of those same container types at the top level. (Actually, we just do
 * whichever makes sense to save callers the trouble of figuring it out - at
 * most one can make sense, because the container either points to an array
 * (possibly a "raw scalar" pseudo array) or an object.)
 *
 * Note that we can return an AGTV_BINARY agtype_value if this is called on an
 * object, but we never do so on an array.  If the caller asks to look through
 * a container type that is not of the type pointed to by the container,
 * immediately fall through and return NULL.  If we cannot find the value,
 * return NULL.  Otherwise, return palloc()'d copy of value.
 */
agtype_value *find_agtype_value_from_container(agtype_container *container,
                                               uint32 flags, agtype_value *key)
{
    agtentry *children = container->children;
    int count = AGTYPE_CONTAINER_SIZE(container);
    agtype_value *result;

    Assert((flags & ~(AGT_FARRAY | AGT_FOBJECT)) == 0);

    /* Quick out without a palloc cycle if object/array is empty */
    if (count <= 0)
    {
        return NULL;
    }

    result = palloc(sizeof(agtype_value));

    if ((flags & AGT_FARRAY) && AGTYPE_CONTAINER_IS_ARRAY(container))
    {
        char *base_addr = (char *)(children + count);
        uint32 offset = 0;
        int i;

        for (i = 0; i < count; i++)
        {
            fill_agtype_value(container, i, base_addr, offset, result);

            if (key->type == result->type)
            {
                if (equals_agtype_scalar_value(key, result))
                    return result;
            }

            AGTE_ADVANCE_OFFSET(offset, children[i]);
        }
    }
    else if ((flags & AGT_FOBJECT) && AGTYPE_CONTAINER_IS_OBJECT(container))
    {
        /* Since this is an object, account for *Pairs* of AGTentrys */
        char *base_addr = (char *)(children + count * 2);
        uint32 stop_low = 0;
        uint32 stop_high = count;

        /* Object key passed by caller must be a string */
        Assert(key->type == AGTV_STRING);

        /* Binary search on object/pair keys *only* */
        while (stop_low < stop_high)
        {
            uint32 stop_middle;
            int difference;
            agtype_value candidate;

            stop_middle = stop_low + (stop_high - stop_low) / 2;

            candidate.type = AGTV_STRING;
            candidate.val.string.val =
                base_addr + get_agtype_offset(container, stop_middle);
            candidate.val.string.len = get_agtype_length(container,
                                                         stop_middle);

            difference = length_compare_agtype_string_value(&candidate, key);

            if (difference == 0)
            {
                /* Found our key, return corresponding value */
                int index = stop_middle + count;

                fill_agtype_value(container, index, base_addr,
                                  get_agtype_offset(container, index), result);

                return result;
            }
            else
            {
                if (difference < 0)
                    stop_low = stop_middle + 1;
                else
                    stop_high = stop_middle;
            }
        }
    }

    /* Not found */
    pfree(result);
    return NULL;
}

/*
 * Get i-th value of an agtype array.
 *
 * Returns palloc()'d copy of the value, or NULL if it does not exist.
 */
agtype_value *get_ith_agtype_value_from_container(agtype_container *container,
                                                  uint32 i)
{
    agtype_value *result;
    char *base_addr;
    uint32 nelements;

    if (!AGTYPE_CONTAINER_IS_ARRAY(container))
        ereport(ERROR, (errmsg("container is not an agtype array")));

    nelements = AGTYPE_CONTAINER_SIZE(container);
    base_addr = (char *)&container->children[nelements];

    if (i >= nelements)
        return NULL;

    result = palloc(sizeof(agtype_value));

    fill_agtype_value(container, i, base_addr, get_agtype_offset(container, i),
                      result);

    return result;
}

/*
 * A helper function to fill in an agtype_value to represent an element of an
 * array, or a key or value of an object.
 *
 * The node's agtentry is at container->children[index], and its variable-length
 * data is at base_addr + offset.  We make the caller determine the offset
 * since in many cases the caller can amortize that work across multiple
 * children.  When it can't, it can just call get_agtype_offset().
 *
 * A nested array or object will be returned as AGTV_BINARY, ie. it won't be
 * expanded.
 */
static void fill_agtype_value(agtype_container *container, int index,
                              char *base_addr, uint32 offset,
                              agtype_value *result)
{
    agtentry entry = container->children[index];

    if (AGTE_IS_NULL(entry))
    {
        result->type = AGTV_NULL;
    }
    else if (AGTE_IS_STRING(entry))
    {
        char *string_val;
        int string_len;

        result->type = AGTV_STRING;
        /* get the position and length of the string */
        string_val = base_addr + offset;
        string_len = get_agtype_length(container, index);
        /* we need to do a deep copy of the string value */
        result->val.string.val = pnstrdup(string_val, string_len);
        result->val.string.len = string_len;
        Assert(result->val.string.len >= 0);
    }
    else if (AGTE_IS_NUMERIC(entry))
    {
        Numeric numeric;
        Numeric numeric_copy;

        result->type = AGTV_NUMERIC;
        /* we need to do a deep copy here */
        numeric = (Numeric)(base_addr + INTALIGN(offset));
        numeric_copy = (Numeric) palloc(VARSIZE(numeric));
        memcpy(numeric_copy, numeric, VARSIZE(numeric));
        result->val.numeric = numeric_copy;
    }
    /*
     * If this is an agtype.
     * This is needed because we allow the original jsonb type to be
     * passed in.
     */
    else if (AGTE_IS_AGTYPE(entry))
    {
        ag_deserialize_extended_type(base_addr, offset, result);
    }
    else if (AGTE_IS_BOOL_TRUE(entry))
    {
        result->type = AGTV_BOOL;
        result->val.boolean = true;
    }
    else if (AGTE_IS_BOOL_FALSE(entry))
    {
        result->type = AGTV_BOOL;
        result->val.boolean = false;
    }
    else
    {
        Assert(AGTE_IS_CONTAINER(entry));
        result->type = AGTV_BINARY;
        /* Remove alignment padding from data pointer and length */
        result->val.binary.data =
            (agtype_container *)(base_addr + INTALIGN(offset));
        result->val.binary.len = get_agtype_length(container, index) -
                                 (INTALIGN(offset) - offset);
    }
}

/*
 * Push agtype_value into agtype_parse_state.
 *
 * Used when parsing agtype tokens to form agtype, or when converting an
 * in-memory agtype_value to an agtype.
 *
 * Initial state of *agtype_parse_state is NULL, since it'll be allocated here
 * originally (caller will get agtype_parse_state back by reference).
 *
 * Only sequential tokens pertaining to non-container types should pass an
 * agtype_value.  There is one exception -- WAGT_BEGIN_ARRAY callers may pass a
 * "raw scalar" pseudo array to append it - the actual scalar should be passed
 * next and it will be added as the only member of the array.
 *
 * Values of type AGTV_BINARY, which are rolled up arrays and objects,
 * are unpacked before being added to the result.
 */
agtype_value *push_agtype_value(agtype_parse_state **pstate,
                                agtype_iterator_token seq,
                                agtype_value *agtval)
{
    agtype_iterator *it;
    agtype_value *res = NULL;
    agtype_value v;
    agtype_iterator_token tok;

    if (!agtval || (seq != WAGT_ELEM && seq != WAGT_VALUE) ||
        agtval->type != AGTV_BINARY)
    {
        /* drop through */
        return push_agtype_value_scalar(pstate, seq, agtval);
    }

    /* unpack the binary and add each piece to the pstate */
    it = agtype_iterator_init(agtval->val.binary.data);
    while ((tok = agtype_iterator_next(&it, &v, false)) != WAGT_DONE)
    {
        res = push_agtype_value_scalar(pstate, tok,
                                       tok < WAGT_BEGIN_ARRAY ? &v : NULL);
    }

    return res;
}

/*
 * Do the actual pushing, with only scalar or pseudo-scalar-array values
 * accepted.
 */
static agtype_value *push_agtype_value_scalar(agtype_parse_state **pstate,
                                              agtype_iterator_token seq,
                                              agtype_value *scalar_val)
{
    agtype_value *result = NULL;

    switch (seq)
    {
    case WAGT_BEGIN_ARRAY:
        Assert(!scalar_val || scalar_val->val.array.raw_scalar);
        *pstate = push_state(pstate);
        result = &(*pstate)->cont_val;
        (*pstate)->cont_val.type = AGTV_ARRAY;
        (*pstate)->cont_val.val.array.num_elems = 0;
        (*pstate)->cont_val.val.array.raw_scalar =
            (scalar_val && scalar_val->val.array.raw_scalar);
        if (scalar_val && scalar_val->val.array.num_elems > 0)
        {
            /* Assume that this array is still really a scalar */
            Assert(scalar_val->type == AGTV_ARRAY);
            (*pstate)->size = scalar_val->val.array.num_elems;
        }
        else
        {
            (*pstate)->size = 4;
        }
        (*pstate)->cont_val.val.array.elems =
            palloc(sizeof(agtype_value) * (*pstate)->size);
        (*pstate)->last_updated_value = NULL;
        break;
    case WAGT_BEGIN_OBJECT:
        Assert(!scalar_val);
        *pstate = push_state(pstate);
        result = &(*pstate)->cont_val;
        (*pstate)->cont_val.type = AGTV_OBJECT;
        (*pstate)->cont_val.val.object.num_pairs = 0;
        (*pstate)->size = 4;
        (*pstate)->cont_val.val.object.pairs =
            palloc(sizeof(agtype_pair) * (*pstate)->size);
        (*pstate)->last_updated_value = NULL;
        break;
    case WAGT_KEY:
        Assert(scalar_val->type == AGTV_STRING);
        append_key(*pstate, scalar_val);
        break;
    case WAGT_VALUE:
        Assert(IS_A_AGTYPE_SCALAR(scalar_val));
        append_value(*pstate, scalar_val);
        break;
    case WAGT_ELEM:
        Assert(IS_A_AGTYPE_SCALAR(scalar_val));
        append_element(*pstate, scalar_val);
        break;
    case WAGT_END_OBJECT:
        uniqueify_agtype_object(&(*pstate)->cont_val);
        /* fall through! */
    case WAGT_END_ARRAY:
        /* Steps here common to WAGT_END_OBJECT case */
        Assert(!scalar_val);
        result = &(*pstate)->cont_val;

        /*
         * Pop stack and push current array/object as value in parent
         * array/object
         */
        *pstate = (*pstate)->next;
        if (*pstate)
        {
            switch ((*pstate)->cont_val.type)
            {
            case AGTV_ARRAY:
                append_element(*pstate, result);
                break;
            case AGTV_OBJECT:
                append_value(*pstate, result);
                break;
            default:
                ereport(ERROR, (errmsg("invalid agtype container type %d",
                                       (*pstate)->cont_val.type)));
            }
        }
        break;
    default:
        ereport(ERROR,
                (errmsg("unrecognized agtype sequential processing token")));
    }

    return result;
}

/*
 * push_agtype_value() worker:  Iteration-like forming of agtype
 */
static agtype_parse_state *push_state(agtype_parse_state **pstate)
{
    agtype_parse_state *ns = palloc(sizeof(agtype_parse_state));

    ns->next = *pstate;
    return ns;
}

/*
 * push_agtype_value() worker:  Append a pair key to state when generating
 *                              agtype
 */
static void append_key(agtype_parse_state *pstate, agtype_value *string)
{
    agtype_value *object = &pstate->cont_val;

    Assert(object->type == AGTV_OBJECT);
    Assert(string->type == AGTV_STRING);

    if (object->val.object.num_pairs >= AGTYPE_MAX_PAIRS)
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "number of agtype object pairs exceeds the maximum allowed (%zu)",
                 AGTYPE_MAX_PAIRS)));

    if (object->val.object.num_pairs >= pstate->size)
    {
        pstate->size *= 2;
        object->val.object.pairs = repalloc(
            object->val.object.pairs, sizeof(agtype_pair) * pstate->size);
    }

    object->val.object.pairs[object->val.object.num_pairs].key = *string;
    object->val.object.pairs[object->val.object.num_pairs].order =
        object->val.object.num_pairs;
}

/*
 * push_agtype_value() worker:  Append a pair value to state when generating an
 *                              agtype
 */
static void append_value(agtype_parse_state *pstate, agtype_value *scalar_val)
{
    agtype_value *object = &pstate->cont_val;

    Assert(object->type == AGTV_OBJECT);

    object->val.object.pairs[object->val.object.num_pairs].value = *scalar_val;

    pstate->last_updated_value =
        &object->val.object.pairs[object->val.object.num_pairs++].value;
}

/*
 * push_agtype_value() worker:  Append an element to state when generating an
 *                              agtype
 */
static void append_element(agtype_parse_state *pstate,
                           agtype_value *scalar_val)
{
    agtype_value *array = &pstate->cont_val;

    Assert(array->type == AGTV_ARRAY);

    if (array->val.array.num_elems >= AGTYPE_MAX_ELEMS)
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "number of agtype array elements exceeds the maximum allowed (%zu)",
                 AGTYPE_MAX_ELEMS)));

    if (array->val.array.num_elems >= pstate->size)
    {
        pstate->size *= 2;
        array->val.array.elems = repalloc(array->val.array.elems,
                                          sizeof(agtype_value) * pstate->size);
    }

    array->val.array.elems[array->val.array.num_elems] = *scalar_val;
    pstate->last_updated_value =
        &array->val.array.elems[array->val.array.num_elems++];
}

/*
 * Given an agtype_container, expand to agtype_iterator to iterate over items
 * fully expanded to in-memory representation for manipulation.
 *
 * See agtype_iterator_next() for notes on memory management.
 */
agtype_iterator *agtype_iterator_init(agtype_container *container)
{
    return iterator_from_container(container, NULL);
}

/*
 * Get next agtype_value while iterating
 *
 * Caller should initially pass their own, original iterator.  They may get
 * back a child iterator palloc()'d here instead.  The function can be relied
 * on to free those child iterators, lest the memory allocated for highly
 * nested objects become unreasonable, but only if callers don't end iteration
 * early (by breaking upon having found something in a search, for example).
 *
 * Callers in such a scenario, that are particularly sensitive to leaking
 * memory in a long-lived context may walk the ancestral tree from the final
 * iterator we left them with to its oldest ancestor, pfree()ing as they go.
 * They do not have to free any other memory previously allocated for iterators
 * but not accessible as direct ancestors of the iterator they're last passed
 * back.
 *
 * Returns "agtype sequential processing" token value.  Iterator "state"
 * reflects the current stage of the process in a less granular fashion, and is
 * mostly used here to track things internally with respect to particular
 * iterators.
 *
 * Clients of this function should not have to handle any AGTV_BINARY values
 * (since recursive calls will deal with this), provided skip_nested is false.
 * It is our job to expand the AGTV_BINARY representation without bothering
 * them with it.  However, clients should not take it upon themselves to touch
 * array or Object element/pair buffers, since their element/pair pointers are
 * garbage.  Also, *val will not be set when returning WAGT_END_ARRAY or
 * WAGT_END_OBJECT, on the assumption that it's only useful to access values
 * when recursing in.
 */
agtype_iterator_token agtype_iterator_next(agtype_iterator **it,
                                           agtype_value *val, bool skip_nested)
{
    if (*it == NULL)
        return WAGT_DONE;

    /*
     * When stepping into a nested container, we jump back here to start
     * processing the child. We will not recurse further in one call, because
     * processing the child will always begin in AGTI_ARRAY_START or
     * AGTI_OBJECT_START state.
     */
recurse:
    switch ((*it)->state)
    {
    case AGTI_ARRAY_START:
        /* Set v to array on first array call */
        val->type = AGTV_ARRAY;
        val->val.array.num_elems = (*it)->num_elems;

        /*
         * v->val.array.elems is not actually set, because we aren't doing
         * a full conversion
         */
        val->val.array.raw_scalar = (*it)->is_scalar;
        (*it)->curr_index = 0;
        (*it)->curr_data_offset = 0;
        (*it)->curr_value_offset = 0; /* not actually used */
        /* Set state for next call */
        (*it)->state = AGTI_ARRAY_ELEM;
        return WAGT_BEGIN_ARRAY;

    case AGTI_ARRAY_ELEM:
        if ((*it)->curr_index >= (*it)->num_elems)
        {
            /*
             * All elements within array already processed.  Report this
             * to caller, and give it back original parent iterator (which
             * independently tracks iteration progress at its level of
             * nesting).
             */
            *it = free_and_get_parent(*it);
            return WAGT_END_ARRAY;
        }

        fill_agtype_value((*it)->container, (*it)->curr_index,
                          (*it)->data_proper, (*it)->curr_data_offset, val);

        AGTE_ADVANCE_OFFSET((*it)->curr_data_offset,
                            (*it)->children[(*it)->curr_index]);
        (*it)->curr_index++;

        if (!IS_A_AGTYPE_SCALAR(val) && !skip_nested)
        {
            /* Recurse into container. */
            *it = iterator_from_container(val->val.binary.data, *it);
            goto recurse;
        }
        else
        {
            /*
             * Scalar item in array, or a container and caller didn't want
             * us to recurse into it.
             */
            return WAGT_ELEM;
        }

    case AGTI_OBJECT_START:
        /* Set v to object on first object call */
        val->type = AGTV_OBJECT;
        val->val.object.num_pairs = (*it)->num_elems;

        /*
         * v->val.object.pairs is not actually set, because we aren't
         * doing a full conversion
         */
        (*it)->curr_index = 0;
        (*it)->curr_data_offset = 0;
        (*it)->curr_value_offset = get_agtype_offset((*it)->container,
                                                     (*it)->num_elems);
        /* Set state for next call */
        (*it)->state = AGTI_OBJECT_KEY;
        return WAGT_BEGIN_OBJECT;

    case AGTI_OBJECT_KEY:
        if ((*it)->curr_index >= (*it)->num_elems)
        {
            /*
             * All pairs within object already processed.  Report this to
             * caller, and give it back original containing iterator
             * (which independently tracks iteration progress at its level
             * of nesting).
             */
            *it = free_and_get_parent(*it);
            return WAGT_END_OBJECT;
        }
        else
        {
            /* Return key of a key/value pair.  */
            fill_agtype_value((*it)->container, (*it)->curr_index,
                              (*it)->data_proper, (*it)->curr_data_offset,
                              val);
            if (val->type != AGTV_STRING)
                ereport(ERROR,
                        (errmsg("unexpected agtype type as object key %d",
                                val->type)));

            /* Set state for next call */
            (*it)->state = AGTI_OBJECT_VALUE;
            return WAGT_KEY;
        }

    case AGTI_OBJECT_VALUE:
        /* Set state for next call */
        (*it)->state = AGTI_OBJECT_KEY;

        fill_agtype_value((*it)->container,
                          (*it)->curr_index + (*it)->num_elems,
                          (*it)->data_proper, (*it)->curr_value_offset, val);

        AGTE_ADVANCE_OFFSET((*it)->curr_data_offset,
                            (*it)->children[(*it)->curr_index]);
        AGTE_ADVANCE_OFFSET(
            (*it)->curr_value_offset,
            (*it)->children[(*it)->curr_index + (*it)->num_elems]);
        (*it)->curr_index++;

        /*
         * Value may be a container, in which case we recurse with new,
         * child iterator (unless the caller asked not to, by passing
         * skip_nested).
         */
        if (!IS_A_AGTYPE_SCALAR(val) && !skip_nested)
        {
            *it = iterator_from_container(val->val.binary.data, *it);
            goto recurse;
        }
        else
        {
            return WAGT_VALUE;
        }
    }

    ereport(ERROR, (errmsg("invalid iterator state %d", (*it)->state)));
    return -1;
}

/*
 * Initialize an iterator for iterating all elements in a container.
 */
static agtype_iterator *iterator_from_container(agtype_container *container,
                                                agtype_iterator *parent)
{
    agtype_iterator *it;

    it = palloc0(sizeof(agtype_iterator));
    it->container = container;
    it->parent = parent;
    it->num_elems = AGTYPE_CONTAINER_SIZE(container);

    /* Array starts just after header */
    it->children = container->children;

    switch (container->header & (AGT_FARRAY | AGT_FOBJECT))
    {
    case AGT_FARRAY:
        it->data_proper = (char *)it->children +
                          it->num_elems * sizeof(agtentry);
        it->is_scalar = AGTYPE_CONTAINER_IS_SCALAR(container);
        /* This is either a "raw scalar", or an array */
        Assert(!it->is_scalar || it->num_elems == 1);

        it->state = AGTI_ARRAY_START;
        break;

    case AGT_FOBJECT:
        it->data_proper = (char *)it->children +
                          it->num_elems * sizeof(agtentry) * 2;
        it->state = AGTI_OBJECT_START;
        break;

    default:
        ereport(ERROR,
                (errmsg("unknown type of agtype container %d",
                        container->header & (AGT_FARRAY | AGT_FOBJECT))));
    }

    return it;
}

/*
 * agtype_iterator_next() worker: Return parent, while freeing memory for
 *                                current iterator
 */
static agtype_iterator *free_and_get_parent(agtype_iterator *it)
{
    agtype_iterator *v = it->parent;

    pfree(it);
    return v;
}

/*
 * Worker for "contains" operator's function
 *
 * Formally speaking, containment is top-down, unordered subtree isomorphism.
 *
 * Takes iterators that belong to some container type.  These iterators
 * "belong" to those values in the sense that they've just been initialized in
 * respect of them by the caller (perhaps in a nested fashion).
 *
 * "val" is lhs agtype, and m_contained is rhs agtype when called from top
 * level. We determine if m_contained is contained within val.
 */
bool agtype_deep_contains(agtype_iterator **val,
                          agtype_iterator **m_contained,
                          bool skip_nested)
{
    agtype_value vval;
    agtype_value vcontained;
    agtype_iterator_token rval;
    agtype_iterator_token rcont;

    /*
     * Guard against stack overflow due to overly complex agtype.
     *
     * Functions called here independently take this precaution, but that
     * might not be sufficient since this is also a recursive function.
     */
    check_stack_depth();

    rval = agtype_iterator_next(val, &vval, false);
    rcont = agtype_iterator_next(m_contained, &vcontained, false);

    if (rval != rcont)
    {
        /*
         * The differing return values can immediately be taken as indicating
         * two differing container types at this nesting level, which is
         * sufficient reason to give up entirely (but it should be the case
         * that they're both some container type).
         */
        Assert(rval == WAGT_BEGIN_OBJECT || rval == WAGT_BEGIN_ARRAY);
        Assert(rcont == WAGT_BEGIN_OBJECT || rcont == WAGT_BEGIN_ARRAY);
        return false;
    }
    else if (rcont == WAGT_BEGIN_OBJECT)
    {
        Assert(vval.type == AGTV_OBJECT);
        Assert(vcontained.type == AGTV_OBJECT);

        /*
         * If the lhs has fewer pairs than the rhs, it can't possibly contain
         * the rhs.  (This conclusion is safe only because we de-duplicate
         * keys in all agtype objects; thus there can be no corresponding
         * optimization in the array case.)  The case probably won't arise
         * often, but since it's such a cheap check we may as well make it.
         */
        if (vval.val.object.num_pairs < vcontained.val.object.num_pairs)
            return false;

        /* Work through rhs "is it contained within?" object */
        for (;;)
        {
            agtype_value *lhs_val; /* lhs_val is from pair in lhs object */

            rcont = agtype_iterator_next(m_contained, &vcontained, false);

            /*
             * When we get through caller's rhs "is it contained within?"
             * object without failing to find one of its values, it's
             * contained.
             */
            if (rcont == WAGT_END_OBJECT)
                return true;

            Assert(rcont == WAGT_KEY);

            /* First, find value by key... */
            lhs_val = find_agtype_value_from_container(
                (*val)->container, AGT_FOBJECT, &vcontained);

            if (!lhs_val)
                return false;

            /*
             * ...at this stage it is apparent that there is at least a key
             * match for this rhs pair.
             */
            rcont = agtype_iterator_next(m_contained, &vcontained, true);

            Assert(rcont == WAGT_VALUE);

            /*
             * Compare rhs pair's value with lhs pair's value just found using
             * key
             */
            if (lhs_val->type != vcontained.type)
            {
                return false;
            }
            else if (IS_A_AGTYPE_SCALAR(lhs_val))
            {
                if (!equals_agtype_scalar_value(lhs_val, &vcontained))
                    return false;
            }
            else if (skip_nested)
            {
                Assert(lhs_val->type == AGTV_BINARY);
                Assert(vcontained.type == AGTV_BINARY);

                /* We will just check if the rhs value is equal to lhs */
                if (compare_agtype_containers_orderability(
                                             lhs_val->val.binary.data,
                                             vcontained.val.binary.data) != 0)
                {
                    return false;
                }
            }
            else
            {
                /* Nested container value (object or array) */
                agtype_iterator *nestval;
                agtype_iterator *nest_contained;

                Assert(lhs_val->type == AGTV_BINARY);
                Assert(vcontained.type == AGTV_BINARY);

                nestval = agtype_iterator_init(lhs_val->val.binary.data);
                nest_contained =
                    agtype_iterator_init(vcontained.val.binary.data);

                /*
                 * Match "value" side of rhs datum object's pair recursively.
                 * It's a nested structure.
                 *
                 * Note that nesting still has to "match up" at the right
                 * nesting sub-levels.  However, there need only be zero or
                 * more matching pairs (or elements) at each nesting level
                 * (provided the *rhs* pairs/elements *all* match on each
                 * level), which enables searching nested structures for a
                 * single String or other primitive type sub-datum quite
                 * effectively (provided the user constructed the rhs nested
                 * structure such that we "know where to look").
                 *
                 * In other words, the mapping of container nodes in the rhs
                 * "vcontained" agtype to internal nodes on the lhs is
                 * injective, and parent-child edges on the rhs must be mapped
                 * to parent-child edges on the lhs to satisfy the condition
                 * of containment (plus of course the mapped nodes must be
                 * equal).
                 */
                if (!agtype_deep_contains(&nestval, &nest_contained, false))
                {
                    return false;
                }
            }
        }
    }
    else if (rcont == WAGT_BEGIN_ARRAY)
    {
        agtype_value *lhs_conts = NULL;
        uint32 num_lhs_elems = vval.val.array.num_elems;

        Assert(vval.type == AGTV_ARRAY);
        Assert(vcontained.type == AGTV_ARRAY);

        /*
         * Handle distinction between "raw scalar" pseudo arrays, and real
         * arrays.
         *
         * A raw scalar may contain another raw scalar, and an array may
         * contain a raw scalar, but a raw scalar may not contain an array. We
         * don't do something like this for the object case, since objects can
         * only contain pairs, never raw scalars (a pair is represented by an
         * rhs object argument with a single contained pair).
         */
        if (vval.val.array.raw_scalar && !vcontained.val.array.raw_scalar)
            return false;

        /* Work through rhs "is it contained within?" array */
        for (;;)
        {
            rcont = agtype_iterator_next(m_contained, &vcontained, true);

            /*
             * When we get through caller's rhs "is it contained within?"
             * array without failing to find one of its values, it's
             * contained.
             */
            if (rcont == WAGT_END_ARRAY)
                return true;

            Assert(rcont == WAGT_ELEM);

            if (IS_A_AGTYPE_SCALAR(&vcontained))
            {
                if (!find_agtype_value_from_container((*val)->container,
                                                      AGT_FARRAY, &vcontained))
                    return false;
            }
            else
            {
                uint32 i;

                /*
                 * If this is first container found in rhs array (at this
                 * depth), initialize temp lhs array of containers
                 */
                if (lhs_conts == NULL)
                {
                    uint32 j = 0;

                    /* Make room for all possible values */
                    lhs_conts = palloc(sizeof(agtype_value) * num_lhs_elems);

                    for (i = 0; i < num_lhs_elems; i++)
                    {
                        /* Store all lhs elements in temp array */
                        rcont = agtype_iterator_next(val, &vval, true);
                        Assert(rcont == WAGT_ELEM);

                        if (vval.type == AGTV_BINARY)
                            lhs_conts[j++] = vval;
                    }

                    /* No container elements in temp array, so give up now */
                    if (j == 0)
                        return false;

                    /* We may have only partially filled array */
                    num_lhs_elems = j;
                }

                /* XXX: Nested array containment is O(N^2) */
                for (i = 0; i < num_lhs_elems; i++)
                {
                    /* Nested container value (object or array) */
                    agtype_iterator *nestval;
                    agtype_iterator *nest_contained;
                    bool contains;

                    nestval =
                        agtype_iterator_init(lhs_conts[i].val.binary.data);
                    nest_contained =
                        agtype_iterator_init(vcontained.val.binary.data);

                    contains = agtype_deep_contains(&nestval, &nest_contained, false);

                    if (nestval)
                        pfree(nestval);
                    if (nest_contained)
                        pfree(nest_contained);
                    if (contains)
                        break;
                }

                /*
                 * Report rhs container value is not contained if couldn't
                 * match rhs container to *some* lhs cont
                 */
                if (i == num_lhs_elems)
                    return false;
            }
        }
    }
    else
    {
        ereport(ERROR, (errmsg("invalid agtype container type")));
    }

    ereport(ERROR, (errmsg("unexpectedly fell off end of agtype container")));
    return false;
}

/*
 * Hash an agtype_value scalar value, mixing the hash value into an existing
 * hash provided by the caller.
 *
 * Some callers may wish to independently XOR in AGT_FOBJECT and AGT_FARRAY
 * flags.
 */
void agtype_hash_scalar_value(const agtype_value *scalar_val, uint32 *hash)
{
    uint32 tmp;

    /* Compute hash value for scalar_val */
    switch (scalar_val->type)
    {
    case AGTV_NULL:
        tmp = 0x01;
        break;
    case AGTV_STRING:
        tmp = DatumGetUInt32(
            hash_any((const unsigned char *)scalar_val->val.string.val,
                     scalar_val->val.string.len));
        break;
    case AGTV_NUMERIC:
        /* Must hash equal numerics to equal hash codes */
        tmp = DatumGetUInt32(DirectFunctionCall1(
            hash_numeric, NumericGetDatum(scalar_val->val.numeric)));
        break;
    case AGTV_BOOL:
        tmp = scalar_val->val.boolean ? 0x02 : 0x04;
        break;
    case AGTV_INTEGER:
        tmp = DatumGetUInt32(DirectFunctionCall1(
            hashint8, Int64GetDatum(scalar_val->val.int_value)));
        break;
    case AGTV_FLOAT:
        tmp = DatumGetUInt32(DirectFunctionCall1(
            hashfloat8, Float8GetDatum(scalar_val->val.float_value)));
        break;
    default:
        ereport(ERROR, (errmsg("invalid agtype scalar type %d to compute hash",
                               scalar_val->type)));
        tmp = 0; /* keep compiler quiet */
        break;
    }

    /*
     * Combine hash values of successive keys, values and elements by rotating
     * the previous value left 1 bit, then XOR'ing in the new
     * key/value/element's hash value.
     */
    *hash = (*hash << 1) | (*hash >> 31);
    *hash ^= tmp;
}

/*
 * Hash a value to a 64-bit value, with a seed. Otherwise, similar to
 * agtype_hash_scalar_value.
 */
void agtype_hash_scalar_value_extended(const agtype_value *scalar_val,
                                       uint64 *hash, uint64 seed)
{
    uint64 tmp = 0;

    switch (scalar_val->type)
    {
    case AGTV_NULL:
        tmp = seed + 0x01;
        break;
    case AGTV_STRING:
        tmp = DatumGetUInt64(hash_any_extended(
            (const unsigned char *)scalar_val->val.string.val,
            scalar_val->val.string.len, seed));
        break;
    case AGTV_NUMERIC:
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hash_numeric_extended, NumericGetDatum(scalar_val->val.numeric),
            UInt64GetDatum(seed)));
        break;
    case AGTV_BOOL:
        if (seed)
        {
            tmp = DatumGetUInt64(DirectFunctionCall2(
                hashcharextended, BoolGetDatum(scalar_val->val.boolean),
                UInt64GetDatum(seed)));
        }
        else
        {
            tmp = scalar_val->val.boolean ? 0x02 : 0x04;
        }
        break;
    case AGTV_INTEGER:
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hashint8extended, Int64GetDatum(scalar_val->val.int_value),
            UInt64GetDatum(seed)));
        break;
    case AGTV_FLOAT:
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hashfloat8extended, Float8GetDatum(scalar_val->val.float_value),
            UInt64GetDatum(seed)));
        break;
    case AGTV_VERTEX:
    {
        graphid id;
        agtype_value *id_agt = GET_AGTYPE_VALUE_OBJECT_VALUE(scalar_val, "id");
        id = id_agt->val.int_value;
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hashint8extended, Float8GetDatum(id), UInt64GetDatum(seed)));
        break;
    }
    case AGTV_EDGE:
    {
        graphid id;
        agtype_value *id_agt = GET_AGTYPE_VALUE_OBJECT_VALUE(scalar_val, "id");
        id = id_agt->val.int_value;
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hashint8extended, Float8GetDatum(id), UInt64GetDatum(seed)));
        break;
    }
    case AGTV_PATH:
    {
        int i;
        for (i = 0; i < scalar_val->val.array.num_elems; i++)
        {
            agtype_value v;
            v = scalar_val->val.array.elems[i];
            agtype_hash_scalar_value_extended(&v, &tmp, seed);
        }
        break;
    }
    default:
        ereport(
            ERROR,
            (errmsg("invalid agtype scalar type %d to compute hash extended",
                    scalar_val->type)));
        break;
    }

    *hash = ROTATE_HIGH_AND_LOW_32BITS(*hash);
    *hash ^= tmp;
}

/*
 * Function to compare two floats, obviously. However, there are a few
 * special cases that we need to cover with regards to NaN and +/-Infinity.
 * NaN is not equal to any other number, including itself. However, for
 * ordering, we need to allow NaN = NaN and NaN > any number including
 * positive infinity -
 *
 *     -Infinity < any number < +Infinity < NaN
 *
 * Note: This is copied from float8_cmp_internal.
 * Note: Special float values can cause exceptions, hence the order of the
 *       comparisons.
 */
static int compare_two_floats_orderability(float8 lhs, float8 rhs)
{
    /*
     * We consider all NANs to be equal and larger than any non-NAN. This is
     * somewhat arbitrary; the important thing is to have a consistent sort
     * order.
     */
    if (isnan(lhs))
    {
        if (isnan(rhs))
            return 0; /* NAN = NAN */
        else
            return 1; /* NAN > non-NAN */
    }
    else if (isnan(rhs))
    {
        return -1; /* non-NAN < NAN */
    }
    else
    {
        if (lhs > rhs)
            return 1;
        else if (lhs < rhs)
            return -1;
        else
            return 0;
    }
}

/*
 * Are two scalar agtype_values of the same type a and b equal?
 */
static bool equals_agtype_scalar_value(agtype_value *a, agtype_value *b)
{
    /* if the values are of the same type */
    if (a->type == b->type)
    {
        switch (a->type)
        {
        case AGTV_NULL:
            return true;
        case AGTV_STRING:
            return length_compare_agtype_string_value(a, b) == 0;
        case AGTV_NUMERIC:
            return DatumGetBool(DirectFunctionCall2(
                numeric_eq, PointerGetDatum(a->val.numeric),
                PointerGetDatum(b->val.numeric)));
        case AGTV_BOOL:
            return a->val.boolean == b->val.boolean;
        case AGTV_INTEGER:
            return a->val.int_value == b->val.int_value;
        case AGTV_FLOAT:
            return a->val.float_value == b->val.float_value;
        case AGTV_VERTEX:
        {
            graphid a_graphid, b_graphid;
            a_graphid = a->val.object.pairs[0].value.val.int_value;
            b_graphid = b->val.object.pairs[0].value.val.int_value;

            return a_graphid == b_graphid;
        }

        default:
            ereport(ERROR, (errmsg("invalid agtype scalar type %d for equals",
                                   a->type)));
        }
    }
    /* otherwise, the values are of differing type */
    else
        ereport(ERROR, (errmsg("agtype input scalars must be of same type")));

    /* execution will never reach this point due to the ereport call */
    return false;
}

/*
 * Compare two scalar agtype_values, returning -1, 0, or 1.
 *
 * Strings are compared using the default collation.  Used by B-tree
 * operators, where a lexical sort order is generally expected.
 */
int compare_agtype_scalar_values(agtype_value *a, agtype_value *b)
{
    if (a->type == b->type)
    {
        switch (a->type)
        {
        case AGTV_NULL:
            return 0;
        case AGTV_STRING:
        {
            /* varstr_cmp isn't guaranteed to return 1, 0, -1 */
            int result = varstr_cmp(a->val.string.val, a->val.string.len,
                                    b->val.string.val, b->val.string.len,
                                    DEFAULT_COLLATION_OID);
            if (result > 0)
            {
                return 1;
            }
            else if (result < 0)
            {
                return -1;
            }
            else
            {
                return 0;
            }
        }
        case AGTV_NUMERIC:
            return DatumGetInt32(DirectFunctionCall2(
                numeric_cmp, PointerGetDatum(a->val.numeric),
                PointerGetDatum(b->val.numeric)));
        case AGTV_BOOL:
            if (a->val.boolean == b->val.boolean)
            {
                return 0;
            }
            else if (a->val.boolean > b->val.boolean)
            {
                return 1;
            }
            else
            {
                return -1;
            }
        case AGTV_INTEGER:
            if (a->val.int_value == b->val.int_value)
            {
                return 0;
            }
            else if (a->val.int_value > b->val.int_value)
            {
                return 1;
            }
            else
            {
                return -1;
            }
        case AGTV_FLOAT:
            return compare_two_floats_orderability(a->val.float_value,
                                                   b->val.float_value);
        case AGTV_VERTEX:
        case AGTV_EDGE:
        {
            agtype_value *a_id, *b_id;
            graphid a_graphid, b_graphid;

            a_id = GET_AGTYPE_VALUE_OBJECT_VALUE(a, "id");
            b_id = GET_AGTYPE_VALUE_OBJECT_VALUE(b, "id");

            a_graphid = a_id->val.int_value;
            b_graphid = b_id->val.int_value;

            if (a_graphid == b_graphid)
            {
                return 0;
            }
            else if (a_graphid > b_graphid)
            {
                return 1;
            }
            else
            {
                return -1;
            }
        }
        case AGTV_PATH:
        {
            int i;

            if (a->val.array.num_elems != b->val.array.num_elems)
                return  a->val.array.num_elems > b->val.array.num_elems ? 1 : -1;

            for (i = 0; i < a->val.array.num_elems; i++)
            {
                agtype_value a_elem, b_elem;
                int res;

                a_elem = a->val.array.elems[i];
                b_elem = b->val.array.elems[i];

                res = compare_agtype_scalar_values(&a_elem, &b_elem);

                if (res)
                {
                    return res;
                }
            }

            return 0;
        }
        default:
            ereport(ERROR, (errmsg("invalid agtype scalar type %d for compare",
                                   a->type)));
        }
    }
    /* check for integer compared to float */
    if (a->type == AGTV_INTEGER && b->type == AGTV_FLOAT)
    {
        return compare_two_floats_orderability((float8)a->val.int_value,
                                               b->val.float_value);
    }
    /* check for float compared to integer */
    if (a->type == AGTV_FLOAT && b->type == AGTV_INTEGER)
    {
        return compare_two_floats_orderability(a->val.float_value,
                                               (float8)b->val.int_value);
    }
    /* check for integer or float compared to numeric */
    if (is_numeric_result(a, b))
    {
        Datum numd, lhsd, rhsd;

        lhsd = get_numeric_datum_from_agtype_value(a);
        rhsd = get_numeric_datum_from_agtype_value(b);
        numd = DirectFunctionCall2(numeric_cmp, lhsd, rhsd);

        return DatumGetInt32(numd);
    }

    ereport(ERROR, (errmsg("agtype input scalar type mismatch")));
    return -1;
}

/*
 * Functions for manipulating the resizeable buffer used by convert_agtype and
 * its subroutines.
 */

/*
 * Reserve 'len' bytes, at the end of the buffer, enlarging it if necessary.
 * Returns the offset to the reserved area. The caller is expected to fill
 * the reserved area later with copy_to_buffer().
 */
int reserve_from_buffer(StringInfo buffer, int len)
{
    int offset;

    /* Make more room if needed */
    enlargeStringInfo(buffer, len);

    /* remember current offset */
    offset = buffer->len;

    /* reserve the space */
    buffer->len += len;

    /*
     * Keep a trailing null in place, even though it's not useful for us; it
     * seems best to preserve the invariants of StringInfos.
     */
    buffer->data[buffer->len] = '\0';

    return offset;
}

/*
 * Copy 'len' bytes to a previously reserved area in buffer.
 */
static void copy_to_buffer(StringInfo buffer, int offset, const char *data,
                           int len)
{
    memcpy(buffer->data + offset, data, len);
}

/*
 * A shorthand for reserve_from_buffer + copy_to_buffer.
 */
static void append_to_buffer(StringInfo buffer, const char *data, int len)
{
    int offset;

    offset = reserve_from_buffer(buffer, len);
    copy_to_buffer(buffer, offset, data, len);
}

/*
 * Append padding, so that the length of the StringInfo is int-aligned.
 * Returns the number of padding bytes appended.
 */
short pad_buffer_to_int(StringInfo buffer)
{
    int padlen;
    int p;
    int offset;

    padlen = INTALIGN(buffer->len) - buffer->len;

    offset = reserve_from_buffer(buffer, padlen);

    /* padlen must be small, so this is probably faster than a memset */
    for (p = 0; p < padlen; p++)
        buffer->data[offset + p] = '\0';

    return padlen;
}

/*
 * Given an agtype_value, convert to agtype. The result is palloc'd.
 */
static agtype *convert_to_agtype(agtype_value *val)
{
    StringInfoData buffer;
    agtentry aentry;
    agtype *res;

    /* Should not already have binary representation */
    Assert(val->type != AGTV_BINARY);

    /* Allocate an output buffer. It will be enlarged as needed */
    initStringInfo(&buffer);

    /* Make room for the varlena header */
    reserve_from_buffer(&buffer, VARHDRSZ);

    convert_agtype_value(&buffer, &aentry, val, 0);

    /*
     * Note: the agtentry of the root is discarded. Therefore the root
     * agtype_container struct must contain enough information to tell what
     * kind of value it is.
     */

    res = (agtype *)buffer.data;

    SET_VARSIZE(res, buffer.len);

    return res;
}

/*
 * Subroutine of convert_agtype: serialize a single agtype_value into buffer.
 *
 * The agtentry header for this node is returned in *header.  It is filled in
 * with the length of this value and appropriate type bits.  If we wish to
 * store an end offset rather than a length, it is the caller's responsibility
 * to adjust for that.
 *
 * If the value is an array or an object, this recurses. 'level' is only used
 * for debugging purposes.
 */
static void convert_agtype_value(StringInfo buffer, agtentry *header,
                                 agtype_value *val, int level)
{
    check_stack_depth();

    if (!val)
        return;

    /*
     * An agtype_value passed as val should never have a type of AGTV_BINARY,
     * and neither should any of its sub-components. Those values will be
     * produced by convert_agtype_array and convert_agtype_object, the results
     * of which will not be passed back to this function as an argument.
     */

    if (IS_A_AGTYPE_SCALAR(val))
        convert_agtype_scalar(buffer, header, val);
    else if (val->type == AGTV_ARRAY)
        convert_agtype_array(buffer, header, val, level);
    else if (val->type == AGTV_OBJECT)
        convert_agtype_object(buffer, header, val, level);
    else
        ereport(ERROR,
                (errmsg("unknown agtype type %d to convert", val->type)));
}

static void convert_agtype_array(StringInfo buffer, agtentry *pheader,
                                 agtype_value *val, int level)
{
    int base_offset;
    int agtentry_offset;
    int i;
    int totallen;
    uint32 header;
    int num_elems = val->val.array.num_elems;

    /* Remember where in the buffer this array starts. */
    base_offset = buffer->len;

    /* Align to 4-byte boundary (any padding counts as part of my data) */
    pad_buffer_to_int(buffer);

    /*
     * Construct the header agtentry and store it in the beginning of the
     * variable-length payload.
     */
    header = num_elems | AGT_FARRAY;
    if (val->val.array.raw_scalar)
    {
        Assert(num_elems == 1);
        Assert(level == 0);
        header |= AGT_FSCALAR;
    }

    append_to_buffer(buffer, (char *)&header, sizeof(uint32));

    /* Reserve space for the agtentrys of the elements. */
    agtentry_offset = reserve_from_buffer(buffer,
                                          sizeof(agtentry) * num_elems);

    totallen = 0;
    for (i = 0; i < num_elems; i++)
    {
        agtype_value *elem = &val->val.array.elems[i];
        int len;
        agtentry meta;

        /*
         * Convert element, producing a agtentry and appending its
         * variable-length data to buffer
         */
        convert_agtype_value(buffer, &meta, elem, level + 1);

        len = AGTE_OFFLENFLD(meta);
        totallen += len;

        /*
         * Bail out if total variable-length data exceeds what will fit in a
         * agtentry length field.  We check this in each iteration, not just
         * once at the end, to forestall possible integer overflow.
         */
        if (totallen > AGTENTRY_OFFLENMASK)
        {
            ereport(
                ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg(
                     "total size of agtype array elements exceeds the maximum of %u bytes",
                     AGTENTRY_OFFLENMASK)));
        }

        /*
         * Convert each AGT_OFFSET_STRIDE'th length to an offset.
         */
        if ((i % AGT_OFFSET_STRIDE) == 0)
            meta = (meta & AGTENTRY_TYPEMASK) | totallen | AGTENTRY_HAS_OFF;

        copy_to_buffer(buffer, agtentry_offset, (char *)&meta,
                       sizeof(agtentry));
        agtentry_offset += sizeof(agtentry);
    }

    /* Total data size is everything we've appended to buffer */
    totallen = buffer->len - base_offset;

    /* Check length again, since we didn't include the metadata above */
    if (totallen > AGTENTRY_OFFLENMASK)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "total size of agtype array elements exceeds the maximum of %u bytes",
                 AGTENTRY_OFFLENMASK)));
    }

    /* Initialize the header of this node in the container's agtentry array */
    *pheader = AGTENTRY_IS_CONTAINER | totallen;
}

void convert_extended_array(StringInfo buffer, agtentry *pheader,
                            agtype_value *val)
{
    convert_agtype_array(buffer, pheader, val, 0);
}

void convert_extended_object(StringInfo buffer, agtentry *pheader,
                             agtype_value *val)
{
    convert_agtype_object(buffer, pheader, val, 0);
}

static void convert_agtype_object(StringInfo buffer, agtentry *pheader,
                                  agtype_value *val, int level)
{
    int base_offset;
    int agtentry_offset;
    int i;
    int totallen;
    uint32 header;
    int num_pairs = val->val.object.num_pairs;

    /* Remember where in the buffer this object starts. */
    base_offset = buffer->len;

    /* Align to 4-byte boundary (any padding counts as part of my data) */
    pad_buffer_to_int(buffer);

    /*
     * Construct the header agtentry and store it in the beginning of the
     * variable-length payload.
     */
    header = num_pairs | AGT_FOBJECT;
    append_to_buffer(buffer, (char *)&header, sizeof(uint32));

    /* Reserve space for the agtentrys of the keys and values. */
    agtentry_offset = reserve_from_buffer(buffer,
                                          sizeof(agtentry) * num_pairs * 2);

    /*
     * Iterate over the keys, then over the values, since that is the ordering
     * we want in the on-disk representation.
     */
    totallen = 0;
    for (i = 0; i < num_pairs; i++)
    {
        agtype_pair *pair = &val->val.object.pairs[i];
        int len;
        agtentry meta;

        /*
         * Convert key, producing an agtentry and appending its variable-length
         * data to buffer
         */
        convert_agtype_scalar(buffer, &meta, &pair->key);

        len = AGTE_OFFLENFLD(meta);
        totallen += len;

        /*
         * Bail out if total variable-length data exceeds what will fit in a
         * agtentry length field.  We check this in each iteration, not just
         * once at the end, to forestall possible integer overflow.
         */
        if (totallen > AGTENTRY_OFFLENMASK)
        {
            ereport(
                ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg(
                     "total size of agtype object elements exceeds the maximum of %u bytes",
                     AGTENTRY_OFFLENMASK)));
        }

        /*
         * Convert each AGT_OFFSET_STRIDE'th length to an offset.
         */
        if ((i % AGT_OFFSET_STRIDE) == 0)
            meta = (meta & AGTENTRY_TYPEMASK) | totallen | AGTENTRY_HAS_OFF;

        copy_to_buffer(buffer, agtentry_offset, (char *)&meta,
                       sizeof(agtentry));
        agtentry_offset += sizeof(agtentry);
    }
    for (i = 0; i < num_pairs; i++)
    {
        agtype_pair *pair = &val->val.object.pairs[i];
        int len;
        agtentry meta;

        /*
         * Convert value, producing an agtentry and appending its
         * variable-length data to buffer
         */
        convert_agtype_value(buffer, &meta, &pair->value, level + 1);

        len = AGTE_OFFLENFLD(meta);
        totallen += len;

        /*
         * Bail out if total variable-length data exceeds what will fit in a
         * agtentry length field.  We check this in each iteration, not just
         * once at the end, to forestall possible integer overflow.
         */
        if (totallen > AGTENTRY_OFFLENMASK)
        {
            ereport(
                ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg(
                     "total size of agtype object elements exceeds the maximum of %u bytes",
                     AGTENTRY_OFFLENMASK)));
        }

        /*
         * Convert each AGT_OFFSET_STRIDE'th length to an offset.
         */
        if (((i + num_pairs) % AGT_OFFSET_STRIDE) == 0)
            meta = (meta & AGTENTRY_TYPEMASK) | totallen | AGTENTRY_HAS_OFF;

        copy_to_buffer(buffer, agtentry_offset, (char *)&meta,
                       sizeof(agtentry));
        agtentry_offset += sizeof(agtentry);
    }

    /* Total data size is everything we've appended to buffer */
    totallen = buffer->len - base_offset;

    /* Check length again, since we didn't include the metadata above */
    if (totallen > AGTENTRY_OFFLENMASK)
    {
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "total size of agtype object elements exceeds the maximum of %u bytes",
                 AGTENTRY_OFFLENMASK)));
    }

    /* Initialize the header of this node in the container's agtentry array */
    *pheader = AGTENTRY_IS_CONTAINER | totallen;
}

static void convert_agtype_scalar(StringInfo buffer, agtentry *entry,
                                  agtype_value *scalar_val)
{
    int numlen;
    short padlen;
    bool status;

    switch (scalar_val->type)
    {
    case AGTV_NULL:
        *entry = AGTENTRY_IS_NULL;
        break;

    case AGTV_STRING:
        append_to_buffer(buffer, scalar_val->val.string.val,
                         scalar_val->val.string.len);

        *entry = scalar_val->val.string.len;
        break;

    case AGTV_NUMERIC:
        numlen = VARSIZE_ANY(scalar_val->val.numeric);
        padlen = pad_buffer_to_int(buffer);

        append_to_buffer(buffer, (char *)scalar_val->val.numeric, numlen);

        *entry = AGTENTRY_IS_NUMERIC | (padlen + numlen);
        break;

    case AGTV_BOOL:
        *entry = (scalar_val->val.boolean) ? AGTENTRY_IS_BOOL_TRUE :
                                             AGTENTRY_IS_BOOL_FALSE;
        break;

    default:
        /* returns true if there was a valid extended type processed */
        status = ag_serialize_extended_type(buffer, entry, scalar_val);
        /* if nothing was found, error log out */
        if (!status)
            ereport(ERROR, (errmsg("invalid agtype scalar type %d to convert",
                                   scalar_val->type)));
    }
}

/*
 * Compare two AGTV_STRING agtype_value values, a and b.
 *
 * This is a special qsort() comparator used to sort strings in certain
 * internal contexts where it is sufficient to have a well-defined sort order.
 * In particular, object pair keys are sorted according to this criteria to
 * facilitate cheap binary searches where we don't care about lexical sort
 * order.
 *
 * a and b are first sorted based on their length.  If a tie-breaker is
 * required, only then do we consider string binary equality.
 */
static int length_compare_agtype_string_value(const void *a, const void *b)
{
    const agtype_value *va = (const agtype_value *)a;
    const agtype_value *vb = (const agtype_value *)b;
    int res;

    Assert(va->type == AGTV_STRING);
    Assert(vb->type == AGTV_STRING);

    if (va->val.string.len == vb->val.string.len)
    {
        res = memcmp(va->val.string.val, vb->val.string.val,
                     va->val.string.len);
    }
    else
    {
        res = (va->val.string.len > vb->val.string.len) ? 1 : -1;
    }

    return res;
}

/*
 * qsort_arg() comparator to compare agtype_pair values.
 *
 * Third argument 'binequal' may point to a bool. If it's set, *binequal is set
 * to true iff a and b have full binary equality, since some callers have an
 * interest in whether the two values are equal or merely equivalent.
 *
 * N.B: String comparisons here are "length-wise"
 *
 * Pairs with equals keys are ordered such that the order field is respected.
 */
static int length_compare_agtype_pair(const void *a, const void *b,
                                      void *binequal)
{
    const agtype_pair *pa = (const agtype_pair *)a;
    const agtype_pair *pb = (const agtype_pair *)b;
    int res;

    res = length_compare_agtype_string_value(&pa->key, &pb->key);
    if (res == 0 && binequal)
        *((bool *)binequal) = true;

    /*
     * Guarantee keeping order of equal pair.  Unique algorithm will prefer
     * first element as value.
     */
    if (res == 0)
        res = (pa->order > pb->order) ? -1 : 1;

    return res;
}

/*
 * Sort and unique-ify pairs in agtype_value object
 */
void uniqueify_agtype_object(agtype_value *object)
{
    bool has_non_uniq = false;

    Assert(object->type == AGTV_OBJECT);

    if (object->val.object.num_pairs > 1)
        qsort_arg(object->val.object.pairs, object->val.object.num_pairs,
                  sizeof(agtype_pair), length_compare_agtype_pair,
                  &has_non_uniq);

    if (has_non_uniq)
    {
        agtype_pair *ptr = object->val.object.pairs + 1;
        agtype_pair *res = object->val.object.pairs;

        while (ptr - object->val.object.pairs < object->val.object.num_pairs)
        {
            /* Avoid copying over duplicate */
            if (length_compare_agtype_string_value(ptr, res) != 0)
            {
                res++;
                if (ptr != res)
                    memcpy(res, ptr, sizeof(agtype_pair));
            }
            ptr++;
        }

        object->val.object.num_pairs = res + 1 - object->val.object.pairs;
    }
}

char *agtype_value_type_to_string(enum agtype_value_type type)
{
    switch (type)
    {
        case AGTV_NULL:
            return "NULL";
        case AGTV_STRING:
            return "string";
        case AGTV_NUMERIC:
            return "numeric";
        case AGTV_INTEGER:
            return "integer";
        case AGTV_FLOAT:
            return "float";
        case AGTV_BOOL:
            return "boolean";
        case AGTV_VERTEX:
            return "vertex";
        case AGTV_EDGE:
            return "edge";
        case AGTV_ARRAY:
            return "array";
        case AGTV_OBJECT:
            return "map";
        case AGTV_BINARY:
            return "binary";
        default:
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                            errmsg("unknown agtype")));
    }

    return NULL;
}

/*
 * Deallocates the passed agtype_value recursively.
 */
void pfree_agtype_value(agtype_value* value)
{
    pfree_agtype_value_content(value);
    pfree(value);
}

/*
 * Helper function that recursively deallocates the contents
 * of the passed agtype_value only. It does not deallocate
 * `value` itself.
 */
static void pfree_agtype_value_content(agtype_value* value)
{
    int i;

    /* guards against stack overflow due to deeply nested agtype_value */
    check_stack_depth();

    switch (value->type)
    {
        case AGTV_NUMERIC:
            pfree(value->val.numeric);
            break;

        case AGTV_STRING:
            /*
             * The char pointer (val.string.val) is not free'd because
             * it is not allocated by an agtype helper function.
             */
            pfree(value->val.string.val);
            break;

        case AGTV_ARRAY:
        case AGTV_PATH:
            for (i = 0; i < value->val.array.num_elems; i++)
            {
                pfree_agtype_value_content(&value->val.array.elems[i]);
            }
            pfree(value->val.array.elems);
            break;

        case AGTV_OBJECT:
        case AGTV_VERTEX:
        case AGTV_EDGE:
            for (i = 0; i < value->val.object.num_pairs; i++)
            {
                pfree_agtype_value_content(&value->val.object.pairs[i].key);
                pfree_agtype_value_content(&value->val.object.pairs[i].value);
            }
            pfree(value->val.object.pairs);
            break;

        case AGTV_BINARY:
            pfree(value->val.binary.data);
            break;

        case AGTV_NULL:
        case AGTV_INTEGER:
        case AGTV_FLOAT:
        case AGTV_BOOL:
            /*
             * These are deallocated by the calling function.
             */
            break;

        default:
            ereport(ERROR,
                    (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                     errmsg("unknown agtype")));
            break;
    }
}

void pfree_agtype_in_state(agtype_in_state* value)
{
    pfree_agtype_value(value->res);
    free(value->parse_state);
}

/*
 * helper function that recursively unpacks the agtype_value to be copied
 * and pushes the scalar values into the copied agtype_value.
 * this helps skip the serialization part at some places where the original
 * properties passed to the function are in agtype_value format and
 * converting it to agtype for iteration can be expensive.
 * the caller of this function will need to push start and end object tokens
 * on its own as this function might be used in places where pushing only start
 * object token at top level is required (for example in alter_properties)
 */
void copy_agtype_value(agtype_parse_state* pstate,
                       agtype_value* original_agtype_value,
                       agtype_value **copied_agtype_value, bool is_top_level)
{
    int i = 0;

    /*
     * guards against stack overflow due to deeply nested agtype_value
     */
    check_stack_depth();

    /*
     * directly pass the agtype_value to be pushed into the copied result
     * if type is scalar or binary (array or object) as push_agtype_value
     * can unpack binary on its own
     */
    if (IS_A_AGTYPE_SCALAR(original_agtype_value) ||
        original_agtype_value->type == AGTV_BINARY)
    {
        *copied_agtype_value = push_agtype_value(&pstate, WAGT_ELEM,
                                                 original_agtype_value);
    }
    /*
     * if the passed in type is object or array, unpack it
     * until we are left with a scalar value to push to copied result
     */
    else if (original_agtype_value->type == AGTV_OBJECT)
    {
        if (!is_top_level)
        {
            *copied_agtype_value = push_agtype_value(&pstate,
                                                     WAGT_BEGIN_OBJECT,
                                                     NULL);
        }

        for (; i < original_agtype_value->val.object.num_pairs; i ++)
        {
            agtype_pair *pair = original_agtype_value->val.object.pairs + i;
            *copied_agtype_value = push_agtype_value(&pstate, WAGT_KEY,
                                                     &pair->key);

            if (IS_A_AGTYPE_SCALAR(&pair->value))
            {
                *copied_agtype_value = push_agtype_value(&pstate, WAGT_VALUE,
                                                         &pair->value);
            }
            else
            {
                /* do a recursive call once a non-scalar value is reached */
                copy_agtype_value(pstate, &pair->value, copied_agtype_value,
                                  false);
            }
        }

        if (!is_top_level)
        {
            *copied_agtype_value = push_agtype_value(&pstate, WAGT_END_OBJECT,
                                                     NULL);
        }
    }
    else if (original_agtype_value->type == AGTV_ARRAY)
    {
        *copied_agtype_value = push_agtype_value(&pstate, WAGT_BEGIN_ARRAY,
                                                 NULL);

        for (; i < original_agtype_value->val.array.num_elems; i++)
        {
            agtype_value elem = original_agtype_value->val.array.elems[i];

            if (IS_A_AGTYPE_SCALAR(&elem))
            {
                *copied_agtype_value = push_agtype_value(&pstate, WAGT_ELEM,
                                                         &elem);
            }
            else
            {
                /* do a recursive call once a non-scalar value is reached */
                copy_agtype_value(pstate, &elem, copied_agtype_value, false);
            }
        }

        *copied_agtype_value = push_agtype_value(&pstate, WAGT_END_ARRAY,
                                                 NULL);
    }
    else
    {
        ereport(ERROR,
                (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
                 errmsg("invalid type provided for copy_agtype_value")));
    }
}
