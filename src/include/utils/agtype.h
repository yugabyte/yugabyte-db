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
 * Declarations for agtype data type support.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
 */

#ifndef AG_AGTYPE_H
#define AG_AGTYPE_H

#include "lib/stringinfo.h"
#include "utils/array.h"
#include "utils/numeric.h"

#include "utils/graphid.h"

/* Tokens used when sequentially processing an agtype value */
typedef enum
{
    WAGT_DONE = 0x0,
    WAGT_KEY,
    WAGT_VALUE,
    WAGT_ELEM,
    WAGT_BEGIN_ARRAY,
    WAGT_END_ARRAY,
    WAGT_BEGIN_OBJECT,
    WAGT_END_OBJECT
} agtype_iterator_token;

#define AGTYPE_ITERATOR_TOKEN_IS_HASHABLE(x) \
    (x > WAGT_DONE && x < WAGT_BEGIN_ARRAY)

/* Strategy numbers for GIN index opclasses */
#define AGTYPE_CONTAINS_STRATEGY_NUMBER 7
#define AGTYPE_EXISTS_STRATEGY_NUMBER 9
#define AGTYPE_EXISTS_ANY_STRATEGY_NUMBER 10
#define AGTYPE_EXISTS_ALL_STRATEGY_NUMBER 11

/*
 * In the standard agtype_ops GIN opclass for agtype, we choose to index both
 * keys and values.  The storage format is text.  The first byte of the text
 * string distinguishes whether this is a key (always a string), null value,
 * boolean value, numeric value, or string value.  However, array elements
 * that are strings are marked as though they were keys; this imprecision
 * supports the definition of the "exists" operator, which treats array
 * elements like keys.  The remainder of the text string is empty for a null
 * value, "t" or "f" for a boolean value, a normalized print representation of
 * a numeric value, or the text of a string value.  However, if the length of
 * this text representation would exceed AGT_GIN_MAX_LENGTH bytes, we instead
 * hash the text representation and store an 8-hex-digit representation of the
 * uint32 hash value, marking the prefix byte with an additional bit to
 * distinguish that this has happened.  Hashing long strings saves space and
 * ensures that we won't overrun the maximum entry length for a GIN index.
 * (But AGT_GIN_MAX_LENGTH is quite a bit shorter than GIN's limit.  It's
 * chosen to ensure that the on-disk text datum will have a short varlena
 * header.) Note that when any hashed item appears in a query, we must recheck
 * index matches against the heap tuple; currently, this costs nothing because
 * we must always recheck for other reasons.
 */
#define AGT_GIN_FLAG_KEY 0x01 /* key (or string array element) */
#define AGT_GIN_FLAG_NULL 0x02 /* null value */
#define AGT_GIN_FLAG_BOOL 0x03 /* boolean value */
#define AGT_GIN_FLAG_NUM 0x04 /* numeric value */
#define AGT_GIN_FLAG_STR 0x05 /* string value (if not an array element) */
#define AGT_GIN_FLAG_HASHED 0x10 /* OR'd into flag if value was hashed */
#define AGT_GIN_MAX_LENGTH 125 /* max length of text part before hashing */

/* Convenience macros */
#define DATUM_GET_AGTYPE_P(d) ((agtype *)PG_DETOAST_DATUM(d))
#define AGTYPE_P_GET_DATUM(p) PointerGetDatum(p)
#define AG_GET_ARG_AGTYPE_P(x) DATUM_GET_AGTYPE_P(PG_GETARG_DATUM(x))
#define AG_RETURN_AGTYPE_P(x) PG_RETURN_POINTER(x)

typedef struct agtype_pair agtype_pair;
typedef struct agtype_value agtype_value;

