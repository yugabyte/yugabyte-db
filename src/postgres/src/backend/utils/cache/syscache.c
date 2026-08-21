/*-------------------------------------------------------------------------
 *
 * syscache.c
 *	  System cache management routines
 *
 * Portions Copyright (c) 1996-2026, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/cache/syscache.c
 *
 * NOTES
 *	  These routines allow the parser/planner/executor to perform
 *	  rapid lookups on the contents of the system catalogs.
 *
 *	  see utils/syscache.h for a list of the cache IDs
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/pg_db_role_setting_d.h"
#include "catalog/pg_depend_d.h"
#include "catalog/pg_description_d.h"
#include "catalog/pg_seclabel_d.h"
#include "catalog/pg_shdepend_d.h"
#include "catalog/pg_shdescription_d.h"
#include "catalog/pg_shseclabel_d.h"
#include "common/int.h"
#include "lib/qunique.h"
#include "miscadmin.h"
#include "storage/lmgr.h"
#include "storage/lock.h"
#include "utils/catcache.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"

/* YB includes */
#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/pg_amop.h"
#include "catalog/pg_attrdef.h"
#include "catalog/pg_constraint.h"
#include "catalog/pg_constraint_d.h"
#include "catalog/pg_inherits.h"
#include "catalog/pg_rewrite.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_tablegroup.h"
#include "fmgr.h"
#include "funcapi.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/rel.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "yb_internal_conn.h"
#include <assert.h>

/*---------------------------------------------------------------------------

	Adding system caches:

	There must be a unique index underlying each syscache (ie, an index
	whose key is the same as that of the cache).  If there is not one
	already, add the definition for it to include/catalog/pg_*.h using
	DECLARE_UNIQUE_INDEX.
	(Adding an index requires a catversion.h update, while simply
	adding/deleting caches only requires a recompile.)

	Add a MAKE_SYSCACHE call to the same pg_*.h file specifying the name of
	your cache, the underlying index, and the initial number of hash buckets.

	The number of hash buckets must be a power of 2.  It's reasonable to
	set this to the number of entries that might be in the particular cache
	in a medium-size database.

	Finally, any place your relation gets heap_insert() or
	heap_update() calls, use CatalogTupleInsert() or CatalogTupleUpdate()
	instead, which also update indexes.  The heap_* calls do not do that.

*---------------------------------------------------------------------------
*/

/*
 *		struct cachedesc: information defining a single syscache
 */
struct cachedesc
{
	Oid			reloid;			/* OID of the relation being cached */
	Oid			indoid;			/* OID of index relation for this cache */
	int			nkeys;			/* # of keys needed for cache lookup */
	int			key[4];			/* attribute numbers of key attrs */
	int			nbuckets;		/* number of hash buckets for this cache */
};

/* Macro to provide nkeys and key array with convenient syntax. */
#define KEY(...) VA_ARGS_NARGS(__VA_ARGS__), { __VA_ARGS__ }

#include "catalog/syscache_info.h"

StaticAssertDecl(lengthof(cacheinfo) == SysCacheSize,
				 "SysCacheSize does not match syscache.c's array");

