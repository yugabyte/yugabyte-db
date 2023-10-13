/*-------------------------------------------------------------------------
 *
 * yb_catalog_version.c
 *	  utility functions related to the ysql catalog version table.
 *
 * Portions Copyright (c) YugaByte, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"
#include "miscadmin.h"

#include <inttypes.h>

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "catalog/catalog.h"
#include "catalog/pg_database.h"
#include "catalog/pg_namespace_d.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_catalog_version.h"
#include "catalog/schemapg.h"
#include "catalog/yb_catalog_version.h"
#include "executor/ybcExpr.h"
#include "executor/ybcModifyTable.h"
#include "nodes/makefuncs.h"
#include "utils/catcache.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"

#include "yb/yql/pggate/ybc_pggate.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"
#include "pg_yb_utils.h"

YbCatalogVersionType yb_catalog_version_type = CATALOG_VERSION_UNSET;

static FormData_pg_attribute Desc_pg_yb_catalog_version[Natts_pg_yb_catalog_version] = {
	Schema_pg_yb_catalog_version
};

static YbCatalogVersionType YbGetCatalogVersionType();
static bool YbGetMasterCatalogVersionFromTable(uint64_t *version);
static bool YbIsSystemCatalogChange(Relation rel);

/* Retrieve Catalog Version */

uint64_t YbGetMasterCatalogVersion()
{
	uint64_t version = YB_CATCACHE_VERSION_UNINITIALIZED;
	switch (YbGetCatalogVersionType())
	{
		case CATALOG_VERSION_CATALOG_TABLE:
			if (YbGetMasterCatalogVersionFromTable(&version))
				return version;
			/*
			 * In spite of the fact the pg_yb_catalog_version table exists it has no actual
			 * version (this could happen during YSQL upgrade),
			 * fallback to an old protobuf mechanism until the next cache refresh.
			 */
			yb_catalog_version_type = CATALOG_VERSION_PROTOBUF_ENTRY;
			switch_fallthrough();
		case CATALOG_VERSION_PROTOBUF_ENTRY:
			/* deprecated (kept for compatibility with old clusters). */
			HandleYBStatus(YBCPgGetCatalogMasterVersion(&version));
			return version;

		case CATALOG_VERSION_UNSET: /* should not happen. */
			break;
	}
	ereport(FATAL,
			(errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Catalog version type was not set, cannot load system catalog.")));
	return version;
}

/* Modify Catalog Version */

bool YbIncrementMasterCatalogVersionTableEntry(bool is_breaking_change)
{
	if (YbGetCatalogVersionType() != CATALOG_VERSION_CATALOG_TABLE)
		return false;

	YBCPgStatement update_stmt    = NULL;
	YBCPgTypeAttrs type_attrs = { 0 };
	YBCPgExpr yb_expr;
	HeapTuple tuple = NULL;
	Relation rel = RelationIdGetRelation(YBCatalogVersionRelationId);

	// template1.
	HandleYBStatus(YBCPgNewUpdate(TemplateDbOid,
								  YBCatalogVersionRelationId,
								  false /* is_region_local */,
								  &update_stmt,
                  YB_TRANSACTIONAL));
	/* Construct HeapTuple */
	Datum		values[3];
	bool		nulls[3];
	/*
	 * TODO The plan is to eventually maintain in a more fine-grained way
	 * (i.e. per-database). Then, values[0] should be MyDatabaseId instead.
	 */
	values[0] = TemplateDbOid;
	nulls[0] = false;
	values[1] = 0;
	nulls[1] = true;
	values[2] = 0;
	nulls[2] = true;
	tuple = heap_form_tuple(RelationGetDescr(rel), values, nulls);

	Datum ybctid = YBCGetYBTupleIdFromTuple(rel,
											tuple,
											RelationGetDescr(rel));

	/* Bind ybctid to identify the current row. */
	YBCPgExpr ybctid_expr = YBCNewConstant(update_stmt, BYTEAOID, InvalidOid, ybctid,
										   false /* is_null */);
	HandleYBStatus(YBCPgDmlBindColumn(update_stmt, YBTupleIdAttributeNumber, ybctid_expr));

	/* Set expression c = c + 1 for current version attribute. */
	AttrNumber attnum = Anum_pg_yb_catalog_version_current_version;
	Var *arg1 = makeVar(1,
						attnum,
						INT8OID,
						0,
						InvalidOid,
						0);

	Const *arg2 = makeConst(INT8OID,
							0,
							InvalidOid,
							sizeof(int64),
							(Datum) 1,
							false,
							true);

	List *args = list_make2(arg1, arg2);

	FuncExpr *expr = makeFuncExpr(F_INT8PL,
								  INT8OID,
								  args,
								  InvalidOid,
								  InvalidOid,
								  COERCE_EXPLICIT_CALL);

	/* INT8 OID. */
	YBCPgExpr ybc_expr = YBCNewEvalExprCall(update_stmt, (Expr *) expr);

	HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
	yb_expr = YBCNewColumnRef(update_stmt,
							  attnum,
							  INT8OID,
							  InvalidOid,
							  &type_attrs);
	HandleYBStatus(YbPgDmlAppendColumnRef(update_stmt, yb_expr));

	/* If breaking change set the latest breaking version to the same expression. */
	if (is_breaking_change)
	{
		ybc_expr = YBCNewEvalExprCall(update_stmt, (Expr *) expr);
		HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum + 1, ybc_expr));
	}

	int rows_affected_count = 0;
	if (*YBCGetGFlags()->log_ysql_catalog_versions)
		ereport(LOG,
				(errmsg("%s: incrementing master catalog version (%sbreaking)",
						__func__, is_breaking_change ? "" : "non")));
	HandleYBStatus(YBCPgDmlExecWriteOp(update_stmt, &rows_affected_count));
	Assert(rows_affected_count == 1);

	/* Cleanup. */
	update_stmt = NULL;
	RelationClose(rel);

	return true;
}

