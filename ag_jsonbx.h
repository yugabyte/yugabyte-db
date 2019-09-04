/*-------------------------------------------------------------------------
 *
 * jsonb.h
 *	  Declarations for jsonb data type support.
 *
 * Copyright (c) 1996-2018, PostgreSQL Global Development Group
 *
 * src/include/utils/jsonb.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef AG_AG_JSONB_H
#define AG_AG_JSONB_H

#include "lib/stringinfo.h"
#include "utils/array.h"
#include "utils/numeric.h"

/* Tokens used when sequentially processing a jsonb value */
typedef enum
{
	WJBX_DONE,
	WJBX_KEY,
	WJBX_VALUE,
	WJBX_ELEM,
	WJBX_BEGIN_ARRAY,
	WJBX_END_ARRAY,
	WJBX_BEGIN_OBJECT,
	WJBX_END_OBJECT
} JsonbXIteratorToken;

/* Strategy numbers for GIN index opclasses */
#define JsonbXContainsStrategyNumber	7
#define JsonbXExistsStrategyNumber		9
#define JsonbXExistsAnyStrategyNumber	10
#define JsonbXExistsAllStrategyNumber	11

/*
 * In the standard jsonb_ops GIN opclass for jsonb, we choose to index both
 * keys and values.  The storage format is text.  The first byte of the text
 * string distinguishes whether this is a key (always a string), null value,
 * boolean value, numeric value, or string value.  However, array elements
 * that are strings are marked as though they were keys; this imprecision
 * supports the definition of the "exists" operator, which treats array
 * elements like keys.  The remainder of the text string is empty for a null
 * value, "t" or "f" for a boolean value, a normalized print representation of
 * a numeric value, or the text of a string value.  However, if the length of
 * this text representation would exceed JGIN_MAXLENGTH bytes, we instead hash
 * the text representation and store an 8-hex-digit representation of the
 * uint32 hash value, marking the prefix byte with an additional bit to
 * distinguish that this has happened.  Hashing long strings saves space and
 * ensures that we won't overrun the maximum entry length for a GIN index.
 * (But JGIN_MAXLENGTH is quite a bit shorter than GIN's limit.  It's chosen
 * to ensure that the on-disk text datum will have a short varlena header.)
 * Note that when any hashed item appears in a query, we must recheck index
 * matches against the heap tuple; currently, this costs nothing because we
 * must always recheck for other reasons.
 */
#define JXGINFLAG_KEY	0x01	/* key (or string array element) */
#define JXGINFLAG_NULL	0x02	/* null value */
#define JXGINFLAG_BOOL	0x03	/* boolean value */
#define JXGINFLAG_NUM	0x04	/* numeric value */
#define JXGINFLAG_STR	0x05	/* string value (if not an array element) */
#define JXGINFLAG_HASHED 0x10	/* OR'd into flag if value was hashed */
#define JXGIN_MAXLENGTH	125		/* max length of text part before hashing */

/* Convenience macros */
#define DatumGetJsonbXP(d)		((JsonbX *) PG_DETOAST_DATUM(d))
#define JsonbXPGetDatum(p)		PointerGetDatum(p)
#define PG_GETARG_JSONBX_P(x)	DatumGetJsonbXP(PG_GETARG_DATUM(x))
#define PG_RETURN_JSONBX_P(x)	PG_RETURN_POINTER(x)

typedef struct JsonbXPair JsonbXPair;
typedef struct JsonbXValue JsonbXValue;

