SET search_path TO helio_api;

 /*
 * Region: Index operator classes
 */
#include "udfs/rum/wildcard_project_path_extensibility_functions--0.12-0.sql"
#include "schema/wildcard_project_path_operator_class--0.12-0.sql"
 
/*
 * Region: Index preconsistent support
 */
-- TODO: Reenable this once we're able.
-- #include "schema/index_operator_classes_preconsistent--0.12-0.sql"

/*
* Commands propagated to helio API surface.
*/
#include "udfs/commands_crud/insert--0.12-0.sql"
#include "udfs/commands_crud/insert_one_helper--0.12-0.sql"
#include "udfs/commands_crud/update--0.12-0.sql"
#include "udfs/commands_crud/delete--0.12-0.sql"
#include "udfs/commands_crud/find_and_modify--0.12-0.sql"
#include "udfs/commands_crud/query_cursors_aggregate--0.12-0.sql"
#include "udfs/schema_mgmt/create_collection_view--0.12-0.sql"
#include "udfs/schema_mgmt/coll_mod--0.12-0.sql"
#include "udfs/schema_mgmt/shard_collection--0.12-0.sql"
#include "udfs/schema_mgmt/create_collection--0.12-0.sql"

/*  
 * Cursor management.
 */
#include "udfs/commands_crud/cursor_functions--0.12-0.sql"

 /*
 * Region: Create Index in Background
 */
#include "udfs/index_mgmt/create_index_background--0.12-0.sql"


/*
 * Region: Schema Management APIs
 */
#include "udfs/schema_mgmt/drop_collection--0.12-0.sql"


/*
 * Region: Index Manangement APIs
 */
#include "udfs/index_mgmt/drop_indexes--0.12-0.sql"

RESET search_path;
