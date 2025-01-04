SET search_path TO helio_api;

#include "pg_documentdb/sql/udfs/query/bson_dollar_comparison--0.23-0.sql"
#include "pg_documentdb/sql/operators/bson_path_operators--0.23-0.sql"
#include "pg_documentdb/sql/schema/index_operator_classes_range--0.23-0.sql"
#include "pg_documentdb/sql/udfs/aggregation/bson_lookup_functions--0.23-0.sql"
#include "pg_documentdb/sql/schema/collection_indexes_metadata--0.23-0.sql"
#include "pg_documentdb/sql/udfs/metadata/collection_metadata_functions--0.23-0.sql"

#include "pg_documentdb/sql/udfs/rum/bson_rum_exclusion_functions--0.23-0.sql"
#include "pg_documentdb/sql/operators/shard_key_and_document_operators--0.23-0.sql"
#include "pg_documentdb/sql/schema/bson_rum_exclusion_operator_class--0.23-0.sql"

#include "pg_documentdb/sql/udfs/rum/bson_rum_hashed_ops_functions--0.23-0.sql"
#include "pg_documentdb/sql/schema/bson_hash_operator_class--0.23-0.sql"
#include "pg_documentdb/sql/udfs/index_mgmt/create_index_background--0.23-0.sql"
#include "pg_documentdb/sql/schema/collection_metadata--0.23-0.sql"
RESET search_path;