#define YB_CATCACHE_LIST \
	YB_CATCACHE_ENTRY(AGGFNOID, 0, "pg_aggregate_fnoid_index", YbCatalogCacheTable_pg_aggregate) \
	YB_CATCACHE_ENTRY(AMNAME, 1, "pg_am_name_index", YbCatalogCacheTable_pg_am) \
	YB_CATCACHE_ENTRY(AMOID, 2, "pg_am_oid_index", YbCatalogCacheTable_pg_am) \
	YB_CATCACHE_ENTRY(AMOPOPID, 3, "pg_amop_opr_fam_index", YbCatalogCacheTable_pg_amop) \
	YB_CATCACHE_ENTRY(AMOPSTRATEGY, 4, "pg_amop_fam_strat_index", YbCatalogCacheTable_pg_amop) \
	YB_CATCACHE_ENTRY(AMPROCNUM, 5, "pg_amproc_fam_proc_index", YbCatalogCacheTable_pg_amproc) \
	YB_CATCACHE_ENTRY(ATTNAME, 6, "pg_attribute_relid_attnam_index", YbCatalogCacheTable_pg_attribute) \
	YB_CATCACHE_ENTRY(ATTNUM, 7, "pg_attribute_relid_attnum_index", YbCatalogCacheTable_pg_attribute) \
	YB_CATCACHE_ENTRY(AUTHMEMMEMROLE, 8, "pg_auth_members_member_role_index", YbCatalogCacheTable_pg_auth_members) \
	YB_CATCACHE_ENTRY(AUTHMEMROLEMEM, 9, "pg_auth_members_role_member_index", YbCatalogCacheTable_pg_auth_members) \
	YB_CATCACHE_ENTRY(AUTHNAME, 10, "pg_authid_rolname_index", YbCatalogCacheTable_pg_authid) \
	YB_CATCACHE_ENTRY(AUTHOID, 11, "pg_authid_oid_index", YbCatalogCacheTable_pg_authid) \
	YB_CATCACHE_ENTRY(CASTSOURCETARGET, 12, "pg_cast_source_target_index", YbCatalogCacheTable_pg_cast) \
	YB_CATCACHE_ENTRY(CLAAMNAMENSP, 13, "pg_opclass_am_name_nsp_index", YbCatalogCacheTable_pg_opclass) \
	YB_CATCACHE_ENTRY(CLAOID, 14, "pg_opclass_oid_index", YbCatalogCacheTable_pg_opclass) \
	YB_CATCACHE_ENTRY(COLLNAMEENCNSP, 15, "pg_collation_name_enc_nsp_index", YbCatalogCacheTable_pg_collation) \
	YB_CATCACHE_ENTRY(COLLOID, 16, "pg_collation_oid_index", YbCatalogCacheTable_pg_collation) \
	YB_CATCACHE_ENTRY(CONDEFAULT, 17, "pg_conversion_default_index", YbCatalogCacheTable_pg_conversion) \
	YB_CATCACHE_ENTRY(CONNAMENSP, 18, "pg_conversion_name_nsp_index", YbCatalogCacheTable_pg_conversion) \
	YB_CATCACHE_ENTRY(CONSTROID, 19, "pg_constraint_oid_index", YbCatalogCacheTable_pg_constraint) \
	YB_CATCACHE_ENTRY(CONVOID, 20, "pg_conversion_oid_index", YbCatalogCacheTable_pg_conversion) \
	YB_CATCACHE_ENTRY(DATABASEOID, 21, "pg_database_oid_index", YbCatalogCacheTable_pg_database) \
	YB_CATCACHE_ENTRY(DEFACLROLENSPOBJ, 22, "pg_default_acl_role_nsp_obj_index", YbCatalogCacheTable_pg_default_acl) \
	YB_CATCACHE_ENTRY(ENUMOID, 23, "pg_enum_oid_index", YbCatalogCacheTable_pg_enum) \
	YB_CATCACHE_ENTRY(ENUMTYPOIDNAME, 24, "pg_enum_typid_label_index", YbCatalogCacheTable_pg_enum) \
	YB_CATCACHE_ENTRY(EVENTTRIGGERNAME, 25, "pg_event_trigger_evtname_index", YbCatalogCacheTable_pg_event_trigger) \
	YB_CATCACHE_ENTRY(EVENTTRIGGEROID, 26, "pg_event_trigger_oid_index", YbCatalogCacheTable_pg_event_trigger) \
	YB_CATCACHE_ENTRY(EXTENSIONNAME, 27, "pg_extension_name_index", YbCatalogCacheTable_pg_extension) \
	YB_CATCACHE_ENTRY(EXTENSIONOID, 28, "pg_extension_oid_index", YbCatalogCacheTable_pg_extension) \
	YB_CATCACHE_ENTRY(FOREIGNDATAWRAPPERNAME, 29, "pg_foreign_data_wrapper_name_index", YbCatalogCacheTable_pg_foreign_data_wrapper) \
	YB_CATCACHE_ENTRY(FOREIGNDATAWRAPPEROID, 30, "pg_foreign_data_wrapper_oid_index", YbCatalogCacheTable_pg_foreign_data_wrapper) \
	YB_CATCACHE_ENTRY(FOREIGNSERVERNAME, 31, "pg_foreign_server_name_index", YbCatalogCacheTable_pg_foreign_server) \
	YB_CATCACHE_ENTRY(FOREIGNSERVEROID, 32, "pg_foreign_server_oid_index", YbCatalogCacheTable_pg_foreign_server) \
	YB_CATCACHE_ENTRY(FOREIGNTABLEREL, 33, "pg_foreign_table_relid_index", YbCatalogCacheTable_pg_foreign_table) \
	YB_CATCACHE_ENTRY(INDEXRELID, 34, "pg_index_indexrelid_index", YbCatalogCacheTable_pg_index) \
	YB_CATCACHE_ENTRY(LANGNAME, 35, "pg_language_name_index", YbCatalogCacheTable_pg_language) \
	YB_CATCACHE_ENTRY(LANGOID, 36, "pg_language_oid_index", YbCatalogCacheTable_pg_language) \
	YB_CATCACHE_ENTRY(NAMESPACENAME, 37, "pg_namespace_nspname_index", YbCatalogCacheTable_pg_namespace) \
	YB_CATCACHE_ENTRY(NAMESPACEOID, 38, "pg_namespace_oid_index", YbCatalogCacheTable_pg_namespace) \
	YB_CATCACHE_ENTRY(OPERNAMENSP, 39, "pg_operator_oprname_l_r_n_index", YbCatalogCacheTable_pg_operator) \
	YB_CATCACHE_ENTRY(OPEROID, 40, "pg_operator_oid_index", YbCatalogCacheTable_pg_operator) \
	YB_CATCACHE_ENTRY(OPFAMILYAMNAMENSP, 41, "pg_opfamily_am_name_nsp_index", YbCatalogCacheTable_pg_opfamily) \
	YB_CATCACHE_ENTRY(OPFAMILYOID, 42, "pg_opfamily_oid_index", YbCatalogCacheTable_pg_opfamily) \
	YB_CATCACHE_ENTRY(PARAMETERACLNAME, 43, "pg_parameter_acl_parname_index", YbCatalogCacheTable_pg_parameter_acl) \
	YB_CATCACHE_ENTRY(PARAMETERACLOID, 44, "pg_parameter_acl_oid_index", YbCatalogCacheTable_pg_parameter_acl) \
	YB_CATCACHE_ENTRY(PARTRELID, 45, "pg_partitioned_table_partrelid_index", YbCatalogCacheTable_pg_partitioned_table) \
	YB_CATCACHE_ENTRY(PROCNAMEARGSNSP, 46, "pg_proc_proname_args_nsp_index", YbCatalogCacheTable_pg_proc) \
	YB_CATCACHE_ENTRY(PROCOID, 47, "pg_proc_oid_index", YbCatalogCacheTable_pg_proc) \
	YB_CATCACHE_ENTRY(PROPGRAPHELALIAS, 48, "pg_propgraph_element_alias_index", YbCatalogCacheTable_pg_propgraph_element) \
	YB_CATCACHE_ENTRY(PROPGRAPHELEMENTLABELELEMENTLABEL, 49, "pg_propgraph_element_label_element_label_index", YbCatalogCacheTable_pg_propgraph_element_label) \
	YB_CATCACHE_ENTRY(PROPGRAPHELOID, 50, "pg_propgraph_element_oid_index", YbCatalogCacheTable_pg_propgraph_element) \
	YB_CATCACHE_ENTRY(PROPGRAPHLABELNAME, 51, "pg_propgraph_label_graph_name_index", YbCatalogCacheTable_pg_propgraph_label) \
	YB_CATCACHE_ENTRY(PROPGRAPHLABELOID, 52, "pg_propgraph_label_oid_index", YbCatalogCacheTable_pg_propgraph_label) \
	YB_CATCACHE_ENTRY(PROPGRAPHLABELPROP, 53, "pg_propgraph_label_property_label_prop_index", YbCatalogCacheTable_pg_propgraph_label_property) \
	YB_CATCACHE_ENTRY(PROPGRAPHPROPNAME, 54, "pg_propgraph_property_name_index", YbCatalogCacheTable_pg_propgraph_property) \
	YB_CATCACHE_ENTRY(PROPGRAPHPROPOID, 55, "pg_propgraph_property_oid_index", YbCatalogCacheTable_pg_propgraph_property) \
	YB_CATCACHE_ENTRY(PUBLICATIONNAME, 56, "pg_publication_pubname_index", YbCatalogCacheTable_pg_publication) \
	YB_CATCACHE_ENTRY(PUBLICATIONNAMESPACE, 57, "pg_publication_namespace_oid_index", YbCatalogCacheTable_pg_publication_namespace) \
	YB_CATCACHE_ENTRY(PUBLICATIONNAMESPACEMAP, 58, "pg_publication_namespace_pnnspid_pnpubid_index", YbCatalogCacheTable_pg_publication_namespace) \
	YB_CATCACHE_ENTRY(PUBLICATIONOID, 59, "pg_publication_oid_index", YbCatalogCacheTable_pg_publication) \
	YB_CATCACHE_ENTRY(PUBLICATIONREL, 60, "pg_publication_rel_oid_index", YbCatalogCacheTable_pg_publication_rel) \
	YB_CATCACHE_ENTRY(PUBLICATIONRELMAP, 61, "pg_publication_rel_prrelid_prpubid_index", YbCatalogCacheTable_pg_publication_rel) \
	YB_CATCACHE_ENTRY(RANGEMULTIRANGE, 62, "pg_range_rngmultitypid_index", YbCatalogCacheTable_pg_range) \
	YB_CATCACHE_ENTRY(RANGETYPE, 63, "pg_range_rngtypid_index", YbCatalogCacheTable_pg_range) \
	YB_CATCACHE_ENTRY(RELNAMENSP, 64, "pg_class_relname_nsp_index", YbCatalogCacheTable_pg_class) \
	YB_CATCACHE_ENTRY(RELOID, 65, "pg_class_oid_index", YbCatalogCacheTable_pg_class) \
	YB_CATCACHE_ENTRY(REPLORIGIDENT, 66, "pg_replication_origin_roiident_index", YbCatalogCacheTable_pg_replication_origin) \
	YB_CATCACHE_ENTRY(REPLORIGNAME, 67, "pg_replication_origin_roname_index", YbCatalogCacheTable_pg_replication_origin) \
	YB_CATCACHE_ENTRY(RULERELNAME, 68, "pg_rewrite_rel_rulename_index", YbCatalogCacheTable_pg_rewrite) \
	YB_CATCACHE_ENTRY(SEQRELID, 69, "pg_sequence_seqrelid_index", YbCatalogCacheTable_pg_sequence) \
	YB_CATCACHE_ENTRY(STATEXTDATASTXOID, 70, "pg_statistic_ext_data_stxoid_inh_index", YbCatalogCacheTable_pg_statistic_ext_data) \
	YB_CATCACHE_ENTRY(STATEXTNAMENSP, 71, "pg_statistic_ext_name_index", YbCatalogCacheTable_pg_statistic_ext) \
	YB_CATCACHE_ENTRY(STATEXTOID, 72, "pg_statistic_ext_oid_index", YbCatalogCacheTable_pg_statistic_ext) \
	YB_CATCACHE_ENTRY(STATRELATTINH, 73, "pg_statistic_relid_att_inh_index", YbCatalogCacheTable_pg_statistic) \
	YB_CATCACHE_ENTRY(SUBSCRIPTIONNAME, 74, "pg_subscription_subname_index", YbCatalogCacheTable_pg_subscription) \
	YB_CATCACHE_ENTRY(SUBSCRIPTIONOID, 75, "pg_subscription_oid_index", YbCatalogCacheTable_pg_subscription) \
	YB_CATCACHE_ENTRY(SUBSCRIPTIONRELMAP, 76, "pg_subscription_rel_srrelid_srsubid_index", YbCatalogCacheTable_pg_subscription_rel) \
	YB_CATCACHE_ENTRY(TABLESPACEOID, 77, "pg_tablespace_oid_index", YbCatalogCacheTable_pg_tablespace) \
	YB_CATCACHE_ENTRY(TRFOID, 78, "pg_transform_oid_index", YbCatalogCacheTable_pg_transform) \
	YB_CATCACHE_ENTRY(TRFTYPELANG, 79, "pg_transform_type_lang_index", YbCatalogCacheTable_pg_transform) \
	YB_CATCACHE_ENTRY(TSCONFIGMAP, 80, "pg_ts_config_map_index", YbCatalogCacheTable_pg_ts_config_map) \
	YB_CATCACHE_ENTRY(TSCONFIGNAMENSP, 81, "pg_ts_config_cfgname_index", YbCatalogCacheTable_pg_ts_config) \
	YB_CATCACHE_ENTRY(TSCONFIGOID, 82, "pg_ts_config_oid_index", YbCatalogCacheTable_pg_ts_config) \
	YB_CATCACHE_ENTRY(TSDICTNAMENSP, 83, "pg_ts_dict_dictname_index", YbCatalogCacheTable_pg_ts_dict) \
	YB_CATCACHE_ENTRY(TSDICTOID, 84, "pg_ts_dict_oid_index", YbCatalogCacheTable_pg_ts_dict) \
	YB_CATCACHE_ENTRY(TSPARSERNAMENSP, 85, "pg_ts_parser_prsname_index", YbCatalogCacheTable_pg_ts_parser) \
	YB_CATCACHE_ENTRY(TSPARSEROID, 86, "pg_ts_parser_oid_index", YbCatalogCacheTable_pg_ts_parser) \
	YB_CATCACHE_ENTRY(TSTEMPLATENAMENSP, 87, "pg_ts_template_tmplname_index", YbCatalogCacheTable_pg_ts_template) \
	YB_CATCACHE_ENTRY(TSTEMPLATEOID, 88, "pg_ts_template_oid_index", YbCatalogCacheTable_pg_ts_template) \
	YB_CATCACHE_ENTRY(TYPENAMENSP, 89, "pg_type_typname_nsp_index", YbCatalogCacheTable_pg_type) \
	YB_CATCACHE_ENTRY(TYPEOID, 90, "pg_type_oid_index", YbCatalogCacheTable_pg_type) \
	YB_CATCACHE_ENTRY(USERMAPPINGOID, 91, "pg_user_mapping_oid_index", YbCatalogCacheTable_pg_user_mapping) \
	YB_CATCACHE_ENTRY(USERMAPPINGUSERSERVER, 92, "pg_user_mapping_user_server_index", YbCatalogCacheTable_pg_user_mapping) \
	YB_CATCACHE_ENTRY(YBCONSTRAINTRELIDTYPIDNAME, 93, "pg_constraint_conrelid_contypid_conname_index", YbCatalogCacheTable_pg_constraint) \
	YB_CATCACHE_ENTRY(YBTABLEGROUPOID, 94, "pg_yb_tablegroup_oid_index", YbCatalogCacheTable_pg_yb_tablegroup)

