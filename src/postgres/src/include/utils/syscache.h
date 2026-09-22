/*-------------------------------------------------------------------------
 *
 * syscache.h
 *	  System catalog cache definitions.
 *
 * See also lsyscache.h, which provides convenience routines for
 * common cache-lookup operations.
 *
 * Portions Copyright (c) 1996-2022, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/utils/syscache.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef SYSCACHE_H
#define SYSCACHE_H

#include "access/attnum.h"
#include "access/htup.h"
/* we intentionally do not include utils/catcache.h here */

/* YB includes */
#include "relcache.h"

/*
 *		SysCache identifiers.
 *
 *		The order of these identifiers must match the order
 *		of the entries in the array cacheinfo[] in syscache.c.
 *		Keep them in alphabetical order (renumbering only costs a
 *		backend rebuild).
 * YB Note:
 * Starting from 2025-03-11, we try to keep existing ids to have same integer
 * value. In other words, newly added ids should be appended at the end
 * and the alphabetical order can no longer be maintained. The purpose is
 * to allow interop between different releases during YSQL upgrade so that
 * an id of the same integer value represents the same catalog cache across
 * different releases. In this way a catalog cache invalidation message
 * generated in release 123 can be applicable to release 456, or vice versa.
 * The function YbCheckCatalogCacheIds() is used to detect any change in
 * this enum list.
 */

enum SysCacheIdentifier
{
	AGGFNOID = 0,
	AMNAME,
	AMOID,
	AMOPOPID,
	AMOPSTRATEGY,
	AMPROCNUM,
	ATTNAME,
	ATTNUM,
	AUTHMEMMEMROLE,
	AUTHMEMROLEMEM,
	AUTHNAME,
	AUTHOID,
	CASTSOURCETARGET,
	CLAAMNAMENSP,
	CLAOID,
	COLLNAMEENCNSP,
	COLLOID,
	CONDEFAULT,
	CONNAMENSP,
	CONSTROID,
	CONVOID,
	DATABASEOID,
	DEFACLROLENSPOBJ,
	ENUMOID,
	ENUMTYPOIDNAME,
	EVENTTRIGGERNAME,
	EVENTTRIGGEROID,
	FOREIGNDATAWRAPPERNAME,
	FOREIGNDATAWRAPPEROID,
	FOREIGNSERVERNAME,
	FOREIGNSERVEROID,
	FOREIGNTABLEREL,
	INDEXRELID,
	LANGNAME,
	LANGOID,
	NAMESPACENAME,
	NAMESPACEOID,
	OPERNAMENSP,
	OPEROID,
	OPFAMILYAMNAMENSP,
	OPFAMILYOID,
	PARAMETERACLNAME,
	PARAMETERACLOID,
	PARTRELID,
	PROCNAMEARGSNSP,
	PROCOID,
	PUBLICATIONNAME,
	PUBLICATIONNAMESPACE,
	PUBLICATIONNAMESPACEMAP,
	PUBLICATIONOID,
	PUBLICATIONREL,
	PUBLICATIONRELMAP,
	RANGEMULTIRANGE,
	RANGETYPE,
	RELNAMENSP,
	RELOID,
	REPLORIGIDENT,
	REPLORIGNAME,
	RULERELNAME,
	SEQRELID,
	STATEXTDATASTXOID,
	STATEXTNAMENSP,
	STATEXTOID,
	STATRELATTINH,
	SUBSCRIPTIONNAME,
	SUBSCRIPTIONOID,
	SUBSCRIPTIONRELMAP,
	TABLESPACEOID,
	TRFOID,
	TRFTYPELANG,
	TSCONFIGMAP,
	TSCONFIGNAMENSP,
	TSCONFIGOID,
	TSDICTNAMENSP,
	TSDICTOID,
	TSPARSERNAMENSP,
	TSPARSEROID,
	TSTEMPLATENAMENSP,
	TSTEMPLATEOID,
	TYPENAMENSP,
	TYPEOID,
	USERMAPPINGOID,
	USERMAPPINGUSERSERVER,
	YBTABLEGROUPOID,
	YBCONSTRAINTRELIDTYPIDNAME,
	/* intentionally out of alphabetical order, to avoid an ABI break: */
	EXTENSIONOID

#define SysCacheSize (EXTENSIONOID + 1)
};

