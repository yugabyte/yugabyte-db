SET search_path TO helio_api;

 /*
 * Region: Index operator classes
 */
#include "udfs/rum/wildcard_project_path_extensibility_functions--1.12-0.sql"
#include "pg_documentdb/sql/schema/wildcard_project_path_operator_class--0.12-0.sql"
 
/*
 * Region: Index preconsistent support
 */
-- TODO: Reenable this once we're able.
-- #include "pg_documentdb/sql/schema/index_operator_classes_preconsistent--0.12-0.sql"

/*
* Commands propagated to helio API surface.
*/
#include "udfs/commands_crud/insert--1.12-0.sql"
#include "udfs/commands_crud/insert_one_helper--1.12-0.sql"
#include "udfs/commands_crud/update--1.12-0.sql"
#include "udfs/commands_crud/delete--1.12-0.sql"
#include "udfs/commands_crud/find_and_modify--1.12-0.sql"
#include "udfs/commands_crud/query_cursors_aggregate--1.12-0.sql"
#include "udfs/schema_mgmt/create_collection_view--1.12-0.sql"
#include "udfs/schema_mgmt/coll_mod--1.12-0.sql"
#include "udfs/schema_mgmt/shard_collection--1.12-0.sql"
#include "udfs/schema_mgmt/create_collection--1.12-0.sql"

/*  
 * Cursor management.
 */
#include "udfs/commands_crud/cursor_functions--1.12-0.sql"

 /*
 * Region: Create Index in Background
 */
#include "udfs/index_mgmt/create_index_background--1.12-0.sql"


/*
 * Region: Schema Management APIs
 */
#include "udfs/schema_mgmt/drop_collection--1.12-0.sql"

/*
 * Region: Aggregation operators.
 */
 #include "udfs/aggregation/group_aggregates_support--1.12-0.sql"

/*
 * Region: Index Manangement APIs
 */
#include "udfs/index_mgmt/drop_indexes--1.12-0.sql"

RESET search_path;
