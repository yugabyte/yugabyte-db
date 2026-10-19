/*-------------------------------------------------------------------------
 *
 * yb_scan_plan.h
 *	  Plan-time YB scan analysis.
 *
 *	  Rules for which scan conditions YB binds to key columns.
 *	  Scan-key setup (yb_scan.c) binds a scan by these rules, and
 *	  cost estimation (yb_scan.c, costsize.c) predicts that binding
 *	  without seeing execution state.  Put a rule that both sides
 *	  need here instead of copying it into the cost code.  Plan-time
 *	  analysis only: no execution state (YbScanDesc, ScanKey) or
 *	  pggate calls belong here.
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
 * src/include/access/yb_scan_plan.h
 *
 *-------------------------------------------------------------------------
 */

#pragma once

#include "postgres.h"

extern int64 YbDatumGetInt64(Datum value, Oid value_typid);
extern bool YbShouldRecheckEquality(Oid column_typid, Oid value_typid);
extern bool YbIsValueOutOfRange(Oid col_typid, Oid val_typid, Datum val);
extern bool YbIsScanCompatible(Oid column_typid, Oid value_typid,
							   bool is_value_scalar, Datum value);