/*
 * agtypes are varlena objects, so must meet the varlena convention that the
 * first int32 of the object contains the total object size in bytes.  Be sure
 * to use VARSIZE() and SET_VARSIZE() to access it, though!
 *
 * agtype is the on-disk representation, in contrast to the in-memory
 * agtype_value representation.  Often, agtype_values are just shims through
 * which a agtype buffer is accessed, but they can also be deep copied and
 * passed around.
 *
 * agtype is a tree structure. Each node in the tree consists of an agtentry
 * header and a variable-length content (possibly of zero size).  The agtentry
 * header indicates what kind of a node it is, e.g. a string or an array,
 * and provides the length of its variable-length portion.
 *
 * The agtentry and the content of a node are not stored physically together.
 * Instead, the container array or object has an array that holds the agtentrys
 * of all the child nodes, followed by their variable-length portions.
 *
 * The root node is an exception; it has no parent array or object that could
 * hold its agtentry. Hence, no agtentry header is stored for the root node.
 * It is implicitly known that the root node must be an array or an object,
 * so we can get away without the type indicator as long as we can distinguish
 * the two.  For that purpose, both an array and an object begin with a uint32
 * header field, which contains an AGT_FOBJECT or AGT_FARRAY flag.  When a
 * naked scalar value needs to be stored as an agtype value, what we actually
 * store is an array with one element, with the flags in the array's header
 * field set to AGT_FSCALAR | AGT_FARRAY.
 *
 * Overall, the agtype struct requires 4-bytes alignment. Within the struct,
 * the variable-length portion of some node types is aligned to a 4-byte
 * boundary, while others are not. When alignment is needed, the padding is
 * in the beginning of the node that requires it. For example, if a numeric
 * node is stored after a string node, so that the numeric node begins at
 * offset 3, the variable-length portion of the numeric node will begin with
 * one padding byte so that the actual numeric data is 4-byte aligned.
 */

/*
 * agtentry format.
 *
 * The least significant 28 bits store either the data length of the entry,
 * or its end+1 offset from the start of the variable-length portion of the
 * containing object.  The next three bits store the type of the entry, and
 * the high-order bit tells whether the least significant bits store a length
 * or an offset.
 *
 * The reason for the offset-or-length complication is to compromise between
 * access speed and data compressibility.  In the initial design each agtentry
 * always stored an offset, but this resulted in agtentry arrays with horrible
 * compressibility properties, so that TOAST compression of an agtype did not
 * work well.  Storing only lengths would greatly improve compressibility,
 * but it makes random access into large arrays expensive (O(N) not O(1)).
 * So what we do is store an offset in every AGT_OFFSET_STRIDE'th agtentry and
 * a length in the rest.  This results in reasonably compressible data (as
 * long as the stride isn't too small).  We may have to examine as many as
 * AGT_OFFSET_STRIDE agtentrys in order to find out the offset or length of any
 * given item, but that's still O(1) no matter how large the container is.
 *
 * We could avoid eating a flag bit for this purpose if we were to store
 * the stride in the container header, or if we were willing to treat the
 * stride as an unchangeable constant.  Neither of those options is very
 * attractive though.
 */
typedef uint32 agtentry;

#define AGTENTRY_OFFLENMASK 0x0FFFFFFF
#define AGTENTRY_TYPEMASK 0x70000000
#define AGTENTRY_HAS_OFF 0x80000000

/* values stored in the type bits */
#define AGTENTRY_IS_STRING 0x00000000
#define AGTENTRY_IS_NUMERIC 0x10000000
#define AGTENTRY_IS_BOOL_FALSE 0x20000000
#define AGTENTRY_IS_BOOL_TRUE 0x30000000
#define AGTENTRY_IS_NULL 0x40000000
#define AGTENTRY_IS_CONTAINER 0x50000000 /* array or object */
#define AGTENTRY_IS_AGTYPE 0x70000000 /* our type designator */

/* Access macros.  Note possible multiple evaluations */
#define AGTE_OFFLENFLD(agte_) ((agte_)&AGTENTRY_OFFLENMASK)
#define AGTE_HAS_OFF(agte_) (((agte_)&AGTENTRY_HAS_OFF) != 0)
#define AGTE_IS_STRING(agte_) \
    (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_STRING)
#define AGTE_IS_NUMERIC(agte_) \
    (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_NUMERIC)
