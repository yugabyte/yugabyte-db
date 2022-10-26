//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//--------------------------------------------------------------------------------------------------

#include "postgres.h"
#include "pg_yb_utils.h"

#include "utils/builtins.h"
#include "ybgate/yb_itemptr.h"

void
YbItemPointerDataCopy(YbItemPointerData src, YbItemPointerData dest)
{
	dest.ybctid = (Datum)0;
	if (src.ybctid == 0)
	{
		dest.ybctid = PointerGetDatum(cstring_to_text_with_len(VARDATA_ANY(src.ybctid),
															   VARSIZE_ANY_EXHDR(src.ybctid)));
	}
}
