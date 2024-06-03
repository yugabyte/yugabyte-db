/*------------------------------------------------------------------------------
 * Copyright (c) YugaByte, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not
 * use this file except in compliance with the License.  You may obtain a copy
 * of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations under
 * the License.
 *------------------------------------------------------------------------------
 */
#ifndef ROWTYPES_H
#define ROWTYPES_H

#include "postgres.h"
#include "access/htup_details.h"
#include "fmgr.h"

/*
 * structure to cache metadata needed for record I/O
 */
typedef struct ColumnIOData
{
	Oid		 column_type;
	Oid		 typiofunc;
	Oid		 typioparam;
	bool	 typisvarlena;
	FmgrInfo proc;
} ColumnIOData;

typedef struct RecordIOData
{
	Oid			 record_type;
	int32		 record_typmod;
	int			 ncolumns;
	ColumnIOData columns[FLEXIBLE_ARRAY_MEMBER];
} RecordIOData;

Datum record_out_internal(HeapTupleHeader rec, TupleDesc *tupdesc_ptr,
						  FmgrInfo *flinfo);

#endif							/* ROWTYPES_H */
