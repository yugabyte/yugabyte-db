
CREATE SCHEMA helio_api_distributed;
GRANT USAGE ON SCHEMA helio_api_distributed TO helio_admin_role;
GRANT USAGE ON SCHEMA helio_api_distributed TO helio_readonly_role;
SET search_path TO helio_api_distributed;
CREATE TABLE helio_api_distributed.helio_cluster_data
(
    metadata helio_core.bson
);
-- seed the table with a baseline version.
INSERT INTO helio_api_distributed.helio_cluster_data (metadata) VALUES ( '{ "last_deploy_version": "1.0-0" }'::helio_core.bson );

#include "internal/pg_documentdb_distributed/sql/udfs/clustermgmt/cluster_operations--0.21-0.sql"
#include "internal/pg_documentdb_distributed/sql/udfs/clustermgmt/cluster_version_utils--0.21-0.sql"

RESET search_path;
