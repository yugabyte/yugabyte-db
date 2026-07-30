-- version checks need this for secondary users
GRANT ALL ON TABLE documentdb_api_distributed.documentdb_cluster_data TO __API_ADMIN_ROLE__;
GRANT SELECT ON TABLE documentdb_api_distributed.documentdb_cluster_data TO __API_READONLY_ROLE__;

#include "udfs/operations/move_collection--0.109-0.sql"