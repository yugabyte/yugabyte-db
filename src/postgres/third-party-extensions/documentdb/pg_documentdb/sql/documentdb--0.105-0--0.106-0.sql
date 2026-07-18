#include "udfs/commands_crud/bson_update_document--0.106-0.sql"
#include "udfs/ttl/delete_expired_rows_background--0.106-0.sql"
#include "udfs/index_mgmt/build_index_background--0.106-0.sql"
#include "schema/setup_background_worker_role--0.106-0.sql"

#include "operators/bson_dollar_operators--0.106-0.sql"
/* YB: rum is not supported.
#include "udfs/index_mgmt/bson_rum_ordering_functions--0.106-0.sql"
*/

#include "udfs/metadata/collection--0.106-0.sql"
#include "udfs/metadata/empty_data_table--0.106-0.sql"

#include "udfs/roles/create_role--0.106-0.sql"
#include "udfs/roles/drop_role--0.106-0.sql"
#include "udfs/roles/roles_info--0.106-0.sql"
#include "udfs/roles/update_role--0.106-0.sql"
#include "udfs/aggregation/group_aggregates_support--0.106-0.sql"
#include "udfs/aggregation/group_aggregates--0.106-0.sql"

#include "udfs/query/bson_dollar_index_hints--0.106-0.sql"