/*
 * Jsonbs are varlena objects, so must meet the varlena convention that the
 * first int32 of the object contains the total object size in bytes.  Be sure
 * to use VARSIZE() and SET_VARSIZE() to access it, though!
 *
 * JsonbX is the on-disk representation, in contrast to the in-memory JsonbValue
 * representation.  Often, JsonbValues are just shims through which a Jsonb
 * buffer is accessed, but they can also be deep copied and passed around.
 *
 * JsonbX is a tree structure. Each node in the tree consists of a JXEntry
 * header and a variable-length content (possibly of zero size).  The JXEntry
 * header indicates what kind of a node it is, e.g. a string or an array,
 * and provides the length of its variable-length portion.
 *
 * The JXEntry and the content of a node are not stored physically together.
 * Instead, the container array or object has an array that holds the JXEntrys
 * of all the child nodes, followed by their variable-length portions.
 *
 * The root node is an exception; it has no parent array or object that could
 * hold its JXEntry. Hence, no JXEntry header is stored for the root node.  It
 * is implicitly known that the root node must be an array or an object,
 * so we can get away without the type indicator as long as we can distinguish
 * the two.  For that purpose, both an array and an object begin with a uint32
 * header field, which contains an JBX_FOBJECT or JBX_FARRAY flag.  When a naked
 * scalar value needs to be stored as a JsonbX value, what we actually store is
 * an array with one element, with the flags in the array's header field set
 * to JBX_FSCALAR | JBX_FARRAY.
 *
 * Overall, the JsonbX struct requires 4-bytes alignment. Within the struct,
 * the variable-length portion of some node types is aligned to a 4-byte
 * boundary, while others are not. When alignment is needed, the padding is
 * in the beginning of the node that requires it. For example, if a numeric
 * node is stored after a string node, so that the numeric node begins at
 * offset 3, the variable-length portion of the numeric node will begin with
 * one padding byte so that the actual numeric data is 4-byte aligned.
 */

/*
 * JXEntry format.
 *
 * The least significant 28 bits store either the data length of the entry,
 * or its end+1 offset from the start of the variable-length portion of the
 * containing object.  The next three bits store the type of the entry, and
 * the high-order bit tells whether the least significant bits store a length
 * or an offset.
 *
 * The reason for the offset-or-length complication is to compromise between
 * access speed and data compressibility.  In the initial design each JXEntry
 * always stored an offset, but this resulted in JXEntry arrays with horrible
 * compressibility properties, so that TOAST compression of a JSONB did not
 * work well.  Storing only lengths would greatly improve compressibility,
 * but it makes random access into large arrays expensive (O(N) not O(1)).
 * So what we do is store an offset in every JB_OFFSET_STRIDE'th JXEntry and
 * a length in the rest.  This results in reasonably compressible data (as
 * long as the stride isn't too small).  We may have to examine as many as
 * JB_OFFSET_STRIDE JXEntrys in order to find out the offset or length of any
 * given item, but that's still O(1) no matter how large the container is.
 *
 * We could avoid eating a flag bit for this purpose if we were to store
 * the stride in the container header, or if we were willing to treat the
 * stride as an unchangeable constant.  Neither of those options is very
 * attractive though.
 */
typedef uint32 JXEntry;

#define JXENTRY_OFFLENMASK		0x0FFFFFFF
#define JXENTRY_TYPEMASK		0x70000000
#define JXENTRY_HAS_OFF			0x80000000

/* values stored in the type bits */
#define JXENTRY_ISSTRING		0x00000000
#define JXENTRY_ISNUMERIC		0x10000000
#define JXENTRY_ISBOOL_FALSE	0x20000000
#define JXENTRY_ISBOOL_TRUE		0x30000000
#define JXENTRY_ISNULL			0x40000000
#define JXENTRY_ISCONTAINER		0x50000000	/* array or object */
#define JXENTRY_ISJSONBX		0x70000000  // out type designator

/* values for the JBX header field when the JXENTRY_ISJSONBX flag is set */
#define JBX_HEADER_INTEGER8		0x00000000
#define JBX_HEADER_FLOAT8		0x00000001

/* Access macros.  Note possible multiple evaluations */
#define JBXE_OFFLENFLD(je_)		((je_) & JXENTRY_OFFLENMASK)
#define JBXE_HAS_OFF(je_)		(((je_) & JXENTRY_HAS_OFF) != 0)
#define JBXE_ISSTRING(je_)		(((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISSTRING)
#define JBXE_ISNUMERIC(je_)		(((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISNUMERIC)
#define JBXE_ISCONTAINER(je_)	(((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISCONTAINER)
#define JBXE_ISNULL(je_)		(((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISNULL)
#define JBXE_ISBOOL_TRUE(je_)	(((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISBOOL_TRUE)
#define JBXE_ISBOOL_FALSE(je_)	(((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISBOOL_FALSE)
#define JBXE_ISBOOL(je_)		(JBXE_ISBOOL_TRUE(je_) || JBXE_ISBOOL_FALSE(je_))
#define JBXE_ISJSONBX(je_)      (((je_) & JXENTRY_TYPEMASK) == JXENTRY_ISJSONBX)

