-- Drop the background index queue and recreate it using the new UDF to provide an option to skip the creation of it.
DROP TABLE IF EXISTS __API_CATALOG_SCHEMA__.__EXTENSION_OBJECT__(_index_queue);

#include "../udfs/index_mgmt/setup_index_queue--0.109-0.sql"

SELECT documentdb_api_internal.setup_index_queue_table(0, 109, 0);
