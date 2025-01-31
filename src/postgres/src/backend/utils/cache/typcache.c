/*-------------------------------------------------------------------------
 *
 * typcache.c
 *	  POSTGRES type cache code
 *
 * The type cache exists to speed lookup of certain information about data
 * types that is not directly available from a type's pg_type row.  For
 * example, we use a type's default btree opclass, or the default hash
 * opclass if no btree opclass exists, to determine which operators should
 * be used for grouping and sorting the type (GROUP BY, ORDER BY ASC/DESC).
 *
 * Several seemingly-odd choices have been made to support use of the type
 * cache by generic array and record handling routines, such as array_eq(),
 * record_cmp(), and hash_array().  Because those routines are used as index
 * support operations, they cannot leak memory.  To allow them to execute
 * efficiently, all information that they would like to re-use across calls
 * is kept in the type cache.
 *
 * Once created, a type cache entry lives as long as the backend does, so
 * there is no need for a call to release a cache entry.  If the type is
 * dropped, the cache entry simply becomes wasted storage.  This is not
 * expected to happen often, and assuming that typcache entries are good
 * permanently allows caching pointers to them in long-lived places.
 *
 * We have some provisions for updating cache entries if the stored data
 * becomes obsolete.  Core data extracted from the pg_type row is updated
 * when we detect updates to pg_type.  Information dependent on opclasses is
 * cleared if we detect updates to pg_opclass.  We also support clearing the
 * tuple descriptor and operator/function parts of a rowtype's cache entry,
 * since those may need to change as a consequence of ALTER TABLE.  Domain
 * constraint changes are also tracked properly.
 *
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * IDENTIFICATION
 *	  src/backend/utils/cache/typcache.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>

#include "access/hash.h"
#include "access/htup_details.h"
#include "access/nbtree.h"
#include "access/parallel.h"
#include "access/relation.h"
#include "access/session.h"
#include "access/table.h"
#include "catalog/pg_am.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_enum.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_range.h"
#include "catalog/pg_type.h"
#include "commands/defrem.h"
#include "executor/executor.h"
#include "lib/dshash.h"
#include "optimizer/optimizer.h"
#include "port/pg_bitutils.h"
#include "storage/lwlock.h"
#include "utils/builtins.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"
#include "utils/syscache.h"
#include "utils/typcache.h"

/*  YB includes. */
#include "pg_yb_utils.h"

/* The main type cache hashtable searched by lookup_type_cache */
static HTAB *TypeCacheHash = NULL;

/* List of type cache entries for domain types */
static TypeCacheEntry *firstDomainTypeEntry = NULL;

/* Private flag bits in the TypeCacheEntry.flags field */
#define TCFLAGS_HAVE_PG_TYPE_DATA			0x000001
#define TCFLAGS_CHECKED_BTREE_OPCLASS		0x000002
#define TCFLAGS_CHECKED_HASH_OPCLASS		0x000004
#define TCFLAGS_CHECKED_EQ_OPR				0x000008
#define TCFLAGS_CHECKED_LT_OPR				0x000010
#define TCFLAGS_CHECKED_GT_OPR				0x000020
#define TCFLAGS_CHECKED_CMP_PROC			0x000040
#define TCFLAGS_CHECKED_HASH_PROC			0x000080
#define TCFLAGS_CHECKED_HASH_EXTENDED_PROC	0x000100
#define TCFLAGS_CHECKED_ELEM_PROPERTIES		0x000200
#define TCFLAGS_HAVE_ELEM_EQUALITY			0x000400
#define TCFLAGS_HAVE_ELEM_COMPARE			0x000800
#define TCFLAGS_HAVE_ELEM_HASHING			0x001000
#define TCFLAGS_HAVE_ELEM_EXTENDED_HASHING	0x002000
#define TCFLAGS_CHECKED_FIELD_PROPERTIES	0x004000
#define TCFLAGS_HAVE_FIELD_EQUALITY			0x008000
#define TCFLAGS_HAVE_FIELD_COMPARE			0x010000
#define TCFLAGS_HAVE_FIELD_HASHING			0x020000
#define TCFLAGS_HAVE_FIELD_EXTENDED_HASHING	0x040000
#define TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS	0x080000
#define TCFLAGS_DOMAIN_BASE_IS_COMPOSITE	0x100000

/* The flags associated with equality/comparison/hashing are all but these: */
#define TCFLAGS_OPERATOR_FLAGS \
	(~(TCFLAGS_HAVE_PG_TYPE_DATA | \
	   TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS | \
	   TCFLAGS_DOMAIN_BASE_IS_COMPOSITE))

/*
 * Data stored about a domain type's constraints.  Note that we do not create
 * this struct for the common case of a constraint-less domain; we just set
 * domainData to NULL to indicate that.
 *
 * Within a DomainConstraintCache, we store expression plan trees, but the
 * check_exprstate fields of the DomainConstraintState nodes are just NULL.
 * When needed, expression evaluation nodes are built by flat-copying the
 * DomainConstraintState nodes and applying ExecInitExpr to check_expr.
 * Such a node tree is not part of the DomainConstraintCache, but is
 * considered to belong to a DomainConstraintRef.
 */
struct DomainConstraintCache
{
	List	   *constraints;	/* list of DomainConstraintState nodes */
	MemoryContext dccContext;	/* memory context holding all associated data */
	long		dccRefCount;	/* number of references to this struct */
};

/* Private information to support comparisons of enum values */
typedef struct
{
	Oid			enum_oid;		/* OID of one enum value */
	float4		sort_order;		/* its sort position */
} EnumItem;

typedef struct TypeCacheEnumData
{
	Oid			bitmap_base;	/* OID corresponding to bit 0 of bitmapset */
	Bitmapset  *sorted_values;	/* Set of OIDs known to be in order */
	int			num_values;		/* total number of values in enum */
	EnumItem	enum_values[FLEXIBLE_ARRAY_MEMBER];
} TypeCacheEnumData;

/*
 * We use a separate table for storing the definitions of non-anonymous
 * record types.  Once defined, a record type will be remembered for the
 * life of the backend.  Subsequent uses of the "same" record type (where
 * sameness means equalTupleDescs) will refer to the existing table entry.
 *
 * Stored record types are remembered in a linear array of TupleDescs,
 * which can be indexed quickly with the assigned typmod.  There is also
 * a hash table to speed searches for matching TupleDescs.
 */

typedef struct RecordCacheEntry
{
	TupleDesc	tupdesc;
} RecordCacheEntry;

/*
 * To deal with non-anonymous record types that are exchanged by backends
 * involved in a parallel query, we also need a shared version of the above.
 */
struct SharedRecordTypmodRegistry
{
	/* A hash table for finding a matching TupleDesc. */
	dshash_table_handle record_table_handle;
	/* A hash table for finding a TupleDesc by typmod. */
	dshash_table_handle typmod_table_handle;
	/* A source of new record typmod numbers. */
	pg_atomic_uint32 next_typmod;
};

/*
 * When using shared tuple descriptors as hash table keys we need a way to be
 * able to search for an equal shared TupleDesc using a backend-local
 * TupleDesc.  So we use this type which can hold either, and hash and compare
 * functions that know how to handle both.
 */
typedef struct SharedRecordTableKey
{
	union
	{
		TupleDesc	local_tupdesc;
		dsa_pointer shared_tupdesc;
	}			u;
	bool		shared;
} SharedRecordTableKey;

/*
 * The shared version of RecordCacheEntry.  This lets us look up a typmod
 * using a TupleDesc which may be in local or shared memory.
 */
typedef struct SharedRecordTableEntry
{
	SharedRecordTableKey key;
} SharedRecordTableEntry;

/*
 * An entry in SharedRecordTypmodRegistry's typmod table.  This lets us look
 * up a TupleDesc in shared memory using a typmod.
 */
typedef struct SharedTypmodTableEntry
{
	uint32		typmod;
	dsa_pointer shared_tupdesc;
} SharedTypmodTableEntry;

/*
 * A comparator function for SharedRecordTableKey.
 */
static int
shared_record_table_compare(const void *a, const void *b, size_t size,
							void *arg)
{
	dsa_area   *area = (dsa_area *) arg;
	SharedRecordTableKey *k1 = (SharedRecordTableKey *) a;
	SharedRecordTableKey *k2 = (SharedRecordTableKey *) b;
	TupleDesc	t1;
	TupleDesc	t2;

	if (k1->shared)
		t1 = (TupleDesc) dsa_get_address(area, k1->u.shared_tupdesc);
	else
		t1 = k1->u.local_tupdesc;

	if (k2->shared)
		t2 = (TupleDesc) dsa_get_address(area, k2->u.shared_tupdesc);
	else
		t2 = k2->u.local_tupdesc;

	return equalTupleDescs(t1, t2) ? 0 : 1;
}

/*
 * A hash function for SharedRecordTableKey.
 */
static uint32
shared_record_table_hash(const void *a, size_t size, void *arg)
{
	dsa_area   *area = (dsa_area *) arg;
	SharedRecordTableKey *k = (SharedRecordTableKey *) a;
	TupleDesc	t;

	if (k->shared)
		t = (TupleDesc) dsa_get_address(area, k->u.shared_tupdesc);
	else
		t = k->u.local_tupdesc;

	return hashTupleDesc(t);
}

/* Parameters for SharedRecordTypmodRegistry's TupleDesc table. */
static const dshash_parameters srtr_record_table_params = {
	sizeof(SharedRecordTableKey),	/* unused */
	sizeof(SharedRecordTableEntry),
	shared_record_table_compare,
	shared_record_table_hash,
	LWTRANCHE_PER_SESSION_RECORD_TYPE
};

/* Parameters for SharedRecordTypmodRegistry's typmod hash table. */
static const dshash_parameters srtr_typmod_table_params = {
	sizeof(uint32),
	sizeof(SharedTypmodTableEntry),
	dshash_memcmp,
	dshash_memhash,
	LWTRANCHE_PER_SESSION_RECORD_TYPMOD
};

/* hashtable for recognizing registered record types */
static HTAB *RecordCacheHash = NULL;

/* arrays of info about registered record types, indexed by assigned typmod */
static TupleDesc *RecordCacheArray = NULL;
static uint64 *RecordIdentifierArray = NULL;
static int32 RecordCacheArrayLen = 0;	/* allocated length of above arrays */
static int32 NextRecordTypmod = 0;	/* number of entries used */

/*
 * Process-wide counter for generating unique tupledesc identifiers.
 * Zero and one (INVALID_TUPLEDESC_IDENTIFIER) aren't allowed to be chosen
 * as identifiers, so we start the counter at INVALID_TUPLEDESC_IDENTIFIER.
 */
static uint64 tupledesc_id_counter = INVALID_TUPLEDESC_IDENTIFIER;

static void load_typcache_tupdesc(TypeCacheEntry *typentry);
static void load_rangetype_info(TypeCacheEntry *typentry);
static void load_multirangetype_info(TypeCacheEntry *typentry);
static void load_domaintype_info(TypeCacheEntry *typentry);
static int	dcs_cmp(const void *a, const void *b);
static void decr_dcc_refcount(DomainConstraintCache *dcc);
static void dccref_deletion_callback(void *arg);
static List *prep_domain_constraints(List *constraints, MemoryContext execctx);
static bool array_element_has_equality(TypeCacheEntry *typentry);
static bool array_element_has_compare(TypeCacheEntry *typentry);
static bool array_element_has_hashing(TypeCacheEntry *typentry);
static bool array_element_has_extended_hashing(TypeCacheEntry *typentry);
static void cache_array_element_properties(TypeCacheEntry *typentry);
static bool record_fields_have_equality(TypeCacheEntry *typentry);
static bool record_fields_have_compare(TypeCacheEntry *typentry);
static bool record_fields_have_hashing(TypeCacheEntry *typentry);
static bool record_fields_have_extended_hashing(TypeCacheEntry *typentry);
static void cache_record_field_properties(TypeCacheEntry *typentry);
static bool range_element_has_hashing(TypeCacheEntry *typentry);
static bool range_element_has_extended_hashing(TypeCacheEntry *typentry);
static void cache_range_element_properties(TypeCacheEntry *typentry);
static bool multirange_element_has_hashing(TypeCacheEntry *typentry);
static bool multirange_element_has_extended_hashing(TypeCacheEntry *typentry);
static void cache_multirange_element_properties(TypeCacheEntry *typentry);
static void TypeCacheRelCallback(Datum arg, Oid relid);
static void TypeCacheTypCallback(Datum arg, int cacheid, uint32 hashvalue);
static void TypeCacheOpcCallback(Datum arg, int cacheid, uint32 hashvalue);
static void TypeCacheConstrCallback(Datum arg, int cacheid, uint32 hashvalue);
static void load_enum_cache_data(TypeCacheEntry *tcache);
static EnumItem *find_enumitem(TypeCacheEnumData *enumdata, Oid arg);
static int	enum_oid_cmp(const void *left, const void *right);
static void shared_record_typmod_registry_detach(dsm_segment *segment,
												 Datum datum);