/* Macro for advancing an offset variable to the next JXEntry */
#define JBXE_ADVANCE_OFFSET(offset, je) \
	do { \
		JXEntry	je_ = (je); \
		if (JBXE_HAS_OFF(je_)) \
			(offset) = JBXE_OFFLENFLD(je_); \
		else \
			(offset) += JBXE_OFFLENFLD(je_); \
	} while(0)

/*
 * We store an offset, not a length, every JB_OFFSET_STRIDE children.
 * Caution: this macro should only be referenced when creating a JSONB
 * value.  When examining an existing value, pay attention to the HAS_OFF
 * bits instead.  This allows changes in the offset-placement heuristic
 * without breaking on-disk compatibility.
 */
#define JBX_OFFSET_STRIDE		32

/*
 * A jsonbx array or object node, within a JsonbX Datum.
 *
 * An array has one child for each element, stored in array order.
 *
 * An object has two children for each key/value pair.  The keys all appear
 * first, in key sort order; then the values appear, in an order matching the
 * key order.  This arrangement keeps the keys compact in memory, making a
 * search for a particular key more cache-friendly.
 */
typedef struct JsonbXContainer
{
	uint32		header;			/* number of elements or key/value pairs, and
								 * flags */
	JXEntry		children[FLEXIBLE_ARRAY_MEMBER];

	/* the data for each child node follows. */
} JsonbXContainer;

/* flags for the header-field in JsonbContainer */
#define JBX_CMASK				0x0FFFFFFF	/* mask for count field */
#define JBX_FSCALAR				0x10000000	/* flag bits */
#define JBX_FOBJECT				0x20000000
#define JBX_FARRAY				0x40000000

/* convenience macros for accessing a JsonbXContainer struct */
#define JsonbXContainerSize(jc)		((jc)->header & JBX_CMASK)
#define JsonbXContainerIsScalar(jc)	(((jc)->header & JBX_FSCALAR) != 0)
#define JsonbXContainerIsObject(jc)	(((jc)->header & JBX_FOBJECT) != 0)
#define JsonbXContainerIsArray(jc)	(((jc)->header & JBX_FARRAY) != 0)

/* The top-level on-disk format for a jsonbx datum. */
typedef struct
{
	int32		vl_len_;		/* varlena header (do not touch directly!) */
	JsonbXContainer root;
} JsonbX;

/* convenience macros for accessing the root container in a JsonbX datum */
#define JBX_ROOT_COUNT(jbxp_)		(*(uint32 *) VARDATA(jbxp_) & JBX_CMASK)
#define JBX_ROOT_IS_SCALAR(jbxp_)	((*(uint32 *) VARDATA(jbxp_) & JBX_FSCALAR) != 0)
#define JBX_ROOT_IS_OBJECT(jbxp_)	((*(uint32 *) VARDATA(jbxp_) & JBX_FOBJECT) != 0)
#define JBX_ROOT_IS_ARRAY(jbxp_)	((*(uint32 *) VARDATA(jbxp_) & JBX_FARRAY) != 0)

/*
 * IMPORTANT NOTE: For jbvXType, IsAJsonbXScalar() checks that the type is
 * between jbvXNull and jbvXBool, inclusive. So, new scalars need to be
 * between these values.
 */
enum jbvXType
{
	/* Scalar types */
	jbvXNull = 0x0,
	jbvXString,
	jbvXNumeric,
	jbvXInteger8,
	jbvXFloat8,
	jbvXBool,
	/* Composite types */
	jbvXArray = 0x10,
	jbvXObject,
	/* Binary (i.e. struct Jsonb) jbvArray/jbvObject */
	jbvXBinary
};

/*
 * JsonbXValue:	In-memory representation of Jsonb.  This is a convenient
 * deserialized representation, that can easily support using the "val"
 * union across underlying types during manipulation.  The JsonbX on-disk
 * representation has various alignment considerations.
 */
struct JsonbXValue
{
	enum jbvXType type;			/* Influences sort order */

	union
	{
		int64	integer8;		/* Cypher 8 byte Integer */
		float8	float8;			/* Cypher 8 byte Float */
		Numeric numeric;
		bool		boolean;
		struct
		{
			int			len;
			char	   *val;	/* Not necessarily null-terminated */
		}			string;		/* String primitive type */

