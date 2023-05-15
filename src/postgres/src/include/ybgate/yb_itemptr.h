/* ----------
 * yb_itemptr.h
 *
 * Utilities for YugaByte/PostgreSQL integration that have to be defined on the
 * PostgreSQL side.
 *
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
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 * src/include/ybgate/yb_itemptr.h
 *
 * This module contains representation for data that's sent from Yugabyte DB to Postgres.
 * ----------
 */

#ifndef YB_ITEMPTR_H
#define YB_ITEMPTR_H

/*
 * YbItemPointerData
 * This is a pointer to an item of Yugabyte database storage. Currently, we keep only ybctid value,
 * but that shouldn't be enough. For example, table_id might be needed.
 *
 * Postgres's ItemPointerData is an address on disk.
 */
typedef struct YbItemPointerData
{
	Datum ybctid;
} YbItemPointerData;

typedef YbItemPointerData *YbItemPointer;

#define YbItemPointerYbctid(itemPointer) \
	((itemPointer)->yb_item.ybctid)

#define YbItemPointerSetInvalid(itemPointer) \
	((itemPointer)->yb_item.ybctid = (Datum)0)

/* Heap tuple keeps data in t_self */
#define HEAPTUPLE_YBITEM(htup) ((htup)->t_self.yb_item)

#define HEAPTUPLE_YBCTID(htup) ((htup)->t_self.yb_item.ybctid)

#define HEAPTUPLE_COPY_YBITEM(fromHtup, toHtup)			\
	COPY_YBITEM(HEAPTUPLE_YBITEM(fromHtup), HEAPTUPLE_YBITEM(toHtup))

/* Index tuple keeps data in t_tid */
#define INDEXTUPLE_YBITEM(itup) ((itup)->t_tid.yb_item)

#define INDEXTUPLE_YBCTID(itup) ((itup)->t_tid.yb_item.ybctid)

#define INDEXTUPLE_COPY_YBITEM(fromItup, toItup)			\
	COPY_YBITEM(INDEXTUPLE_YBITEM(fromItup), INDEXTUPLE_YBITEM(toItup))

/* TupleTableSlot keeps data in tts_tid */
#define TABLETUPLE_YBITEM(tslot) ((tslot)->tts_tid.yb_item)

#define TABLETUPLE_YBCTID(tslot) ((tslot)->tts_tid.yb_item.ybctid)

#define TABLETUPLE_COPY_YBITEM(fromTslot, toTslot)			\
  COPY_YBITEM(TABLETUPLE_YBITEM(fromTslot), TABLETUPLE_YBITEM(toTslot))

/* Copy YbItemPointerData from a source to destination */
#define COPY_YBITEM(src, dest) COPY_YBCTID(src.ybctid, dest.ybctid)

/* Copy ybctid from a source to destination */
#define COPY_YBCTID(src, dest)                            			\
	do {                                                            \
		if (IsYugaByteEnabled()) {                                  \
			dest = (src == 0) ? 0 :                                 \
				PointerGetDatum(cstring_to_text_with_len(VARDATA_ANY(src), \
														 VARSIZE_ANY_EXHDR(src))); \
		} else {                                                    \
			dest = 0;                                               \
		}                                                           \
	} while (false)


#endif /* YB_ITEMPTR_H */