static TupleDesc find_or_make_matching_shared_tupledesc(TupleDesc tupdesc);
static dsa_pointer share_tupledesc(dsa_area *area, TupleDesc tupdesc,
								   uint32 typmod);


/*
 * lookup_type_cache
 *
 * Fetch the type cache entry for the specified datatype, and make sure that
 * all the fields requested by bits in 'flags' are valid.
 *
 * The result is never NULL --- we will ereport() if the passed type OID is
 * invalid.  Note however that we may fail to find one or more of the
 * values requested by 'flags'; the caller needs to check whether the fields
 * are InvalidOid or not.
 */
TypeCacheEntry *
lookup_type_cache(Oid type_id, int flags)
{
	TypeCacheEntry *typentry;
	bool		found;

	if (TypeCacheHash == NULL)
	{
		/* First time through: initialize the hash table */
		HASHCTL		ctl;

		ctl.keysize = sizeof(Oid);
		ctl.entrysize = sizeof(TypeCacheEntry);
		TypeCacheHash = hash_create("Type information cache", 64,
									&ctl, HASH_ELEM | HASH_BLOBS);

		/* Also set up callbacks for SI invalidations */
		CacheRegisterRelcacheCallback(TypeCacheRelCallback, (Datum) 0);
		CacheRegisterSyscacheCallback(TYPEOID, TypeCacheTypCallback, (Datum) 0);
		CacheRegisterSyscacheCallback(CLAOID, TypeCacheOpcCallback, (Datum) 0);
		CacheRegisterSyscacheCallback(CONSTROID, TypeCacheConstrCallback, (Datum) 0);

		/* Also make sure CacheMemoryContext exists */
		if (!CacheMemoryContext)
			CreateCacheMemoryContext();
	}

	/* Try to look up an existing entry */
	typentry = (TypeCacheEntry *) hash_search(TypeCacheHash,
											  (void *) &type_id,
											  HASH_FIND, NULL);
	if (typentry == NULL)
	{
		/*
		 * If we didn't find one, we want to make one.  But first look up the
		 * pg_type row, just to make sure we don't make a cache entry for an
		 * invalid type OID.  If the type OID is not valid, present a
		 * user-facing error, since some code paths such as domain_in() allow
		 * this function to be reached with a user-supplied OID.
		 */
		HeapTuple	tp;
		Form_pg_type typtup;

		tp = SearchSysCache1(TYPEOID, ObjectIdGetDatum(type_id));
		if (!HeapTupleIsValid(tp))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type with OID %u does not exist", type_id)));
		typtup = (Form_pg_type) GETSTRUCT(tp);
		if (!typtup->typisdefined)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" is only a shell",
							NameStr(typtup->typname))));

		/* Now make the typcache entry */
		typentry = (TypeCacheEntry *) hash_search(TypeCacheHash,
												  (void *) &type_id,
												  HASH_ENTER, &found);
		Assert(!found);			/* it wasn't there a moment ago */

		MemSet(typentry, 0, sizeof(TypeCacheEntry));

		/* These fields can never change, by definition */
		typentry->type_id = type_id;
		typentry->type_id_hash = GetSysCacheHashValue1(TYPEOID,
													   ObjectIdGetDatum(type_id));

		/* Keep this part in sync with the code below */
		typentry->typlen = typtup->typlen;
		typentry->typbyval = typtup->typbyval;
		typentry->typalign = typtup->typalign;
		typentry->typstorage = typtup->typstorage;
		typentry->typtype = typtup->typtype;
		typentry->typrelid = typtup->typrelid;
		typentry->typsubscript = typtup->typsubscript;
		typentry->typelem = typtup->typelem;
		typentry->typcollation = typtup->typcollation;
		typentry->flags |= TCFLAGS_HAVE_PG_TYPE_DATA;

		/* If it's a domain, immediately thread it into the domain cache list */
		if (typentry->typtype == TYPTYPE_DOMAIN)
		{
			typentry->nextDomain = firstDomainTypeEntry;
			firstDomainTypeEntry = typentry;
		}

		ReleaseSysCache(tp);
	}
	else if (!(typentry->flags & TCFLAGS_HAVE_PG_TYPE_DATA))
	{
		/*
		 * We have an entry, but its pg_type row got changed, so reload the
		 * data obtained directly from pg_type.
		 */
		HeapTuple	tp;
		Form_pg_type typtup;

		tp = SearchSysCache1(TYPEOID, ObjectIdGetDatum(type_id));
		if (!HeapTupleIsValid(tp))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type with OID %u does not exist", type_id)));
		typtup = (Form_pg_type) GETSTRUCT(tp);
		if (!typtup->typisdefined)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" is only a shell",
							NameStr(typtup->typname))));

		/*
		 * Keep this part in sync with the code above.  Many of these fields
		 * shouldn't ever change, particularly typtype, but copy 'em anyway.
		 */
		typentry->typlen = typtup->typlen;
		typentry->typbyval = typtup->typbyval;
		typentry->typalign = typtup->typalign;
		typentry->typstorage = typtup->typstorage;
		typentry->typtype = typtup->typtype;
		typentry->typrelid = typtup->typrelid;
		typentry->typsubscript = typtup->typsubscript;
		typentry->typelem = typtup->typelem;
		typentry->typcollation = typtup->typcollation;
		typentry->flags |= TCFLAGS_HAVE_PG_TYPE_DATA;

		ReleaseSysCache(tp);
	}

	/*
	 * Look up opclasses if we haven't already and any dependent info is
	 * requested.
	 */
	if ((flags & (TYPECACHE_EQ_OPR | TYPECACHE_LT_OPR | TYPECACHE_GT_OPR |
				  TYPECACHE_CMP_PROC |
				  TYPECACHE_EQ_OPR_FINFO | TYPECACHE_CMP_PROC_FINFO |
				  TYPECACHE_BTREE_OPFAMILY)) &&
		!(typentry->flags & TCFLAGS_CHECKED_BTREE_OPCLASS))
	{
		Oid			opclass;

		opclass = GetDefaultOpClass(type_id, IsYugaByteEnabled() ? LSM_AM_OID : BTREE_AM_OID);
		if (OidIsValid(opclass))
		{
			typentry->btree_opf = get_opclass_family(opclass);
			typentry->btree_opintype = get_opclass_input_type(opclass);
		}
		else
		{
			typentry->btree_opf = typentry->btree_opintype = InvalidOid;
		}

		/*
		 * Reset information derived from btree opclass.  Note in particular
		 * that we'll redetermine the eq_opr even if we previously found one;
		 * this matters in case a btree opclass has been added to a type that
		 * previously had only a hash opclass.
		 */
		typentry->flags &= ~(TCFLAGS_CHECKED_EQ_OPR |
							 TCFLAGS_CHECKED_LT_OPR |
							 TCFLAGS_CHECKED_GT_OPR |
							 TCFLAGS_CHECKED_CMP_PROC);
		typentry->flags |= TCFLAGS_CHECKED_BTREE_OPCLASS;
	}

	/*
	 * If we need to look up equality operator, and there's no btree opclass,
	 * force lookup of hash opclass.
	 */
	if ((flags & (TYPECACHE_EQ_OPR | TYPECACHE_EQ_OPR_FINFO)) &&
		!(typentry->flags & TCFLAGS_CHECKED_EQ_OPR) &&
		typentry->btree_opf == InvalidOid)
		flags |= TYPECACHE_HASH_OPFAMILY;

	if ((flags & (TYPECACHE_HASH_PROC | TYPECACHE_HASH_PROC_FINFO |
				  TYPECACHE_HASH_EXTENDED_PROC |
				  TYPECACHE_HASH_EXTENDED_PROC_FINFO |
				  TYPECACHE_HASH_OPFAMILY)) &&
		!(typentry->flags & TCFLAGS_CHECKED_HASH_OPCLASS))
	{
		Oid			opclass;

		opclass = GetDefaultOpClass(type_id, HASH_AM_OID);
		if (OidIsValid(opclass))
		{
			typentry->hash_opf = get_opclass_family(opclass);
			typentry->hash_opintype = get_opclass_input_type(opclass);
		}
		else
		{
			typentry->hash_opf = typentry->hash_opintype = InvalidOid;
		}

		/*
		 * Reset information derived from hash opclass.  We do *not* reset the
		 * eq_opr; if we already found one from the btree opclass, that
		 * decision is still good.
		 */
		typentry->flags &= ~(TCFLAGS_CHECKED_HASH_PROC |
							 TCFLAGS_CHECKED_HASH_EXTENDED_PROC);
		typentry->flags |= TCFLAGS_CHECKED_HASH_OPCLASS;
	}

	/*
	 * Look for requested operators and functions, if we haven't already.
	 */
	if ((flags & (TYPECACHE_EQ_OPR | TYPECACHE_EQ_OPR_FINFO)) &&
		!(typentry->flags & TCFLAGS_CHECKED_EQ_OPR))
	{
		Oid			eq_opr = InvalidOid;

		if (typentry->btree_opf != InvalidOid)
			eq_opr = get_opfamily_member(typentry->btree_opf,
										 typentry->btree_opintype,
										 typentry->btree_opintype,
										 BTEqualStrategyNumber);
		if (eq_opr == InvalidOid &&
			typentry->hash_opf != InvalidOid)
			eq_opr = get_opfamily_member(typentry->hash_opf,
										 typentry->hash_opintype,
										 typentry->hash_opintype,
										 HTEqualStrategyNumber);

		/*
		 * If the proposed equality operator is array_eq or record_eq, check
		 * to see if the element type or column types support equality.  If
		 * not, array_eq or record_eq would fail at runtime, so we don't want
		 * to report that the type has equality.  (We can omit similar
		 * checking for ranges and multiranges because ranges can't be created
		 * in the first place unless their subtypes support equality.)
		 */
		if (eq_opr == ARRAY_EQ_OP &&
			!array_element_has_equality(typentry))
			eq_opr = InvalidOid;
		else if (eq_opr == RECORD_EQ_OP &&
				 !record_fields_have_equality(typentry))
			eq_opr = InvalidOid;

		/* Force update of eq_opr_finfo only if we're changing state */
		if (typentry->eq_opr != eq_opr)
			typentry->eq_opr_finfo.fn_oid = InvalidOid;

		typentry->eq_opr = eq_opr;

		/*
		 * Reset info about hash functions whenever we pick up new info about
		 * equality operator.  This is so we can ensure that the hash
		 * functions match the operator.
		 */
		typentry->flags &= ~(TCFLAGS_CHECKED_HASH_PROC |
							 TCFLAGS_CHECKED_HASH_EXTENDED_PROC);
		typentry->flags |= TCFLAGS_CHECKED_EQ_OPR;
	}
	if ((flags & TYPECACHE_LT_OPR) &&
		!(typentry->flags & TCFLAGS_CHECKED_LT_OPR))
	{
		Oid			lt_opr = InvalidOid;

		if (typentry->btree_opf != InvalidOid)
			lt_opr = get_opfamily_member(typentry->btree_opf,
										 typentry->btree_opintype,
										 typentry->btree_opintype,
										 BTLessStrategyNumber);

		/*
		 * As above, make sure array_cmp or record_cmp will succeed; but again
		 * we need no special check for ranges or multiranges.
		 */
		if (lt_opr == ARRAY_LT_OP &&
			!array_element_has_compare(typentry))
			lt_opr = InvalidOid;
		else if (lt_opr == RECORD_LT_OP &&
				 !record_fields_have_compare(typentry))
			lt_opr = InvalidOid;

		typentry->lt_opr = lt_opr;
		typentry->flags |= TCFLAGS_CHECKED_LT_OPR;
	}
	if ((flags & TYPECACHE_GT_OPR) &&
		!(typentry->flags & TCFLAGS_CHECKED_GT_OPR))
	{
		Oid			gt_opr = InvalidOid;

		if (typentry->btree_opf != InvalidOid)
			gt_opr = get_opfamily_member(typentry->btree_opf,
										 typentry->btree_opintype,
										 typentry->btree_opintype,
										 BTGreaterStrategyNumber);

		/*
		 * As above, make sure array_cmp or record_cmp will succeed; but again
		 * we need no special check for ranges or multiranges.
		 */
		if (gt_opr == ARRAY_GT_OP &&
			!array_element_has_compare(typentry))
			gt_opr = InvalidOid;
		else if (gt_opr == RECORD_GT_OP &&
				 !record_fields_have_compare(typentry))
			gt_opr = InvalidOid;

		typentry->gt_opr = gt_opr;
		typentry->flags |= TCFLAGS_CHECKED_GT_OPR;
	}
	if ((flags & (TYPECACHE_CMP_PROC | TYPECACHE_CMP_PROC_FINFO)) &&
		!(typentry->flags & TCFLAGS_CHECKED_CMP_PROC))
	{
		Oid			cmp_proc = InvalidOid;

		if (typentry->btree_opf != InvalidOid)
			cmp_proc = get_opfamily_proc(typentry->btree_opf,
										 typentry->btree_opintype,
										 typentry->btree_opintype,
										 BTORDER_PROC);

		/*
		 * As above, make sure array_cmp or record_cmp will succeed; but again
		 * we need no special check for ranges or multiranges.
		 */
		if (cmp_proc == F_BTARRAYCMP &&
			!array_element_has_compare(typentry))
			cmp_proc = InvalidOid;
		else if (cmp_proc == F_BTRECORDCMP &&
				 !record_fields_have_compare(typentry))
			cmp_proc = InvalidOid;

		/* Force update of cmp_proc_finfo only if we're changing state */
		if (typentry->cmp_proc != cmp_proc)
			typentry->cmp_proc_finfo.fn_oid = InvalidOid;

		typentry->cmp_proc = cmp_proc;
		typentry->flags |= TCFLAGS_CHECKED_CMP_PROC;
	}
	if ((flags & (TYPECACHE_HASH_PROC | TYPECACHE_HASH_PROC_FINFO)) &&
		!(typentry->flags & TCFLAGS_CHECKED_HASH_PROC))
	{
		Oid			hash_proc = InvalidOid;

		/*
		 * We insist that the eq_opr, if one has been determined, match the
		 * hash opclass; else report there is no hash function.
		 */
		if (typentry->hash_opf != InvalidOid &&
			(!OidIsValid(typentry->eq_opr) ||
			 typentry->eq_opr == get_opfamily_member(typentry->hash_opf,
													 typentry->hash_opintype,
													 typentry->hash_opintype,
													 HTEqualStrategyNumber)))
			hash_proc = get_opfamily_proc(typentry->hash_opf,
										  typentry->hash_opintype,
										  typentry->hash_opintype,
										  HASHSTANDARD_PROC);

		/*
		 * As above, make sure hash_array, hash_record, or hash_range will
		 * succeed.
		 */
		if (hash_proc == F_HASH_ARRAY &&
			!array_element_has_hashing(typentry))
			hash_proc = InvalidOid;
		else if (hash_proc == F_HASH_RECORD &&
				 !record_fields_have_hashing(typentry))
			hash_proc = InvalidOid;
		else if (hash_proc == F_HASH_RANGE &&
				 !range_element_has_hashing(typentry))
			hash_proc = InvalidOid;

		/*
		 * Likewise for hash_multirange.
		 */
		if (hash_proc == F_HASH_MULTIRANGE &&
			!multirange_element_has_hashing(typentry))
			hash_proc = InvalidOid;

		/* Force update of hash_proc_finfo only if we're changing state */
		if (typentry->hash_proc != hash_proc)
			typentry->hash_proc_finfo.fn_oid = InvalidOid;

		typentry->hash_proc = hash_proc;
		typentry->flags |= TCFLAGS_CHECKED_HASH_PROC;
	}
	if ((flags & (TYPECACHE_HASH_EXTENDED_PROC |
				  TYPECACHE_HASH_EXTENDED_PROC_FINFO)) &&
		!(typentry->flags & TCFLAGS_CHECKED_HASH_EXTENDED_PROC))
	{
		Oid			hash_extended_proc = InvalidOid;

		/*
		 * We insist that the eq_opr, if one has been determined, match the
		 * hash opclass; else report there is no hash function.
		 */
		if (typentry->hash_opf != InvalidOid &&
			(!OidIsValid(typentry->eq_opr) ||
			 typentry->eq_opr == get_opfamily_member(typentry->hash_opf,
													 typentry->hash_opintype,
													 typentry->hash_opintype,
													 HTEqualStrategyNumber)))
			hash_extended_proc = get_opfamily_proc(typentry->hash_opf,
												   typentry->hash_opintype,
												   typentry->hash_opintype,
												   HASHEXTENDED_PROC);

		/*
		 * As above, make sure hash_array_extended, hash_record_extended, or
		 * hash_range_extended will succeed.
		 */
		if (hash_extended_proc == F_HASH_ARRAY_EXTENDED &&
			!array_element_has_extended_hashing(typentry))
			hash_extended_proc = InvalidOid;
		else if (hash_extended_proc == F_HASH_RECORD_EXTENDED &&
				 !record_fields_have_extended_hashing(typentry))
			hash_extended_proc = InvalidOid;
		else if (hash_extended_proc == F_HASH_RANGE_EXTENDED &&
				 !range_element_has_extended_hashing(typentry))
			hash_extended_proc = InvalidOid;

		/*
		 * Likewise for hash_multirange_extended.
		 */
		if (hash_extended_proc == F_HASH_MULTIRANGE_EXTENDED &&
			!multirange_element_has_extended_hashing(typentry))
			hash_extended_proc = InvalidOid;

		/* Force update of proc finfo only if we're changing state */
		if (typentry->hash_extended_proc != hash_extended_proc)
			typentry->hash_extended_proc_finfo.fn_oid = InvalidOid;

		typentry->hash_extended_proc = hash_extended_proc;
		typentry->flags |= TCFLAGS_CHECKED_HASH_EXTENDED_PROC;
	}

	/*
	 * Set up fmgr lookup info as requested
	 *
	 * Note: we tell fmgr the finfo structures live in CacheMemoryContext,
	 * which is not quite right (they're really in the hash table's private
	 * memory context) but this will do for our purposes.
	 *
	 * Note: the code above avoids invalidating the finfo structs unless the
	 * referenced operator/function OID actually changes.  This is to prevent
	 * unnecessary leakage of any subsidiary data attached to an finfo, since
	 * that would cause session-lifespan memory leaks.
	 */
	if ((flags & TYPECACHE_EQ_OPR_FINFO) &&
		typentry->eq_opr_finfo.fn_oid == InvalidOid &&
		typentry->eq_opr != InvalidOid)
	{
		Oid			eq_opr_func;

		eq_opr_func = get_opcode(typentry->eq_opr);
		if (eq_opr_func != InvalidOid)
			fmgr_info_cxt(eq_opr_func, &typentry->eq_opr_finfo,
						  CacheMemoryContext);
	}
	if ((flags & TYPECACHE_CMP_PROC_FINFO) &&
		typentry->cmp_proc_finfo.fn_oid == InvalidOid &&
		typentry->cmp_proc != InvalidOid)
	{
		fmgr_info_cxt(typentry->cmp_proc, &typentry->cmp_proc_finfo,
					  CacheMemoryContext);
	}
	if ((flags & TYPECACHE_HASH_PROC_FINFO) &&
		typentry->hash_proc_finfo.fn_oid == InvalidOid &&
		typentry->hash_proc != InvalidOid)
	{
		fmgr_info_cxt(typentry->hash_proc, &typentry->hash_proc_finfo,
					  CacheMemoryContext);
	}
	if ((flags & TYPECACHE_HASH_EXTENDED_PROC_FINFO) &&
		typentry->hash_extended_proc_finfo.fn_oid == InvalidOid &&
		typentry->hash_extended_proc != InvalidOid)
	{
		fmgr_info_cxt(typentry->hash_extended_proc,
					  &typentry->hash_extended_proc_finfo,
					  CacheMemoryContext);
	}

	/*
	 * If it's a composite type (row type), get tupdesc if requested
	 */
	if ((flags & TYPECACHE_TUPDESC) &&
		typentry->tupDesc == NULL &&
		typentry->typtype == TYPTYPE_COMPOSITE)
	{
		load_typcache_tupdesc(typentry);
	}

	/*
	 * If requested, get information about a range type
	 *
	 * This includes making sure that the basic info about the range element
	 * type is up-to-date.
	 */
	if ((flags & TYPECACHE_RANGE_INFO) &&
		typentry->typtype == TYPTYPE_RANGE)
	{
		if (typentry->rngelemtype == NULL)
			load_rangetype_info(typentry);
		else if (!(typentry->rngelemtype->flags & TCFLAGS_HAVE_PG_TYPE_DATA))
			(void) lookup_type_cache(typentry->rngelemtype->type_id, 0);
	}

	/*
	 * If requested, get information about a multirange type
	 */
	if ((flags & TYPECACHE_MULTIRANGE_INFO) &&
		typentry->rngtype == NULL &&
		typentry->typtype == TYPTYPE_MULTIRANGE)
	{
		load_multirangetype_info(typentry);
	}

	/*
	 * If requested, get information about a domain type
	 */
	if ((flags & TYPECACHE_DOMAIN_BASE_INFO) &&
		typentry->domainBaseType == InvalidOid &&
		typentry->typtype == TYPTYPE_DOMAIN)
	{
		typentry->domainBaseTypmod = -1;
		typentry->domainBaseType =
			getBaseTypeAndTypmod(type_id, &typentry->domainBaseTypmod);
	}
	if ((flags & TYPECACHE_DOMAIN_CONSTR_INFO) &&
		(typentry->flags & TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS) == 0 &&
		typentry->typtype == TYPTYPE_DOMAIN)
	{
		load_domaintype_info(typentry);
	}

	return typentry;
}

