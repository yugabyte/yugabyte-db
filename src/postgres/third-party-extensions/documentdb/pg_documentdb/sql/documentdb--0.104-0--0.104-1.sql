
#include "udfs/query/bson_dollar_comparison--0.104-1.sql"

-- update the job to use the correct syntax.
SELECT cron.alter_job(
    (SELECT jobid FROM cron.job WHERE jobname = 'documentdb_cursor_cleanup_task' LIMIT 1),
    command => 'SELECT documentdb_api_internal.cursor_directory_cleanup();');