static const char *yb_cache_index_name_table[] = {
#define YB_CATCACHE_ENTRY(name, id, idx, tbl) idx,
	YB_CATCACHE_LIST
#undef YB_CATCACHE_ENTRY
};

static_assert(SysCacheSize == sizeof(yb_cache_index_name_table) /
			  sizeof(const char *), "Wrong catalog cache number");

char	   *SysCacheName[] = {
#define YB_CATCACHE_ENTRY(name, id, idx, tbl) #name,
	YB_CATCACHE_LIST
#undef YB_CATCACHE_ENTRY
};

static_assert(SysCacheSize == sizeof(SysCacheName) /
			  sizeof(SysCacheName[0]), "SysCacheName array size mismatch");


/* List of all the tables that have caches on them */
static const char *yb_cache_table_name_table[] = {
	"pg_aggregate",
	"pg_am",
	"pg_amop",
	"pg_amproc",
	"pg_attribute",
	"pg_auth_members",
	"pg_authid",
	"pg_cast",
	"pg_class",
	"pg_collation",
	"pg_constraint",
	"pg_conversion",
	"pg_database",
	"pg_default_acl",
	"pg_enum",
	"pg_event_trigger",
	"pg_extension",
	"pg_foreign_data_wrapper",
	"pg_foreign_server",
	"pg_foreign_table",
	"pg_index",
	"pg_language",
	"pg_namespace",
	"pg_opclass",
	"pg_operator",
	"pg_opfamily",
	"pg_parameter_acl",
	"pg_partitioned_table",
	"pg_proc",
	"pg_propgraph_element",
	"pg_propgraph_element_label",
	"pg_propgraph_label",
	"pg_propgraph_label_property",
	"pg_propgraph_property",
	"pg_publication",
	"pg_publication_namespace",
	"pg_publication_rel",
	"pg_range",
	"pg_replication_origin",
	"pg_rewrite",
	"pg_sequence",
	"pg_statistic",
	"pg_statistic_ext",
	"pg_statistic_ext_data",
	"pg_subscription",
	"pg_subscription_rel",
	"pg_tablespace",
	"pg_transform",
	"pg_ts_config",
	"pg_ts_config_map",
	"pg_ts_dict",
	"pg_ts_parser",
	"pg_ts_template",
	"pg_type",
	"pg_user_mapping",
	"pg_yb_tablegroup",
	"pg_inherits"
};

static_assert(YbNumCatalogCacheTables ==
			  sizeof(yb_cache_table_name_table) / sizeof(const char *),
			  "yb_catalog_cache_table_name_table size mismatch");


/* Maps cache id to the table id in yb_cache_table_name_table */
static YbCatalogCacheTable yb_catalog_cache_tables[] = {
#define YB_CATCACHE_ENTRY(name, id, idx, tbl) tbl,
	YB_CATCACHE_LIST
#undef YB_CATCACHE_ENTRY
};

static_assert(SysCacheSize ==
			  sizeof(yb_catalog_cache_tables) / sizeof(YbCatalogCacheTable),
			  "yb_catalog_cache_tables size mismatch");

static CatCache *SysCache[SysCacheSize];

static bool CacheInitialized = false;

/* Sorted array of OIDs of tables that have caches on them */
static Oid	SysCacheRelationOid[SysCacheSize];
static int	SysCacheRelationOidSize;

/* Sorted array of OIDs of tables and indexes used by caches */
static Oid	SysCacheSupportingRelOid[SysCacheSize * 2];
static int	SysCacheSupportingRelOidSize;

static int	oid_compare(const void *a, const void *b);

/*
 * Utility function for YugaByte mode. Is used to automatically add entries
 * from common catalog tables to the cache immediately after they are inserted.
 */
void
YbSetSysCacheTuple(Relation rel, HeapTuple tup)
{
	TupleDesc	tupdesc = RelationGetDescr(rel);

	switch (RelationGetRelid(rel))
	{
		case RelationRelationId:
			SetCatCacheTuple(SysCache[RELOID], tup, tupdesc);
			SetCatCacheTuple(SysCache[RELNAMENSP], tup, tupdesc);
			break;
		case TypeRelationId:
			SetCatCacheTuple(SysCache[TYPEOID], tup, tupdesc);
			SetCatCacheTuple(SysCache[TYPENAMENSP], tup, tupdesc);
			break;
		case ProcedureRelationId:
			SetCatCacheTuple(SysCache[PROCOID], tup, tupdesc);
			SetCatCacheTuple(SysCache[PROCNAMEARGSNSP], tup, tupdesc);
			break;
		case AttributeRelationId:
			SetCatCacheTuple(SysCache[ATTNUM], tup, tupdesc);
			SetCatCacheTuple(SysCache[ATTNAME], tup, tupdesc);
			break;
		case PartitionedRelationId:
			SetCatCacheTuple(SysCache[PARTRELID], tup, tupdesc);
			break;

		default:
			/* For non-critical tables/indexes nothing to do */
			return;
	}
}