/*
 * load_typcache_tupdesc --- helper routine to set up composite type's tupDesc
 */
static void
load_typcache_tupdesc(TypeCacheEntry *typentry)
{
	Relation	rel;

	if (!OidIsValid(typentry->typrelid))	/* should not happen */
		elog(ERROR, "invalid typrelid for composite type %u",
			 typentry->type_id);
	rel = relation_open(typentry->typrelid, AccessShareLock);
	Assert(rel->rd_rel->reltype == typentry->type_id);

	/*
	 * Link to the tupdesc and increment its refcount (we assert it's a
	 * refcounted descriptor).  We don't use IncrTupleDescRefCount() for this,
	 * because the reference mustn't be entered in the current resource owner;
	 * it can outlive the current query.
	 */
	typentry->tupDesc = RelationGetDescr(rel);

	Assert(typentry->tupDesc->tdrefcount > 0);
	typentry->tupDesc->tdrefcount++;

	/*
	 * In future, we could take some pains to not change tupDesc_identifier if
	 * the tupdesc didn't really change; but for now it's not worth it.
	 */
	typentry->tupDesc_identifier = ++tupledesc_id_counter;

	relation_close(rel, AccessShareLock);
}

/*
 * load_rangetype_info --- helper routine to set up range type information
 */
static void
load_rangetype_info(TypeCacheEntry *typentry)
{
	Form_pg_range pg_range;
	HeapTuple	tup;
	Oid			subtypeOid;
	Oid			opclassOid;
	Oid			canonicalOid;
	Oid			subdiffOid;
	Oid			opfamilyOid;
	Oid			opcintype;
	Oid			cmpFnOid;

	/* get information from pg_range */
	tup = SearchSysCache1(RANGETYPE, ObjectIdGetDatum(typentry->type_id));
	/* should not fail, since we already checked typtype ... */
	if (!HeapTupleIsValid(tup))
		elog(ERROR, "cache lookup failed for range type %u",
			 typentry->type_id);
	pg_range = (Form_pg_range) GETSTRUCT(tup);

	subtypeOid = pg_range->rngsubtype;
	typentry->rng_collation = pg_range->rngcollation;
	opclassOid = pg_range->rngsubopc;
	canonicalOid = pg_range->rngcanonical;
	subdiffOid = pg_range->rngsubdiff;

	ReleaseSysCache(tup);

	/* get opclass properties and look up the comparison function */
	opfamilyOid = get_opclass_family(opclassOid);
	opcintype = get_opclass_input_type(opclassOid);

	cmpFnOid = get_opfamily_proc(opfamilyOid, opcintype, opcintype,
								 BTORDER_PROC);
	if (!RegProcedureIsValid(cmpFnOid))
		elog(ERROR, "missing support function %d(%u,%u) in opfamily %u",
			 BTORDER_PROC, opcintype, opcintype, opfamilyOid);

	/* set up cached fmgrinfo structs */
	fmgr_info_cxt(cmpFnOid, &typentry->rng_cmp_proc_finfo,
				  CacheMemoryContext);
	if (OidIsValid(canonicalOid))
		fmgr_info_cxt(canonicalOid, &typentry->rng_canonical_finfo,
					  CacheMemoryContext);
	if (OidIsValid(subdiffOid))
		fmgr_info_cxt(subdiffOid, &typentry->rng_subdiff_finfo,
					  CacheMemoryContext);

	/* Lastly, set up link to the element type --- this marks data valid */
	typentry->rngelemtype = lookup_type_cache(subtypeOid, 0);
}

