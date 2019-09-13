/*-------------------------------------------------------------------------
 *
 * ag_jsonbx_util.c
 *	  converting between JsonbX and JsonbXValues, and iterating.
 *
 * Copyright (c) 2014-2018, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  ag_jsonbx_util.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/hash.h"
#include "catalog/pg_collation.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/memutils.h"
#include "utils/varlena.h"

#include "ag_extended_type.h"
#include "ag_jsonbx.h"
/*
 * Maximum number of elements in an array (or key/value pairs in an object).
 * This is limited by two things: the size of the JXEntry array must fit
 * in MaxAllocSize, and the number of elements (or pairs) must fit in the bits
 * reserved for that in the JsonbXContainer.header field.
 *
 * (The total size of an array's or object's elements is also limited by
 * JENTRY_OFFLENMASK, but we're not concerned about that here.)
 */
#define JSONBX_MAX_ELEMS (Min(MaxAllocSize / sizeof(JsonbXValue), JBX_CMASK))
#define JSONBX_MAX_PAIRS (Min(MaxAllocSize / sizeof(JsonbXPair), JBX_CMASK))

static void fillJsonbXValue(JsonbXContainer *container, int index,
                            char *base_addr, uint32 offset,
                            JsonbXValue *result);
static bool equalsJsonbXScalarValue(JsonbXValue *a, JsonbXValue *b);
static int compareJsonbXScalarValue(JsonbXValue *a, JsonbXValue *b);
static JsonbX *convertToJsonbX(JsonbXValue *val);
static void convertJsonbXValue(StringInfo buffer, JXEntry *header,
                               JsonbXValue *val, int level);
static void convertJsonbXArray(StringInfo buffer, JXEntry *header,
                               JsonbXValue *val, int level);
static void convertJsonbXObject(StringInfo buffer, JXEntry *header,
                                JsonbXValue *val, int level);
static void convertJsonbXScalar(StringInfo buffer, JXEntry *header,
                                JsonbXValue *scalarVal);

static void appendToBuffer(StringInfo buffer, const char *data, int len);
static void copyToBuffer(StringInfo buffer, int offset, const char *data,
                         int len);

static JsonbXIterator *iteratorFromContainer(JsonbXContainer *container,
                                             JsonbXIterator *parent);
static JsonbXIterator *freeAndGetParent(JsonbXIterator *it);
static JsonbXParseState *pushState(JsonbXParseState **pstate);
static void appendKey(JsonbXParseState *pstate, JsonbXValue *scalarVal);
static void appendValue(JsonbXParseState *pstate, JsonbXValue *scalarVal);
static void appendElement(JsonbXParseState *pstate, JsonbXValue *scalarVal);
static int lengthCompareJsonbXStringValue(const void *a, const void *b);
static int lengthCompareJsonbXPair(const void *a, const void *b, void *arg);
static void uniqueifyJsonbXObject(JsonbXValue *object);
static JsonbXValue *pushJsonbXValueScalar(JsonbXParseState **pstate,
                                          JsonbXIteratorToken seq,
                                          JsonbXValue *scalarVal);

/*
 * Turn an in-memory JsonbXValue into a JsonbX for on-disk storage.
 *
 * There isn't a JsonbToJsonbValue(), because generally we find it more
 * convenient to directly iterate through the JsonbX representation and only
 * really convert nested scalar values.  JsonbXIteratorNext() does this, so that
 * clients of the iteration code don't have to directly deal with the binary
 * representation (JsonbXDeepContains() is a notable exception, although all
 * exceptions are internal to this module).  In general, functions that accept
 * a JsonbXValue argument are concerned with the manipulation of scalar values,
 * or simple containers of scalar values, where it would be inconvenient to
 * deal with a great amount of other state.
 */
JsonbX *JsonbXValueToJsonbX(JsonbXValue *val)
{
    JsonbX *out;

    if (IsAJsonbXScalar(val))
    {
        /* Scalar value */
        JsonbXParseState *pstate = NULL;
        JsonbXValue *res;
        JsonbXValue scalarArray;

        scalarArray.type = jbvXArray;
        scalarArray.val.array.rawScalar = true;
        scalarArray.val.array.nElems = 1;

        pushJsonbXValue(&pstate, WJBX_BEGIN_ARRAY, &scalarArray);
        pushJsonbXValue(&pstate, WJBX_ELEM, val);
        res = pushJsonbXValue(&pstate, WJBX_END_ARRAY, NULL);

        out = convertToJsonbX(res);
    }
    else if (val->type == jbvXObject || val->type == jbvXArray)
    {
        out = convertToJsonbX(val);
    }
    else
    {
        Assert(val->type == jbvXBinary);
        out = palloc(VARHDRSZ + val->val.binary.len);
        SET_VARSIZE(out, VARHDRSZ + val->val.binary.len);
        memcpy(VARDATA(out), val->val.binary.data, val->val.binary.len);
    }

    return out;
}

/*
 * Get the offset of the variable-length portion of a JsonbX node within
 * the variable-length-data part of its container.  The node is identified
 * by index within the container's JXEntry array.
 */
uint32 getJsonbXOffset(const JsonbXContainer *jc, int index)
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
        offset += JBXE_OFFLENFLD(jc->children[i]);
        if (JBXE_HAS_OFF(jc->children[i]))
            break;
    }

    return offset;
}

/*
 * Get the length of the variable-length portion of a JsonbX node.
 * The node is identified by index within the container's JXEntry array.
 */