/*
 * Should YbPreloadCatalogCache populate the full set of catcache LIST entries?
 *
 * In minimal-preload mode the caller preloads only the pg_rewrite (RULERELNAME)
 * list -- the one whose on-demand rebuild during relcache init is expensive
 * (see YbPreloadCatalogCache). This function decides whether to additionally
 * preload the rest.
 *
 * - Outside minimal-preload mode: yes, always.
 * - In minimal-preload mode: only if the current backend's YbInternalConnKind
 *   descriptor opts in via preload_lists_in_minimal_mode. The relcache-init
 *   builder is the one kind that opts in -- it is transient and needs its
 *   lists while building the relcache init file. Other minimal-preload kinds
 *   leave the remaining lists (notably pg_proc's by-name list, which must be
 *   complete for correctness) to be built on demand from a full
 *   SearchCatCacheList scan.
 */
static bool
YbShouldPreloadCatcacheLists(void)
{
	YbInternalConnKind kind;

	if (!YbUseMinimalCatalogCachesPreload())
		return true;

	kind = YbLookupInternalConnKindByBackendType(MyBackendType);
	return kind != YB_INTERNAL_CONN_KIND_NONE &&
		YbInternalConnKindDescriptors[kind].preload_lists_in_minimal_mode;
}

/*
 * In YugaByte mode preload the given cache with data from master.
 * If no index cache is associated with the given cache (most of the time), its id should be -1.
 */
void
YbPreloadCatalogCache(int cache_id, int idx_cache_id)
{

	CatCache   *cache = SysCache[cache_id];
	CatCache   *idx_cache = idx_cache_id != -1 ? SysCache[idx_cache_id] : NULL;
	List	   *dest_list = NIL;
	List	   *list_of_lists = NIL;
	HeapTuple	ntp;
	Relation	relation = table_open(cache->cc_reloid, AccessShareLock);
	TupleDesc	tupdesc = RelationGetDescr(relation);

	SysScanDesc scandesc = systable_beginscan(relation,
											  cache->cc_indexoid,
											  false /* indexOK */ ,
											  NULL /* snapshot */ ,
											  0 /* nkeys */ ,
											  NULL /* key */ );

	size_t		scanned = 0;
	instr_time	start;

	if (yb_debug_log_catcache_events)
		INSTR_TIME_SET_CURRENT(start);

	while (HeapTupleIsValid(ntp = systable_getnext(scandesc)))
	{
		scanned++;
		SetCatCacheTuple(cache, ntp, RelationGetDescr(relation));

		if (idx_cache)
			SetCatCacheTuple(idx_cache, ntp, RelationGetDescr(relation));

		/*
		 * In minimal-preload mode preload only the pg_rewrite (RULERELNAME)
		 * list, which is safe to preload because we throw it away when we
		 * are done preloading the corresponding relcache entry. The other
		 * catcache lists are unsafe to preload in minimal mode because they
		 * may be incomplete.
		 */
		if (cache_id != RULERELNAME && !YbShouldPreloadCatcacheLists())
			continue;

		bool		is_add_to_list_required = true;

		switch (cache_id)
		{
			case PROCOID:
				{
					/*
					 * Special handling for the common case of looking up
					 * functions (procedures) by name (i.e. partial key).
					 * We set up the partial cache list for function by-name
					 * lookup on initialization to avoid scanning the large
					 * pg_proc table each time.
					 */
					bool		is_null = false;
					ScanKeyData key = idx_cache->cc_skey[0];
					Datum		ndt = heap_getattr(ntp, key.sk_attno, tupdesc, &is_null);

					if (is_null)
					{
						YBC_LOG_WARNING("Ignoring unexpected null "
										"entry while initializing proc cache list");
						is_add_to_list_required = false;
						break;
					}

					dest_list = NIL;
					/* Look for an existing list for functions with this name. */
					ListCell   *lc;

					foreach(lc, list_of_lists)
					{
						List	   *fnlist = lfirst(lc);
						HeapTuple	otp = linitial(fnlist);
						Datum		odt = heap_getattr(otp, key.sk_attno, tupdesc, &is_null);
						Datum		key_matches = FunctionCall2Coll(&key.sk_func,
																	key.sk_collation,
																	ndt, odt);

						if (DatumGetBool(key_matches))
						{
							dest_list = fnlist;
							break;
						}
					}
					break;
				}
			case RULERELNAME:
				{
					/*
					 * Special handling for pg_rewrite: preload rules list by
					 * relation oid. Note that rules should be ordered by name -
					 * which is achieved using RewriteRelRulenameIndexId index.
					 */
					if (dest_list)
					{
						HeapTuple	ltp = llast(dest_list);
						Form_pg_rewrite ltp_struct = (Form_pg_rewrite) GETSTRUCT(ltp);
						Form_pg_rewrite ntp_struct = (Form_pg_rewrite) GETSTRUCT(ntp);

						if (ntp_struct->ev_class != ltp_struct->ev_class)
							dest_list = NIL;
					}
					break;
				}
			case AMOPOPID:
				{
					/*
					 * Add a cache list for AMOPOPID for lookup by operator
					 * only.
					 */
					if (dest_list)
					{
						HeapTuple	ltp = llast(dest_list);
						Form_pg_amop ltp_struct = (Form_pg_amop) GETSTRUCT(ltp);
						Form_pg_amop ntp_struct = (Form_pg_amop) GETSTRUCT(ntp);

						if (ntp_struct->amopopr != ltp_struct->amopopr)
							dest_list = NIL;
					}
					break;
				}
			case CONSTROID:
				{
					/*
					 * Add a cache list for YBCONSTRAINTRELIDTYPIDNAME for lookup by conrelid only.
					 */
					if (!yb_enable_fkey_catcache)
					{
						is_add_to_list_required = false;
						break;
					}
					if (dest_list)
					{
						HeapTuple	ltp = llast(dest_list);
						Form_pg_constraint ltp_struct = (Form_pg_constraint) GETSTRUCT(ltp);
						Form_pg_constraint ntp_struct = (Form_pg_constraint) GETSTRUCT(ntp);

						if (ntp_struct->conrelid != ltp_struct->conrelid)
							dest_list = NIL;
					}
					break;
				}
			default:
				is_add_to_list_required = false;
				break;
		}

		if (is_add_to_list_required)
		{
			if (dest_list)
			{
				List	   *old_dest_list = dest_list;

				(void) old_dest_list;
				dest_list = lappend(dest_list, ntp);
				Assert(dest_list == old_dest_list);
			}
			else
			{
				dest_list = list_make1(ntp);
				list_of_lists = lappend(list_of_lists, dest_list);
			}
		}
	}

	systable_endscan(scandesc);

	table_close(relation, AccessShareLock);

	if (list_of_lists)
	{
		/* Load up the lists computed above into the catalog cache. */
		CatCache   *dest_cache = cache;

		switch (cache_id)
		{
			case PROCOID:
			case CONSTROID:
				Assert(idx_cache);
				dest_cache = idx_cache;
				break;
			case RULERELNAME:
			case AMOPOPID:
				break;
			default:
				Assert(false);
				break;
		}
		ListCell   *lc;

		foreach(lc, list_of_lists)
			SetCatCacheList(dest_cache, 1, lfirst(lc));
		list_free_deep(list_of_lists);
	}

	if (yb_debug_log_catcache_events)
	{
		instr_time	duration;

		INSTR_TIME_SET_CURRENT(duration);
		INSTR_TIME_SUBTRACT(duration, start);
		elog(LOG, "YbPreloadCatalogCache: %zu entries added for "
			 "cache id %d, index oid %d (relation %s), took " INT64_FORMAT " us",
			 scanned, cache->id, cache->cc_indexoid, cache->cc_relname,
			 INSTR_TIME_GET_MICROSEC(duration));
	}

	/*
	 * Done: mark cache(s) as loaded. We can only safely set yb_cc_is_fully_loaded
	 * if we did full preloading; minimal preloading doesn't load user objects.
	 */
	if (!YBCIsInitDbModeEnvVarSet() &&
		YbNeedAdditionalCatalogTables() &&
		!YbUseMinimalCatalogCachesPreload())
	{
		cache->yb_cc_is_fully_loaded = true;
		if (idx_cache)
			idx_cache->yb_cc_is_fully_loaded = true;
	}
}