/*
 * load_multirangetype_info --- helper routine to set up multirange type
 * information
 */
static void
load_multirangetype_info(TypeCacheEntry *typentry)
{
	Oid			rangetypeOid;

	rangetypeOid = get_multirange_range(typentry->type_id);
	if (!OidIsValid(rangetypeOid))
		elog(ERROR, "cache lookup failed for multirange type %u",
			 typentry->type_id);

	typentry->rngtype = lookup_type_cache(rangetypeOid, TYPECACHE_RANGE_INFO);
}

/*
 * load_domaintype_info --- helper routine to set up domain constraint info
 *
 * Note: we assume we're called in a relatively short-lived context, so it's
 * okay to leak data into the current context while scanning pg_constraint.
 * We build the new DomainConstraintCache data in a context underneath
 * GetCurrentMemoryContext(), and reparent it under CacheMemoryContext when
 * complete.
 */
static void
load_domaintype_info(TypeCacheEntry *typentry)
{
	Oid			typeOid = typentry->type_id;
	DomainConstraintCache *dcc;
	bool		notNull = false;
	DomainConstraintState **ccons;
	int			cconslen;
	Relation	conRel;
	MemoryContext oldcxt;

	/*
	 * If we're here, any existing constraint info is stale, so release it.
	 * For safety, be sure to null the link before trying to delete the data.
	 */
	if (typentry->domainData)
	{
		dcc = typentry->domainData;
		typentry->domainData = NULL;
		decr_dcc_refcount(dcc);
	}

	/*
	 * We try to optimize the common case of no domain constraints, so don't
	 * create the dcc object and context until we find a constraint.  Likewise
	 * for the temp sorting array.
	 */
	dcc = NULL;
	ccons = NULL;
	cconslen = 0;

	/*
	 * Scan pg_constraint for relevant constraints.  We want to find
	 * constraints for not just this domain, but any ancestor domains, so the
	 * outer loop crawls up the domain stack.
	 */
	conRel = table_open(ConstraintRelationId, AccessShareLock);

	for (;;)
	{
		HeapTuple	tup;
		HeapTuple	conTup;
		Form_pg_type typTup;
		int			nccons = 0;
		ScanKeyData key[1];
		SysScanDesc scan;

		tup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typeOid));
		if (!HeapTupleIsValid(tup))
			elog(ERROR, "cache lookup failed for type %u", typeOid);
		typTup = (Form_pg_type) GETSTRUCT(tup);

		if (typTup->typtype != TYPTYPE_DOMAIN)
		{
			/* Not a domain, so done */
			ReleaseSysCache(tup);
			break;
		}

		/* Test for NOT NULL Constraint */
		if (typTup->typnotnull)
			notNull = true;

		/* Look for CHECK Constraints on this domain */
		ScanKeyInit(&key[0],
					Anum_pg_constraint_contypid,
					BTEqualStrategyNumber, F_OIDEQ,
					ObjectIdGetDatum(typeOid));

		scan = systable_beginscan(conRel, ConstraintTypidIndexId, true,
								  NULL, 1, key);

		while (HeapTupleIsValid(conTup = systable_getnext(scan)))
		{
			Form_pg_constraint c = (Form_pg_constraint) GETSTRUCT(conTup);
			Datum		val;
			bool		isNull;
			char	   *constring;
			Expr	   *check_expr;
			DomainConstraintState *r;

			/* Ignore non-CHECK constraints (presently, shouldn't be any) */
			if (c->contype != CONSTRAINT_CHECK)
				continue;

			/* Not expecting conbin to be NULL, but we'll test for it anyway */
			val = fastgetattr(conTup, Anum_pg_constraint_conbin,
							  conRel->rd_att, &isNull);
			if (isNull)
				elog(ERROR, "domain \"%s\" constraint \"%s\" has NULL conbin",
					 NameStr(typTup->typname), NameStr(c->conname));

			/* Convert conbin to C string in caller context */
			constring = TextDatumGetCString(val);

			/* Create the DomainConstraintCache object and context if needed */
			if (dcc == NULL)
			{
				MemoryContext cxt;

				cxt = AllocSetContextCreate(GetCurrentMemoryContext(),
											"Domain constraints",
											ALLOCSET_SMALL_SIZES);
				dcc = (DomainConstraintCache *)
					MemoryContextAlloc(cxt, sizeof(DomainConstraintCache));
				dcc->constraints = NIL;
				dcc->dccContext = cxt;
				dcc->dccRefCount = 0;
			}

			/* Create node trees in DomainConstraintCache's context */
			oldcxt = MemoryContextSwitchTo(dcc->dccContext);

			check_expr = (Expr *) stringToNode(constring);

			/*
			 * Plan the expression, since ExecInitExpr will expect that.
			 *
			 * Note: caching the result of expression_planner() is not very
			 * good practice.  Ideally we'd use a CachedExpression here so
			 * that we would react promptly to, eg, changes in inlined
			 * functions.  However, because we don't support mutable domain
			 * CHECK constraints, it's not really clear that it's worth the
			 * extra overhead to do that.
			 */
			check_expr = expression_planner(check_expr);

			r = makeNode(DomainConstraintState);
			r->constrainttype = DOM_CONSTRAINT_CHECK;
			r->name = pstrdup(NameStr(c->conname));
			r->check_expr = check_expr;
			r->check_exprstate = NULL;

			MemoryContextSwitchTo(oldcxt);

			/* Accumulate constraints in an array, for sorting below */
			if (ccons == NULL)
			{
				cconslen = 8;
				ccons = (DomainConstraintState **)
					palloc(cconslen * sizeof(DomainConstraintState *));
			}
			else if (nccons >= cconslen)
			{
				cconslen *= 2;
				ccons = (DomainConstraintState **)
					repalloc(ccons, cconslen * sizeof(DomainConstraintState *));
			}
			ccons[nccons++] = r;
		}

		systable_endscan(scan);

		if (nccons > 0)
		{
			/*
			 * Sort the items for this domain, so that CHECKs are applied in a
			 * deterministic order.
			 */
			if (nccons > 1)
				qsort(ccons, nccons, sizeof(DomainConstraintState *), dcs_cmp);

			/*
			 * Now attach them to the overall list.  Use lcons() here because
			 * constraints of parent domains should be applied earlier.
			 */
			oldcxt = MemoryContextSwitchTo(dcc->dccContext);
			while (nccons > 0)
				dcc->constraints = lcons(ccons[--nccons], dcc->constraints);
			MemoryContextSwitchTo(oldcxt);
		}

		/* loop to next domain in stack */
		typeOid = typTup->typbasetype;
		ReleaseSysCache(tup);
	}

	table_close(conRel, AccessShareLock);

	/*
	 * Only need to add one NOT NULL check regardless of how many domains in
	 * the stack request it.
	 */
	if (notNull)
	{
		DomainConstraintState *r;

		/* Create the DomainConstraintCache object and context if needed */
		if (dcc == NULL)
		{
			MemoryContext cxt;

			cxt = AllocSetContextCreate(GetCurrentMemoryContext(),
										"Domain constraints",
										ALLOCSET_SMALL_SIZES);
			dcc = (DomainConstraintCache *)
				MemoryContextAlloc(cxt, sizeof(DomainConstraintCache));
			dcc->constraints = NIL;
			dcc->dccContext = cxt;
			dcc->dccRefCount = 0;
		}

		/* Create node trees in DomainConstraintCache's context */
		oldcxt = MemoryContextSwitchTo(dcc->dccContext);

		r = makeNode(DomainConstraintState);

		r->constrainttype = DOM_CONSTRAINT_NOTNULL;
		r->name = pstrdup("NOT NULL");
		r->check_expr = NULL;
		r->check_exprstate = NULL;

		/* lcons to apply the nullness check FIRST */
		dcc->constraints = lcons(r, dcc->constraints);

		MemoryContextSwitchTo(oldcxt);
	}

	/*
	 * If we made a constraint object, move it into CacheMemoryContext and
	 * attach it to the typcache entry.
	 */
	if (dcc)
	{
		MemoryContextSetParent(dcc->dccContext, CacheMemoryContext);
		typentry->domainData = dcc;
		dcc->dccRefCount++;		/* count the typcache's reference */
	}

	/* Either way, the typcache entry's domain data is now valid. */
	typentry->flags |= TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS;
}

/*
 * qsort comparator to sort DomainConstraintState pointers by name
 */
static int
dcs_cmp(const void *a, const void *b)
{
	const DomainConstraintState *const *ca = (const DomainConstraintState *const *) a;
	const DomainConstraintState *const *cb = (const DomainConstraintState *const *) b;

	return strcmp((*ca)->name, (*cb)->name);
}

/*
 * decr_dcc_refcount --- decrement a DomainConstraintCache's refcount,
 * and free it if no references remain
 */
static void
decr_dcc_refcount(DomainConstraintCache *dcc)
{
	Assert(dcc->dccRefCount > 0);
	if (--(dcc->dccRefCount) <= 0)
		MemoryContextDelete(dcc->dccContext);
}

/*
 * Context reset/delete callback for a DomainConstraintRef
 */
static void
dccref_deletion_callback(void *arg)
{
	DomainConstraintRef *ref = (DomainConstraintRef *) arg;
	DomainConstraintCache *dcc = ref->dcc;

	/* Paranoia --- be sure link is nulled before trying to release */
	if (dcc)
	{
		ref->constraints = NIL;
		ref->dcc = NULL;
		decr_dcc_refcount(dcc);
	}
}

/*
 * prep_domain_constraints --- prepare domain constraints for execution
 *
 * The expression trees stored in the DomainConstraintCache's list are
 * converted to executable expression state trees stored in execctx.
 */
static List *
prep_domain_constraints(List *constraints, MemoryContext execctx)
{
	List	   *result = NIL;
	MemoryContext oldcxt;
	ListCell   *lc;

	oldcxt = MemoryContextSwitchTo(execctx);

	foreach(lc, constraints)
	{
		DomainConstraintState *r = (DomainConstraintState *) lfirst(lc);
		DomainConstraintState *newr;

		newr = makeNode(DomainConstraintState);
		newr->constrainttype = r->constrainttype;
		newr->name = r->name;
		newr->check_expr = r->check_expr;
		newr->check_exprstate = ExecInitExpr(r->check_expr, NULL);

		result = lappend(result, newr);
	}

	MemoryContextSwitchTo(oldcxt);

	return result;
}

/*
 * InitDomainConstraintRef --- initialize a DomainConstraintRef struct
 *
 * Caller must tell us the MemoryContext in which the DomainConstraintRef
 * lives.  The ref will be cleaned up when that context is reset/deleted.
 *
 * Caller must also tell us whether it wants check_exprstate fields to be
 * computed in the DomainConstraintState nodes attached to this ref.
 * If it doesn't, we need not make a copy of the DomainConstraintState list.
 */
void
InitDomainConstraintRef(Oid type_id, DomainConstraintRef *ref,
						MemoryContext refctx, bool need_exprstate)
{
	/* Look up the typcache entry --- we assume it survives indefinitely */
	ref->tcache = lookup_type_cache(type_id, TYPECACHE_DOMAIN_CONSTR_INFO);
	ref->need_exprstate = need_exprstate;
	/* For safety, establish the callback before acquiring a refcount */
	ref->refctx = refctx;
	ref->dcc = NULL;
	ref->callback.func = dccref_deletion_callback;
	ref->callback.arg = (void *) ref;
	MemoryContextRegisterResetCallback(refctx, &ref->callback);
	/* Acquire refcount if there are constraints, and set up exported list */
	if (ref->tcache->domainData)
	{
		ref->dcc = ref->tcache->domainData;
		ref->dcc->dccRefCount++;
		if (ref->need_exprstate)
			ref->constraints = prep_domain_constraints(ref->dcc->constraints,
													   ref->refctx);
		else
			ref->constraints = ref->dcc->constraints;
	}
	else
		ref->constraints = NIL;
}

/*
 * UpdateDomainConstraintRef --- recheck validity of domain constraint info
 *
 * If the domain's constraint set changed, ref->constraints is updated to
 * point at a new list of cached constraints.
 *
 * In the normal case where nothing happened to the domain, this is cheap
 * enough that it's reasonable (and expected) to check before *each* use
 * of the constraint info.
 */
