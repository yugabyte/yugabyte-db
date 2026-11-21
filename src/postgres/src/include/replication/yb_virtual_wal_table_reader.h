/*--------------------------------------------------------------------------------------------------
 *
 * yb_virtual_wal_table_reader.h
 *        Reads the change stream of a replication slot from a table instead of the CDC service.
 *
 * Copyright (c) YugabyteDB, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.  You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License
 * is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
 * or implied.  See the License for the specific language governing permissions and limitations
 * under the License.
 *
 * IDENTIFICATION
 *        src/postgres/src/include/replication/yb_virtual_wal_table_reader.h
 *
 *--------------------------------------------------------------------------------------------------
 */

#ifndef YB_VIRTUAL_WAL_TABLE_READER_H
#define YB_VIRTUAL_WAL_TABLE_READER_H

#include "c.h"

#include "access/xlogdefs.h"
#include "yb/yql/pggate/ybc_pggate.h"

/*
 * Start reading at the first commit group whose commit hybrid time is at least
 * resume_commit_time. Only rows of the tables in table_oids are returned.
 */
extern void YbTableReaderInit(const Oid *table_oids, int num_tables,
							  XLogRecPtr resume_commit_time);

extern void YbTableReaderDestroy(void);

/*
 * Returns the next batch of whole commit groups, each wrapped in BEGIN and
 * COMMIT records, allocated in the current memory context. The batch is empty
 * when nothing new is in the table. The batch holds the values of its records,
 * which are freed when the memory context it is allocated in is reset.
 */
extern YbcPgChangeRecordBatch *YbTableReaderGetChanges(void);

#endif							/* YB_VIRTUAL_WAL_TABLE_READER_H */
