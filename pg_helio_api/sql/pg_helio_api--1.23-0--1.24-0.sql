SET search_path TO helio_api;
#include "pg_documentdb/sql/udfs/telemetry/command_feature_counter--0.24-0.sql"
#include "pg_documentdb/sql/udfs/commands_diagnostic/validate--0.24-0.sql"
#include "pg_documentdb/sql/udfs/vector/bson_extract_vector--0.24-0.sql"
#include "pg_documentdb/sql/udfs/rum/bson_rum_shard_exclusion_functions--0.24-0.sql"
#include "operators/bson_unique_shard_path_operators--1.24-0.sql"
#include "operators/bson_path_operators--1.24-0.sql"
#include "schema/unique_shard_path_operator_class--1.24-0.sql"
#include "pg_documentdb/sql/udfs/query/bson_dollar_text--0.24-0.sql"
#include "pg_documentdb/sql/udfs/rum/bson_rum_text_path_funcs--0.24-0.sql"
#include "pg_documentdb/sql/operators/bson_dollar_text_operators--0.24-0.sql"
#include "pg_documentdb/sql/udfs/ttl/ttl_support_functions--0.24-0.sql"
#include "pg_documentdb/sql/schema/bson_rum_text_path_ops--0.24-0.sql"
#include "pg_documentdb/sql/udfs/commands_diagnostic/db_stats--0.24-0.sql"
#include "pg_documentdb/sql/udfs/commands_diagnostic/index_stats--0.24-0.sql"
#include "pg_documentdb/sql/udfs/aggregation/bson_coercion_compat--0.24-0.sql"
#include "pg_documentdb/sql/udfs/projection/bson_projection--0.24-0.sql"

#include "pg_documentdb/sql/udfs/schema_validation/schema_validation--0.24-0.sql"
#include "pg_documentdb/sql/udfs/schema_mgmt/shard_collection--0.24-0.sql"

RESET search_path;