void
UpdateDomainConstraintRef(DomainConstraintRef *ref)
{
	TypeCacheEntry *typentry = ref->tcache;

	/* Make sure typcache entry's data is up to date */
	if ((typentry->flags & TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS) == 0 &&
		typentry->typtype == TYPTYPE_DOMAIN)
		load_domaintype_info(typentry);

	/* Transfer to ref object if there's new info, adjusting refcounts */
	if (ref->dcc != typentry->domainData)
	{
		/* Paranoia --- be sure link is nulled before trying to release */
		DomainConstraintCache *dcc = ref->dcc;

		if (dcc)
		{
			/*
			 * Note: we just leak the previous list of executable domain
			 * constraints.  Alternatively, we could keep those in a child
			 * context of ref->refctx and free that context at this point.
			 * However, in practice this code path will be taken so seldom
			 * that the extra bookkeeping for a child context doesn't seem
			 * worthwhile; we'll just allow a leak for the lifespan of refctx.
			 */
			ref->constraints = NIL;
			ref->dcc = NULL;
			decr_dcc_refcount(dcc);
		}
		dcc = typentry->domainData;
		if (dcc)
		{
			ref->dcc = dcc;
			dcc->dccRefCount++;
			if (ref->need_exprstate)
				ref->constraints = prep_domain_constraints(dcc->constraints,
														   ref->refctx);
			else
				ref->constraints = dcc->constraints;
		}
	}
}

/*
 * DomainHasConstraints --- utility routine to check if a domain has constraints
 *
 * This is defined to return false, not fail, if type is not a domain.
 */
bool
DomainHasConstraints(Oid type_id)
{
	TypeCacheEntry *typentry;

	/*
	 * Note: a side effect is to cause the typcache's domain data to become
	 * valid.  This is fine since we'll likely need it soon if there is any.
	 */
	typentry = lookup_type_cache(type_id, TYPECACHE_DOMAIN_CONSTR_INFO);

	return (typentry->domainData != NULL);
}


/*
 * array_element_has_equality and friends are helper routines to check
 * whether we should believe that array_eq and related functions will work
 * on the given array type or composite type.
 *
 * The logic above may call these repeatedly on the same type entry, so we
 * make use of the typentry->flags field to cache the results once known.
 * Also, we assume that we'll probably want all these facts about the type
 * if we want any, so we cache them all using only one lookup of the
 * component datatype(s).
 */

static bool
array_element_has_equality(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_array_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_EQUALITY) != 0;
}

static bool
array_element_has_compare(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_array_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_COMPARE) != 0;
}

static bool
array_element_has_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_array_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_HASHING) != 0;
}

static bool
array_element_has_extended_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_array_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_EXTENDED_HASHING) != 0;
}

static void
cache_array_element_properties(TypeCacheEntry *typentry)
{
	Oid			elem_type = get_base_element_type(typentry->type_id);

	if (OidIsValid(elem_type))
	{
		TypeCacheEntry *elementry;

		elementry = lookup_type_cache(elem_type,
									  TYPECACHE_EQ_OPR |
									  TYPECACHE_CMP_PROC |
									  TYPECACHE_HASH_PROC |
									  TYPECACHE_HASH_EXTENDED_PROC);
		if (OidIsValid(elementry->eq_opr))
			typentry->flags |= TCFLAGS_HAVE_ELEM_EQUALITY;
		if (OidIsValid(elementry->cmp_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_COMPARE;
		if (OidIsValid(elementry->hash_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_HASHING;
		if (OidIsValid(elementry->hash_extended_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_EXTENDED_HASHING;
	}
	typentry->flags |= TCFLAGS_CHECKED_ELEM_PROPERTIES;
}

/*
 * Likewise, some helper functions for composite types.
 */

static bool
record_fields_have_equality(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_FIELD_PROPERTIES))
		cache_record_field_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_FIELD_EQUALITY) != 0;
}

static bool
record_fields_have_compare(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_FIELD_PROPERTIES))
		cache_record_field_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_FIELD_COMPARE) != 0;
}

static bool
record_fields_have_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_FIELD_PROPERTIES))
		cache_record_field_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_FIELD_HASHING) != 0;
}

static bool
record_fields_have_extended_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_FIELD_PROPERTIES))
		cache_record_field_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_FIELD_EXTENDED_HASHING) != 0;
}

static void
cache_record_field_properties(TypeCacheEntry *typentry)
{
	/*
	 * For type RECORD, we can't really tell what will work, since we don't
	 * have access here to the specific anonymous type.  Just assume that
	 * equality and comparison will (we may get a failure at runtime).  We
	 * could also claim that hashing works, but then if code that has the
	 * option between a comparison-based (sort-based) and a hash-based plan
	 * chooses hashing, stuff could fail that would otherwise work if it chose
	 * a comparison-based plan.  In practice more types support comparison
	 * than hashing.
	 */
	if (typentry->type_id == RECORDOID)
	{
		typentry->flags |= (TCFLAGS_HAVE_FIELD_EQUALITY |
							TCFLAGS_HAVE_FIELD_COMPARE);
	}
	else if (typentry->typtype == TYPTYPE_COMPOSITE)
	{
		TupleDesc	tupdesc;
		int			newflags;
		int			i;

		/* Fetch composite type's tupdesc if we don't have it already */
		if (typentry->tupDesc == NULL)
			load_typcache_tupdesc(typentry);
		tupdesc = typentry->tupDesc;

		/* Must bump the refcount while we do additional catalog lookups */
		IncrTupleDescRefCount(tupdesc);

		/* Have each property if all non-dropped fields have the property */
		newflags = (TCFLAGS_HAVE_FIELD_EQUALITY |
					TCFLAGS_HAVE_FIELD_COMPARE |
					TCFLAGS_HAVE_FIELD_HASHING |
					TCFLAGS_HAVE_FIELD_EXTENDED_HASHING);
		for (i = 0; i < tupdesc->natts; i++)
		{
			TypeCacheEntry *fieldentry;
			Form_pg_attribute attr = TupleDescAttr(tupdesc, i);

			if (attr->attisdropped)
				continue;

			fieldentry = lookup_type_cache(attr->atttypid,
										   TYPECACHE_EQ_OPR |
										   TYPECACHE_CMP_PROC |
										   TYPECACHE_HASH_PROC |
										   TYPECACHE_HASH_EXTENDED_PROC);
			if (!OidIsValid(fieldentry->eq_opr))
				newflags &= ~TCFLAGS_HAVE_FIELD_EQUALITY;
			if (!OidIsValid(fieldentry->cmp_proc))
				newflags &= ~TCFLAGS_HAVE_FIELD_COMPARE;
			if (!OidIsValid(fieldentry->hash_proc))
				newflags &= ~TCFLAGS_HAVE_FIELD_HASHING;
			if (!OidIsValid(fieldentry->hash_extended_proc))
				newflags &= ~TCFLAGS_HAVE_FIELD_EXTENDED_HASHING;

			/* We can drop out of the loop once we disprove all bits */
			if (newflags == 0)
				break;
		}
		typentry->flags |= newflags;

		DecrTupleDescRefCount(tupdesc);
	}
	else if (typentry->typtype == TYPTYPE_DOMAIN)
	{
		/* If it's domain over composite, copy base type's properties */
		TypeCacheEntry *baseentry;

		/* load up basetype info if we didn't already */
		if (typentry->domainBaseType == InvalidOid)
		{
			typentry->domainBaseTypmod = -1;
			typentry->domainBaseType =
				getBaseTypeAndTypmod(typentry->type_id,
									 &typentry->domainBaseTypmod);
		}
		baseentry = lookup_type_cache(typentry->domainBaseType,
									  TYPECACHE_EQ_OPR |
									  TYPECACHE_CMP_PROC |
									  TYPECACHE_HASH_PROC |
									  TYPECACHE_HASH_EXTENDED_PROC);
		if (baseentry->typtype == TYPTYPE_COMPOSITE)
		{
			typentry->flags |= TCFLAGS_DOMAIN_BASE_IS_COMPOSITE;
			typentry->flags |= baseentry->flags & (TCFLAGS_HAVE_FIELD_EQUALITY |
												   TCFLAGS_HAVE_FIELD_COMPARE |
												   TCFLAGS_HAVE_FIELD_HASHING |
												   TCFLAGS_HAVE_FIELD_EXTENDED_HASHING);
		}
	}
	typentry->flags |= TCFLAGS_CHECKED_FIELD_PROPERTIES;
}

/*
 * Likewise, some helper functions for range and multirange types.
 *
 * We can borrow the flag bits for array element properties to use for range
 * element properties, since those flag bits otherwise have no use in a
 * range or multirange type's typcache entry.
 */

static bool
range_element_has_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_range_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_HASHING) != 0;
}

static bool
range_element_has_extended_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_range_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_EXTENDED_HASHING) != 0;
}

static void
cache_range_element_properties(TypeCacheEntry *typentry)
{
	/* load up subtype link if we didn't already */
	if (typentry->rngelemtype == NULL &&
		typentry->typtype == TYPTYPE_RANGE)
		load_rangetype_info(typentry);

	if (typentry->rngelemtype != NULL)
	{
		TypeCacheEntry *elementry;

		/* might need to calculate subtype's hash function properties */
		elementry = lookup_type_cache(typentry->rngelemtype->type_id,
									  TYPECACHE_HASH_PROC |
									  TYPECACHE_HASH_EXTENDED_PROC);
		if (OidIsValid(elementry->hash_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_HASHING;
		if (OidIsValid(elementry->hash_extended_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_EXTENDED_HASHING;
	}
	typentry->flags |= TCFLAGS_CHECKED_ELEM_PROPERTIES;
}

static bool
multirange_element_has_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_multirange_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_HASHING) != 0;
}

static bool
multirange_element_has_extended_hashing(TypeCacheEntry *typentry)
{
	if (!(typentry->flags & TCFLAGS_CHECKED_ELEM_PROPERTIES))
		cache_multirange_element_properties(typentry);
	return (typentry->flags & TCFLAGS_HAVE_ELEM_EXTENDED_HASHING) != 0;
}

static void
cache_multirange_element_properties(TypeCacheEntry *typentry)
{
	/* load up range link if we didn't already */
	if (typentry->rngtype == NULL &&
		typentry->typtype == TYPTYPE_MULTIRANGE)
		load_multirangetype_info(typentry);

	if (typentry->rngtype != NULL && typentry->rngtype->rngelemtype != NULL)
	{
		TypeCacheEntry *elementry;

		/* might need to calculate subtype's hash function properties */
		elementry = lookup_type_cache(typentry->rngtype->rngelemtype->type_id,
									  TYPECACHE_HASH_PROC |
									  TYPECACHE_HASH_EXTENDED_PROC);
		if (OidIsValid(elementry->hash_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_HASHING;
		if (OidIsValid(elementry->hash_extended_proc))
			typentry->flags |= TCFLAGS_HAVE_ELEM_EXTENDED_HASHING;
	}
	typentry->flags |= TCFLAGS_CHECKED_ELEM_PROPERTIES;
}

/*
 * Make sure that RecordCacheArray and RecordIdentifierArray are large enough
 * to store 'typmod'.
 */
static void
ensure_record_cache_typmod_slot_exists(int32 typmod)
{
	if (RecordCacheArray == NULL)
	{
		RecordCacheArray = (TupleDesc *)
			MemoryContextAllocZero(CacheMemoryContext, 64 * sizeof(TupleDesc));
		RecordIdentifierArray = (uint64 *)
			MemoryContextAllocZero(CacheMemoryContext, 64 * sizeof(uint64));
		RecordCacheArrayLen = 64;
	}

	if (typmod >= RecordCacheArrayLen)
	{
		int32		newlen = pg_nextpower2_32(typmod + 1);

		RecordCacheArray = (TupleDesc *) repalloc(RecordCacheArray,
												  newlen * sizeof(TupleDesc));
		memset(RecordCacheArray + RecordCacheArrayLen, 0,
			   (newlen - RecordCacheArrayLen) * sizeof(TupleDesc));
		RecordIdentifierArray = (uint64 *) repalloc(RecordIdentifierArray,
													newlen * sizeof(uint64));
		memset(RecordIdentifierArray + RecordCacheArrayLen, 0,
			   (newlen - RecordCacheArrayLen) * sizeof(uint64));
		RecordCacheArrayLen = newlen;
	}
}

/*
 * lookup_rowtype_tupdesc_internal --- internal routine to lookup a rowtype
 *
 * Same API as lookup_rowtype_tupdesc_noerror, but the returned tupdesc
 * hasn't had its refcount bumped.
 */
static TupleDesc
lookup_rowtype_tupdesc_internal(Oid type_id, int32 typmod, bool noError)
{
	if (type_id != RECORDOID)
	{
		/*
		 * It's a named composite type, so use the regular typcache.
		 */
		TypeCacheEntry *typentry;

		typentry = lookup_type_cache(type_id, TYPECACHE_TUPDESC);
		if (typentry->tupDesc == NULL && !noError)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("type %s is not composite",
							format_type_be(type_id))));
		return typentry->tupDesc;
	}
	else
	{
		/*
		 * It's a transient record type, so look in our record-type table.
		 */
		if (typmod >= 0)
		{
			/* It is already in our local cache? */
			if (typmod < RecordCacheArrayLen &&
				RecordCacheArray[typmod] != NULL)
				return RecordCacheArray[typmod];

			/* Are we attached to a shared record typmod registry? */
			if (CurrentSession->shared_typmod_registry != NULL)
			{
				SharedTypmodTableEntry *entry;

				/* Try to find it in the shared typmod index. */
				entry = dshash_find(CurrentSession->shared_typmod_table,
									&typmod, false);
				if (entry != NULL)
				{
					TupleDesc	tupdesc;

					tupdesc = (TupleDesc)
						dsa_get_address(CurrentSession->area,
										entry->shared_tupdesc);
					Assert(typmod == tupdesc->tdtypmod);

					/* We may need to extend the local RecordCacheArray. */
					ensure_record_cache_typmod_slot_exists(typmod);

					/*
					 * Our local array can now point directly to the TupleDesc
					 * in shared memory, which is non-reference-counted.
					 */
					RecordCacheArray[typmod] = tupdesc;
					Assert(tupdesc->tdrefcount == -1);

					/*
					 * We don't share tupdesc identifiers across processes, so
					 * assign one locally.
					 */
					RecordIdentifierArray[typmod] = ++tupledesc_id_counter;

					dshash_release_lock(CurrentSession->shared_typmod_table,
										entry);

					return RecordCacheArray[typmod];
				}
			}
		}

		if (!noError)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("record type has not been registered")));
		return NULL;
	}
}

