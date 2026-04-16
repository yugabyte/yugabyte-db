/*-------------------------------------------------------------------------
 * Copyright (c) Microsoft Corporation.  All rights reserved.
 *
 * include/aggregation/aggregation_commands.h
 *
 * Exports for the aggregation external commands.
 *
 *-------------------------------------------------------------------------
 */

 #ifndef DOCUMENTDB_AGGREGATION_COMMANDS_H
 #define DOCUMENTDB_AGGREGATION_COMMANDS_H

#include <utils/array.h>

extern Datum delete_cursors(ArrayType *cursorArray);
extern Datum find_cursor_first_page(text *database, pgbson *findSpec, int64_t cursorId);
extern Datum aggregate_cursor_first_page(text *database, pgbson *aggregationSpec,
										 int64_t cursorId);
extern Datum aggregation_cursor_get_more(text *database, pgbson *getMoreSpec,
										 pgbson *cursorSpec, AttrNumber
										 maxResponseAttributeNumber);

extern Datum list_collections_first_page(text *database, pgbson *listCollectionsSpec);
extern Datum list_indexes_first_page(text *database, pgbson *listIndexesSpec);

 #endif
