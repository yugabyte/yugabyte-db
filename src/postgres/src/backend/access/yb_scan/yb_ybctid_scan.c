/*-------------------------------------------------------------------------
 *
 * yb_ybctid_scan.c
 *	  YB ybctid point-lookup implementation.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * permissions and limitations under the License.
 *
 * src/backend/access/yb_scan/yb_ybctid_scan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "access/sysattr.h"
#include "access/yb_special_scans.h"
#include "access/yb_target.h"
#include "executor/ybExpr.h"
#include "pg_yb_utils.h"
#include "utils/rel.h"
#include "yb/yql/pggate/ybc_pggate.h"

static bool
YbFetchRowData(YbcPgStatement ybc_stmt, Relation relation, Datum ybctid,
			   Datum *values, bool *nulls, YbcPgSysColumns *syscols)
{
	bool		has_data = false;
	TupleDesc	tupdesc = RelationGetDescr(relation);

	/* Bind ybctid to identify the current row. */
	YbcPgExpr	ybctid_expr = YBCNewConstant(ybc_stmt,
											 BYTEAOID,
											 InvalidOid,
											 ybctid,
											 false);

	HandleYBStatus(YBCPgDmlBindColumn(ybc_stmt, YBTupleIdAttributeNumber,
									  ybctid_expr));

	/*
	 * Set up the scan targets. For index-based scan we need to return all "real" columns.
	 */
	for (AttrNumber attnum = 1; attnum <= tupdesc->natts; attnum++)
	{
		if (!TupleDescAttr(tupdesc, attnum - 1)->attisdropped)
			YbDmlAppendTargetRegular(tupdesc, attnum, ybc_stmt);
	}
	YbDmlAppendTargetSystem(YBTupleIdAttributeNumber, ybc_stmt);

	/*
	 * Execute the select statement.
	 * This select statement fetch the row for a specific YBCTID, LIMIT setting is not needed.
	 */
	HandleYBStatus(YBCPgExecSelect(ybc_stmt, NULL /* exec_params */ ));

	/* Fetch one row. */
	HandleYBStatus(YBCPgDmlFetch(ybc_stmt,
								 tupdesc->natts,
								 (uint64_t *) values,
								 nulls,
								 syscols,
								 &has_data));

	return has_data;
}

bool
YbFetchHeapTuple(Relation relation, Datum ybctid, HeapTuple *tuple)
{
	TupleDesc	tupdesc = RelationGetDescr(relation);
	Datum	   *values = (Datum *) palloc0(tupdesc->natts * sizeof(Datum));
	bool	   *nulls = (bool *) palloc(tupdesc->natts * sizeof(bool));
	YbcPgSysColumns syscols;

	/* Read data */
	YbcPgStatement ybc_stmt = YbNewSelect(relation, NULL /* prepare_params */ );

	const bool has_data = YbFetchRowData(ybc_stmt, relation, ybctid, values, nulls, &syscols);

	/* Write into the given tuple */
	if (has_data)
	{
		*tuple = heap_form_tuple(tupdesc, values, nulls);
		(*tuple)->t_tableOid = RelationGetRelid(relation);
		if (syscols.ybctid != NULL)
			HEAPTUPLE_YBCTID(*tuple) = PointerGetDatum(syscols.ybctid);
	}


	/* Free up memory and return data */
	pfree(values);
	pfree(nulls);
	YBCPgDeleteStatement(ybc_stmt);
	return has_data;
}
