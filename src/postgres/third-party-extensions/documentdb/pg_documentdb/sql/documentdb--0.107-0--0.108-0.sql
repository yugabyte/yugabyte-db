#include "udfs/commands_crud/bson_update_document--0.108-0.sql"
#include "udfs/index_mgmt/create_index_background--0.108-0.sql"
/* YB: rum is not supported.
#include "udfs/rum/bson_rum_text_path_adapter_funcs--0.24-0.sql"
*/
#include "udfs/commands_crud/command_node_worker--0.108-0.sql"

GRANT UPDATE (indisvalid) ON pg_catalog.pg_index to __API_ADMIN_ROLE__;

-- update operator restrict function for $range.
ALTER OPERATOR __API_CATALOG_SCHEMA__.@<>(__CORE_SCHEMA__.bson, __CORE_SCHEMA__.bson) SET (RESTRICT = __API_SCHEMA_INTERNAL__.bson_dollar_selectivity);

/* YB: rum is not supported.
-- if there's existing installations that were created before 0.108 we need to update the operator class to not depend on rum directly.
DO LANGUAGE plpgsql $cmd$
DECLARE text_ops_op_family oid;
BEGIN
    SELECT opf.oid INTO text_ops_op_family FROM pg_opfamily opf JOIN pg_namespace pgn ON opf.opfnamespace = pgn.oid where opf.opfname = 'bson_rum_text_path_ops' AND pgn.nspname = 'documentdb_api_catalog';

    UPDATE pg_amproc SET amproc = 'documentdb_api_internal.rum_extract_tsquery'::regproc WHERE amprocfamily = text_ops_op_family AND amprocnum = 3;
    UPDATE pg_amproc SET amproc = 'documentdb_api_internal.rum_tsquery_consistent'::regproc WHERE amprocfamily = text_ops_op_family AND amprocnum = 4;
    UPDATE pg_amproc SET amproc = 'documentdb_api_internal.rum_tsvector_config'::regproc WHERE amprocfamily = text_ops_op_family AND amprocnum = 6;
    UPDATE pg_amproc SET amproc = 'documentdb_api_internal.rum_tsquery_pre_consistent'::regproc WHERE amprocfamily = text_ops_op_family AND amprocnum = 7;
    UPDATE pg_amproc SET amproc = 'documentdb_api_internal.rum_tsquery_distance'::regproc WHERE amprocfamily = text_ops_op_family AND amprocnum = 8;
    UPDATE pg_amproc SET amproc = 'documentdb_api_internal.rum_ts_join_pos'::regproc WHERE amprocfamily = text_ops_op_family AND amprocnum = 10;
END;
$cmd$;
*/