bool YbMarkStatementIfCatalogVersionIncrement(YBCPgStatement ybc_stmt, Relation rel) {
	if (YbGetCatalogVersionType() != CATALOG_VERSION_PROTOBUF_ENTRY)
	{
		/*
		 * Nothing to do -- only need to maintain this for the (old)
		 * protobuf-based way of storing the version.
		 */
		return false;
	}

	bool is_syscatalog_change = YbIsSystemCatalogChange(rel);
	bool modifies_row = false;
	HandleYBStatus(YBCPgDmlModifiesRow(ybc_stmt, &modifies_row));

	/*
	 * If this write may invalidate catalog cache tuples (i.e. UPDATE or DELETE),
	 * or this write may insert into a cached list, we must increment the
	 * cache version so other sessions can invalidate their caches.
	 * NOTE: If this relation caches lists, an INSERT could effectively be
	 * UPDATINGing the list object.
	 */
	bool is_syscatalog_version_change = is_syscatalog_change
			&& (modifies_row || RelationHasCachedLists(rel));

	/* Let the master know if this should increment the catalog version. */
	if (is_syscatalog_version_change)
	{
		HandleYBStatus(YBCPgSetIsSysCatalogVersionChange(ybc_stmt));
	}

	return is_syscatalog_version_change;
}

YbCatalogVersionType YbGetCatalogVersionType()
{
	if (IsBootstrapProcessingMode())
	{
		/*
		 * We don't have the catalog version table at the start of initdb,
		 * and there's no point in switching later on.
		 */
		yb_catalog_version_type = CATALOG_VERSION_PROTOBUF_ENTRY;
	}
	else if (yb_catalog_version_type == CATALOG_VERSION_UNSET)
	{
		bool catalog_version_table_exists = false;
		HandleYBStatus(YBCPgTableExists(
		    TemplateDbOid, YBCatalogVersionRelationId, &catalog_version_table_exists));
		yb_catalog_version_type = catalog_version_table_exists
		    ? CATALOG_VERSION_CATALOG_TABLE
		    : CATALOG_VERSION_PROTOBUF_ENTRY;
	}
	return yb_catalog_version_type;
}


/*
 * Check if operation changes a system table, ignore changes during
 * initialization (bootstrap mode).
 */
bool YbIsSystemCatalogChange(Relation rel)
{
	return IsSystemRelation(rel) && !IsBootstrapProcessingMode();
}


bool YbGetMasterCatalogVersionFromTable(uint64_t *version)
{
	*version = 0; // unset;

	int natts = Natts_pg_yb_catalog_version;
	/*
	 * pg_yb_catalog_version is a shared catalog table, so as per DocDB store,
	 * it belongs to the template1 database.
	 */
	int oid_attnum = Anum_pg_yb_catalog_version_db_oid;
	int current_version_attnum = Anum_pg_yb_catalog_version_current_version;
	Form_pg_attribute oid_attrdesc = &Desc_pg_yb_catalog_version[oid_attnum - 1];

	YBCPgStatement ybc_stmt;

	HandleYBStatus(YBCPgNewSelect(TemplateDbOid,
	                              YBCatalogVersionRelationId,
	                              NULL /* prepare_params */,
	                              false /* is_region_local */,
	                              &ybc_stmt));

	/*
	 * Bind ybctid to identify the relevant row.
	 * For now using a global version for all databases (maintained in the row
	 * for the TemplateDbOid database). Later this will be more fine-grained
	 * (per database) -- then we should use MyDatabaseOid instead.
	 */
	Datum oid_datum = Int32GetDatum(TemplateDbOid);
	YBCPgExpr pkey_expr = YBCNewConstant(ybc_stmt,
	                                     oid_attrdesc->atttypid,
	                                     oid_attrdesc->attcollation,
	                                     oid_datum,
	                                     false /* is_null */);

	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, 1, pkey_expr));

	/* Add scan targets */
	for (AttrNumber attnum = 1; attnum <= natts; attnum++)
	{
		Form_pg_attribute att = &Desc_pg_yb_catalog_version[attnum - 1];
		YBCPgTypeAttrs type_attrs = { att->atttypmod };
		YBCPgExpr   expr = YBCNewColumnRef(ybc_stmt, attnum, att->atttypid,
										   att->attcollation, &type_attrs);
		HandleYBStatus(YBCPgDmlAppendTarget(ybc_stmt, expr));
	}

	HandleYBStatus(YBCPgExecSelect(ybc_stmt, NULL /* exec_params */));

	bool      has_data = false;

	Datum           *values = (Datum *) palloc0(natts * sizeof(Datum));
	bool            *nulls  = (bool *) palloc(natts * sizeof(bool));
	YBCPgSysColumns syscols;

	/* Fetch one row. */
	HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
	                             natts,
	                             (uint64_t *) values,
	                             nulls,
	                             &syscols,
	                             &has_data));

	bool result = false;
	if (has_data)
	{
		*version = (uint64_t) DatumGetInt64(values[current_version_attnum - 1]);
		result = true;
	}
	pfree(values);
	pfree(nulls);
	return result;
}
