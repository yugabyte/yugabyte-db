SET search_path TO helio_api;
#include "pg_documentdb/sql/udfs/users/create_user--0.19-0.sql"
#include "pg_documentdb/sql/udfs/users/drop_user--0.19-0.sql"
#include "pg_documentdb/sql/udfs/users/update_user--0.19-0.sql"
#include "pg_documentdb/sql/udfs/users/users_info--0.19-0.sql"
#include "udfs/aggregation/bson_merge_functions--1.19-0.sql"

#include "pg_documentdb/sql/udfs/query/bson_orderby--0.19-0.sql"
#include "udfs/projection/bson_expression--1.19-0.sql"

#include "udfs/projection/bson_projection--1.19-0.sql"
#include "pg_documentdb/sql/udfs/query/bson_dollar_evaluation--0.19-0.sql"
RESET search_path;
