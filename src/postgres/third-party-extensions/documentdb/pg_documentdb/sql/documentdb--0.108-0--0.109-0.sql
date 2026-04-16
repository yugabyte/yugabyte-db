
#include "udfs/query/bson_dollar_evaluation--0.109-0.sql"
#include "udfs/commands_diagnostic/kill_op--0.109-0.sql"
#include "udfs/aggregation/group_aggregates_support--0.109-0.sql"
#include "udfs/aggregation/group_aggregates--0.109-0.sql"
/* YB: rum is not supported.
#include "udfs/rum/bson_rum_shard_exclusion_functions--0.109-0.sql"
#include "schema/unique_shard_path_operator_class--0.109-0.sql"
*/
#include "udfs/aggregation/bson_aggregation_getmore--0.109-0.sql"

#include "schema/background_index_queue--0.109-0.sql"

#include "udfs/aggregation/bson_aggregation_redact--0.109-0.sql"
#include "udfs/projection/bson_projection--0.109-0.sql"
#include "udfs/projection/bson_expression--0.109-0.sql"
#include "udfs/query/bson_query_match--0.109-0.sql"

-- fix the return of gin_bson_compare which was created incorrectly.
UPDATE pg_proc SET prorettype = 'integer'::regtype WHERE proname = 'gin_bson_compare' AND pronamespace = 'documentdb_api_catalog'::regnamespace;

GRANT UPDATE (indisexclusion) ON pg_catalog.pg_index to __API_ADMIN_ROLE__;

GRANT pg_read_all_stats to __API_BG_WORKER_ROLE__;

-- Killop access
GRANT pg_signal_backend to __API_ADMIN_ROLE__;