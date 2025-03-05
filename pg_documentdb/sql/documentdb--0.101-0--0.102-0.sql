#include "udfs/metadata/list_databases--0.102-0.sql"
#include "udfs/ttl/ttl_support_functions--0.102-0.sql"
#include "udfs/commands_diagnostic/current_op--0.102-0.sql"
#include "udfs/query/bson_query_match--0.102-0.sql"
#include "udfs/aggregation/bson_aggregation_redact--0.102-0.sql"
#include "udfs/aggregation/bson_merge_functions--0.102-0.sql"

-- Schedule TTL Cron job to prune TTL indexes on every documentdb instance.
DO LANGUAGE plpgsql $cmd$
BEGIN
    IF NOT EXISTS(SELECT 1 FROM cron.job where jobname = 'documentdb_ttl_task') THEN
        PERFORM cron.schedule('documentdb_ttl_task', '* * * * *', $$CALL documentdb_api_internal.delete_expired_rows();$$);
    END IF;
END;
$cmd$;