uint32 getJsonbXLength(const JsonbXContainer *jc, int index)
{
    uint32 off;
    uint32 len;

    /*
	 * If the length is stored directly in the JXEntry, just return it.
	 * Otherwise, get the begin offset of the entry, and subtract that from
	 * the stored end+1 offset.
	 */
    if (JBXE_HAS_OFF(jc->children[index]))
    {
        off = getJsonbXOffset(jc, index);
        len = JBXE_OFFLENFLD(jc->children[index]) - off;
    }
    else
        len = JBXE_OFFLENFLD(jc->children[index]);

    return len;
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
int compareJsonbXContainers(JsonbXContainer *a, JsonbXContainer *b)
{
    JsonbXIterator *ita, *itb;
    int res = 0;

    ita = JsonbXIteratorInit(a);
    itb = JsonbXIteratorInit(b);

    do
    {
        JsonbXValue va, vb;
        JsonbXIteratorToken ra, rb;

        ra = JsonbXIteratorNext(&ita, &va, false);
        rb = JsonbXIteratorNext(&itb, &vb, false);

        if (ra == rb)
        {
            if (ra == WJBX_DONE)
            {
                /* Decisively equal */
                break;
            }

            if (ra == WJBX_END_ARRAY || ra == WJBX_END_OBJECT)
            {
                /*
				 * There is no array or object to compare at this stage of
				 * processing.  jbvArray/jbvObject values are compared
				 * initially, at the WJB_BEGIN_ARRAY and WJB_BEGIN_OBJECT
				 * tokens.
				 */
                continue;
            }

            if (va.type == vb.type)
            {
                switch (va.type)
                {
                case jbvXString:
                case jbvXNull:
                case jbvXNumeric:
                case jbvXBool:
                case jbvXInteger8:
                case jbvXFloat8:
                    res = compareJsonbXScalarValue(&va, &vb);
                    break;
                case jbvXArray:

                    /*
						 * This could be a "raw scalar" pseudo array.  That's
						 * a special case here though, since we still want the
						 * general type-based comparisons to apply, and as far
						 * as we're concerned a pseudo array is just a scalar.
						 */
                    if (va.val.array.rawScalar != vb.val.array.rawScalar)
                        res = (va.val.array.rawScalar) ? -1 : 1;
                    if (va.val.array.nElems != vb.val.array.nElems)
                        res = (va.val.array.nElems > vb.val.array.nElems) ? 1 :
                                                                            -1;
                    break;
                case jbvXObject:
                    if (va.val.object.nPairs != vb.val.object.nPairs)
                        res = (va.val.object.nPairs > vb.val.object.nPairs) ?
                                  1 :
                                  -1;
                    break;
                case jbvXBinary:
                    elog(ERROR, "unexpected jbvXBinary value");
                default:
                    elog(ERROR, "unexpected jbvXType");
                }
            }
            else
            {
                /* Type-defined order */
                res = (va.type > vb.type) ? 1 : -1;
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
			 * elements/pairs (when processing WJB_BEGIN_OBJECT, say). They're
			 * either two heterogeneously-typed containers, or a container and
			 * some scalar type.
			 *
			 * We don't have to consider the WJB_END_ARRAY and WJB_END_OBJECT
			 * cases here, because we would have seen the corresponding
			 * WJB_BEGIN_ARRAY and WJB_BEGIN_OBJECT tokens first, and
			 * concluded that they don't match.
			 */
            Assert(ra != WJBX_END_ARRAY && ra != WJBX_END_OBJECT);
            Assert(rb != WJBX_END_ARRAY && rb != WJBX_END_OBJECT);

            Assert(va.type != vb.type);
            Assert(va.type != jbvXBinary);
            Assert(vb.type != jbvXBinary);
            /* Type-defined order */
            res = (va.type > vb.type) ? 1 : -1;
        }
    } while (res == 0);

    while (ita != NULL)
    {
        JsonbXIterator *i = ita->parent;

        pfree(ita);
        ita = i;
    }
    while (itb != NULL)
    {
        JsonbXIterator *i = itb->parent;

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
 * better pass a JsonbX String, because their keys can only be strings.
 * Otherwise, for an array, any type of JsonbXValue will do.
 *
 * In order to proceed with the search, it is necessary for callers to have
 * both specified an interest in exactly one particular container type with an
 * appropriate flag, as well as having the pointed-to JsonbX container be of
 * one of those same container types at the top level. (Actually, we just do
 * whichever makes sense to save callers the trouble of figuring it out - at
 * most one can make sense, because the container either points to an array
 * (possibly a "raw scalar" pseudo array) or an object.)
 *
 * Note that we can return a jbvXBinary JsonbXValue if this is called on an
 * object, but we never do so on an array.  If the caller asks to look through
 * a container type that is not of the type pointed to by the container,
 * immediately fall through and return NULL.  If we cannot find the value,
 * return NULL.  Otherwise, return palloc()'d copy of value.
 */
JsonbXValue *findJsonbXValueFromContainer(JsonbXContainer *container,
                                          uint32 flags, JsonbXValue *key)
{
    JXEntry *children = container->children;
    int count = JsonbXContainerSize(container);
    JsonbXValue *result;

    Assert((flags & ~(JBX_FARRAY | JBX_FOBJECT)) == 0);

    /* Quick out without a palloc cycle if object/array is empty */
    if (count <= 0)
        return NULL;

    result = palloc(sizeof(JsonbXValue));

    if ((flags & JBX_FARRAY) && JsonbXContainerIsArray(container))
    {
        char *base_addr = (char *)(children + count);
        uint32 offset = 0;
        int i;

        for (i = 0; i < count; i++)
        {
            fillJsonbXValue(container, i, base_addr, offset, result);

            if (key->type == result->type)
            {
                if (equalsJsonbXScalarValue(key, result))
                    return result;
            }

            JBXE_ADVANCE_OFFSET(offset, children[i]);
        }
    }
    else if ((flags & JBX_FOBJECT) && JsonbXContainerIsObject(container))
    {
        /* Since this is an object, account for *Pairs* of Jentrys */
        char *base_addr = (char *)(children + count * 2);
        uint32 stopLow = 0, stopHigh = count;

        /* Object key passed by caller must be a string */
        Assert(key->type == jbvXString);

        /* Binary search on object/pair keys *only* */
        while (stopLow < stopHigh)
        {
            uint32 stopMiddle;
            int difference;
            JsonbXValue candidate;

            stopMiddle = stopLow + (stopHigh - stopLow) / 2;

            candidate.type = jbvXString;
            candidate.val.string.val = base_addr +
                                       getJsonbXOffset(container, stopMiddle);
            candidate.val.string.len = getJsonbXLength(container, stopMiddle);

            difference = lengthCompareJsonbXStringValue(&candidate, key);

            if (difference == 0)
            {
                /* Found our key, return corresponding value */
                int index = stopMiddle + count;

                fillJsonbXValue(container, index, base_addr,
                                getJsonbXOffset(container, index), result);

                return result;
            }
            else
            {
                if (difference < 0)
                    stopLow = stopMiddle + 1;
                else
                    stopHigh = stopMiddle;
            }
        }
    }

    /* Not found */
    pfree(result);
    return NULL;
}

/*
 * Get i-th value of a JsonbX array.
 *
 * Returns palloc()'d copy of the value, or NULL if it does not exist.
 */
JsonbXValue *getIthJsonbXValueFromContainer(JsonbXContainer *container,
                                            uint32 i)
{
    JsonbXValue *result;
    char *base_addr;
    uint32 nelements;

    if (!JsonbXContainerIsArray(container))
        elog(ERROR, "not a jsonbx array");

    nelements = JsonbXContainerSize(container);
    base_addr = (char *)&container->children[nelements];

    if (i >= nelements)
        return NULL;

    result = palloc(sizeof(JsonbXValue));

    fillJsonbXValue(container, i, base_addr, getJsonbXOffset(container, i),
                    result);

    return result;
}

/*
 * A helper function to fill in a JsonbXValue to represent an element of an
 * array, or a key or value of an object.
 *
 * The node's JXEntry is at container->children[index], and its variable-length
 * data is at base_addr + offset.  We make the caller determine the offset
 * since in many cases the caller can amortize that work across multiple
 * children.  When it can't, it can just call getJsonbXOffset().
 *
 * A nested array or object will be returned as jbvXBinary, ie. it won't be
 * expanded.
 */
static void fillJsonbXValue(JsonbXContainer *container, int index,
                            char *base_addr, uint32 offset,
                            JsonbXValue *result)
{
    JXEntry entry = container->children[index];

    if (JBXE_ISNULL(entry))
    {
        result->type = jbvXNull;
    }
    else if (JBXE_ISSTRING(entry))
    {
        result->type = jbvXString;
        result->val.string.val = base_addr + offset;
        result->val.string.len = getJsonbXLength(container, index);
        Assert(result->val.string.len >= 0);
    }
    else if (JBXE_ISNUMERIC(entry))
    {
        result->type = jbvXNumeric;
        result->val.numeric = (Numeric)(base_addr + INTALIGN(offset));
    }
    /* if this is a JSONB extended type */
    else if (JBXE_ISJSONBX(entry))
    {
        ag_deserialize_extended_type(base_addr, offset, result);
    }
    else if (JBXE_ISBOOL_TRUE(entry))
    {
        result->type = jbvXBool;
        result->val.boolean = true;
    }
    else if (JBXE_ISBOOL_FALSE(entry))
    {
        result->type = jbvXBool;
        result->val.boolean = false;
    }
    else
    {
        Assert(JBXE_ISCONTAINER(entry));
        result->type = jbvXBinary;
        /* Remove alignment padding from data pointer and length */
        result->val.binary.data =
            (JsonbXContainer *)(base_addr + INTALIGN(offset));
        result->val.binary.len = getJsonbXLength(container, index) -
                                 (INTALIGN(offset) - offset);
    }
}

/*
 * Push JsonbXValue into JsonbXParseState.
 *
 * Used when parsing JSON tokens to form JsonbX, or when converting an in-memory
 * JsonbXValue to a JsonbX.
 *
 * Initial state of *JsonbXParseState is NULL, since it'll be allocated here
 * originally (caller will get JsonbXParseState back by reference).
 *
 * Only sequential tokens pertaining to non-container types should pass a
 * JsonbXValue.  There is one exception -- WJBX_BEGIN_ARRAY callers may pass a
 * "raw scalar" pseudo array to append it - the actual scalar should be passed
 * next and it will be added as the only member of the array.
 *
 * Values of type jvbXBinary, which are rolled up arrays and objects,
 * are unpacked before being added to the result.
 */
JsonbXValue *pushJsonbXValue(JsonbXParseState **pstate,
                             JsonbXIteratorToken seq, JsonbXValue *jbxval)
{
    JsonbXIterator *it;
    JsonbXValue *res = NULL;
    JsonbXValue v;
    JsonbXIteratorToken tok;

    if (!jbxval || (seq != WJBX_ELEM && seq != WJBX_VALUE) ||
        jbxval->type != jbvXBinary)
    {
        /* drop through */
        return pushJsonbXValueScalar(pstate, seq, jbxval);
    }

    /* unpack the binary and add each piece to the pstate */
    it = JsonbXIteratorInit(jbxval->val.binary.data);
    while ((tok = JsonbXIteratorNext(&it, &v, false)) != WJBX_DONE)
        res = pushJsonbXValueScalar(pstate, tok,
                                    tok < WJBX_BEGIN_ARRAY ? &v : NULL);

    return res;
}

/*
 * Do the actual pushing, with only scalar or pseudo-scalar-array values
 * accepted.
 */
static JsonbXValue *pushJsonbXValueScalar(JsonbXParseState **pstate,
                                          JsonbXIteratorToken seq,
                                          JsonbXValue *scalarVal)
{
    JsonbXValue *result = NULL;

    switch (seq)
    {
    case WJBX_BEGIN_ARRAY:
        Assert(!scalarVal || scalarVal->val.array.rawScalar);
        *pstate = pushState(pstate);
        result = &(*pstate)->contVal;
        (*pstate)->contVal.type = jbvXArray;
        (*pstate)->contVal.val.array.nElems = 0;
        (*pstate)->contVal.val.array.rawScalar =
            (scalarVal && scalarVal->val.array.rawScalar);
        if (scalarVal && scalarVal->val.array.nElems > 0)
        {
            /* Assume that this array is still really a scalar */
            Assert(scalarVal->type == jbvXArray);
            (*pstate)->size = scalarVal->val.array.nElems;
        }
        else
        {
            (*pstate)->size = 4;
        }
        (*pstate)->contVal.val.array.elems =
            palloc(sizeof(JsonbXValue) * (*pstate)->size);
        break;
    case WJBX_BEGIN_OBJECT:
        Assert(!scalarVal);
        *pstate = pushState(pstate);
        result = &(*pstate)->contVal;
        (*pstate)->contVal.type = jbvXObject;
        (*pstate)->contVal.val.object.nPairs = 0;
        (*pstate)->size = 4;
        (*pstate)->contVal.val.object.pairs =
            palloc(sizeof(JsonbXPair) * (*pstate)->size);
        break;
    case WJBX_KEY:
        Assert(scalarVal->type == jbvXString);
        appendKey(*pstate, scalarVal);
        break;
    case WJBX_VALUE:
        Assert(IsAJsonbXScalar(scalarVal));
        appendValue(*pstate, scalarVal);
        break;
    case WJBX_ELEM:
        Assert(IsAJsonbXScalar(scalarVal));
        appendElement(*pstate, scalarVal);
        break;
    case WJBX_END_OBJECT:
        uniqueifyJsonbXObject(&(*pstate)->contVal);
        /* fall through! */
    case WJBX_END_ARRAY:
        /* Steps here common to WJB_END_OBJECT case */
        Assert(!scalarVal);
        result = &(*pstate)->contVal;

        /*
			 * Pop stack and push current array/object as value in parent
			 * array/object
			 */
        *pstate = (*pstate)->next;
        if (*pstate)
        {
            switch ((*pstate)->contVal.type)
            {
            case jbvXArray:
                appendElement(*pstate, result);
                break;
            case jbvXObject:
                appendValue(*pstate, result);
                break;
            default:
                elog(ERROR, "invalid jsonbx container type");
            }
        }
        break;
    default:
        elog(ERROR, "unrecognized jsonbx sequential processing token");
    }

    return result;
}

/*
 * pushJsonbXValue() worker:  Iteration-like forming of Jsonb
 */
static JsonbXParseState *pushState(JsonbXParseState **pstate)
{
    JsonbXParseState *ns = palloc(sizeof(JsonbXParseState));

    ns->next = *pstate;
    return ns;
}

/*
 * pushJsonbXValue() worker:  Append a pair key to state when generating a Jsonb
 */
static void appendKey(JsonbXParseState *pstate, JsonbXValue *string)
{
    JsonbXValue *object = &pstate->contVal;

    Assert(object->type == jbvXObject);
    Assert(string->type == jbvXString);

    if (object->val.object.nPairs >= JSONBX_MAX_PAIRS)
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "number of jsonbx object pairs exceeds the maximum allowed (%zu)",
                 JSONBX_MAX_PAIRS)));

    if (object->val.object.nPairs >= pstate->size)
    {
        pstate->size *= 2;
        object->val.object.pairs = repalloc(object->val.object.pairs,
                                            sizeof(JsonbXPair) * pstate->size);
    }

    object->val.object.pairs[object->val.object.nPairs].key = *string;
    object->val.object.pairs[object->val.object.nPairs].order =
        object->val.object.nPairs;
}

/*
 * pushJsonbXValue() worker:  Append a pair value to state when generating a
 * Jsonb
 */
static void appendValue(JsonbXParseState *pstate, JsonbXValue *scalarVal)
{
    JsonbXValue *object = &pstate->contVal;

    Assert(object->type == jbvXObject);

    object->val.object.pairs[object->val.object.nPairs++].value = *scalarVal;
}

/*
 * pushJsonbXValue() worker:  Append an element to state when generating a Jsonb
 */
static void appendElement(JsonbXParseState *pstate, JsonbXValue *scalarVal)
{
    JsonbXValue *array = &pstate->contVal;

    Assert(array->type == jbvXArray);

    if (array->val.array.nElems >= JSONBX_MAX_ELEMS)
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "number of jsonbx array elements exceeds the maximum allowed (%zu)",
                 JSONBX_MAX_ELEMS)));

    if (array->val.array.nElems >= pstate->size)
    {
        pstate->size *= 2;
        array->val.array.elems = repalloc(array->val.array.elems,
                                          sizeof(JsonbXValue) * pstate->size);
    }

    array->val.array.elems[array->val.array.nElems++] = *scalarVal;
}

