/*-------------------------------------------------------------------------
 *
 * yb_target.h
 *	  Target and column-reference helpers for YbcPgStatement.
 *
 *	  "Targets" tell DocDB which columns to return in the result
 *	  set.  "Column refs" tell DocDB which columns are referenced
 *	  by pushed-down expressions (WHERE clauses, etc.) that are
 *	  evaluated server-side.
 *
 *	  Used by AM implementations (yb_scan_core, yb_lsm, ybgin,
 *	  yb_catalog_scan, yb_sample_scan) and the executor
 *	  (createplan, ybModifyTable).
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
 * src/include/access/yb_target.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

#include "access/tupdesc.h"
#include "nodes/plannodes.h"
#include "utils/relcache.h"
#include "yb/yql/pggate/ybc_pggate.h"

/* Bind a single datum value to a column in a PgGate statement. */
extern void YbBindDatumToColumn(YbcPgStatement stmt,
								int attr_num,
								Oid type_id,
								Oid collation_id,
								Datum datum,
								bool is_null,
								const YbcPgTypeEntity *null_type_entity);

/* Add targets to the given statement. */
extern void YbDmlAppendTargetSystem(AttrNumber attnum, YbcPgStatement handle);
extern void YbDmlAppendTargetRegular(TupleDesc tupdesc, AttrNumber attnum,
									 YbcPgStatement handle);
extern void YbDmlAppendTargetRegularAttr(const FormData_pg_attribute *attr,
										 YbcPgStatement handle);

extern void YbDmlAppendTargetsAggregate(List *aggrefs, Scan *outer_plan,
										TupleDesc tupdesc, Relation index,
										bool xs_want_itup, YbcPgStatement handle);
extern void YbDmlAppendTargets(List *colrefs, YbcPgStatement handle);

/* Column refs: tell DocDB which columns are needed for pushdowns. */
extern void YbAppendPrimaryColumnRef(YbcPgStatement dml, YbcPgExpr colref);
extern void YbAppendPrimaryColumnRefs(YbcPgStatement dml, List *colrefs);

/* Apply pushdown expressions (WHERE quals) to the statement. */
extern void YbApplyPrimaryPushdown(YbcPgStatement dml,
								   const YbPushdownExprs *pushdown);
extern void YbApplySecondaryIndexPushdown(YbcPgStatement dml,
										  const YbPushdownExprs *pushdown);

extern Oid	ybc_get_attcollation(TupleDesc bind_desc, AttrNumber attnum);