/*
 * lookup_rowtype_tupdesc
 *
 * Given a typeid/typmod that should describe a known composite type,
 * return the tuple descriptor for the type.  Will ereport on failure.
 * (Use ereport because this is reachable with user-specified OIDs,
 * for example from record_in().)
 *
 * Note: on success, we increment the refcount of the returned TupleDesc,
 * and log the reference in CurrentResourceOwner.  Caller must call
 * ReleaseTupleDesc when done using the tupdesc.  (There are some
 * cases in which the returned tupdesc is not refcounted, in which
 * case PinTupleDesc/ReleaseTupleDesc are no-ops; but in these cases
 * the tupdesc is guaranteed to live till process exit.)
 */
TupleDesc
lookup_rowtype_tupdesc(Oid type_id, int32 typmod)
{
	TupleDesc	tupDesc;

	tupDesc = lookup_rowtype_tupdesc_internal(type_id, typmod, false);
	PinTupleDesc(tupDesc);
	return tupDesc;
}

/*
 * lookup_rowtype_tupdesc_noerror
 *
 * As above, but if the type is not a known composite type and noError
 * is true, returns NULL instead of ereport'ing.  (Note that if a bogus
 * type_id is passed, you'll get an ereport anyway.)
 */
TupleDesc
lookup_rowtype_tupdesc_noerror(Oid type_id, int32 typmod, bool noError)
{
	TupleDesc	tupDesc;

	tupDesc = lookup_rowtype_tupdesc_internal(type_id, typmod, noError);
	if (tupDesc != NULL)
		PinTupleDesc(tupDesc);
	return tupDesc;
}

/*
 * lookup_rowtype_tupdesc_copy
 *
 * Like lookup_rowtype_tupdesc(), but the returned TupleDesc has been
 * copied into the GetCurrentMemoryContext() and is not reference-counted.
 */
TupleDesc
lookup_rowtype_tupdesc_copy(Oid type_id, int32 typmod)
{
	TupleDesc	tmp;

	tmp = lookup_rowtype_tupdesc_internal(type_id, typmod, false);
	return CreateTupleDescCopyConstr(tmp);
}

/*
 * lookup_rowtype_tupdesc_domain
 *
 * Same as lookup_rowtype_tupdesc_noerror(), except that the type can also be
 * a domain over a named composite type; so this is effectively equivalent to
 * lookup_rowtype_tupdesc_noerror(getBaseType(type_id), typmod, noError)
 * except for being a tad faster.
 *
 * Note: the reason we don't fold the look-through-domain behavior into plain
 * lookup_rowtype_tupdesc() is that we want callers to know they might be
 * dealing with a domain.  Otherwise they might construct a tuple that should
 * be of the domain type, but not apply domain constraints.
 */
TupleDesc
lookup_rowtype_tupdesc_domain(Oid type_id, int32 typmod, bool noError)
{
	TupleDesc	tupDesc;

	if (type_id != RECORDOID)
	{
		/*
		 * Check for domain or named composite type.  We might as well load
		 * whichever data is needed.
		 */
		TypeCacheEntry *typentry;

		typentry = lookup_type_cache(type_id,
									 TYPECACHE_TUPDESC |
									 TYPECACHE_DOMAIN_BASE_INFO);
		if (typentry->typtype == TYPTYPE_DOMAIN)
			return lookup_rowtype_tupdesc_noerror(typentry->domainBaseType,
												  typentry->domainBaseTypmod,
												  noError);
		if (typentry->tupDesc == NULL && !noError)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("type %s is not composite",
							format_type_be(type_id))));
		tupDesc = typentry->tupDesc;
	}
	else
		tupDesc = lookup_rowtype_tupdesc_internal(type_id, typmod, noError);
	if (tupDesc != NULL)
		PinTupleDesc(tupDesc);
	return tupDesc;
}

/*
 * Hash function for the hash table of RecordCacheEntry.
 */
static uint32
record_type_typmod_hash(const void *data, size_t size)
{
	RecordCacheEntry *entry = (RecordCacheEntry *) data;

	return hashTupleDesc(entry->tupdesc);
}

/*
 * Match function for the hash table of RecordCacheEntry.
 */
static int
record_type_typmod_compare(const void *a, const void *b, size_t size)
{
	RecordCacheEntry *left = (RecordCacheEntry *) a;
	RecordCacheEntry *right = (RecordCacheEntry *) b;

	return equalTupleDescs(left->tupdesc, right->tupdesc) ? 0 : 1;
}

/*
 * assign_record_type_typmod
 *
 * Given a tuple descriptor for a RECORD type, find or create a cache entry
 * for the type, and set the tupdesc's tdtypmod field to a value that will
 * identify this cache entry to lookup_rowtype_tupdesc.
 */
void
assign_record_type_typmod(TupleDesc tupDesc)
{
	RecordCacheEntry *recentry;
	TupleDesc	entDesc;
	bool		found;
	MemoryContext oldcxt;

	Assert(tupDesc->tdtypeid == RECORDOID);

	if (RecordCacheHash == NULL)
	{
		/* First time through: initialize the hash table */
		HASHCTL		ctl;

		ctl.keysize = sizeof(TupleDesc);	/* just the pointer */
		ctl.entrysize = sizeof(RecordCacheEntry);
		ctl.hash = record_type_typmod_hash;
		ctl.match = record_type_typmod_compare;
		RecordCacheHash = hash_create("Record information cache", 64,
									  &ctl,
									  HASH_ELEM | HASH_FUNCTION | HASH_COMPARE);

		/* Also make sure CacheMemoryContext exists */
		if (!CacheMemoryContext)
			CreateCacheMemoryContext();
	}

	/*
	 * Find a hashtable entry for this tuple descriptor. We don't use
	 * HASH_ENTER yet, because if it's missing, we need to make sure that all
	 * the allocations succeed before we create the new entry.
	 */
	recentry = (RecordCacheEntry *) hash_search(RecordCacheHash,
												(void *) &tupDesc,
												HASH_FIND, &found);
	if (found && recentry->tupdesc != NULL)
	{
		tupDesc->tdtypmod = recentry->tupdesc->tdtypmod;
		return;
	}

	/* Not present, so need to manufacture an entry */
	oldcxt = MemoryContextSwitchTo(CacheMemoryContext);

	/* Look in the SharedRecordTypmodRegistry, if attached */
	entDesc = find_or_make_matching_shared_tupledesc(tupDesc);
	if (entDesc == NULL)
	{
		/*
		 * Make sure we have room before we CreateTupleDescCopy() or advance
		 * NextRecordTypmod.
		 */
		ensure_record_cache_typmod_slot_exists(NextRecordTypmod);

		/* Reference-counted local cache only. */
		entDesc = CreateTupleDescCopy(tupDesc);
		entDesc->tdrefcount = 1;
		entDesc->tdtypmod = NextRecordTypmod++;
	}
	else
	{
		ensure_record_cache_typmod_slot_exists(entDesc->tdtypmod);
	}

	RecordCacheArray[entDesc->tdtypmod] = entDesc;

	/* Assign a unique tupdesc identifier, too. */
	RecordIdentifierArray[entDesc->tdtypmod] = ++tupledesc_id_counter;

	/* Fully initialized; create the hash table entry */
	recentry = (RecordCacheEntry *) hash_search(RecordCacheHash,
												(void *) &tupDesc,
												HASH_ENTER, NULL);
	recentry->tupdesc = entDesc;

	/* Update the caller's tuple descriptor. */
	tupDesc->tdtypmod = entDesc->tdtypmod;

	MemoryContextSwitchTo(oldcxt);
}

/*
 * assign_record_type_identifier
 *
 * Get an identifier, which will be unique over the lifespan of this backend
 * process, for the current tuple descriptor of the specified composite type.
 * For named composite types, the value is guaranteed to change if the type's
 * definition does.  For registered RECORD types, the value will not change
 * once assigned, since the registered type won't either.  If an anonymous
 * RECORD type is specified, we return a new identifier on each call.
 */
uint64
assign_record_type_identifier(Oid type_id, int32 typmod)
{
	if (type_id != RECORDOID)
	{
		/*
		 * It's a named composite type, so use the regular typcache.
		 */
		TypeCacheEntry *typentry;

		typentry = lookup_type_cache(type_id, TYPECACHE_TUPDESC);
		if (typentry->tupDesc == NULL)
			ereport(ERROR,
					(errcode(ERRCODE_WRONG_OBJECT_TYPE),
					 errmsg("type %s is not composite",
							format_type_be(type_id))));
		Assert(typentry->tupDesc_identifier != 0);
		return typentry->tupDesc_identifier;
	}
	else
	{
		/*
		 * It's a transient record type, so look in our record-type table.
		 */
		if (typmod >= 0 && typmod < RecordCacheArrayLen &&
			RecordCacheArray[typmod] != NULL)
		{
			Assert(RecordIdentifierArray[typmod] != 0);
			return RecordIdentifierArray[typmod];
		}

		/* For anonymous or unrecognized record type, generate a new ID */
		return ++tupledesc_id_counter;
	}
}

/*
 * Return the amount of shmem required to hold a SharedRecordTypmodRegistry.
 * This exists only to avoid exposing private innards of
 * SharedRecordTypmodRegistry in a header.
 */
size_t
SharedRecordTypmodRegistryEstimate(void)
{
	return sizeof(SharedRecordTypmodRegistry);
}

/*
 * Initialize 'registry' in a pre-existing shared memory region, which must be
 * maximally aligned and have space for SharedRecordTypmodRegistryEstimate()
 * bytes.
 *
 * 'area' will be used to allocate shared memory space as required for the
 * typemod registration.  The current process, expected to be a leader process
 * in a parallel query, will be attached automatically and its current record
 * types will be loaded into *registry.  While attached, all calls to
 * assign_record_type_typmod will use the shared registry.  Worker backends
 * will need to attach explicitly.
 *
 * Note that this function takes 'area' and 'segment' as arguments rather than
 * accessing them via CurrentSession, because they aren't installed there
 * until after this function runs.
 */
