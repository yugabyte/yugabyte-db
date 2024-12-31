
CREATE SCHEMA helio_core;

SET search_path TO helio_core;


/*
 * Region: BSON Type and IO
 */
 #include "pg_documentdb_core/sql/types/bson--0.10-0.sql"
 #include "pg_documentdb_core/sql/types/bsonsequence--0.10-0.sql"
 #include "udfs/bson_io/bson_io--1.10-0.sql"
 #include "udfs/bsonsequence_io/bsonsequence_io--1.10-0.sql"
 #include "pg_documentdb_core/sql/types/bsonquery--0.10-0.sql"

/*
 * Region: Planner support functions
 */
 #include "udfs/planner/bson_selectivity--1.10-0.sql"

/*
 * Region: Bson utility operators
 */
 #include "pg_documentdb_core/sql/operators/bson_get_value_operators--0.10-0.sql"

/*
 * Region: Bson BTree Operator Class
 */
 #include "udfs/bson_btree/bson_btree--1.10-0.sql"
 #include "pg_documentdb_core/sql/operators/bson_btree_operators--0.10-0.sql"
 #include "udfs/bsonquery_btree/bsonquery_btree--1.10-0.sql"
 #include "pg_documentdb_core/sql/operators/bsonquery_btree--0.10-0.sql"
 #include "pg_documentdb_core/sql/schema/btree_operator_class--0.10-0.sql"


RESET search_path;