/*
 * The single source of truth for the set of catalog tables that have caches on
 * them.  Both the YbCatalogCacheTable enum and yb_cache_table_name_table[] in
 * syscache.c are generated from this list, so a table's enum value and its
 * name string cannot drift apart.  Keep it that way: the size assertion on
 * yb_cache_table_name_table[] catches an entry left out of one of the two,
 * but not one inserted into the middle of one and appended to the end of the
 * other, which would silently misname every table after the insertion point.
 *
 * Each entry carries its own prefix so that one list can generate both
 * spellings of the enumerator.  A table that has no syscache of its own and is
 * cached ad hoc instead takes the YbAdhocCacheTable prefix; the only one today
 * is pg_inherits, whose cache lives in yb_inheritscache.c.  Every other table
 * takes YbCatalogCacheTable.
 *
 * The YbCatalogCacheTable entries are in alphabetical order.  The ad hoc ones
 * are not, and have to stay at the end of the list, because
 * YbNumCatalogCacheTables below is defined as the last enumerator plus one.
 * Getting that wrong is a build failure rather than a latent bug: the size
 * assertion on yb_cache_table_name_table[] in syscache.c compares that count
 * against the actual length of this list.
 */
#define YB_CATCACHE_TABLE_LIST \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_aggregate) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_am) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_amop) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_amproc) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_attribute) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_auth_members) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_authid) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_cast) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_class) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_collation) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_constraint) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_conversion) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_database) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_default_acl) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_enum) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_event_trigger) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_extension) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_foreign_data_wrapper) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_foreign_server) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_foreign_table) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_index) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_language) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_namespace) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_opclass) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_operator) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_opfamily) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_parameter_acl) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_partitioned_table) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_proc) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_publication) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_publication_namespace) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_publication_rel) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_range) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_replication_origin) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_rewrite) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_sequence) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_statistic) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_statistic_ext) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_statistic_ext_data) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_subscription) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_subscription_rel) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_tablespace) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_transform) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_ts_config) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_ts_config_map) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_ts_dict) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_ts_parser) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_ts_template) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_type) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_user_mapping) \
	YB_CATCACHE_TABLE_ENTRY(YbCatalogCacheTable, pg_yb_tablegroup) \
	YB_CATCACHE_TABLE_ENTRY(YbAdhocCacheTable, pg_inherits)

typedef enum YbCatalogCacheTable
{
#define YB_CATCACHE_TABLE_ENTRY(prefix, table) prefix##_##table,
	YB_CATCACHE_TABLE_LIST
#undef YB_CATCACHE_TABLE_ENTRY
} YbCatalogCacheTable;

#define YbNumCatalogCacheTables (YbAdhocCacheTable_pg_inherits + 1)

extern long YbNumCatalogCacheMisses;
extern long YbNumCatalogCacheTableMisses[];
extern long YbNumCatalogCacheNegMisses[];

extern void InitCatalogCache(void);
extern void InitCatalogCachePhase2(void);

extern HeapTuple SearchSysCache(int cacheId,
								Datum key1, Datum key2, Datum key3, Datum key4);

/*
 * The use of argument specific numbers is encouraged. They're faster, and
 * insulates the caller from changes in the maximum number of keys.
 */
extern HeapTuple SearchSysCache1(int cacheId,
								 Datum key1);
extern HeapTuple SearchSysCache2(int cacheId,
								 Datum key1, Datum key2);
extern HeapTuple SearchSysCache3(int cacheId,
								 Datum key1, Datum key2, Datum key3);
extern HeapTuple SearchSysCache4(int cacheId,
								 Datum key1, Datum key2, Datum key3, Datum key4);

extern void ReleaseSysCache(HeapTuple tuple);

extern HeapTuple SearchSysCacheLocked1(int cacheId,
									   Datum key1);

/* convenience routines */
extern HeapTuple SearchSysCacheCopy(int cacheId,
									Datum key1, Datum key2, Datum key3, Datum key4);
extern HeapTuple SearchSysCacheLockedCopy1(int cacheId,
										   Datum key1);
extern bool SearchSysCacheExists(int cacheId,
								 Datum key1, Datum key2, Datum key3, Datum key4);
extern Oid	GetSysCacheOid(int cacheId, AttrNumber oidcol,
						   Datum key1, Datum key2, Datum key3, Datum key4);

extern HeapTuple SearchSysCacheAttName(Oid relid, const char *attname);
extern HeapTuple SearchSysCacheCopyAttName(Oid relid, const char *attname);
extern bool SearchSysCacheExistsAttName(Oid relid, const char *attname);

extern HeapTuple SearchSysCacheAttNum(Oid relid, int16 attnum);
extern HeapTuple SearchSysCacheCopyAttNum(Oid relid, int16 attnum);

extern Datum SysCacheGetAttr(int cacheId, HeapTuple tup,
							 AttrNumber attributeNumber, bool *isNull);

extern uint32 GetSysCacheHashValue(int cacheId,
								   Datum key1, Datum key2, Datum key3, Datum key4);

