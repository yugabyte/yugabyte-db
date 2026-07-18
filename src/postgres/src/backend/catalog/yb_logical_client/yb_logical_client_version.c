/*-------------------------------------------------------------------------
 *
 * yb_logical_client_version.c
 *	  utility functions related to the ysql logical client version table.
 *
 * Portions Copyright (c) YugabyteDB, Inc.
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/yb_scan.h"
#include "catalog/pg_type.h"
#include "catalog/pg_yb_logical_client_version.h"
#include "catalog/schemapg.h"
#include "catalog/yb_logical_client_version.h"
#include "executor/ybExpr.h"
#include "executor/ybModifyTable.h"
#include "miscadmin.h"
#include "nodes/makefuncs.h"
#include "pg_yb_utils.h"
#include "utils/fmgroids.h"
#include "utils/rel.h"
#include "yb/yql/pggate/ybc_gflags.h"
#include "yb/yql/pggate/ybc_pg_typedefs.h"


YbLogicalClientVersionType yb_logical_client_version_type = LOGICAL_CLIENT_VERSION_UNSET;

static FormData_pg_attribute Desc_pg_yb_logical_client_version[Natts_pg_yb_logical_client_version] = {
	Schema_pg_yb_logical_client_version
};

static bool YbGetMasterLogicalClientVersionFromTable(Oid db_oid, uint64_t *version);
static Datum YbGetMasterLogicalClientVersionTableEntryYbctid(Relation logical_client_version_rel,
															 Oid db_oid);

/*  Oid used for storing coarse grained LCV */
#define DefaultOid ((Oid) 0)

uint64_t
YbGetMasterLogicalClientVersion()
{
	uint64_t	version = YB_CATCACHE_VERSION_UNINITIALIZED;

	switch (YbGetLogicalClientVersionType())
	{
		case LOGICAL_CLIENT_VERSION_CATALOG_TABLE:
			/* For coarse-grained versioning, use DefaultOid */
			if (YbGetMasterLogicalClientVersionFromTable(DefaultOid, &version))
				return version;
			ereport(LOG,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("failed to get master logical client version from table")));
			yb_switch_fallthrough();

		case LOGICAL_CLIENT_VERSION_UNSET:	/* should not happen. */
			break;
	}
	ereport(FATAL,
			(errcode(ERRCODE_INTERNAL_ERROR),
			 errmsg("logical client version type was not set, cannot load system catalog.")));
	return version;
}

bool
YbGetMasterLogicalClientVersionFromTable(Oid db_oid, uint64_t *version)
{
	*version = 0;				/* unset; */

	int			natts = Natts_pg_yb_logical_client_version;

	/*
	 * pg_yb_logical_client_version is a shared catalog table, so as per DocDB store,
	 * it belongs to the template1 database.
	 */
	int			oid_attnum = Anum_pg_yb_logical_client_version_db_oid;
	int			current_version_attnum = Anum_pg_yb_logical_client_version_current_version;
	Form_pg_attribute oid_attrdesc = &Desc_pg_yb_logical_client_version[oid_attnum - 1];

	YbcPgStatement ybc_stmt;

	HandleYBStatus(YBCPgNewSelect(Template1DbOid,
								  YBLogicalClientVersionRelationId,
								  NULL /* prepare_params */ ,
								  YbBuildSystemTableLocalityInfo(YBLogicalClientVersionRelationId),
								  &ybc_stmt));

	Datum		oid_datum = Int32GetDatum(db_oid);
	YbcPgExpr	pkey_expr = YBCNewConstant(ybc_stmt,
										   oid_attrdesc->atttypid,
										   oid_attrdesc->attcollation,
										   oid_datum,
										   false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, 1, pkey_expr));

	/* Add scan targets: regular columns in ascending attnum order,
	 * matching the order used by the sys table prefetcher's OrderColumns(). */
	for (AttrNumber attnum = 1; attnum <= natts; attnum++)
		YbDmlAppendTargetRegularAttr(&Desc_pg_yb_logical_client_version[attnum - 1], ybc_stmt);

	HandleYBStatus(YBCPgExecSelect(ybc_stmt, NULL /* exec_params */ ));

	bool		has_data = false;

	Datum	   *values = (Datum *) palloc0(natts * sizeof(Datum));
	bool	   *nulls = (bool *) palloc(natts * sizeof(bool));
	YbcPgSysColumns syscols;
	bool		result = false;

	/*
	 * When prefetching is enabled the prefetcher loads all rows from the table
	 * even though we bind to the row matching db_oid.  Iterate through the
	 * returned rows and pick the one that matches db_oid, mirroring the
	 * workaround in YbGetMasterCatalogVersionFromTable.
	 */
	while (true)
	{
		HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
									 natts,
									 (uint64_t *) values,
									 nulls,
									 &syscols,
									 &has_data));

		if (!has_data)
		{
			ereport(LOG,
					(errcode(ERRCODE_INTERNAL_ERROR),
					 errmsg("logical client version for database %u was "
							"not found", db_oid)));
			break;
		}

		if (DatumGetUInt32(values[oid_attnum - 1]) == db_oid)
		{
			*version = DatumGetUInt64(values[current_version_attnum - 1]);
			result = true;
			break;
		}
	}

	pfree(values);
	pfree(nulls);
	return result;
}