#define AGTE_IS_CONTAINER(agte_) \
    (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_CONTAINER)
#define AGTE_IS_NULL(agte_) (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_NULL)
#define AGTE_IS_BOOL_TRUE(agte_) \
    (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_BOOL_TRUE)
#define AGTE_IS_BOOL_FALSE(agte_) \
    (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_BOOL_FALSE)
#define AGTE_IS_BOOL(agte_) \
    (AGTE_IS_BOOL_TRUE(agte_) || AGTE_IS_BOOL_FALSE(agte_))
#define AGTE_IS_AGTYPE(agte_) \
    (((agte_)&AGTENTRY_TYPEMASK) == AGTENTRY_IS_AGTYPE)

/* Macro for advancing an offset variable to the next agtentry */
#define AGTE_ADVANCE_OFFSET(offset, agte) \
    do \
    { \
        agtentry agte_ = (agte); \
        if (AGTE_HAS_OFF(agte_)) \
            (offset) = AGTE_OFFLENFLD(agte_); \
        else \
            (offset) += AGTE_OFFLENFLD(agte_); \
    } while (0)

/*
 * We store an offset, not a length, every AGT_OFFSET_STRIDE children.
 * Caution: this macro should only be referenced when creating an agtype
 * value.  When examining an existing value, pay attention to the HAS_OFF
 * bits instead.  This allows changes in the offset-placement heuristic
 * without breaking on-disk compatibility.
 */
#define AGT_OFFSET_STRIDE 32

/*
 * An agtype array or object node, within an agtype Datum.
 *
 * An array has one child for each element, stored in array order.
 *
 * An object has two children for each key/value pair.  The keys all appear
 * first, in key sort order; then the values appear, in an order matching the
 * key order.  This arrangement keeps the keys compact in memory, making a
 * search for a particular key more cache-friendly.
 */
typedef struct agtype_container
{
    uint32 header; /* number of elements or key/value pairs, and flags */
    agtentry children[FLEXIBLE_ARRAY_MEMBER];

    /* the data for each child node follows. */
} agtype_container;

/* flags for the header-field in agtype_container*/
#define AGT_CMASK   0x0FFFFFFF /* mask for count field */
#define AGT_FSCALAR 0x10000000 /* flag bits */
#define AGT_FOBJECT 0x20000000
#define AGT_FARRAY  0x40000000
#define AGT_FBINARY 0x80000000 /* our binary objects */

/*
 * It should be noted that while AGT_FBINARY utilizes the AGTV_BINARY mechanism,
 * it is not necessarily an agtype serialized (binary) value. We are just using
 * that mechanism to pass blobs of data more quickly between components. In the
 * case of the path from the VLE routine, the blob is a graphid array where the
 * first element contains the header embedded in it. This way it is just cast to
 * the graphid array to be used after verifying that it is an AGT_FBINARY and an
 * AGT_FBINARY_TYPE_VLE_PATH.
 */

/*
 * Flags for the agtype_container type AGT_FBINARY are in the AGT_CMASK (count)
 * field. We put the flags here as this is a strictly AGTV_BINARY blob of data
 * and count is irrelevant because there is only one. The additional flags allow
 * for differing types of user defined binary blobs. To be consistent and clear,
 * we create binary specific masks, flags, and macros.
 */
#define AGT_FBINARY_MASK 0x0FFFFFFF /* mask for binary flags */
#define AGTYPE_FBINARY_CONTAINER_TYPE(agtc) ((agtc)->header &AGT_FBINARY_MASK)
#define AGT_ROOT_DATA_FBINARY(agtp_) VARDATA(agtp_);
#define AGT_FBINARY_TYPE_VLE_PATH 0x00000001

/* convenience macros for accessing an agtype_container struct */
#define AGTYPE_CONTAINER_SIZE(agtc) ((agtc)->header & AGT_CMASK)
#define AGTYPE_CONTAINER_IS_SCALAR(agtc) (((agtc)->header & AGT_FSCALAR) != 0)
#define AGTYPE_CONTAINER_IS_OBJECT(agtc) (((agtc)->header & AGT_FOBJECT) != 0)
#define AGTYPE_CONTAINER_IS_ARRAY(agtc) (((agtc)->header & AGT_FARRAY) != 0)
#define AGTYPE_CONTAINER_IS_BINARY(agtc) (((agtc)->header & AGT_FBINARY) != 0)