/*
 * InitCatalogCache - initialize the caches
 *
 * Note that no database access is done here; we only allocate memory
 * and initialize the cache structure.  Interrogation of the database
 * to complete initialization of a cache happens upon first use
 * of that cache.
 */
void
InitCatalogCache(void)
{
	SysCacheIdentifier cacheId;

	Assert(!CacheInitialized);

	SysCacheRelationOidSize = SysCacheSupportingRelOidSize = 0;

	for (cacheId = 0; cacheId < SysCacheSize; cacheId++)
	{
		/*
		 * Assert that every enumeration value defined in syscache.h has been
		 * populated in the cacheinfo array.
		 */
		Assert(OidIsValid(cacheinfo[cacheId].reloid));
		Assert(OidIsValid(cacheinfo[cacheId].indoid));
		/* .nbuckets and .key[] are checked by InitCatCache() */

		SysCache[cacheId] = InitCatCache(cacheId,
										 cacheinfo[cacheId].reloid,
										 cacheinfo[cacheId].indoid,
										 cacheinfo[cacheId].nkeys,
										 cacheinfo[cacheId].key,
										 cacheinfo[cacheId].nbuckets);
		if (!SysCache[cacheId])
			elog(ERROR, "could not initialize cache %u (%d)",
				 cacheinfo[cacheId].reloid, cacheId);
		/* Accumulate data for OID lists, too */
		SysCacheRelationOid[SysCacheRelationOidSize++] =
			cacheinfo[cacheId].reloid;
		SysCacheSupportingRelOid[SysCacheSupportingRelOidSize++] =
			cacheinfo[cacheId].reloid;
		SysCacheSupportingRelOid[SysCacheSupportingRelOidSize++] =
			cacheinfo[cacheId].indoid;
		/* see comments for RelationInvalidatesSnapshotsOnly */
		Assert(!RelationInvalidatesSnapshotsOnly(cacheinfo[cacheId].reloid));
	}

	Assert(SysCacheRelationOidSize <= lengthof(SysCacheRelationOid));
	Assert(SysCacheSupportingRelOidSize <= lengthof(SysCacheSupportingRelOid));

	/* Sort and de-dup OID arrays, so we can use binary search. */
	qsort(SysCacheRelationOid, SysCacheRelationOidSize,
		  sizeof(Oid), oid_compare);
	SysCacheRelationOidSize =
		qunique(SysCacheRelationOid, SysCacheRelationOidSize, sizeof(Oid),
				oid_compare);

	qsort(SysCacheSupportingRelOid, SysCacheSupportingRelOidSize,
		  sizeof(Oid), oid_compare);
	SysCacheSupportingRelOidSize =
		qunique(SysCacheSupportingRelOid, SysCacheSupportingRelOidSize,
				sizeof(Oid), oid_compare);

	CacheInitialized = true;
}

/*
 * InitCatalogCachePhase2 - finish initializing the caches
 *
 * Finish initializing all the caches, including necessary database
 * access.
 *
 * This is *not* essential; normally we allow syscaches to be initialized
 * on first use.  However, it is useful as a mechanism to preload the
 * relcache with entries for the most-commonly-used system catalogs.
 * Therefore, we invoke this routine when we need to write a new relcache
 * init file.
 */
void
InitCatalogCachePhase2(void)
{
	SysCacheIdentifier cacheId;

	Assert(CacheInitialized);

	for (cacheId = 0; cacheId < SysCacheSize; cacheId++)
		InitCatCachePhase2(SysCache[cacheId], true);
}


/*
 * SearchSysCache
 *
 *	A layer on top of SearchCatCache that does the initialization and
 *	key-setting for you.
 *
 *	Returns the cache copy of the tuple if one is found, NULL if not.
 *	The tuple is the 'cache' copy and must NOT be modified!
 *
 *	When the caller is done using the tuple, call ReleaseSysCache()
 *	to release the reference count grabbed by SearchSysCache().  If this
 *	is not done, the tuple will remain locked in cache until end of
 *	transaction, which is tolerable but not desirable.
 *
 *	CAUTION: The tuple that is returned must NOT be freed by the caller!
 */
HeapTuple
SearchSysCache(SysCacheIdentifier cacheId,
			   Datum key1,
			   Datum key2,
			   Datum key3,
			   Datum key4)
{
	if (IsMultiThreadedMode())
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("catalog cache lookup is not allowed in multithread mode"),
				 errhint("Try to set yb_enable_expression_pushdown to false.")));
	Assert(cacheId >= 0 && cacheId < SysCacheSize && SysCache[cacheId]);

	return SearchCatCache(SysCache[cacheId], key1, key2, key3, key4);
}

HeapTuple
SearchSysCache1(SysCacheIdentifier cacheId,
				Datum key1)
{
	if (IsMultiThreadedMode())
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("catalog cache lookup is not allowed in multithread mode"),
				 errhint("Try to set yb_enable_expression_pushdown to false.")));
	Assert(cacheId >= 0 && cacheId < SysCacheSize && SysCache[cacheId]);
	Assert(SysCache[cacheId]->cc_nkeys == 1);

	return SearchCatCache1(SysCache[cacheId], key1);
}

HeapTuple
SearchSysCache2(SysCacheIdentifier cacheId,
				Datum key1, Datum key2)
{
	if (IsMultiThreadedMode())
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("catalog cache lookup is not allowed in multithread mode"),
				 errhint("Try to set yb_enable_expression_pushdown to false.")));
	Assert(cacheId >= 0 && cacheId < SysCacheSize && SysCache[cacheId]);
	Assert(SysCache[cacheId]->cc_nkeys == 2);

	return SearchCatCache2(SysCache[cacheId], key1, key2);
}

HeapTuple
SearchSysCache3(SysCacheIdentifier cacheId,
				Datum key1, Datum key2, Datum key3)
{
	if (IsMultiThreadedMode())
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("catalog cache lookup is not allowed in multithread mode"),
				 errhint("Try to set yb_enable_expression_pushdown to false.")));
	Assert(cacheId >= 0 && cacheId < SysCacheSize && SysCache[cacheId]);
	Assert(SysCache[cacheId]->cc_nkeys == 3);

	return SearchCatCache3(SysCache[cacheId], key1, key2, key3);
}

HeapTuple
SearchSysCache4(SysCacheIdentifier cacheId,
				Datum key1, Datum key2, Datum key3, Datum key4)
{
	if (IsMultiThreadedMode())
		ereport(ERROR,
				(errcode(ERRCODE_INTERNAL_ERROR),
				 errmsg("catalog cache lookup is not allowed in multithread mode"),
				 errhint("Try to set yb_enable_expression_pushdown to false.")));
	Assert(cacheId >= 0 && cacheId < SysCacheSize && SysCache[cacheId]);
	Assert(SysCache[cacheId]->cc_nkeys == 4);

	return SearchCatCache4(SysCache[cacheId], key1, key2, key3, key4);
}