/*
 * Given a JsonbXContainer, expand to JsonbXIterator to iterate over items
 * fully expanded to in-memory representation for manipulation.
 *
 * See JsonbXIteratorNext() for notes on memory management.
 */
JsonbXIterator *JsonbXIteratorInit(JsonbXContainer *container)
{
    return iteratorFromContainer(container, NULL);
}

/*
 * Get next JsonbXValue while iterating
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
 * Returns "JsonbX sequential processing" token value.  Iterator "state"
 * reflects the current stage of the process in a less granular fashion, and is
 * mostly used here to track things internally with respect to particular
 * iterators.
 *
 * Clients of this function should not have to handle any jbvXBinary values
 * (since recursive calls will deal with this), provided skipNested is false.
 * It is our job to expand the jbvXBinary representation without bothering them
 * with it.  However, clients should not take it upon themselves to touch array
 * or Object element/pair buffers, since their element/pair pointers are
 * garbage.  Also, *val will not be set when returning WJBX_END_ARRAY or
 * WJBX_END_OBJECT, on the assumption that it's only useful to access values
 * when recursing in.
 */
JsonbXIteratorToken JsonbXIteratorNext(JsonbXIterator **it, JsonbXValue *val,
                                       bool skipNested)
{
    if (*it == NULL)
        return WJBX_DONE;

    /*
	 * When stepping into a nested container, we jump back here to start
	 * processing the child. We will not recurse further in one call, because
	 * processing the child will always begin in JBXI_ARRAY_START or
	 * JBXI_OBJECT_START state.
	 */
recurse:
    switch ((*it)->state)
    {
    case JBXI_ARRAY_START:
        /* Set v to array on first array call */
        val->type = jbvXArray;
        val->val.array.nElems = (*it)->nElems;

        /*
			 * v->val.array.elems is not actually set, because we aren't doing
			 * a full conversion
			 */
        val->val.array.rawScalar = (*it)->isScalar;
        (*it)->curIndex = 0;
        (*it)->curDataOffset = 0;
        (*it)->curValueOffset = 0; /* not actually used */
        /* Set state for next call */
        (*it)->state = JBXI_ARRAY_ELEM;
        return WJBX_BEGIN_ARRAY;

    case JBXI_ARRAY_ELEM:
        if ((*it)->curIndex >= (*it)->nElems)
        {
            /*
				 * All elements within array already processed.  Report this
				 * to caller, and give it back original parent iterator (which
				 * independently tracks iteration progress at its level of
				 * nesting).
				 */
            *it = freeAndGetParent(*it);
            return WJBX_END_ARRAY;
        }

        fillJsonbXValue((*it)->container, (*it)->curIndex, (*it)->dataProper,
                        (*it)->curDataOffset, val);

        JBXE_ADVANCE_OFFSET((*it)->curDataOffset,
                            (*it)->children[(*it)->curIndex]);
        (*it)->curIndex++;

        if (!IsAJsonbXScalar(val) && !skipNested)
        {
            /* Recurse into container. */
            *it = iteratorFromContainer(val->val.binary.data, *it);
            goto recurse;
        }
        else
        {
            /*
				 * Scalar item in array, or a container and caller didn't want
				 * us to recurse into it.
				 */
            return WJBX_ELEM;
        }

    case JBXI_OBJECT_START:
        /* Set v to object on first object call */
        val->type = jbvXObject;
        val->val.object.nPairs = (*it)->nElems;

        /*
			 * v->val.object.pairs is not actually set, because we aren't
			 * doing a full conversion
			 */
        (*it)->curIndex = 0;
        (*it)->curDataOffset = 0;
        (*it)->curValueOffset = getJsonbXOffset((*it)->container,
                                                (*it)->nElems);
        /* Set state for next call */
        (*it)->state = JBXI_OBJECT_KEY;
        return WJBX_BEGIN_OBJECT;

    case JBXI_OBJECT_KEY:
        if ((*it)->curIndex >= (*it)->nElems)
        {
            /*
				 * All pairs within object already processed.  Report this to
				 * caller, and give it back original containing iterator
				 * (which independently tracks iteration progress at its level
				 * of nesting).
				 */
            *it = freeAndGetParent(*it);
            return WJBX_END_OBJECT;
        }
        else
        {
            /* Return key of a key/value pair.  */
            fillJsonbXValue((*it)->container, (*it)->curIndex,
                            (*it)->dataProper, (*it)->curDataOffset, val);
            if (val->type != jbvXString)
                elog(ERROR, "unexpected jsonbx type as object key");

            /* Set state for next call */
            (*it)->state = JBXI_OBJECT_VALUE;
            return WJBX_KEY;
        }

    case JBXI_OBJECT_VALUE:
        /* Set state for next call */
        (*it)->state = JBXI_OBJECT_KEY;

        fillJsonbXValue((*it)->container, (*it)->curIndex + (*it)->nElems,
                        (*it)->dataProper, (*it)->curValueOffset, val);

        JBXE_ADVANCE_OFFSET((*it)->curDataOffset,
                            (*it)->children[(*it)->curIndex]);
        JBXE_ADVANCE_OFFSET((*it)->curValueOffset,
                            (*it)->children[(*it)->curIndex + (*it)->nElems]);
        (*it)->curIndex++;

        /*
			 * Value may be a container, in which case we recurse with new,
			 * child iterator (unless the caller asked not to, by passing
			 * skipNested).
			 */
        if (!IsAJsonbXScalar(val) && !skipNested)
        {
            *it = iteratorFromContainer(val->val.binary.data, *it);
            goto recurse;
        }
        else
            return WJBX_VALUE;
    }

    elog(ERROR, "invalid iterator state");
    return -1;
}