/* The top-level on-disk format for an agtype datum. */
typedef struct
{
    int32 vl_len_; /* varlena header (do not touch directly!) */
    agtype_container root;
} agtype;

/* convenience macros for accessing the root container in an agtype datum */
#define AGT_ROOT_COUNT(agtp_) (*(uint32 *)VARDATA(agtp_) & AGT_CMASK)
#define AGT_ROOT_IS_SCALAR(agtp_) \
    ((*(uint32 *)VARDATA(agtp_) & AGT_FSCALAR) != 0)
#define AGT_ROOT_IS_OBJECT(agtp_) \
    ((*(uint32 *)VARDATA(agtp_) & AGT_FOBJECT) != 0)
#define AGT_ROOT_IS_ARRAY(agtp_) \
    ((*(uint32 *)VARDATA(agtp_) & AGT_FARRAY) != 0)
#define AGT_ROOT_IS_BINARY(agtp_) \
    ((*(uint32 *)VARDATA(agtp_) & AGT_FBINARY) != 0)
#define AGT_ROOT_BINARY_FLAGS(agtp_) \
    (*(uint32 *)VARDATA(agtp_) & AGT_FBINARY_MASK)
#define AGT_ROOT_IS_VPC(agtp_) \
    (AGT_ROOT_IS_BINARY(agtp_) && (AGT_ROOT_BINARY_FLAGS(agtp_) == AGT_FBINARY_TYPE_VLE_PATH))

/* values for the AGTYPE header field to denote the stored data type */
#define AGT_HEADER_INTEGER 0x00000000
#define AGT_HEADER_FLOAT 0x00000001
#define AGT_HEADER_VERTEX 0x00000002
#define AGT_HEADER_EDGE 0x00000003
#define AGT_HEADER_PATH 0x00000004

/*
 * IMPORTANT NOTE: For agtype_value_type, IS_A_AGTYPE_SCALAR() checks that the
 * type is between AGTV_NULL and AGTV_ARRAY, excluding AGTV_ARRAY. So, new scalars need to
 * be between these values.
 */
enum agtype_value_type
{
    /* Scalar types */
    AGTV_NULL = 0x0,
    AGTV_STRING,
    AGTV_NUMERIC,
    AGTV_INTEGER,
    AGTV_FLOAT,
    AGTV_BOOL,
    AGTV_VERTEX,
    AGTV_EDGE,
    AGTV_PATH,
    /* Composite types */
    AGTV_ARRAY = 0x10,
    AGTV_OBJECT,
    /* Binary (i.e. struct agtype) AGTV_ARRAY/AGTV_OBJECT */
    AGTV_BINARY
};

/*
 * agtype_value: In-memory representation of agtype.  This is a convenient
 * deserialized representation, that can easily support using the "val"
 * union across underlying types during manipulation.  The agtype on-disk
 * representation has various alignment considerations.
 */
struct agtype_value
{
    enum agtype_value_type type; /* Influences sort order */

    union
    {
        int64 int_value; /* Cypher 8 byte Integer */
        float8 float_value; /* Cypher 8 byte Float */
        Numeric numeric;
        bool boolean;
        struct
        {
            int len;
            char *val; /* Not necessarily null-terminated */
        } string; /* String primitive type */

        struct
        {
            int num_elems;
            agtype_value *elems;
            bool raw_scalar; /* Top-level "raw scalar" array? */
        } array; /* Array container type */

        struct
        {
            int num_pairs; /* 1 pair, 2 elements */
            agtype_pair *pairs;
        } object; /* Associative container type */

        struct
        {
            int len;
            agtype_container *data;
        } binary; /* Array or object, in on-disk format */
    } val;
};

