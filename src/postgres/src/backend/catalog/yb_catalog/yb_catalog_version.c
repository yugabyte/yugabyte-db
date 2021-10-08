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
#include "pg_yb_utils.h"

YbCatalogVersionType yb_catalog_version_type = CATALOG_VERSION_UNSET;

static const char* kCatalogVersionTableName = "pg_yb_catalog_version";

static FormData_pg_attribute Desc_pg_yb_catalog_version[Natts_pg_yb_catalog_version] = {
	Schema_pg_yb_catalog_version
};

static YbCatalogVersionType YbGetCatalogVersionType(bool can_use_cache);
static void YbGetMasterCatalogVersionFromTable(uint64_t *version);
static bool YbIsSystemCatalogChange(Relation rel);
static bool IsCatalogVersionTablePopulated();

/* Retrieve Catalog Version */

void YbGetMasterCatalogVersion(uint64_t *version, bool can_use_cache)
{
	switch (YbGetCatalogVersionType(can_use_cache)) {
		case CATALOG_VERSION_CATALOG_TABLE:
			YbGetMasterCatalogVersionFromTable(version);
			return;
		case CATALOG_VERSION_PROTOBUF_ENTRY:
			/* deprecated (kept for compatibility with old clusters). */
			HandleYBStatus(YBCPgGetCatalogMasterVersion(version));
			return;

		case CATALOG_VERSION_UNSET: /* should not happen. */
			break;
	}
	ereport(FATAL,
			(errcode(ERRCODE_INTERNAL_ERROR),
				errmsg("Catalog version type was not set, cannot load system catalog.")));
}

/* Modify Catalog Version */

bool YbIncrementMasterCatalogVersionTableEntry(bool is_breaking_change)
{
	if (YbGetCatalogVersionType(true /* can_use_cache */) != CATALOG_VERSION_CATALOG_TABLE) {
		return false;
	}

	YBCPgStatement update_stmt    = NULL;
	HeapTuple tuple = NULL;
	Relation rel = RelationIdGetRelation(YBCatalogVersionRelationId);

	// template1.
	HandleYBStatus(YBCPgNewUpdate(TemplateDbOid,
								  YBCatalogVersionRelationId,
								  false /* is_single_row_txn */,
								  &update_stmt));
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
	YBCPgExpr ybc_expr = YBCNewEvalSingleParamExprCall(update_stmt,
	                                                   (Expr *) expr,
	                                                   attnum,
	                                                   INT8OID,
	                                                   0,
	                                                   InvalidOid);

	HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));

	/* If breaking change set the latest breaking version to the same expression. */
	if (is_breaking_change)
	{
		YBExprParamDesc params[2];
		params[0].attno = attnum + 1;
		params[0].typid = INT8OID;
		params[0].typmod = 0;
		params[0].collid = InvalidOid;

		params[1].attno = attnum;
		params[1].typid = INT8OID;
		params[1].typmod = 0;
		params[1].collid = InvalidOid;

		YBCPgExpr ybc_expr = YBCNewEvalExprCall(update_stmt, (Expr *) expr, params, 2);
		HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum + 1, ybc_expr));
	}

	int rows_affected_count = 0;
	HandleYBStatus(YBCPgDmlExecWriteOp(update_stmt, &rows_affected_count));
	Assert(rows_affected_count == 1);

	/* Cleanup. */
	update_stmt = NULL;
	RelationClose(rel);

	return true;
}

bool YbMarkStatementIfCatalogVersionIncrement(YBCPgStatement ybc_stmt, Relation rel) {
	if (YbGetCatalogVersionType(true /* can_use_cache */) != CATALOG_VERSION_PROTOBUF_ENTRY)
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

/*
 * Check if pg_yb_catalog_version table exists and contains values.
 *
 * YSQL upgrade and our way of handling DDLs in separate
 * transaction make one additional check necessary - that the table
 * actually contains stuff.
 *
 * If YSQL upgrade has already created a table, but the initial row
 * wasn't inserted yet (or if insert failed), table will be left
 * empty.
 *
 * If that's the case, ignore the presence of the table.
 */
bool IsCatalogVersionTablePopulated() {
  	bool catalog_version_table_exists = false;
	HandleYBStatus(YBCPgTableExists(TemplateDbOid,
									YBCatalogVersionRelationId,
									&catalog_version_table_exists));
	if (!catalog_version_table_exists)
		return false;

	uint64_t version = 0;
	YbGetMasterCatalogVersionFromTable(&version);
	return version > 0;
}

YbCatalogVersionType YbGetCatalogVersionType(bool can_use_cache) {
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
		/* First call, need to set the version type. */
		yb_catalog_version_type =
			IsCatalogVersionTablePopulated() ?
				CATALOG_VERSION_CATALOG_TABLE :
				CATALOG_VERSION_PROTOBUF_ENTRY;
	}
	/*
	 * If we're on a legacy protobuf system and we are allowed to use cache,
	 * check if we might have migrated to the table-based system already.
	 *
	 * 1) First, use get_relname_relid to check if the catalog version table is
	 *    in pg_class cache.
	 *    This check utilizes RELNAMENSP cache that allows negative entries on
	 *    pg_catalog namespace, so lookup will be very cheap.
	 *
	 * 2) If so - use IsCatalogVersionTablePopulated to check if master is
	 *    aware of this table (this will be false if the table is currently
	 *    being created), and if that table contains values.
	 *
	 * (2) would require RPC exchange with YB, but this is only possible during
	 * the transition time (when table is created and not yet populated), e.g.
	 * during migration or if migration errored out in the middle.
	 * As such, this isn't a big deal.
	 *
	 * If cache usage is currently not allowed - that's okay since we expect
	 * other operations to use this routine as well.
	 */
	else if (can_use_cache &&
			 yb_catalog_version_type == CATALOG_VERSION_PROTOBUF_ENTRY &&
			 get_relname_relid(kCatalogVersionTableName,
							   PG_CATALOG_NAMESPACE) != InvalidOid &&
			 IsCatalogVersionTablePopulated())
	{
		yb_catalog_version_type = CATALOG_VERSION_CATALOG_TABLE;
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


void YbGetMasterCatalogVersionFromTable(uint64_t *version) {

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

	if (has_data)
	{
		*version = (uint64_t) DatumGetInt64(values[current_version_attnum - 1]);
	}
	pfree(values);
	pfree(nulls);
}
