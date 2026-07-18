CREATE FUNCTION documentdb_extended_rumhandler(internal)
RETURNS index_am_handler
AS 'MODULE_PATHNAME'
LANGUAGE C;

/*
 * RUM access method
 */

CREATE SCHEMA documentdb_extended_rum_catalog;
CREATE ACCESS METHOD documentdb_extended_rum TYPE INDEX HANDLER documentdb_extended_rumhandler;

#include "pg_documentdb/sql/schema/single_path_operator_class--0.10-0.sql"
#include "pg_documentdb/sql/schema/composite_path_operator_class--0.103-0.sql"

-- register order by functions
ALTER OPERATOR FAMILY documentdb_extended_rum_catalog.bson_extended_rum_composite_path_ops USING documentdb_extended_rum ADD FUNCTION 8 (documentdb_core.bson)documentdb_api_internal.bson_rum_composite_ordering(bytea, documentdb_core.bson, int2, internal);
ALTER OPERATOR FAMILY documentdb_extended_rum_catalog.bson_extended_rum_composite_path_ops USING documentdb_extended_rum ADD OPERATOR 21 documentdb_api_catalog.|-<>(documentdb_core.bson, documentdb_core.bson) FOR ORDER BY documentdb_core.bson_btree_ops;

GRANT USAGE ON SCHEMA documentdb_extended_rum_catalog TO documentdb_admin_role;
GRANT USAGE ON SCHEMA documentdb_extended_rum_catalog TO documentdb_readonly_role;