/*
 * Initialize an iterator for iterating all elements in a container.
 */
static JsonbXIterator *iteratorFromContainer(JsonbXContainer *container,
                                             JsonbXIterator *parent)
{
    JsonbXIterator *it;

    it = palloc0(sizeof(JsonbXIterator));
    it->container = container;
    it->parent = parent;
    it->nElems = JsonbXContainerSize(container);

    /* Array starts just after header */
    it->children = container->children;

    switch (container->header & (JBX_FARRAY | JBX_FOBJECT))
    {
    case JBX_FARRAY:
        it->dataProper = (char *)it->children + it->nElems * sizeof(JXEntry);
        it->isScalar = JsonbXContainerIsScalar(container);
        /* This is either a "raw scalar", or an array */
        Assert(!it->isScalar || it->nElems == 1);

        it->state = JBXI_ARRAY_START;
        break;

    case JBX_FOBJECT:
        it->dataProper = (char *)it->children +
                         it->nElems * sizeof(JXEntry) * 2;
        it->state = JBXI_OBJECT_START;
        break;

    default:
        elog(ERROR, "unknown type of jsonbx container");
    }

    return it;
}

/*
 * JsonbXIteratorNext() worker:	Return parent, while freeing memory for current
 * iterator
 */
static JsonbXIterator *freeAndGetParent(JsonbXIterator *it)
{
    JsonbXIterator *v = it->parent;

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
 * "val" is lhs JsonbX, and mContained is rhs JsonbX when called from top level.
 * We determine if mContained is contained within val.
 */
bool JsonbXDeepContains(JsonbXIterator **val, JsonbXIterator **mContained)
{
    JsonbXValue vval, vcontained;
    JsonbXIteratorToken rval, rcont;

    /*
	 * Guard against stack overflow due to overly complex Jsonb.
	 *
	 * Functions called here independently take this precaution, but that
	 * might not be sufficient since this is also a recursive function.
	 */
    check_stack_depth();

    rval = JsonbXIteratorNext(val, &vval, false);
    rcont = JsonbXIteratorNext(mContained, &vcontained, false);

    if (rval != rcont)
    {
        /*
		 * The differing return values can immediately be taken as indicating
		 * two differing container types at this nesting level, which is
		 * sufficient reason to give up entirely (but it should be the case
		 * that they're both some container type).
		 */
        Assert(rval == WJBX_BEGIN_OBJECT || rval == WJBX_BEGIN_ARRAY);
        Assert(rcont == WJBX_BEGIN_OBJECT || rcont == WJBX_BEGIN_ARRAY);
        return false;
    }
    else if (rcont == WJBX_BEGIN_OBJECT)
    {
        Assert(vval.type == jbvXObject);
        Assert(vcontained.type == jbvXObject);

        /*
		 * If the lhs has fewer pairs than the rhs, it can't possibly contain
		 * the rhs.  (This conclusion is safe only because we de-duplicate
		 * keys in all JsonbX objects; thus there can be no corresponding
		 * optimization in the array case.)  The case probably won't arise
		 * often, but since it's such a cheap check we may as well make it.
		 */
        if (vval.val.object.nPairs < vcontained.val.object.nPairs)
            return false;

        /* Work through rhs "is it contained within?" object */
        for (;;)
        {
            JsonbXValue *lhsVal; /* lhsVal is from pair in lhs object */

            rcont = JsonbXIteratorNext(mContained, &vcontained, false);

            /*
			 * When we get through caller's rhs "is it contained within?"
			 * object without failing to find one of its values, it's
			 * contained.
			 */
            if (rcont == WJBX_END_OBJECT)
                return true;

            Assert(rcont == WJBX_KEY);

            /* First, find value by key... */
            lhsVal = findJsonbXValueFromContainer((*val)->container,
                                                  JBX_FOBJECT, &vcontained);

            if (!lhsVal)
                return false;

            /*
			 * ...at this stage it is apparent that there is at least a key
			 * match for this rhs pair.
			 */
            rcont = JsonbXIteratorNext(mContained, &vcontained, true);

            Assert(rcont == WJBX_VALUE);

            /*
			 * Compare rhs pair's value with lhs pair's value just found using
			 * key
			 */
            if (lhsVal->type != vcontained.type)
            {
                return false;
            }
            else if (IsAJsonbXScalar(lhsVal))
            {
                if (!equalsJsonbXScalarValue(lhsVal, &vcontained))
                    return false;
            }
            else
            {
                /* Nested container value (object or array) */
                JsonbXIterator *nestval, *nestContained;

                Assert(lhsVal->type == jbvXBinary);
                Assert(vcontained.type == jbvXBinary);

                nestval = JsonbXIteratorInit(lhsVal->val.binary.data);
                nestContained = JsonbXIteratorInit(vcontained.val.binary.data);

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
				 * "vcontained" JsonbX to internal nodes on the lhs is
				 * injective, and parent-child edges on the rhs must be mapped
				 * to parent-child edges on the lhs to satisfy the condition
				 * of containment (plus of course the mapped nodes must be
				 * equal).
				 */
                if (!JsonbXDeepContains(&nestval, &nestContained))
                    return false;
            }
        }
    }
    else if (rcont == WJBX_BEGIN_ARRAY)
    {
        JsonbXValue *lhsConts = NULL;
        uint32 nLhsElems = vval.val.array.nElems;

        Assert(vval.type == jbvXArray);
        Assert(vcontained.type == jbvXArray);

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
        if (vval.val.array.rawScalar && !vcontained.val.array.rawScalar)
            return false;

        /* Work through rhs "is it contained within?" array */
        for (;;)
        {
            rcont = JsonbXIteratorNext(mContained, &vcontained, true);

            /*
			 * When we get through caller's rhs "is it contained within?"
			 * array without failing to find one of its values, it's
			 * contained.
			 */
            if (rcont == WJBX_END_ARRAY)
                return true;

            Assert(rcont == WJBX_ELEM);

            if (IsAJsonbXScalar(&vcontained))
            {
                if (!findJsonbXValueFromContainer((*val)->container,
                                                  JBX_FARRAY, &vcontained))
                    return false;
            }
            else
            {
                uint32 i;

                /*
				 * If this is first container found in rhs array (at this
				 * depth), initialize temp lhs array of containers
				 */
                if (lhsConts == NULL)
                {
                    uint32 j = 0;

                    /* Make room for all possible values */
                    lhsConts = palloc(sizeof(JsonbXValue) * nLhsElems);

                    for (i = 0; i < nLhsElems; i++)
                    {
                        /* Store all lhs elements in temp array */
                        rcont = JsonbXIteratorNext(val, &vval, true);
                        Assert(rcont == WJBX_ELEM);

                        if (vval.type == jbvXBinary)
                            lhsConts[j++] = vval;
                    }

                    /* No container elements in temp array, so give up now */
                    if (j == 0)
                        return false;

                    /* We may have only partially filled array */
                    nLhsElems = j;
                }

                /* XXX: Nested array containment is O(N^2) */
                for (i = 0; i < nLhsElems; i++)
                {
                    /* Nested container value (object or array) */
                    JsonbXIterator *nestval, *nestContained;
                    bool contains;

                    nestval = JsonbXIteratorInit(lhsConts[i].val.binary.data);
                    nestContained =
                        JsonbXIteratorInit(vcontained.val.binary.data);

                    contains = JsonbXDeepContains(&nestval, &nestContained);

                    if (nestval)
                        pfree(nestval);
                    if (nestContained)
                        pfree(nestContained);
                    if (contains)
                        break;
                }

                /*
				 * Report rhs container value is not contained if couldn't
				 * match rhs container to *some* lhs cont
				 */
                if (i == nLhsElems)
                    return false;
            }
        }
    }
    else
    {
        elog(ERROR, "invalid jsonbx container type");
    }

    elog(ERROR, "unexpectedly fell off end of jsonbx container");
    return false;
}

/*
 * Hash a JsonbXValue scalar value, mixing the hash value into an existing
 * hash provided by the caller.
 *
 * Some callers may wish to independently XOR in JBX_FOBJECT and JBX_FARRAY
 * flags.
 */
void JsonbXHashScalarValue(const JsonbXValue *scalarVal, uint32 *hash)
{
    uint32 tmp;

    /* Compute hash value for scalarVal */
    switch (scalarVal->type)
    {
    case jbvXNull:
        tmp = 0x01;
        break;
    case jbvXString:
        tmp = DatumGetUInt32(
            hash_any((const unsigned char *)scalarVal->val.string.val,
                     scalarVal->val.string.len));
        break;
    case jbvXNumeric:
        /* Must hash equal numerics to equal hash codes */
        tmp = DatumGetUInt32(DirectFunctionCall1(
            hash_numeric, NumericGetDatum(scalarVal->val.numeric)));
        break;
    case jbvXBool:
        tmp = scalarVal->val.boolean ? 0x02 : 0x04;

        break;
    case jbvXInteger8:
        tmp = DatumGetUInt32(DirectFunctionCall1(
            hashint8, Int64GetDatum(scalarVal->val.integer8)));
        break;
    case jbvXFloat8:
        tmp = DatumGetUInt32(DirectFunctionCall1(
            hashfloat8, Float8GetDatum(scalarVal->val.float8)));
        break;

    default:
        elog(ERROR, "invalid jsonbx scalar type");
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
 * JsonbXHashScalarValue.
 */
void JsonbXHashScalarValueExtended(const JsonbXValue *scalarVal, uint64 *hash,
                                   uint64 seed)
{
    uint64 tmp;

    switch (scalarVal->type)
    {
    case jbvXNull:
        tmp = seed + 0x01;
        break;
    case jbvXString:
        tmp = DatumGetUInt64(
            hash_any_extended((const unsigned char *)scalarVal->val.string.val,
                              scalarVal->val.string.len, seed));
        break;
    case jbvXNumeric:
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hash_numeric_extended, NumericGetDatum(scalarVal->val.numeric),
            UInt64GetDatum(seed)));
        break;
    case jbvXBool:
        if (seed)
            tmp = DatumGetUInt64(DirectFunctionCall2(
                hashcharextended, BoolGetDatum(scalarVal->val.boolean),
                UInt64GetDatum(seed)));
        else
            tmp = scalarVal->val.boolean ? 0x02 : 0x04;

        break;
    case jbvXInteger8:
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hashint8extended, Int64GetDatum(scalarVal->val.integer8),
            UInt64GetDatum(seed)));
        break;
    case jbvXFloat8:
        tmp = DatumGetUInt64(DirectFunctionCall2(
            hashfloat8extended, Float8GetDatum(scalarVal->val.float8),
            UInt64GetDatum(seed)));
        break;
    default:
        elog(ERROR, "invalid jsonbx scalar type");
        break;
    }

    *hash = ROTATE_HIGH_AND_LOW_32BITS(*hash);
    *hash ^= tmp;
}