/*
 * ReleaseSysCache
 *		Release previously grabbed reference count on a tuple
 */
void
ReleaseSysCache(HeapTuple tuple)
{
	ReleaseCatCache(tuple);
}

/*
 * SearchSysCacheLocked1
 *
 * Combine SearchSysCache1() with acquiring a LOCKTAG_TUPLE at mode
 * InplaceUpdateTupleLock.  This is a tool for complying with the
 * README.tuplock section "Locking to write inplace-updated tables".  After
 * the caller's heap_update(), it should UnlockTuple(InplaceUpdateTupleLock)
 * and ReleaseSysCache().
 *
 * The returned tuple may be the subject of an uncommitted update, so this
 * doesn't prevent the "tuple concurrently updated" error.
 */
HeapTuple
SearchSysCacheLocked1(SysCacheIdentifier cacheId,
					  Datum key1)
{
	CatCache   *cache = SysCache[cacheId];
	ItemPointerData tid;
	LOCKTAG		tag;

	if (YBGetObjectLockMode() != PG_OBJECT_LOCK_MODE)
	{
		HeapTuple tuple = SearchSysCache1(cacheId, key1);
		if (*YBCGetGFlags()->TEST_enable_obj_tuple_locks)
		{
			SET_LOCKTAG_TUPLE(tag,
							cache->cc_relisshared ? InvalidOid : MyDatabaseId,
							cache->cc_reloid,
							0,
							0);
			(void) LockAcquire(&tag, InplaceUpdateTupleLock, false, false);
			ReleaseSysCache(tuple);
			AcceptInvalidationMessages();
			tuple = SearchSysCache1(cacheId, key1);
		}
		return tuple;
	}

	/*----------
	 * Since inplace updates may happen just before our LockTuple(), we must
	 * return content acquired after LockTuple() of the TID we return.  If we
	 * just fetched twice instead of looping, the following sequence would
	 * defeat our locking:
	 *
	 * GRANT:   SearchSysCache1() = TID (1,5)
	 * GRANT:   LockTuple(pg_class, (1,5))
	 * [no more inplace update of (1,5) until we release the lock]
	 * CLUSTER: SearchSysCache1() = TID (1,5)
	 * CLUSTER: heap_update() = TID (1,8)
	 * CLUSTER: COMMIT
	 * GRANT:   SearchSysCache1() = TID (1,8)
	 * GRANT:   return (1,8) from SearchSysCacheLocked1()
	 * VACUUM:  SearchSysCache1() = TID (1,8)
	 * VACUUM:  LockTuple(pg_class, (1,8))  # two TIDs now locked for one rel
	 * VACUUM:  inplace update
	 * GRANT:   heap_update() = (1,9)  # lose inplace update
	 *
	 * In the happy case, this takes two fetches, one to determine the TID to
	 * lock and another to get the content and confirm the TID didn't change.
	 *
	 * This is valid even if the row gets updated to a new TID, the old TID
	 * becomes LP_UNUSED, and the row gets updated back to its old TID.  We'd
	 * still hold the right LOCKTAG_TUPLE and a copy of the row captured after
	 * the LOCKTAG_TUPLE.
	 */
	ItemPointerSetInvalid(&tid);
	for (;;)
	{
		HeapTuple	tuple;
		LOCKMODE	lockmode = InplaceUpdateTupleLock;

		tuple = SearchSysCache1(cacheId, key1);
		if (ItemPointerIsValid(&tid))
		{
			if (!HeapTupleIsValid(tuple))
			{
				LockRelease(&tag, lockmode, false);
				return tuple;
			}
			if (ItemPointerEquals(&tid, &tuple->t_self))
				return tuple;
			LockRelease(&tag, lockmode, false);
		}
		else if (!HeapTupleIsValid(tuple))
			return tuple;

		tid = tuple->t_self;
		ReleaseSysCache(tuple);

		/*
		 * Do like LockTuple(rel, &tid, lockmode).  While cc_relisshared won't
		 * change from one iteration to another, it may have been a temporary
		 * "false" until our first SearchSysCache1().
		 */
		SET_LOCKTAG_TUPLE(tag,
						  cache->cc_relisshared ? InvalidOid : MyDatabaseId,
						  cache->cc_reloid,
						  ItemPointerGetBlockNumber(&tid),
						  ItemPointerGetOffsetNumber(&tid));
		(void) LockAcquire(&tag, lockmode, false, false);

		/*
		 * If an inplace update just finished, ensure we process the syscache
		 * inval.
		 *
		 * If a heap_update() call just released its LOCKTAG_TUPLE, we'll
		 * probably find the old tuple and reach "tuple concurrently updated".
		 * If that heap_update() aborts, our LOCKTAG_TUPLE blocks inplace
		 * updates while our caller works.
		 */
		AcceptInvalidationMessages();
	}
}

/*
 * SearchSysCacheCopy
 *
 * A convenience routine that does SearchSysCache and (if successful)
 * returns a modifiable copy of the syscache entry.  The original
 * syscache entry is released before returning.  The caller should
 * heap_freetuple() the result when done with it.
 */
HeapTuple
SearchSysCacheCopy(SysCacheIdentifier cacheId,
				   Datum key1,
				   Datum key2,
				   Datum key3,
				   Datum key4)
{
	HeapTuple	tuple,
				newtuple;

	tuple = SearchSysCache(cacheId, key1, key2, key3, key4);
	if (!HeapTupleIsValid(tuple))
		return tuple;
	newtuple = heap_copytuple(tuple);
	ReleaseSysCache(tuple);
	return newtuple;
}

/*
 * SearchSysCacheLockedCopy1
 *
 * Meld SearchSysCacheLocked1 with SearchSysCacheCopy().  After the
 * caller's heap_update(), it should UnlockTuple(InplaceUpdateTupleLock) and
 * heap_freetuple().
 */
HeapTuple
SearchSysCacheLockedCopy1(SysCacheIdentifier cacheId,
						  Datum key1)
{
	HeapTuple	tuple,
				newtuple;

	tuple = SearchSysCacheLocked1(cacheId, key1);
	if (!HeapTupleIsValid(tuple))
		return tuple;
	newtuple = heap_copytuple(tuple);
	ReleaseSysCache(tuple);
	return newtuple;
}

/*
 * SearchSysCacheExists
 *
 * A convenience routine that just probes to see if a tuple can be found.
 * No lock is retained on the syscache entry.
 */
bool
SearchSysCacheExists(SysCacheIdentifier cacheId,
					 Datum key1,
					 Datum key2,
					 Datum key3,
					 Datum key4)
{
	HeapTuple	tuple;

	tuple = SearchSysCache(cacheId, key1, key2, key3, key4);
	if (!HeapTupleIsValid(tuple))
		return false;
	ReleaseSysCache(tuple);
	return true;
}

/*
 * GetSysCacheOid
 *
 * A convenience routine that does SearchSysCache and returns the OID in the
 * oidcol column of the found tuple, or InvalidOid if no tuple could be found.
 * No lock is retained on the syscache entry.
 */
Oid
GetSysCacheOid(SysCacheIdentifier cacheId,
			   AttrNumber oidcol,
			   Datum key1,
			   Datum key2,
			   Datum key3,
			   Datum key4)
{
	HeapTuple	tuple;
	bool		isNull;
	Oid			result;

	tuple = SearchSysCache(cacheId, key1, key2, key3, key4);
	if (!HeapTupleIsValid(tuple))
		return InvalidOid;
	result = DatumGetObjectId(heap_getattr(tuple, oidcol,
										   SysCache[cacheId]->cc_tupdesc,
										   &isNull));
	Assert(!isNull);			/* columns used as oids should never be NULL */
	ReleaseSysCache(tuple);
	return result;
}


/*
 * SearchSysCacheAttName
 *
 * This routine is equivalent to SearchSysCache on the ATTNAME cache,
 * except that it will return NULL if the found attribute is marked
 * attisdropped.  This is convenient for callers that want to act as
 * though dropped attributes don't exist.
 */