		struct
		{
			int			nElems;
			JsonbXValue *elems;
			bool		rawScalar;	/* Top-level "raw scalar" array? */
		}			array;		/* Array container type */

		struct
		{
			int			nPairs; /* 1 pair, 2 elements */
			JsonbXPair  *pairs;
		}			object;		/* Associative container type */

		struct
		{
			int			len;
			JsonbXContainer *data;
		}			binary;		/* Array or object, in on-disk format */
	}			val;
};

#define IsAJsonbXScalar(jsonbXval)	((jsonbXval)->type >= jbvXNull && \
									(jsonbXval)->type <= jbvXBool)

/*
 * Key/value pair within an Object.
 *
 * This struct type is only used briefly while constructing a Jsonb; it is
 * *not* the on-disk representation.
 *
 * Pairs with duplicate keys are de-duplicated.  We store the originally
 * observed pair ordering for the purpose of removing duplicates in a
 * well-defined way (which is "last observed wins").
 */
struct JsonbXPair
{
	JsonbXValue	key;			/* Must be a jbvString */
	JsonbXValue	value;			/* May be of any type */
	uint32		order;			/* Pair's index in original sequence */
};

/* Conversion state used when parsing JsonbX from text, or for type coercion */
typedef struct JsonbXParseState
{
	JsonbXValue	contVal;
	Size		size;
	struct JsonbXParseState *next;
} JsonbXParseState;

/*
 * JsonbIterator holds details of the type for each iteration. It also stores a
 * JsonbX varlena buffer, which can be directly accessed in some contexts.
 */
typedef enum
{
	JBXI_ARRAY_START,
	JBXI_ARRAY_ELEM,
	JBXI_OBJECT_START,
	JBXI_OBJECT_KEY,
	JBXI_OBJECT_VALUE
} JsonbXIterState;

typedef struct JsonbXIterator
{
	/* Container being iterated */
	JsonbXContainer *container;
	uint32		nElems;			/* Number of elements in children array (will
								 * be nPairs for objects) */
	bool		isScalar;		/* Pseudo-array scalar value? */
	JXEntry	   *children;		/* JXEntrys for child nodes */
	/* Data proper.  This points to the beginning of the variable-length data */
	char	   *dataProper;

	/* Current item in buffer (up to nElems) */
	int			curIndex;

	/* Data offset corresponding to current item */
	uint32		curDataOffset;

	/*
	 * If the container is an object, we want to return keys and values
	 * alternately; so curDataOffset points to the current key, and
	 * curValueOffset points to the current value.
	 */
	uint32		curValueOffset;

	/* Private state */
	JsonbXIterState state;

	struct JsonbXIterator *parent;
} JsonbXIterator;

/* Support functions */
extern uint32 getJsonbXOffset(const JsonbXContainer *jc, int index);
extern uint32 getJsonbXLength(const JsonbXContainer *jc, int index);
extern int	compareJsonbXContainers(JsonbXContainer *a, JsonbXContainer *b);
extern JsonbXValue *findJsonbXValueFromContainer(JsonbXContainer *sheader,
												 uint32 flags,
												 JsonbXValue *key);
extern JsonbXValue *getIthJsonbXValueFromContainer(JsonbXContainer *sheader,
												   uint32 i);
extern JsonbXValue *pushJsonbXValue(JsonbXParseState **pstate,
									JsonbXIteratorToken seq,
									JsonbXValue *jbVal);
extern JsonbXIterator *JsonbXIteratorInit(JsonbXContainer *container);
extern JsonbXIteratorToken JsonbXIteratorNext(JsonbXIterator **it,
											  JsonbXValue *val,
											  bool skipNested);
extern JsonbX *JsonbXValueToJsonbX(JsonbXValue *val);
extern bool JsonbXDeepContains(JsonbXIterator **val,
							   JsonbXIterator **mContained);
extern void JsonbXHashScalarValue(const JsonbXValue *scalarVal, uint32 *hash);
extern void JsonbXHashScalarValueExtended(const JsonbXValue *scalarVal,
										  uint64 *hash, uint64 seed);

/* jsonb.c support functions */
extern char *JsonbXToCString(StringInfo out, JsonbXContainer *in,
							 int estimated_len);
extern char *JsonbXToCStringIndent(StringInfo out, JsonbXContainer *in,
								   int estimated_len);

#endif							/* AG_AG_JSONB_H */