/*
 * Are two scalar JsonbXValues of the same type a and b equal?
 */
static bool equalsJsonbXScalarValue(JsonbXValue *aScalar, JsonbXValue *bScalar)
{
    if (aScalar->type == bScalar->type)
    {
        switch (aScalar->type)
        {
        case jbvXNull:
            return true;
        case jbvXString:
            return lengthCompareJsonbXStringValue(aScalar, bScalar) == 0;
        case jbvXNumeric:
            return DatumGetBool(DirectFunctionCall2(
                numeric_eq, PointerGetDatum(aScalar->val.numeric),
                PointerGetDatum(bScalar->val.numeric)));
        case jbvXBool:
            return aScalar->val.boolean == bScalar->val.boolean;
        case jbvXInteger8:
            return aScalar->val.integer8 == bScalar->val.integer8;
        case jbvXFloat8:
            return aScalar->val.float8 == bScalar->val.float8;

        default:
            elog(ERROR, "invalid jsonbx scalar type");
        }
    }
    elog(ERROR, "jsonbx scalar type mismatch");
    return -1;
}

/*
 * Compare two scalar JsonbXValues, returning -1, 0, or 1.
 *
 * Strings are compared using the default collation.  Used by B-tree
 * operators, where a lexical sort order is generally expected.
 */