static void
YbIncrementMasterDBLogicalClientVersionTableEntryImpl(Oid db_oid)
{
	Assert(YbGetLogicalClientVersionType() == LOGICAL_CLIENT_VERSION_CATALOG_TABLE);

	YbcPgTypeAttrs type_attrs = {0};

	Relation rel = RelationIdGetRelation(YBLogicalClientVersionRelationId);

	YbcPgStatement update_stmt = YbNewUpdate(rel, YB_TRANSACTIONAL);

	Datum		ybctid = YbGetMasterLogicalClientVersionTableEntryYbctid(rel, db_oid);

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(update_stmt, BYTEAOID, InvalidOid,
											 ybctid, false /* is_null */ );

	HandleYBStatus(YBCPgDmlBindColumn(update_stmt, YBTupleIdAttributeNumber,
									  ybctid_expr));

	/* Set expression c = c + 1 for current version attribute. */
	AttrNumber	attnum = Anum_pg_yb_logical_client_version_current_version;
	Var		   *arg1 = makeVar(1,
							   attnum,
							   INT8OID,
							   0,
							   InvalidOid,
							   0);

	Const	   *arg2 = makeConst(INT8OID,
								 0,
								 InvalidOid,
								 sizeof(int64),
								 (Datum) 1,
								 false,
								 true);

	List	   *args = list_make2(arg1, arg2);

	FuncExpr   *expr = makeFuncExpr(F_INT8PL,
									INT8OID,
									args,
									InvalidOid,
									InvalidOid,
									COERCE_EXPLICIT_CALL);

	/* INT8 OID. */
	YbcPgExpr	ybc_expr = YBCNewEvalExprCall(update_stmt, (Expr *) expr);

	HandleYBStatus(YBCPgDmlAssignColumn(update_stmt, attnum, ybc_expr));
	YbcPgExpr yb_expr = YBCNewColumnRef(update_stmt, attnum, INT8OID, InvalidOid, &type_attrs);
	YbAppendPrimaryColumnRef(update_stmt, yb_expr);

	int			rows_affected_count = 0;

	HandleYBStatus(YBCPgDmlExecWriteOp(update_stmt, &rows_affected_count));

	Assert(rows_affected_count == 1);

	/* Cleanup. */
	update_stmt = NULL;
	RelationClose(rel);
}


bool
YbIncrementMasterLogicalClientVersionTableEntry()
{
	/* Use DefaultOid for getting coarse-grained LCV */
	YbIncrementMasterDBLogicalClientVersionTableEntryImpl(DefaultOid);
	return true;
}

Datum
YbGetMasterLogicalClientVersionTableEntryYbctid(Relation logical_client_version_rel,
												Oid db_oid)
{
	/*
	 * Construct virtual slot (db_oid, null) for computing ybctid using
	 * YBCComputeYBTupleIdFromSlot. Note that db_oid is the primary key so we
	 * can use null for other columns for simplicity.
	 */
	TupleTableSlot *slot = MakeSingleTupleTableSlot(RelationGetDescr(logical_client_version_rel),
													&TTSOpsVirtual);

	slot->tts_values[0] = db_oid;
	slot->tts_isnull[0] = false;
	slot->tts_values[1] = 0;
	slot->tts_isnull[1] = true;
	slot->tts_nvalid = 2;

	Datum		ybctid = YBCComputeYBTupleIdFromSlot(logical_client_version_rel, slot);

	ExecDropSingleTupleTableSlot(slot);
	return ybctid;
}

YbLogicalClientVersionType
YbGetLogicalClientVersionType()
{
	if (IsBootstrapProcessingMode())
	{
		/*
		 * We don't have the logical client version table at the start of initdb,
		 * and there's no point in switching later on.
		 */
		yb_logical_client_version_type = LOGICAL_CLIENT_VERSION_UNSET;
	}
	else if (yb_logical_client_version_type == LOGICAL_CLIENT_VERSION_UNSET)
	{
		uint64_t	logical_client_version;
		bool		coarse_logical_client_version_set;

		coarse_logical_client_version_set =
			YbGetMasterLogicalClientVersionFromTable(DefaultOid,
													 &logical_client_version);

		yb_logical_client_version_type =
			(coarse_logical_client_version_set ?
			 LOGICAL_CLIENT_VERSION_CATALOG_TABLE :
			 LOGICAL_CLIENT_VERSION_UNSET);
	}
	return yb_logical_client_version_type;
}

bool
YbLogicalClientVersionTableExists()
{
	bool		exists = false;

	HandleYBStatus(YBCPgTableExists(Template1DbOid,
									YBLogicalClientVersionRelationId,
									&exists));
	return exists;
}