void
SharedRecordTypmodRegistryInit(SharedRecordTypmodRegistry *registry,
							   dsm_segment *segment,
							   dsa_area *area)
{
	MemoryContext old_context;
	dshash_table *record_table;
	dshash_table *typmod_table;
	int32		typmod;

	Assert(!IsParallelWorker());

	/* We can't already be attached to a shared registry. */
	Assert(CurrentSession->shared_typmod_registry == NULL);
	Assert(CurrentSession->shared_record_table == NULL);
	Assert(CurrentSession->shared_typmod_table == NULL);

	old_context = MemoryContextSwitchTo(TopMemoryContext);

	/* Create the hash table of tuple descriptors indexed by themselves. */
	record_table = dshash_create(area, &srtr_record_table_params, area);

	/* Create the hash table of tuple descriptors indexed by typmod. */
	typmod_table = dshash_create(area, &srtr_typmod_table_params, NULL);

	MemoryContextSwitchTo(old_context);

	/* Initialize the SharedRecordTypmodRegistry. */
	registry->record_table_handle = dshash_get_hash_table_handle(record_table);
	registry->typmod_table_handle = dshash_get_hash_table_handle(typmod_table);
	pg_atomic_init_u32(&registry->next_typmod, NextRecordTypmod);

	/*
	 * Copy all entries from this backend's private registry into the shared
	 * registry.
	 */
	for (typmod = 0; typmod < NextRecordTypmod; ++typmod)
	{
		SharedTypmodTableEntry *typmod_table_entry;
		SharedRecordTableEntry *record_table_entry;
		SharedRecordTableKey record_table_key;
		dsa_pointer shared_dp;
		TupleDesc	tupdesc;
		bool		found;

		tupdesc = RecordCacheArray[typmod];
		if (tupdesc == NULL)
			continue;

		/* Copy the TupleDesc into shared memory. */
		shared_dp = share_tupledesc(area, tupdesc, typmod);

		/* Insert into the typmod table. */
		typmod_table_entry = dshash_find_or_insert(typmod_table,
												   &tupdesc->tdtypmod,
												   &found);
		if (found)
			elog(ERROR, "cannot create duplicate shared record typmod");
		typmod_table_entry->typmod = tupdesc->tdtypmod;
		typmod_table_entry->shared_tupdesc = shared_dp;
		dshash_release_lock(typmod_table, typmod_table_entry);

		/* Insert into the record table. */
		record_table_key.shared = false;
		record_table_key.u.local_tupdesc = tupdesc;
		record_table_entry = dshash_find_or_insert(record_table,
												   &record_table_key,
												   &found);
		if (!found)
		{
			record_table_entry->key.shared = true;
			record_table_entry->key.u.shared_tupdesc = shared_dp;
		}
		dshash_release_lock(record_table, record_table_entry);
	}

	/*
	 * Set up the global state that will tell assign_record_type_typmod and
	 * lookup_rowtype_tupdesc_internal about the shared registry.
	 */
	CurrentSession->shared_record_table = record_table;
	CurrentSession->shared_typmod_table = typmod_table;
	CurrentSession->shared_typmod_registry = registry;

	/*
	 * We install a detach hook in the leader, but only to handle cleanup on
	 * failure during GetSessionDsmHandle().  Once GetSessionDsmHandle() pins
	 * the memory, the leader process will use a shared registry until it
	 * exits.
	 */
	on_dsm_detach(segment, shared_record_typmod_registry_detach, (Datum) 0);
}

/*
 * Attach to 'registry', which must have been initialized already by another
 * backend.  Future calls to assign_record_type_typmod and
 * lookup_rowtype_tupdesc_internal will use the shared registry until the
 * current session is detached.
 */
void
SharedRecordTypmodRegistryAttach(SharedRecordTypmodRegistry *registry)
{
	MemoryContext old_context;
	dshash_table *record_table;
	dshash_table *typmod_table;

	Assert(IsParallelWorker());

	/* We can't already be attached to a shared registry. */
	Assert(CurrentSession != NULL);
	Assert(CurrentSession->segment != NULL);
	Assert(CurrentSession->area != NULL);
	Assert(CurrentSession->shared_typmod_registry == NULL);
	Assert(CurrentSession->shared_record_table == NULL);
	Assert(CurrentSession->shared_typmod_table == NULL);

	/*
	 * We can't already have typmods in our local cache, because they'd clash
	 * with those imported by SharedRecordTypmodRegistryInit.  This should be
	 * a freshly started parallel worker.  If we ever support worker
	 * recycling, a worker would need to zap its local cache in between
	 * servicing different queries, in order to be able to call this and
	 * synchronize typmods with a new leader; but that's problematic because
	 * we can't be very sure that record-typmod-related state hasn't escaped
	 * to anywhere else in the process.
	 */
	Assert(NextRecordTypmod == 0);

	old_context = MemoryContextSwitchTo(TopMemoryContext);

	/* Attach to the two hash tables. */
	record_table = dshash_attach(CurrentSession->area,
								 &srtr_record_table_params,
								 registry->record_table_handle,
								 CurrentSession->area);
	typmod_table = dshash_attach(CurrentSession->area,
								 &srtr_typmod_table_params,
								 registry->typmod_table_handle,
								 NULL);

	MemoryContextSwitchTo(old_context);

	/*
	 * Set up detach hook to run at worker exit.  Currently this is the same
	 * as the leader's detach hook, but in future they might need to be
	 * different.
	 */
	on_dsm_detach(CurrentSession->segment,
				  shared_record_typmod_registry_detach,
				  PointerGetDatum(registry));

	/*
	 * Set up the session state that will tell assign_record_type_typmod and
	 * lookup_rowtype_tupdesc_internal about the shared registry.
	 */
	CurrentSession->shared_typmod_registry = registry;
	CurrentSession->shared_record_table = record_table;
	CurrentSession->shared_typmod_table = typmod_table;
}

/*
 * TypeCacheRelCallback
 *		Relcache inval callback function
 *
 * Delete the cached tuple descriptor (if any) for the given rel's composite
 * type, or for all composite types if relid == InvalidOid.  Also reset
 * whatever info we have cached about the composite type's comparability.
 *
 * This is called when a relcache invalidation event occurs for the given
 * relid.  We must scan the whole typcache hash since we don't know the
 * type OID corresponding to the relid.  We could do a direct search if this
 * were a syscache-flush callback on pg_type, but then we would need all
 * ALTER-TABLE-like commands that could modify a rowtype to issue syscache
 * invals against the rel's pg_type OID.  The extra SI signaling could very
 * well cost more than we'd save, since in most usages there are not very
 * many entries in a backend's typcache.  The risk of bugs-of-omission seems
 * high, too.
 *
 * Another possibility, with only localized impact, is to maintain a second
 * hashtable that indexes composite-type typcache entries by their typrelid.
 * But it's still not clear it's worth the trouble.
 */
static void
TypeCacheRelCallback(Datum arg, Oid relid)
{
	HASH_SEQ_STATUS status;
	TypeCacheEntry *typentry;

	/* TypeCacheHash must exist, else this callback wouldn't be registered */
	hash_seq_init(&status, TypeCacheHash);
	while ((typentry = (TypeCacheEntry *) hash_seq_search(&status)) != NULL)
	{
		if (typentry->typtype == TYPTYPE_COMPOSITE)
		{
			/* Skip if no match, unless we're zapping all composite types */
			if (relid != typentry->typrelid && relid != InvalidOid)
				continue;

			/* Delete tupdesc if we have it */
			if (typentry->tupDesc != NULL)
			{
				/*
				 * Release our refcount, and free the tupdesc if none remain.
				 * (Can't use DecrTupleDescRefCount because this reference is
				 * not logged in current resource owner.)
				 */
				Assert(typentry->tupDesc->tdrefcount > 0);
				if (--typentry->tupDesc->tdrefcount == 0)
					FreeTupleDesc(typentry->tupDesc);
				typentry->tupDesc = NULL;

				/*
				 * Also clear tupDesc_identifier, so that anything watching
				 * that will realize that the tupdesc has possibly changed.
				 * (Alternatively, we could specify that to detect possible
				 * tupdesc change, one must check for tupDesc != NULL as well
				 * as tupDesc_identifier being the same as what was previously
				 * seen.  That seems error-prone.)
				 */
				typentry->tupDesc_identifier = 0;
			}

			/* Reset equality/comparison/hashing validity information */
			typentry->flags &= ~TCFLAGS_OPERATOR_FLAGS;
		}
		else if (typentry->typtype == TYPTYPE_DOMAIN)
		{
			/*
			 * If it's domain over composite, reset flags.  (We don't bother
			 * trying to determine whether the specific base type needs a
			 * reset.)  Note that if we haven't determined whether the base
			 * type is composite, we don't need to reset anything.
			 */
			if (typentry->flags & TCFLAGS_DOMAIN_BASE_IS_COMPOSITE)
				typentry->flags &= ~TCFLAGS_OPERATOR_FLAGS;
		}
	}
}

/*
 * TypeCacheTypCallback
 *		Syscache inval callback function
 *
 * This is called when a syscache invalidation event occurs for any
 * pg_type row.  If we have information cached about that type, mark
 * it as needing to be reloaded.
 */
static void
TypeCacheTypCallback(Datum arg, int cacheid, uint32 hashvalue)
{
	HASH_SEQ_STATUS status;
	TypeCacheEntry *typentry;

	/* TypeCacheHash must exist, else this callback wouldn't be registered */
	hash_seq_init(&status, TypeCacheHash);
	while ((typentry = (TypeCacheEntry *) hash_seq_search(&status)) != NULL)
	{
		/* Is this the targeted type row (or it's a total cache flush)? */
		if (hashvalue == 0 || typentry->type_id_hash == hashvalue)
		{
			/*
			 * Mark the data obtained directly from pg_type as invalid.  Also,
			 * if it's a domain, typnotnull might've changed, so we'll need to
			 * recalculate its constraints.
			 */
			typentry->flags &= ~(TCFLAGS_HAVE_PG_TYPE_DATA |
								 TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS);
		}
	}
}

/*
 * TypeCacheOpcCallback
 *		Syscache inval callback function
 *
 * This is called when a syscache invalidation event occurs for any pg_opclass
 * row.  In principle we could probably just invalidate data dependent on the
 * particular opclass, but since updates on pg_opclass are rare in production
 * it doesn't seem worth a lot of complication: we just mark all cached data
 * invalid.
 *
 * Note that we don't bother watching for updates on pg_amop or pg_amproc.
 * This should be safe because ALTER OPERATOR FAMILY ADD/DROP OPERATOR/FUNCTION
 * is not allowed to be used to add/drop the primary operators and functions
 * of an opclass, only cross-type members of a family; and the latter sorts
 * of members are not going to get cached here.
 */
static void
TypeCacheOpcCallback(Datum arg, int cacheid, uint32 hashvalue)
{
	HASH_SEQ_STATUS status;
	TypeCacheEntry *typentry;

	/* TypeCacheHash must exist, else this callback wouldn't be registered */
	hash_seq_init(&status, TypeCacheHash);
	while ((typentry = (TypeCacheEntry *) hash_seq_search(&status)) != NULL)
	{
		/* Reset equality/comparison/hashing validity information */
		typentry->flags &= ~TCFLAGS_OPERATOR_FLAGS;
	}
}

/*
 * TypeCacheConstrCallback
 *		Syscache inval callback function
 *
 * This is called when a syscache invalidation event occurs for any
 * pg_constraint row.  We flush information about domain constraints
 * when this happens.
 *
 * It's slightly annoying that we can't tell whether the inval event was for
 * a domain constraint record or not; there's usually more update traffic
 * for table constraints than domain constraints, so we'll do a lot of
 * useless flushes.  Still, this is better than the old no-caching-at-all
 * approach to domain constraints.
 */
static void
TypeCacheConstrCallback(Datum arg, int cacheid, uint32 hashvalue)
{
	TypeCacheEntry *typentry;

	/*
	 * Because this is called very frequently, and typically very few of the
	 * typcache entries are for domains, we don't use hash_seq_search here.
	 * Instead we thread all the domain-type entries together so that we can
	 * visit them cheaply.
	 */
	for (typentry = firstDomainTypeEntry;
		 typentry != NULL;
		 typentry = typentry->nextDomain)
	{
		/* Reset domain constraint validity information */
		typentry->flags &= ~TCFLAGS_CHECKED_DOMAIN_CONSTRAINTS;
	}
}


/*
 * Check if given OID is part of the subset that's sortable by comparisons
 */
static inline bool
enum_known_sorted(TypeCacheEnumData *enumdata, Oid arg)
{
	Oid			offset;

	if (arg < enumdata->bitmap_base)
		return false;
	offset = arg - enumdata->bitmap_base;
	if (offset > (Oid) INT_MAX)
		return false;
	return bms_is_member((int) offset, enumdata->sorted_values);
}


/*
 * compare_values_of_enum
 *		Compare two members of an enum type.
 *		Return <0, 0, or >0 according as arg1 <, =, or > arg2.
 *
 * Note: currently, the enumData cache is refreshed only if we are asked
 * to compare an enum value that is not already in the cache.  This is okay
 * because there is no support for re-ordering existing values, so comparisons
 * of previously cached values will return the right answer even if other
 * values have been added since we last loaded the cache.
 *
 * Note: the enum logic has a special-case rule about even-numbered versus
 * odd-numbered OIDs, but we take no account of that rule here; this
 * routine shouldn't even get called when that rule applies.
 */