static int compareJsonbXScalarValue(JsonbXValue *aScalar, JsonbXValue *bScalar)
{
    if (aScalar->type == bScalar->type)
    {
        switch (aScalar->type)
        {
        case jbvXNull:
            return 0;
        case jbvXString:
            return varstr_cmp(aScalar->val.string.val, aScalar->val.string.len,
                              bScalar->val.string.val, bScalar->val.string.len,
                              DEFAULT_COLLATION_OID);
        case jbvXNumeric:
            return DatumGetInt32(DirectFunctionCall2(
                numeric_cmp, PointerGetDatum(aScalar->val.numeric),
                PointerGetDatum(bScalar->val.numeric)));
        case jbvXBool:
            if (aScalar->val.boolean == bScalar->val.boolean)
                return 0;
            else if (aScalar->val.boolean > bScalar->val.boolean)
                return 1;
            else
                return -1;
        case jbvXInteger8:
            if (aScalar->val.integer8 == bScalar->val.integer8)
                return 0;
            else if (aScalar->val.integer8 > bScalar->val.integer8)
                return 1;
            else
                return -1;
        case jbvXFloat8:
            if (aScalar->val.float8 == bScalar->val.float8)
                return 0;
            else if (aScalar->val.float8 > bScalar->val.float8)
                return 1;
            else
                return -1;

        default:
            elog(ERROR, "invalid jsonbx scalar type");
        }
    }
    elog(ERROR, "jsonbx scalar type mismatch");
    return -1;
}

