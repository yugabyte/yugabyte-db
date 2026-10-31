/*-------------------------------------------------------------------------
 *
 * yb_scan_plan.c
 *	  Plan-time YB scan analysis.
 *
 *	  Implements the binding rules declared in yb_scan_plan.h.  See
 *	  that header for what belongs here.
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
 * src/backend/access/yb_scan/yb_scan_plan.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <limits.h>

#include "access/yb_scan_plan.h"
#include "catalog/pg_type.h"

/* Return true if typid is one of the Object Identifier Types */
static bool
YbIsOidType(Oid typid)
{
	switch (typid)
	{
		case OIDOID:
		case REGPROCOID:
		case REGPROCEDUREOID:
		case REGOPEROID:
		case REGOPERATOROID:
		case REGCLASSOID:
		case REGTYPEOID:
		case REGROLEOID:
		case REGNAMESPACEOID:
		case REGCONFIGOID:
		case REGDICTIONARYOID:
			return true;
		default:
			return false;
	}
}

int64
YbDatumGetInt64(Datum value, Oid value_typid)
{
	switch (value_typid)
	{
		case INT2OID:
			return DatumGetInt16(value);
		case INT4OID:
			return DatumGetInt32(value);
		case INT8OID:
			return DatumGetInt64(value);
		default:
			ereport(ERROR,
					(errcode(ERRCODE_DATATYPE_MISMATCH),
					 errmsg("not an integer type")));
	}
}

static bool
YbIsIntegerInRange(Datum value, Oid value_typid, int min, int max)
{
	int64		val = YbDatumGetInt64(value, value_typid);

	return val >= min && val <= max;
}

/*
 * Determine whether an equality between two types needs further recheck.  For
 * now, this only flags cases where the storage column type is smaller than the
 * value type.  Through YbIsValueOutOfRange, the same pairs also decide whether
 * YbIsScanCompatible binds a scalar key of any strategy, whether an IN list
 * element or tuple is dropped, and whether an equality makes the scan
 * unsatisfiable.
 *
 * TODO(jason): check if any other type combos need checking.  float4 and
 * float8 look suspicious.
 *
 * SELECT a.typname, b.typname
 *   FROM pg_operator
 *   JOIN pg_type a ON oprleft = a.oid
 *   JOIN pg_type b ON oprright = b.oid
 *  WHERE oprname = '=' AND oprleft != oprright;
 *    typname   |   typname
 * -------------+-------------
 *  name        | text
 *  int8        | int2
 *  int8        | int4
 *  int2        | int8
 *  int2        | int4
 *  int4        | int8
 *  int4        | int2
 *  text        | name
 *  xid         | int4
 *  float4      | float8
 *  float8      | float4
 *  date        | timestamp
 *  date        | timestamptz
 *  timestamp   | date
 *  timestamp   | timestamptz
 *  timestamptz | date
 *  timestamptz | timestamp
 * (17 rows)
 */
bool
YbShouldRecheckEquality(Oid column_typid, Oid value_typid)
{
	switch (column_typid)
	{
		case INT2OID:
			if (value_typid == INT4OID)
				return true;
			yb_switch_fallthrough();
		case INT4OID:
			if (value_typid == INT8OID)
				return true;
			break;
		default:
			break;
	}

	return false;
}

bool
YbIsValueOutOfRange(Oid col_typid, Oid val_typid, Datum val)
{
	return (YbShouldRecheckEquality(col_typid, val_typid) &&
			!YbIsIntegerInRange(val, val_typid,
								col_typid == INT2OID ? SHRT_MIN : INT_MIN,
								col_typid == INT2OID ? SHRT_MAX : INT_MAX));
}

/*
 * Return true if a scan key column type is compatible with value type.
 */
bool
YbIsScanCompatible(Oid column_typid,
				   Oid value_typid,
				   bool is_value_scalar,
				   Datum value)
{
	if (column_typid == value_typid)
		return true;

	switch (column_typid)
	{
		case INT2OID:
		case INT4OID:
		case INT8OID:

			/*
			 * A narrower integer column cannot hold every value of a wider
			 * type, and binding an out-of-range value would truncate it, so
			 * that "c0 < 65539" on an int2 column would bind as "c0 < 3".  So
			 * bind a scalar value only when the column type can hold it, and
			 * leave an out-of-range one unbound for recheck.  A plain equality
			 * to such a value makes the scan unsatisfiable before binding
			 * (ybHasOutOfRangeEquality).  A non-scalar value, an IN list,
			 * binds, and YbCullArray drops its out-of-range elements.
			 */
			if (value_typid != INT2OID && value_typid != INT4OID &&
				value_typid != INT8OID)
				return false;
			return (!is_value_scalar ||
					!YbIsValueOutOfRange(column_typid, value_typid, value));

		case TEXTOID:
		case BPCHAROID:
		case VARCHAROID:
			return (value_typid == TEXTOID || value_typid == BPCHAROID ||
					value_typid == VARCHAROID);

		default:
			if (YbIsOidType(column_typid) && YbIsOidType(value_typid))
				return true;
			/* Conservatively return false. */
			return false;
	}
}