/* list-search interface.  Users of this must import catcache.h too */
struct catclist;
extern struct catclist *SearchSysCacheList(int cacheId, int nkeys,
										   Datum key1, Datum key2, Datum key3);

extern void SysCacheInvalidate(int cacheId, uint32 hashValue);

extern bool RelationInvalidatesSnapshotsOnly(Oid relid);
extern bool RelationHasSysCache(Oid relid);
extern bool RelationSupportsSysCache(Oid relid);

/* YB */
extern void YbSetSysCacheTuple(Relation rel, HeapTuple tup);
extern void YbPreloadCatalogCache(int cache_id, int idx_cache_id);
#ifndef NDEBUG
extern bool YbCheckCatalogCacheIndexNameTable();
extern bool YbCheckSysCacheNames();
#endif
extern const char *YbGetCatalogCacheIndexName(int cache_id);
extern const char *YbGetCatalogCacheTableNameFromTableId(int table_id);
extern const char *YbGetCatalogCacheTableNameFromCacheId(int cache_id);
extern int	YbGetCatalogCacheTableIdFromCacheId(int cache_id);
extern uint32 YbSysCacheComputeHashValue(int cache_id, Datum v1, Datum v2, Datum v3, Datum v4);
extern void YbCopyCacheInfoToValues(int cache_id, Datum *values);
extern void YbSetAdditionalNegCacheIds(List *neg_cache_ids);

/*
 * The use of the macros below rather than direct calls to the corresponding
 * functions is encouraged, as it insulates the caller from changes in the
 * maximum number of keys.
 */
#define SearchSysCacheCopy1(cacheId, key1) \
	SearchSysCacheCopy(cacheId, key1, 0, 0, 0)
#define SearchSysCacheCopy2(cacheId, key1, key2) \
	SearchSysCacheCopy(cacheId, key1, key2, 0, 0)
#define SearchSysCacheCopy3(cacheId, key1, key2, key3) \
	SearchSysCacheCopy(cacheId, key1, key2, key3, 0)
#define SearchSysCacheCopy4(cacheId, key1, key2, key3, key4) \
	SearchSysCacheCopy(cacheId, key1, key2, key3, key4)

#define SearchSysCacheExists1(cacheId, key1) \
	SearchSysCacheExists(cacheId, key1, 0, 0, 0)
#define SearchSysCacheExists2(cacheId, key1, key2) \
	SearchSysCacheExists(cacheId, key1, key2, 0, 0)
#define SearchSysCacheExists3(cacheId, key1, key2, key3) \
	SearchSysCacheExists(cacheId, key1, key2, key3, 0)
#define SearchSysCacheExists4(cacheId, key1, key2, key3, key4) \
	SearchSysCacheExists(cacheId, key1, key2, key3, key4)

#define GetSysCacheOid1(cacheId, oidcol, key1) \
	GetSysCacheOid(cacheId, oidcol, key1, 0, 0, 0)
#define GetSysCacheOid2(cacheId, oidcol, key1, key2) \
	GetSysCacheOid(cacheId, oidcol, key1, key2, 0, 0)
#define GetSysCacheOid3(cacheId, oidcol, key1, key2, key3) \
	GetSysCacheOid(cacheId, oidcol, key1, key2, key3, 0)
#define GetSysCacheOid4(cacheId, oidcol, key1, key2, key3, key4) \
	GetSysCacheOid(cacheId, oidcol, key1, key2, key3, key4)

#define GetSysCacheHashValue1(cacheId, key1) \
	GetSysCacheHashValue(cacheId, key1, 0, 0, 0)
#define GetSysCacheHashValue2(cacheId, key1, key2) \
	GetSysCacheHashValue(cacheId, key1, key2, 0, 0)
#define GetSysCacheHashValue3(cacheId, key1, key2, key3) \
	GetSysCacheHashValue(cacheId, key1, key2, key3, 0)
#define GetSysCacheHashValue4(cacheId, key1, key2, key3, key4) \
	GetSysCacheHashValue(cacheId, key1, key2, key3, key4)

#define SearchSysCacheList1(cacheId, key1) \
	SearchSysCacheList(cacheId, 1, key1, 0, 0)
#define SearchSysCacheList2(cacheId, key1, key2) \
	SearchSysCacheList(cacheId, 2, key1, key2, 0)
#define SearchSysCacheList3(cacheId, key1, key2, key3) \
	SearchSysCacheList(cacheId, 3, key1, key2, key3)

#define ReleaseSysCacheList(x)	ReleaseCatCacheList(x)

#endif							/* SYSCACHE_H */
