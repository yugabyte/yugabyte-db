#include "udfs/query/bson_orderby--0.104-0.sql"
#include "operators/bson_orderby_operators--0.104-0.sql"
#include "operators/bson_btree_orderby_operators_family--0.104-0.sql"
#include "schema/bson_orderby_hash_operator_class--0.104-0.sql"

#include "udfs/projection/bson_projection--0.104-0.sql"
#include "udfs/index_mgmt/create_index_background--0.104-0.sql"
#include "udfs/schema_mgmt/compact--0.104-0.sql"

#include "udfs/schema_mgmt/cursor_support--0.104-0.sql"

-- Schedule the index build task
DO LANGUAGE plpgsql $cmd$
BEGIN
    PERFORM __API_SCHEMA_INTERNAL__.schedule_background_index_build_jobs();
END;
$cmd$;

DO LANGUAGE plpgsql $cmd$
BEGIN
    IF NOT EXISTS(SELECT 1 FROM cron.job where jobname = 'documentdb_cursor_cleanup_task') THEN
        PERFORM cron.schedule('documentdb_cursor_cleanup_task', '* * * * *', $$CALL documentdb_api_internal.cursor_directory_cleanup();$$);
    END IF;
END;
$cmd$;