/*
 * Functions for manipulating the resizeable buffer used by convertJsonbX and
 * its subroutines.
 */

/*
 * Reserve 'len' bytes, at the end of the buffer, enlarging it if necessary.
 * Returns the offset to the reserved area. The caller is expected to fill
 * the reserved area later with copyToBuffer().
 */
int reserveFromBuffer(StringInfo buffer, int len)
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
static void copyToBuffer(StringInfo buffer, int offset, const char *data,
                         int len)
{
    memcpy(buffer->data + offset, data, len);
}

/*
 * A shorthand for reserveFromBuffer + copyToBuffer.
 */
static void appendToBuffer(StringInfo buffer, const char *data, int len)
{
    int offset;

    offset = reserveFromBuffer(buffer, len);
    copyToBuffer(buffer, offset, data, len);
}

/*
 * Append padding, so that the length of the StringInfo is int-aligned.
 * Returns the number of padding bytes appended.
 */
short padBufferToInt(StringInfo buffer)
{
    int padlen, p, offset;

    padlen = INTALIGN(buffer->len) - buffer->len;

    offset = reserveFromBuffer(buffer, padlen);

    /* padlen must be small, so this is probably faster than a memset */
    for (p = 0; p < padlen; p++)
        buffer->data[offset + p] = '\0';

    return padlen;
}

/*
 * Given a JsonbXValue, convert to JsonbX. The result is palloc'd.
 */
static JsonbX *convertToJsonbX(JsonbXValue *val)
{
    StringInfoData buffer;
    JXEntry jentry;
    JsonbX *res;

    /* Should not already have binary representation */
    Assert(val->type != jbvXBinary);

    /* Allocate an output buffer. It will be enlarged as needed */
    initStringInfo(&buffer);

    /* Make room for the varlena header */
    reserveFromBuffer(&buffer, VARHDRSZ);

    convertJsonbXValue(&buffer, &jentry, val, 0);

    /*
	 * Note: the JXEntry of the root is discarded. Therefore the root
	 * JsonbXContainer struct must contain enough information to tell what kind
	 * of value it is.
	 */

    res = (JsonbX *)buffer.data;

    SET_VARSIZE(res, buffer.len);

    return res;
}

/*
 * Subroutine of convertJsonbX: serialize a single JsonbXValue into buffer.
 *
 * The JXEntry header for this node is returned in *header.  It is filled in
 * with the length of this value and appropriate type bits.  If we wish to
 * store an end offset rather than a length, it is the caller's responsibility
 * to adjust for that.
 *
 * If the value is an array or an object, this recurses. 'level' is only used
 * for debugging purposes.
 */
static void convertJsonbXValue(StringInfo buffer, JXEntry *header,
                               JsonbXValue *val, int level)
{
    check_stack_depth();

    if (!val)
        return;

    /*
	 * A JsonbXValue passed as val should never have a type of jbvXBinary, and
	 * neither should any of its sub-components. Those values will be produced
	 * by convertJsonbXArray and convertJsonbXObject, the results of which will
	 * not be passed back to this function as an argument.
	 */

    if (IsAJsonbXScalar(val))
        convertJsonbXScalar(buffer, header, val);
    else if (val->type == jbvXArray)
        convertJsonbXArray(buffer, header, val, level);
    else if (val->type == jbvXObject)
        convertJsonbXObject(buffer, header, val, level);
    else
        elog(ERROR, "unknown type of jsonbx container to convert");
}

static void convertJsonbXArray(StringInfo buffer, JXEntry *pheader,
                               JsonbXValue *val, int level)
{
    int base_offset;
    int jentry_offset;
    int i;
    int totallen;
    uint32 header;
    int nElems = val->val.array.nElems;

    /* Remember where in the buffer this array starts. */
    base_offset = buffer->len;

    /* Align to 4-byte boundary (any padding counts as part of my data) */
    padBufferToInt(buffer);

    /*
	 * Construct the header Jentry and store it in the beginning of the
	 * variable-length payload.
	 */
    header = nElems | JBX_FARRAY;
    if (val->val.array.rawScalar)
    {
        Assert(nElems == 1);
        Assert(level == 0);
        header |= JBX_FSCALAR;
    }

    appendToBuffer(buffer, (char *)&header, sizeof(uint32));

    /* Reserve space for the JXEntries of the elements. */
    jentry_offset = reserveFromBuffer(buffer, sizeof(JXEntry) * nElems);

    totallen = 0;
    for (i = 0; i < nElems; i++)
    {
        JsonbXValue *elem = &val->val.array.elems[i];
        int len;
        JXEntry meta;

        /*
         * Convert element, producing a JXEntry and appending its
         * variable-length data to buffer
         */
        convertJsonbXValue(buffer, &meta, elem, level + 1);

        len = JBXE_OFFLENFLD(meta);
        totallen += len;

        /*
         * Bail out if total variable-length data exceeds what will fit in a
         * JXEntry length field.  We check this in each iteration, not just
         * once at the end, to forestall possible integer overflow.
         */
        if (totallen > JXENTRY_OFFLENMASK)
            ereport(
                ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg(
                     "total size of jsonbx array elements exceeds the maximum of %u bytes",
                     JXENTRY_OFFLENMASK)));

        /*
         * Convert each JBX_OFFSET_STRIDE'th length to an offset.
         */
        if ((i % JBX_OFFSET_STRIDE) == 0)
            meta = (meta & JXENTRY_TYPEMASK) | totallen | JXENTRY_HAS_OFF;

        copyToBuffer(buffer, jentry_offset, (char *)&meta, sizeof(JXEntry));
        jentry_offset += sizeof(JXEntry);
    }

    /* Total data size is everything we've appended to buffer */
    totallen = buffer->len - base_offset;

    /* Check length again, since we didn't include the metadata above */
    if (totallen > JXENTRY_OFFLENMASK)
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "total size of jsonbx array elements exceeds the maximum of %u bytes",
                 JXENTRY_OFFLENMASK)));

    /* Initialize the header of this node in the container's JXEntry array */
    *pheader = JXENTRY_ISCONTAINER | totallen;
}