#define IS_A_AGTYPE_SCALAR(agtype_val) \
    ((agtype_val)->type >= AGTV_NULL && (agtype_val)->type < AGTV_ARRAY)

/*
 * Key/value pair within an Object.
 *
 * This struct type is only used briefly while constructing an agtype ; it is
 * *not* the on-disk representation.
 *
 * Pairs with duplicate keys are de-duplicated.  We store the originally
 * observed pair ordering for the purpose of removing duplicates in a
 * well-defined way (which is "last observed wins").
 */
struct agtype_pair
{
    agtype_value key; /* Must be a AGTV_STRING */
    agtype_value value; /* May be of any type */
    uint32 order; /* Pair's index in original sequence */
};

/* Conversion state used when parsing agtype from text, or for type coercion */
typedef struct agtype_parse_state
{
    agtype_value cont_val;
    Size size;
    struct agtype_parse_state *next;
    /*
     * This holds the last append_value scalar copy or the last append_element
     * scalar copy - it can only be one of the two. It is needed because when
     * an object or list is built, the upper level object or list will get a
     * copy of the result value on close. Our routines modify the value after
     * close and need this to update that value if necessary. Which is the
     * case for some casts.
     */
    agtype_value *last_updated_value;
} agtype_parse_state;

/*
 * agtype_iterator holds details of the type for each iteration. It also stores
 * an agtype varlena buffer, which can be directly accessed in some contexts.
 */
typedef enum
{
    AGTI_ARRAY_START,
    AGTI_ARRAY_ELEM,
    AGTI_OBJECT_START,
    AGTI_OBJECT_KEY,
    AGTI_OBJECT_VALUE
} agt_iterator_state;

typedef struct agtype_iterator
{
    /* Container being iterated */
    agtype_container *container;
    uint32 num_elems; /*
                       * Number of elements in children array (will be
                       * num_pairs for objects)
                       */
    bool is_scalar; /* Pseudo-array scalar value? */
    agtentry *children; /* agtentrys for child nodes */
    /* Data proper. This points to the beginning of the variable-length data */
    char *data_proper;

    /* Current item in buffer (up to num_elems) */
    int curr_index;

    /* Data offset corresponding to current item */
    uint32 curr_data_offset;

    /*
     * If the container is an object, we want to return keys and values
     * alternately; so curr_data_offset points to the current key, and
     * curr_value_offset points to the current value.
     */
    uint32 curr_value_offset;

    /* Private state */
    agt_iterator_state state;

    struct agtype_iterator *parent;
} agtype_iterator;

/* agtype parse state */
typedef struct agtype_in_state
{
    agtype_parse_state *parse_state;
    agtype_value *res;
} agtype_in_state;

/* Support functions */
int reserve_from_buffer(StringInfo buffer, int len);
short pad_buffer_to_int(StringInfo buffer);
uint32 get_agtype_offset(const agtype_container *agtc, int index);
uint32 get_agtype_length(const agtype_container *agtc, int index);
int compare_agtype_containers_orderability(agtype_container *a,
                                           agtype_container *b);
agtype_value *find_agtype_value_from_container(agtype_container *container,
                                               uint32 flags,
                                               agtype_value *key);
agtype_value *get_ith_agtype_value_from_container(agtype_container *container,
                                                  uint32 i);
agtype_value *push_agtype_value(agtype_parse_state **pstate,
                                agtype_iterator_token seq,
                                agtype_value *agtval);
agtype_iterator *agtype_iterator_init(agtype_container *container);
agtype_iterator_token agtype_iterator_next(agtype_iterator **it,
                                           agtype_value *val,
                                           bool skip_nested);
agtype *agtype_value_to_agtype(agtype_value *val);
bool agtype_deep_contains(agtype_iterator **val,
                          agtype_iterator **m_contained);
void agtype_hash_scalar_value(const agtype_value *scalar_val, uint32 *hash);
void agtype_hash_scalar_value_extended(const agtype_value *scalar_val,
                                       uint64 *hash, uint64 seed);
