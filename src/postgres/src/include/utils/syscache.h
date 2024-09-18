/*-------------------------------------------------------------------------
 *
 * syscache.h
 *	  System catalog cache definitions.
 *
 * See also lsyscache.h, which provides convenience routines for
 * common cache-lookup operations.
 *
 * Portions Copyright (c) 1996-2018, PostgreSQL Global Development Group
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
#include "relcache.h"

/* we intentionally do not include utils/catcache.h here */

/*
 *		SysCache identifiers.
 *
 *		The order of these identifiers must match the order
 *		of the entries in the array cacheinfo[] in syscache.c.
 *		Keep them in alphabetical order (renumbering only costs a
 *		backend rebuild).
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
	PARTRELID,
	PROCNAMEARGSNSP,
	PROCOID,
	PUBLICATIONNAME,
	PUBLICATIONOID,
	PUBLICATIONREL,
	PUBLICATIONRELMAP,
	RANGETYPE,
	RELNAMENSP,
	RELOID,
	REPLORIGIDENT,
	REPLORIGNAME,
	RULERELNAME,
	SEQRELID,
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
	YBCONSTRAINTRELIDTYPIDNAME

#define SysCacheSize (YBCONSTRAINTRELIDTYPIDNAME + 1)
};

typedef enum YbCatalogCacheTable
{
	YbCatalogCacheTable_pg_aggregate,
	YbCatalogCacheTable_pg_am,
	YbCatalogCacheTable_pg_amop,
	YbCatalogCacheTable_pg_amproc,
	YbCatalogCacheTable_pg_attribute,
	YbCatalogCacheTable_pg_auth_members,
	YbCatalogCacheTable_pg_authid,
	YbCatalogCacheTable_pg_cast,
	YbCatalogCacheTable_pg_class,
	YbCatalogCacheTable_pg_collation,
	YbCatalogCacheTable_pg_constraint,
	YbCatalogCacheTable_pg_conversion,
	YbCatalogCacheTable_pg_database,
	YbCatalogCacheTable_pg_default_acl,
	YbCatalogCacheTable_pg_enum,
	YbCatalogCacheTable_pg_event_trigger,
	YbCatalogCacheTable_pg_foreign_data_wrapper,
	YbCatalogCacheTable_pg_foreign_server,
	YbCatalogCacheTable_pg_foreign_table,
	YbCatalogCacheTable_pg_index,
	YbCatalogCacheTable_pg_language,
	YbCatalogCacheTable_pg_namespace,
	YbCatalogCacheTable_pg_opclass,
	YbCatalogCacheTable_pg_operator,
	YbCatalogCacheTable_pg_opfamily,
	YbCatalogCacheTable_pg_partitioned_table,
	YbCatalogCacheTable_pg_proc,
	YbCatalogCacheTable_pg_publication,
	YbCatalogCacheTable_pg_publication_rel,
	YbCatalogCacheTable_pg_range,
	YbCatalogCacheTable_pg_replication_origin,
	YbCatalogCacheTable_pg_rewrite,
	YbCatalogCacheTable_pg_sequence,
	YbCatalogCacheTable_pg_statistic,
	YbCatalogCacheTable_pg_statistic_ext,
	YbCatalogCacheTable_pg_subscription,
	YbCatalogCacheTable_pg_subscription_rel,
	YbCatalogCacheTable_pg_tablespace,
	YbCatalogCacheTable_pg_transform,
	YbCatalogCacheTable_pg_ts_config,
	YbCatalogCacheTable_pg_ts_config_map,
	YbCatalogCacheTable_pg_ts_dict,
	YbCatalogCacheTable_pg_ts_parser,
	YbCatalogCacheTable_pg_ts_template,
	YbCatalogCacheTable_pg_type,
	YbCatalogCacheTable_pg_user_mapping,
	YbCatalogCacheTable_pg_yb_tablegroup

#define YbNumCatalogCacheTables (YbCatalogCacheTable_pg_yb_tablegroup + 1)
} YbCatalogCacheTable;

/* Used in IsYugaByteEnabled() mode only */
extern void YbSetSysCacheTuple(Relation rel, HeapTuple tup);
extern void YbPreloadCatalogCache(int cache_id, int idx_cache_id);
extern void YbInitPinnedCacheIfNeeded(bool shared_only);
extern void YbResetPinnedCache();
extern bool YbIsObjectPinned(Oid classId, Oid objectId, bool shared_dependency);
extern void YbPinObjectIfNeeded(Oid classId, Oid objectId, bool shared_dependency);
#ifndef NDEBUG
extern bool YbCheckCatalogCacheIndexNameTable();
#endif
extern const char* YbGetCatalogCacheIndexName(int cache_id);
extern const char *YbGetCatalogCacheTableNameFromTableId(int table_id);
extern const char *YbGetCatalogCacheTableNameFromCacheId(int cache_id);
extern int YbGetCatalogCacheTableIdFromCacheId(int cache_id);

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

/* convenience routines */
extern HeapTuple SearchSysCacheCopy(int cacheId,
				   Datum key1, Datum key2, Datum key3, Datum key4);
extern bool SearchSysCacheExists(int cacheId,
					 Datum key1, Datum key2, Datum key3, Datum key4);
extern Oid GetSysCacheOid(int cacheId,
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

#define GetSysCacheOid1(cacheId, key1) \
	GetSysCacheOid(cacheId, key1, 0, 0, 0)
#define GetSysCacheOid2(cacheId, key1, key2) \
	GetSysCacheOid(cacheId, key1, key2, 0, 0)
#define GetSysCacheOid3(cacheId, key1, key2, key3) \
	GetSysCacheOid(cacheId, key1, key2, key3, 0)
#define GetSysCacheOid4(cacheId, key1, key2, key3, key4) \
	GetSysCacheOid(cacheId, key1, key2, key3, key4)

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