static void convertJsonbXObject(StringInfo buffer, JXEntry *pheader,
                                JsonbXValue *val, int level)
{
    int base_offset;
    int jentry_offset;
    int i;
    int totallen;
    uint32 header;
    int nPairs = val->val.object.nPairs;

    /* Remember where in the buffer this object starts. */
    base_offset = buffer->len;

    /* Align to 4-byte boundary (any padding counts as part of my data) */
    padBufferToInt(buffer);

    /*
	 * Construct the header JXEntry and store it in the beginning of the
	 * variable-length payload.
	 */
    header = nPairs | JBX_FOBJECT;
    appendToBuffer(buffer, (char *)&header, sizeof(uint32));

    /* Reserve space for the JXEntries of the keys and values. */
    jentry_offset = reserveFromBuffer(buffer, sizeof(JXEntry) * nPairs * 2);

    /*
	 * Iterate over the keys, then over the values, since that is the ordering
	 * we want in the on-disk representation.
	 */
    totallen = 0;
    for (i = 0; i < nPairs; i++)
    {
        JsonbXPair *pair = &val->val.object.pairs[i];
        int len;
        JXEntry meta;

        /*
		 * Convert key, producing a JXEntry and appending its variable-length
		 * data to buffer
		 */
        convertJsonbXScalar(buffer, &meta, &pair->key);

        len = JBXE_OFFLENFLD(meta);
        totallen += len;

        /*
		 * Bail out if total variable-length data exceeds what will fit in a
		 * JXEntry length field.  We check this in each iteration, not just
		 * once at the end, to forestall possible integer overflow.
		 */
        if (totallen > JXENTRY_OFFLENMASK)
            ereport(
                ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg(
                     "total size of jsonbx object elements exceeds the maximum of %u bytes",
                     JXENTRY_OFFLENMASK)));

        /*
		 * Convert each JBX_OFFSET_STRIDE'th length to an offset.
		 */
        if ((i % JBX_OFFSET_STRIDE) == 0)
            meta = (meta & JXENTRY_TYPEMASK) | totallen | JXENTRY_HAS_OFF;

        copyToBuffer(buffer, jentry_offset, (char *)&meta, sizeof(JXEntry));
        jentry_offset += sizeof(JXEntry);
    }
    for (i = 0; i < nPairs; i++)
    {
        JsonbXPair *pair = &val->val.object.pairs[i];
        int len;
        JXEntry meta;

        /*
		 * Convert value, producing a JXEntry and appending its variable-length
		 * data to buffer
		 */
        convertJsonbXValue(buffer, &meta, &pair->value, level + 1);

        len = JBXE_OFFLENFLD(meta);
        totallen += len;

        /*
		 * Bail out if total variable-length data exceeds what will fit in a
		 * JXEntry length field.  We check this in each iteration, not just
		 * once at the end, to forestall possible integer overflow.
		 */
        if (totallen > JXENTRY_OFFLENMASK)
            ereport(
                ERROR,
                (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
                 errmsg(
                     "total size of jsonbx object elements exceeds the maximum of %u bytes",
                     JXENTRY_OFFLENMASK)));

        /*
		 * Convert each JBX_OFFSET_STRIDE'th length to an offset.
		 */
        if (((i + nPairs) % JBX_OFFSET_STRIDE) == 0)
            meta = (meta & JXENTRY_TYPEMASK) | totallen | JXENTRY_HAS_OFF;

        copyToBuffer(buffer, jentry_offset, (char *)&meta, sizeof(JXEntry));
        jentry_offset += sizeof(JXEntry);
    }

    /* Total data size is everything we've appended to buffer */
    totallen = buffer->len - base_offset;

    /* Check length again, since we didn't include the metadata above */
    if (totallen > JXENTRY_OFFLENMASK)
        ereport(
            ERROR,
            (errcode(ERRCODE_PROGRAM_LIMIT_EXCEEDED),
             errmsg(
                 "total size of jsonbx object elements exceeds the maximum of %u bytes",
                 JXENTRY_OFFLENMASK)));

    /* Initialize the header of this node in the container's JXEntry array */
    *pheader = JXENTRY_ISCONTAINER | totallen;
}

static void convertJsonbXScalar(StringInfo buffer, JXEntry *jxentry,
                                JsonbXValue *scalarVal)
{
    int numlen;
    short padlen;
    bool status;

    switch (scalarVal->type)
    {
    case jbvXNull:
        *jxentry = JXENTRY_ISNULL;
        break;

    case jbvXString:
        appendToBuffer(buffer, scalarVal->val.string.val,
                       scalarVal->val.string.len);

        *jxentry = scalarVal->val.string.len;
        break;

    case jbvXNumeric:
        numlen = VARSIZE_ANY(scalarVal->val.numeric);
        padlen = padBufferToInt(buffer);

        appendToBuffer(buffer, (char *)scalarVal->val.numeric, numlen);

        *jxentry = JXENTRY_ISNUMERIC | (padlen + numlen);
        break;

    case jbvXBool:
        *jxentry = (scalarVal->val.boolean) ? JXENTRY_ISBOOL_TRUE :
                                              JXENTRY_ISBOOL_FALSE;
        break;

    default:
        /* returns true if there was a valid extended type processed */
        status = ag_serialize_extended_type(buffer, jxentry, scalarVal);
        /* if nothing was found, error log out */
        if (!status)
            elog(ERROR, "invalid jsonbx scalar type");
    }
}

/*
 * Compare two jbvXString JsonbXValue values, a and b.
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
static int lengthCompareJsonbXStringValue(const void *a, const void *b)
{
    const JsonbXValue *va = (const JsonbXValue *)a;
    const JsonbXValue *vb = (const JsonbXValue *)b;
    int res;

    Assert(va->type == jbvXString);
    Assert(vb->type == jbvXString);

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
 * qsort_arg() comparator to compare JsonbXPair values.
 *
 * Third argument 'binequal' may point to a bool. If it's set, *binequal is set
 * to true iff a and b have full binary equality, since some callers have an
 * interest in whether the two values are equal or merely equivalent.
 *
 * N.B: String comparisons here are "length-wise"
 *
 * Pairs with equals keys are ordered such that the order field is respected.
 */
static int lengthCompareJsonbXPair(const void *a, const void *b,
                                   void *binequal)
{
    const JsonbXPair *pa = (const JsonbXPair *)a;
    const JsonbXPair *pb = (const JsonbXPair *)b;
    int res;

    res = lengthCompareJsonbXStringValue(&pa->key, &pb->key);
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
 * Sort and unique-ify pairs in JsonbXValue object
 */
static void uniqueifyJsonbXObject(JsonbXValue *object)
{
    bool hasNonUniq = false;

    Assert(object->type == jbvXObject);

    if (object->val.object.nPairs > 1)
        qsort_arg(object->val.object.pairs, object->val.object.nPairs,
                  sizeof(JsonbXPair), lengthCompareJsonbXPair, &hasNonUniq);

    if (hasNonUniq)
    {
        JsonbXPair *ptr = object->val.object.pairs + 1,
                   *res = object->val.object.pairs;

        while (ptr - object->val.object.pairs < object->val.object.nPairs)
        {
            /* Avoid copying over duplicate */
            if (lengthCompareJsonbXStringValue(ptr, res) != 0)
            {
                res++;
                if (ptr != res)
                    memcpy(res, ptr, sizeof(JsonbXPair));
            }
            ptr++;
        }

        object->val.object.nPairs = res + 1 - object->val.object.pairs;
    }
}
