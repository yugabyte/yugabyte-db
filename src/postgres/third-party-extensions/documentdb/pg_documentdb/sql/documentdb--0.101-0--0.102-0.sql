#include "udfs/metadata/list_databases--0.102-0.sql"
#include "udfs/ttl/ttl_support_functions--0.102-0.sql"
#include "udfs/commands_diagnostic/current_op--0.102-0.sql"
#include "udfs/query/bson_query_match--0.102-0.sql"
#include "udfs/projection/bson_projection--0.102-0.sql"
#include "udfs/aggregation/bson_aggregation_redact--0.102-0.sql"
#include "udfs/aggregation/bson_merge_functions--0.102-0.sql"

#include "udfs/aggregation/group_aggregates_support--0.102-0.sql"
#include "udfs/aggregation/group_aggregates--0.102-0.sql"

#include "types/bsonindexbounds--0.102-0.sql"
#include "udfs/index_mgmt/bson_indexbounds_functions--0.102-0.sql"
#include "operators/bson_btree_pfe_operators--0.102-0.sql"
#include "operators/bsonindexbounds_btree_pfe_family--0.102-0.sql"

-- Schedule TTL Cron job to prune TTL indexes on every documentdb instance.
DO LANGUAGE plpgsql $cmd$
BEGIN
    IF NOT EXISTS(SELECT 1 FROM cron.job where jobname = 'documentdb_ttl_task') THEN
        PERFORM cron.schedule('documentdb_ttl_task', '* * * * *', $$CALL documentdb_api_internal.delete_expired_rows();$$);
    END IF;
END;
$cmd$;

DROP AGGREGATE IF EXISTS __API_CATALOG_SCHEMA__.BSON_OUT(__CORE_SCHEMA__.bson, text, text, text, text);
DROP FUNCTION IF EXISTS __API_CATALOG_SCHEMA__.bson_out_transition;
DROP FUNCTION IF EXISTS __API_CATALOG_SCHEMA__.bson_out_final;

-- ALTER creation_time column for all the documentdb tables
#include "udfs/schema_mgmt/data_table_upgrade--0.102-0.sql"
SELECT __API_SCHEMA_INTERNAL_V2__.apply_extension_data_table_upgrade(0, 102, 0);