HeapTuple
SearchSysCacheAttName(Oid relid, const char *attname)
{
	HeapTuple	tuple;

	tuple = SearchSysCache2(ATTNAME,
							ObjectIdGetDatum(relid),
							CStringGetDatum(attname));
	if (!HeapTupleIsValid(tuple))
		return NULL;
	if (((Form_pg_attribute) GETSTRUCT(tuple))->attisdropped)
	{
		ReleaseSysCache(tuple);
		return NULL;
	}
	return tuple;
}

/*
 * SearchSysCacheCopyAttName
 *
 * As above, an attisdropped-aware version of SearchSysCacheCopy.
 */
HeapTuple
SearchSysCacheCopyAttName(Oid relid, const char *attname)
{
	HeapTuple	tuple,
				newtuple;

	tuple = SearchSysCacheAttName(relid, attname);
	if (!HeapTupleIsValid(tuple))
		return tuple;
	newtuple = heap_copytuple(tuple);
	ReleaseSysCache(tuple);
	return newtuple;
}

/*
 * SearchSysCacheExistsAttName
 *
 * As above, an attisdropped-aware version of SearchSysCacheExists.
 */
bool
SearchSysCacheExistsAttName(Oid relid, const char *attname)
{
	HeapTuple	tuple;

	tuple = SearchSysCacheAttName(relid, attname);
	if (!HeapTupleIsValid(tuple))
		return false;
	ReleaseSysCache(tuple);
	return true;
}


/*
 * SearchSysCacheAttNum
 *
 * This routine is equivalent to SearchSysCache on the ATTNUM cache,
 * except that it will return NULL if the found attribute is marked
 * attisdropped.  This is convenient for callers that want to act as
 * though dropped attributes don't exist.
 */
HeapTuple
SearchSysCacheAttNum(Oid relid, int16 attnum)
{
	HeapTuple	tuple;

	tuple = SearchSysCache2(ATTNUM,
							ObjectIdGetDatum(relid),
							Int16GetDatum(attnum));
	if (!HeapTupleIsValid(tuple))
		return NULL;
	if (((Form_pg_attribute) GETSTRUCT(tuple))->attisdropped)
	{
		ReleaseSysCache(tuple);
		return NULL;
	}
	return tuple;
}

/*
 * SearchSysCacheCopyAttNum
 *
 * As above, an attisdropped-aware version of SearchSysCacheCopy.
 */
HeapTuple
SearchSysCacheCopyAttNum(Oid relid, int16 attnum)
{
	HeapTuple	tuple,
				newtuple;

	tuple = SearchSysCacheAttNum(relid, attnum);
	if (!HeapTupleIsValid(tuple))
		return NULL;
	newtuple = heap_copytuple(tuple);
	ReleaseSysCache(tuple);
	return newtuple;
}


/*
 * SysCacheGetAttr
 *
 *		Given a tuple previously fetched by SearchSysCache(),
 *		extract a specific attribute.
 *
 * This is equivalent to using heap_getattr() on a tuple fetched
 * from a non-cached relation.  Usually, this is only used for attributes
 * that could be NULL or variable length; the fixed-size attributes in
 * a system table are accessed just by mapping the tuple onto the C struct
 * declarations from include/catalog/.
 *
 * As with heap_getattr(), if the attribute is of a pass-by-reference type
 * then a pointer into the tuple data area is returned --- the caller must
 * not modify or pfree the datum!
 *
 * Note: it is legal to use SysCacheGetAttr() with a cacheId referencing
 * a different cache for the same catalog the tuple was fetched from.
 */
Datum
SysCacheGetAttr(SysCacheIdentifier cacheId, HeapTuple tup,
				AttrNumber attributeNumber,
				bool *isNull)
{
	/*
	 * We just need to get the TupleDesc out of the cache entry, and then we
	 * can apply heap_getattr().  Normally the cache control data is already
	 * valid (because the caller recently fetched the tuple via this same
	 * cache), but there are cases where we have to initialize the cache here.
	 */
	if (cacheId < 0 || cacheId >= SysCacheSize || !SysCache[cacheId])
		elog(ERROR, "invalid cache ID: %d", cacheId);
	if (!SysCache[cacheId]->cc_tupdesc)
	{
		InitCatCachePhase2(SysCache[cacheId], false);
		Assert(SysCache[cacheId]->cc_tupdesc);
	}

	return heap_getattr(tup, attributeNumber,
						SysCache[cacheId]->cc_tupdesc,
						isNull);
}

/*
 * SysCacheGetAttrNotNull
 *
 * As above, a version of SysCacheGetAttr which knows that the attr cannot
 * be NULL.
 */
Datum
SysCacheGetAttrNotNull(SysCacheIdentifier cacheId, HeapTuple tup,
					   AttrNumber attributeNumber)
{
	bool		isnull;
	Datum		attr;

	attr = SysCacheGetAttr(cacheId, tup, attributeNumber, &isnull);

	if (isnull)
	{
		elog(ERROR,
			 "unexpected null value in cached tuple for catalog %s column %s",
			 get_rel_name(cacheinfo[cacheId].reloid),
			 NameStr(TupleDescAttr(SysCache[cacheId]->cc_tupdesc, attributeNumber - 1)->attname));
	}

	return attr;
}

/*
 * GetSysCacheHashValue
 *
 * Get the hash value that would be used for a tuple in the specified cache
 * with the given search keys.
 *
 * The reason for exposing this as part of the API is that the hash value is
 * exposed in cache invalidation operations, so there are places outside the
 * catcache code that need to be able to compute the hash values.
 */
uint32
GetSysCacheHashValue(SysCacheIdentifier cacheId,
					 Datum key1,
					 Datum key2,
					 Datum key3,
					 Datum key4)
{
	if (cacheId < 0 || cacheId >= SysCacheSize || !SysCache[cacheId])
		elog(ERROR, "invalid cache ID: %d", cacheId);

	return GetCatCacheHashValue(SysCache[cacheId], key1, key2, key3, key4);
}

/*
 * List-search interface
 */
struct catclist *
SearchSysCacheList(SysCacheIdentifier cacheId, int nkeys,
				   Datum key1, Datum key2, Datum key3)
{
	if (cacheId < 0 || cacheId >= SysCacheSize || !SysCache[cacheId])
		elog(ERROR, "invalid cache ID: %d", cacheId);

	return SearchCatCacheList(SysCache[cacheId], nkeys,
							  key1, key2, key3);
}

/*
 * SysCacheInvalidate
 *
 *	Invalidate entries in the specified cache, given a hash value.
 *	See CatCacheInvalidate() for more info.
 *
 *	This routine is only quasi-public: it should only be used by inval.c.
 */
void
SysCacheInvalidate(SysCacheIdentifier cacheId, uint32 hashValue)
{
	if (cacheId < 0 || cacheId >= SysCacheSize)
		elog(ERROR, "invalid cache ID: %d", cacheId);

	/* if this cache isn't initialized yet, no need to do anything */
	if (!SysCache[cacheId])
		return;

	CatCacheInvalidate(SysCache[cacheId], hashValue);
}

/*
 * Certain relations that do not have system caches send snapshot invalidation
 * messages in lieu of catcache messages.  This is for the benefit of
 * GetCatalogSnapshot(), which can then reuse its existing MVCC snapshot
 * for scanning one of those catalogs, rather than taking a new one, if no
 * invalidation has been received.
 *
 * Relations that have syscaches need not (and must not) be listed here.  The
 * catcache invalidation messages will also flush the snapshot.  If you add a
 * syscache for one of these relations, remove it from this list.
 */