int
compare_values_of_enum(TypeCacheEntry *tcache, Oid arg1, Oid arg2)
{
	TypeCacheEnumData *enumdata;
	EnumItem   *item1;
	EnumItem   *item2;

	/*
	 * Equal OIDs are certainly equal --- this case was probably handled by
	 * our caller, but we may as well check.
	 */
	if (arg1 == arg2)
		return 0;

	/* Load up the cache if first time through */
	if (tcache->enumData == NULL)
		load_enum_cache_data(tcache);
	enumdata = tcache->enumData;

	/*
	 * If both OIDs are known-sorted, we can just compare them directly.
	 */
	if (enum_known_sorted(enumdata, arg1) &&
		enum_known_sorted(enumdata, arg2))
	{
		if (arg1 < arg2)
			return -1;
		else
			return 1;
	}

	/*
	 * Slow path: we have to identify their actual sort-order positions.
	 */
	item1 = find_enumitem(enumdata, arg1);
	item2 = find_enumitem(enumdata, arg2);

	if (item1 == NULL || item2 == NULL)
	{
		/*
		 * We couldn't find one or both values.  That means the enum has
		 * changed under us, so re-initialize the cache and try again. We
		 * don't bother retrying the known-sorted case in this path.
		 */
		load_enum_cache_data(tcache);
		enumdata = tcache->enumData;

		item1 = find_enumitem(enumdata, arg1);
		item2 = find_enumitem(enumdata, arg2);

		/*
		 * If we still can't find the values, complain: we must have corrupt
		 * data.
		 */
		if (item1 == NULL)
			elog(ERROR, "enum value %u not found in cache for enum %s",
				 arg1, format_type_be(tcache->type_id));
		if (item2 == NULL)
			elog(ERROR, "enum value %u not found in cache for enum %s",
				 arg2, format_type_be(tcache->type_id));
	}

	if (item1->sort_order < item2->sort_order)
		return -1;
	else if (item1->sort_order > item2->sort_order)
		return 1;
	else
		return 0;
}

/*
 * Load (or re-load) the enumData member of the typcache entry.
 */
static void
load_enum_cache_data(TypeCacheEntry *tcache)
{
	TypeCacheEnumData *enumdata;
	Relation	enum_rel;
	SysScanDesc enum_scan;
	HeapTuple	enum_tuple;
	ScanKeyData skey;
	EnumItem   *items;
	int			numitems;
	int			maxitems;
	Oid			bitmap_base;
	Bitmapset  *bitmap;
	MemoryContext oldcxt;
	int			bm_size,
				start_pos;

	/* Check that this is actually an enum */
	if (tcache->typtype != TYPTYPE_ENUM)
		ereport(ERROR,
				(errcode(ERRCODE_WRONG_OBJECT_TYPE),
				 errmsg("%s is not an enum",
						format_type_be(tcache->type_id))));

	/*
	 * Read all the information for members of the enum type.  We collect the
	 * info in working memory in the caller's context, and then transfer it to
	 * permanent memory in CacheMemoryContext.  This minimizes the risk of
	 * leaking memory from CacheMemoryContext in the event of an error partway
	 * through.
	 */
	maxitems = 64;
	items = (EnumItem *) palloc(sizeof(EnumItem) * maxitems);
	numitems = 0;

	/* Scan pg_enum for the members of the target enum type. */
	ScanKeyInit(&skey,
				Anum_pg_enum_enumtypid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(tcache->type_id));

	enum_rel = table_open(EnumRelationId, AccessShareLock);
	enum_scan = systable_beginscan(enum_rel,
								   EnumTypIdLabelIndexId,
								   true, NULL,
								   1, &skey);

	while (HeapTupleIsValid(enum_tuple = systable_getnext(enum_scan)))
	{
		Form_pg_enum en = (Form_pg_enum) GETSTRUCT(enum_tuple);

		if (numitems >= maxitems)
		{
			maxitems *= 2;
			items = (EnumItem *) repalloc(items, sizeof(EnumItem) * maxitems);
		}
		items[numitems].enum_oid = en->oid;
		items[numitems].sort_order = en->enumsortorder;
		numitems++;
	}

	systable_endscan(enum_scan);
	table_close(enum_rel, AccessShareLock);

	/* Sort the items into OID order */
	qsort(items, numitems, sizeof(EnumItem), enum_oid_cmp);

	/*
	 * Here, we create a bitmap listing a subset of the enum's OIDs that are
	 * known to be in order and can thus be compared with just OID comparison.
	 *
	 * The point of this is that the enum's initial OIDs were certainly in
	 * order, so there is some subset that can be compared via OID comparison;
	 * and we'd rather not do binary searches unnecessarily.
	 *
	 * This is somewhat heuristic, and might identify a subset of OIDs that
	 * isn't exactly what the type started with.  That's okay as long as the
	 * subset is correctly sorted.
	 */
	bitmap_base = InvalidOid;
	bitmap = NULL;
	bm_size = 1;				/* only save sets of at least 2 OIDs */

	for (start_pos = 0; start_pos < numitems - 1; start_pos++)
	{
		/*
		 * Identify longest sorted subsequence starting at start_pos
		 */
		Bitmapset  *this_bitmap = bms_make_singleton(0);
		int			this_bm_size = 1;
		Oid			start_oid = items[start_pos].enum_oid;
		float4		prev_order = items[start_pos].sort_order;
		int			i;

		for (i = start_pos + 1; i < numitems; i++)
		{
			Oid			offset;

			offset = items[i].enum_oid - start_oid;
			/* quit if bitmap would be too large; cutoff is arbitrary */
			if (offset >= 8192)
				break;
			/* include the item if it's in-order */
			if (items[i].sort_order > prev_order)
			{
				prev_order = items[i].sort_order;
				this_bitmap = bms_add_member(this_bitmap, (int) offset);
				this_bm_size++;
			}
		}

		/* Remember it if larger than previous best */
		if (this_bm_size > bm_size)
		{
			bms_free(bitmap);
			bitmap_base = start_oid;
			bitmap = this_bitmap;
			bm_size = this_bm_size;
		}
		else
			bms_free(this_bitmap);

		/*
		 * Done if it's not possible to find a longer sequence in the rest of
		 * the list.  In typical cases this will happen on the first
		 * iteration, which is why we create the bitmaps on the fly instead of
		 * doing a second pass over the list.
		 */
		if (bm_size >= (numitems - start_pos - 1))
			break;
	}

	/* OK, copy the data into CacheMemoryContext */
	oldcxt = MemoryContextSwitchTo(CacheMemoryContext);
	enumdata = (TypeCacheEnumData *)
		palloc(offsetof(TypeCacheEnumData, enum_values) +
			   numitems * sizeof(EnumItem));
	enumdata->bitmap_base = bitmap_base;
	enumdata->sorted_values = bms_copy(bitmap);
	enumdata->num_values = numitems;
	memcpy(enumdata->enum_values, items, numitems * sizeof(EnumItem));
	MemoryContextSwitchTo(oldcxt);

	pfree(items);
	bms_free(bitmap);

	/* And link the finished cache struct into the typcache */
	if (tcache->enumData != NULL)
		pfree(tcache->enumData);
	tcache->enumData = enumdata;
}

/*
 * Locate the EnumItem with the given OID, if present
 */
static EnumItem *
find_enumitem(TypeCacheEnumData *enumdata, Oid arg)
{
	EnumItem	srch;

	/* On some versions of Solaris, bsearch of zero items dumps core */
	if (enumdata->num_values <= 0)
		return NULL;

	srch.enum_oid = arg;
	return bsearch(&srch, enumdata->enum_values, enumdata->num_values,
				   sizeof(EnumItem), enum_oid_cmp);
}

/*
 * qsort comparison function for OID-ordered EnumItems
 */
static int
enum_oid_cmp(const void *left, const void *right)
{
	const EnumItem *l = (const EnumItem *) left;
	const EnumItem *r = (const EnumItem *) right;

	if (l->enum_oid < r->enum_oid)
		return -1;
	else if (l->enum_oid > r->enum_oid)
		return 1;
	else
		return 0;
}

/*
 * Copy 'tupdesc' into newly allocated shared memory in 'area', set its typmod
 * to the given value and return a dsa_pointer.
 */
static dsa_pointer
share_tupledesc(dsa_area *area, TupleDesc tupdesc, uint32 typmod)
{
	dsa_pointer shared_dp;
	TupleDesc	shared;

	shared_dp = dsa_allocate(area, TupleDescSize(tupdesc));
	shared = (TupleDesc) dsa_get_address(area, shared_dp);
	TupleDescCopy(shared, tupdesc);
	shared->tdtypmod = typmod;

	return shared_dp;
}

/*
 * If we are attached to a SharedRecordTypmodRegistry, use it to find or
 * create a shared TupleDesc that matches 'tupdesc'.  Otherwise return NULL.
 * Tuple descriptors returned by this function are not reference counted, and
 * will exist at least as long as the current backend remained attached to the
 * current session.
 */
static TupleDesc
find_or_make_matching_shared_tupledesc(TupleDesc tupdesc)
{
	TupleDesc	result;
	SharedRecordTableKey key;
	SharedRecordTableEntry *record_table_entry;
	SharedTypmodTableEntry *typmod_table_entry;
	dsa_pointer shared_dp;
	bool		found;
	uint32		typmod;

	/* If not even attached, nothing to do. */
	if (CurrentSession->shared_typmod_registry == NULL)
		return NULL;

	/* Try to find a matching tuple descriptor in the record table. */
	key.shared = false;
	key.u.local_tupdesc = tupdesc;
	record_table_entry = (SharedRecordTableEntry *)
		dshash_find(CurrentSession->shared_record_table, &key, false);
	if (record_table_entry)
	{
		Assert(record_table_entry->key.shared);
		dshash_release_lock(CurrentSession->shared_record_table,
							record_table_entry);
		result = (TupleDesc)
			dsa_get_address(CurrentSession->area,
							record_table_entry->key.u.shared_tupdesc);
		Assert(result->tdrefcount == -1);

		return result;
	}

	/* Allocate a new typmod number.  This will be wasted if we error out. */
	typmod = (int)
		pg_atomic_fetch_add_u32(&CurrentSession->shared_typmod_registry->next_typmod,
								1);

	/* Copy the TupleDesc into shared memory. */
	shared_dp = share_tupledesc(CurrentSession->area, tupdesc, typmod);

	/*
	 * Create an entry in the typmod table so that others will understand this
	 * typmod number.
	 */
	PG_TRY();
	{
		typmod_table_entry = (SharedTypmodTableEntry *)
			dshash_find_or_insert(CurrentSession->shared_typmod_table,
								  &typmod, &found);
		if (found)
			elog(ERROR, "cannot create duplicate shared record typmod");
	}
	PG_CATCH();
	{
		dsa_free(CurrentSession->area, shared_dp);
		PG_RE_THROW();
	}
	PG_END_TRY();
	typmod_table_entry->typmod = typmod;
	typmod_table_entry->shared_tupdesc = shared_dp;
	dshash_release_lock(CurrentSession->shared_typmod_table,
						typmod_table_entry);

	/*
	 * Finally create an entry in the record table so others with matching
	 * tuple descriptors can reuse the typmod.
	 */
	record_table_entry = (SharedRecordTableEntry *)
		dshash_find_or_insert(CurrentSession->shared_record_table, &key,
							  &found);
	if (found)
	{
		/*
		 * Someone concurrently inserted a matching tuple descriptor since the
		 * first time we checked.  Use that one instead.
		 */
		dshash_release_lock(CurrentSession->shared_record_table,
							record_table_entry);

		/* Might as well free up the space used by the one we created. */
		found = dshash_delete_key(CurrentSession->shared_typmod_table,
								  &typmod);
		Assert(found);
		dsa_free(CurrentSession->area, shared_dp);

		/* Return the one we found. */
		Assert(record_table_entry->key.shared);
		result = (TupleDesc)
			dsa_get_address(CurrentSession->area,
							record_table_entry->key.u.shared_tupdesc);
		Assert(result->tdrefcount == -1);

		return result;
	}

	/* Store it and return it. */
	record_table_entry->key.shared = true;
	record_table_entry->key.u.shared_tupdesc = shared_dp;
	dshash_release_lock(CurrentSession->shared_record_table,
						record_table_entry);
	result = (TupleDesc)
		dsa_get_address(CurrentSession->area, shared_dp);
	Assert(result->tdrefcount == -1);

	return result;
}

/*
 * On-DSM-detach hook to forget about the current shared record typmod
 * infrastructure.  This is currently used by both leader and workers.
 */
static void
shared_record_typmod_registry_detach(dsm_segment *segment, Datum datum)
{
	/* Be cautious here: maybe we didn't finish initializing. */
	if (CurrentSession->shared_record_table != NULL)
	{
		dshash_detach(CurrentSession->shared_record_table);
		CurrentSession->shared_record_table = NULL;
	}
	if (CurrentSession->shared_typmod_table != NULL)
	{
		dshash_detach(CurrentSession->shared_typmod_table);
		CurrentSession->shared_typmod_table = NULL;
	}
	CurrentSession->shared_typmod_registry = NULL;
}
