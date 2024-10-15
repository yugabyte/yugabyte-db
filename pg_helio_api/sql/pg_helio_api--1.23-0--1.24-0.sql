SET search_path TO helio_api;
#include "udfs/telemetry/command_feature_counter--1.24-0.sql"
#include "udfs/commands_diagnostic/validate--1.24-0.sql"
#include "udfs/vector/bson_extract_vector--1.24-0.sql"
#include "udfs/query/bson_dollar_text--1.24-0.sql"
#include "udfs/rum/bson_rum_text_path_funcs--1.24-0.sql"
#include "operators/bson_dollar_text_operators--1.24-0.sql"
#include "udfs/ttl/ttl_support_functions--1.24-0.sql"
#include "schema/bson_rum_text_path_ops--1.24-0.sql"
#include "udfs/aggregation/bson_coercion_compat--1.24-0.sql"

RESET search_path;