void convert_extended_array(StringInfo buffer, agtentry *pheader,
                            agtype_value *val);
void convert_extended_object(StringInfo buffer, agtentry *pheader,
                             agtype_value *val);
Datum get_numeric_datum_from_agtype_value(agtype_value *agtv);
bool is_numeric_result(agtype_value *lhs, agtype_value *rhs);
void copy_agtype_value(agtype_parse_state* pstate,
                       agtype_value* original_agtype_value,
                       agtype_value **copied_agtype_value, bool is_top_level);

/* agtype.c support functions */
/*
 * This is a shortcut for when using string constants to call
 * get_agtype_value_object_value.
 *
 * Note: sizeof() works here because we use string constants. Normally,
 * however, you should not use sizeof() in place of strlen().
 *
 * Note: We also subtract 1 from the value because sizeof() a string constant
 * includes the null terminator whereas strlen() does not and neither does
 * the string representation in agtype_value.
 */
#define GET_AGTYPE_VALUE_OBJECT_VALUE(agtv_object, search_key) \
        get_agtype_value_object_value(agtv_object, search_key, \
                                      sizeof(search_key) - 1)

agtype_value *get_agtype_value_object_value(const agtype_value *agtv_object,
                                            char *search_key,
                                            int search_key_length);
char *agtype_to_cstring(StringInfo out, agtype_container *in,
                        int estimated_len);
char *agtype_to_cstring_indent(StringInfo out, agtype_container *in,
                               int estimated_len);
size_t check_string_length(size_t len);
Datum integer_to_agtype(int64 i);
Datum float_to_agtype(float8 f);
Datum string_to_agtype(char *s);
Datum boolean_to_agtype(bool b);
void uniqueify_agtype_object(agtype_value *object);
char *agtype_value_type_to_string(enum agtype_value_type type);
bool is_decimal_needed(char *numstr);
int compare_agtype_scalar_values(agtype_value *a, agtype_value *b);
agtype_value *alter_property_value(agtype_value *properties, char *var_name,
                                   agtype *new_v, bool remove_property);
void remove_null_from_agtype_object(agtype_value *object);
agtype_value *alter_properties(agtype_value *original_properties,
                               agtype *new_properties);
agtype *get_one_agtype_from_variadic_args(FunctionCallInfo fcinfo,
                                          int variadic_offset,
                                          int expected_nargs);
Datum make_vertex(Datum id, Datum label, Datum properties);
Datum make_edge(Datum id, Datum startid, Datum endid, Datum label,
                   Datum properties);
Datum make_path(List *path);
Datum column_get_datum(TupleDesc tupdesc, HeapTuple tuple, int column,
                       const char *attname, Oid typid, bool isnull);
agtype_value *agtype_value_build_vertex(graphid id, char *label,
                                        Datum properties);
agtype_value *agtype_value_build_edge(graphid id, char *label, graphid end_id,
                                      graphid start_id, Datum properties);
agtype_value *get_agtype_value(char *funcname, agtype *agt_arg,
                               enum agtype_value_type type, bool error);
bool is_agtype_null(agtype *agt_arg);
agtype_value *string_to_agtype_value(char *s);
agtype_value *integer_to_agtype_value(int64 int_value);
void add_agtype(Datum val, bool is_null, agtype_in_state *result, Oid val_type,
                bool key_scalar);
agtype_value *extract_entity_properties(agtype *object, bool error_on_scalar);
agtype_iterator *get_next_list_element(agtype_iterator *it,
                                       agtype_container *agtc,
                                       agtype_value *elem);
void pfree_agtype_value(agtype_value* value);
void pfree_agtype_value_content(agtype_value* value);
void pfree_agtype_in_state(agtype_in_state* value);

/* Oid accessors for AGTYPE */
Oid get_AGTYPEOID(void);
Oid get_AGTYPEARRAYOID(void);
void clear_global_Oids_AGTYPE(void);
#define AGTYPEOID get_AGTYPEOID()
#define AGTYPEARRAYOID get_AGTYPEARRAYOID()

#endif
