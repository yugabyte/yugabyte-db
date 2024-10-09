/* ----------
 * ybctid.h
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
 * src/include/ybctid.h
 *
 * This module contains representation for data that's sent from Yugabyte DB to Postgres.
 * ----------
 */

#ifndef YBCTID_H
#define YBCTID_H

#define HEAPTUPLE_YBCTID(htup) ((htup)->t_ybctid)

#define HEAPTUPLE_COPY_YBCTID(fromHtup, toHtup) \
	COPY_YBCTID(HEAPTUPLE_YBCTID(fromHtup), HEAPTUPLE_YBCTID(toHtup))

#define INDEXTUPLE_YBCTID(itup) ((itup)->t_ybctid)

#define INDEXTUPLE_COPY_YBCTID(fromItup, toItup)			\
	COPY_YBCTID(INDEXTUPLE_YBCTID(fromItup), INDEXTUPLE_YBCTID(toItup))

#define TABLETUPLE_YBCTID(tslot) ((tslot)->tts_ybctid)

#define TABLETUPLE_COPY_YBCTID(fromTslot, toTslot)			\
  COPY_YBCTID(TABLETUPLE_YBCTID(fromTslot), TABLETUPLE_YBCTID(toTslot))

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


#endif /* YBCTID_H */
