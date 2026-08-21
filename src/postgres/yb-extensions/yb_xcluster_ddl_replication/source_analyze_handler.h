/*-----------------------------------------------------------------------------
 * Copyright (c) YugabyteDB, Inc.
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
 *
 *-----------------------------------------------------------------------------
 */

#ifndef YB_XCLUSTER_DDL_REPLICATION_SOURCE_ANALYZE
#define YB_XCLUSTER_DDL_REPLICATION_SOURCE_ANALYZE

#include "postgres.h"

/*
 * Returns whether the statistics of the just analyzed relation are worth
 * replicating to the target universe. Temporary relations, system catalogs and
 * the extension's own tables are skipped.
 */
extern bool ShouldReplicateAnalyzedRelation(Oid relid);

/*
 * Builds the SQL statement that applies, on the target, the statistics that
 * ANALYZE just computed on the source for relid and its indexes.
 *
 * The statement is a set of pg_restore_relation_stats / pg_restore_attribute_stats
 * calls wrapped in a DO block, so that the target can simply execute it without
 * having to redo any of the sampling work.
 *
 * Extended statistics (CREATE STATISTICS) are not covered: Postgres has no
 * import function for them, so the target keeps whatever it computed itself.
 *
 * Returns NULL if the relation has no statistics to replicate. Must be called
 * with an active SPI connection; the result is allocated in the SPI context.
 */
extern char *BuildRestoreStatsQuery(Oid relid);

#endif