bool
RelationInvalidatesSnapshotsOnly(Oid relid)
{
	switch (relid)
	{
		case DbRoleSettingRelationId:
		case DependRelationId:
		case SharedDependRelationId:
		case DescriptionRelationId:
		case SharedDescriptionRelationId:
		case SecLabelRelationId:
		case SharedSecLabelRelationId:
			return true;
		default:
			break;
	}

	return false;
}

/*
 * Test whether a relation has a system cache.
 */
bool
RelationHasSysCache(Oid relid)
{
	int			low = 0,
				high = SysCacheRelationOidSize - 1;

	while (low <= high)
	{
		int			middle = low + (high - low) / 2;

		if (SysCacheRelationOid[middle] == relid)
			return true;
		if (SysCacheRelationOid[middle] < relid)
			low = middle + 1;
		else
			high = middle - 1;
	}

	return false;
}

/*
 * Test whether a relation supports a system cache, ie it is either a
 * cached table or the index used for a cache.
 */
bool
RelationSupportsSysCache(Oid relid)
{
	int			low = 0,
				high = SysCacheSupportingRelOidSize - 1;

	while (low <= high)
	{
		int			middle = low + (high - low) / 2;

		if (SysCacheSupportingRelOid[middle] == relid)
			return true;
		if (SysCacheSupportingRelOid[middle] < relid)
			low = middle + 1;
		else
			high = middle - 1;
	}

	return false;
}


/*
 * OID comparator for qsort
 */
static int
oid_compare(const void *a, const void *b)
{
	Oid			oa = *((const Oid *) a);
	Oid			ob = *((const Oid *) b);

	return pg_cmp_u32(oa, ob);
}

/*
 * Verify the table yb_cache_index_name_table is consistent with cacheinfo.
 * Should only be invoked when pg_class is fully loaded for SearchSysCache1
 * to find these indexes.
 */
#ifndef NDEBUG
bool
YbCheckCatalogCacheIndexNameTable()
{
	/*
	 * We can only do this verification during initdb because otherwise
	 * during YSQL upgrade we can see assertion failure.
	 */
	if (!YBCIsInitDbModeEnvVarSet())
		return true;
	int			cache_id;

	for (cache_id = 0; cache_id < SysCacheSize; cache_id++)
	{
		const char *index_name = yb_cache_index_name_table[cache_id];
		Oid			indoid = cacheinfo[cache_id].indoid;
		HeapTuple	tuple = SearchSysCache1(RELOID, indoid);

		Assert(HeapTupleIsValid(tuple));
		Form_pg_class classForm = (Form_pg_class) GETSTRUCT(tuple);

		if (strcmp(NameStr(classForm->relname), index_name))
		{
			ReleaseSysCache(tuple);
			YBC_LOG_WARNING("Cache id %u has name mismatch: %s vs %s", cache_id,
							NameStr(classForm->relname), index_name);
			return false;
		}
		ReleaseSysCache(tuple);

		const char *table_name = YbGetCatalogCacheTableNameFromCacheId(cache_id);
		Oid			reloid = cacheinfo[cache_id].reloid;

		tuple = SearchSysCache1(RELOID, reloid);
		Assert(HeapTupleIsValid(tuple));
		classForm = (Form_pg_class) GETSTRUCT(tuple);
		if (strcmp(NameStr(classForm->relname), table_name))
		{
			ReleaseSysCache(tuple);
			YBC_LOG_WARNING("Cache id %u has name mismatch: %s vs %s", cache_id,
							NameStr(classForm->relname), table_name);
			return false;
		}
		ReleaseSysCache(tuple);
	}
	return true;
}

/*
 * Verify that the SysCacheName array is consistent with the SysCacheIdentifier enum.
 */
bool
YbCheckSysCacheNames()
{
#define YB_CATCACHE_ENTRY(name, id, idx, tbl) \
	if (strcmp(SysCacheName[name], #name)) return false;
	YB_CATCACHE_LIST
#undef YB_CATCACHE_ENTRY
	return true;
}
#endif							/* NDEBUG */

const char *
YbGetCatalogCacheIndexName(int cache_id)
{
	return yb_cache_index_name_table[cache_id];
}

const char *
YbGetCatalogCacheTableNameFromTableId(int table_id)
{
	Assert(table_id >= 0 && table_id < YbNumCatalogCacheTables);
	return yb_cache_table_name_table[table_id];
}

int
YbGetCatalogCacheTableIdFromCacheId(int cache_id)
{
	int			table_id = yb_catalog_cache_tables[cache_id];

	Assert(table_id >= 0 && table_id < YbNumCatalogCacheTables);
	return table_id;
}

const char *
YbGetCatalogCacheTableNameFromCacheId(int cache_id)
{
	return YbGetCatalogCacheTableNameFromTableId(YbGetCatalogCacheTableIdFromCacheId(cache_id));
}

uint32
YbSysCacheComputeHashValue(int cache_id, Datum v1, Datum v2, Datum v3, Datum v4)
{
	elog(LOG, "Computing hash for cache_id: %d, v1: " UINT64_FORMAT ", v2: " UINT64_FORMAT ", v3: " UINT64_FORMAT ", v4: " UINT64_FORMAT,
		 cache_id, (uint64) v1, (uint64) v2, (uint64) v3, (uint64) v4);
	CatCache   *cache = SysCache[cache_id];

	return YbCatalogCacheComputeHashValue(cache, v1, v2, v3, v4);
}

/*
 * Copies data from the cacheinfo array to the supplied values array.
 * The values array is expected to have space for at least 10 Datums.
 */
void
YbCopyCacheInfoToValues(int cache_id, Datum *values)
{
	values[0] = Int32GetDatum(cache_id);
	values[1] = CStringGetTextDatum(SysCacheName[cache_id]);
	values[2] = ObjectIdGetDatum(cacheinfo[cache_id].reloid);
	values[3] = ObjectIdGetDatum(cacheinfo[cache_id].indoid);
	values[4] = Int32GetDatum(cacheinfo[cache_id].nkeys);
	values[5] = Int32GetDatum(cacheinfo[cache_id].key[0]);
	values[6] = Int32GetDatum(cacheinfo[cache_id].key[1]);
	values[7] = Int32GetDatum(cacheinfo[cache_id].key[2]);
	values[8] = Int32GetDatum(cacheinfo[cache_id].key[3]);
	values[9] = Int32GetDatum(cacheinfo[cache_id].nbuckets);
}

void
YbCheckCatalogCacheIds()
{
	/*
	 * If any existing id has its integer value changed, we need to increment
	 * YbSharedInvalCatcacheMsgVersion so that old release PG backend will not
	 * apply the catalog cache invalidation message.
	 *
	 * If an existing ID is removed, interop isn't possible so we need to
	 * bump YbSharedInvalCatcacheMsgVersion.
	 * If new ids are added, we need to add them at the end of the above
	 * list. This is to allow we can keep YbSharedInvalCatcacheMsgVersion
	 * unchanged so that old release PG backend can apply messages of any
	 * existing ids. If a message catcache id's integer value is out of bound,
	 * it will not be applied because the corresponding catalog cache does not
	 * exist in the old PG backend. If there is a new id appended, we need to
	 * evaluate whether we need to bump YbSharedInvalCatcacheMsgVersion
	 * or not:
	 * (1) if the new id represents a new catalog table that did not exist in
	 * in the old release, then interop is possible when the new id is not
	 * involved, and there is no need to bump YbSharedInvalCatcacheMsgVersion.
	 * (2) if the new id represents an existing catalog table in the old
	 * release, then in a new PG backend the new catcache should be invalidated
	 * but old PG backend cannot provide that message needed. In this case
	 * interop isn't possible so we need to bump YbSharedInvalCatcacheMsgVersion.
	 */
#define YB_CATCACHE_ENTRY(name, id, idx, tbl) \
	static_assert(name == id, \
				  "The cache ID " #name " has changed from " #id ". You need to increment YbSharedInvalCatcacheMsgVersion");
	YB_CATCACHE_LIST
#undef YB_CATCACHE_ENTRY
}
