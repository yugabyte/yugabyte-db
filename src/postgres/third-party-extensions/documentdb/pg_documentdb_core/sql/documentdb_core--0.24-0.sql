
CREATE SCHEMA documentdb_core;


/*
 * Region: BSON Type and IO
 */
#include "types/bson--0.10-0.sql"
#include "types/bson--0.16-0.sql"
#include "types/bsonsequence--0.10-0.sql"

#include "udfs/bson_io/bson_io--0.11-0.sql"
#include "udfs/bsonsequence_io/bsonsequence_io--0.10-0.sql"

#include "types/bsonquery--0.10-0.sql"

/*
 * Region: Planner support functions
 */
#include "udfs/planner/bson_selectivity--0.10-0.sql"

/*
 * Region: Bson utility operators
 */
#include "operators/bson_get_value_operators--0.10-0.sql"

/*
 * Region: Bson BTree Operator Class
 */
#include "udfs/bson_btree/bson_btree--0.19-0.sql"
#include "operators/bson_btree_operators--0.10-0.sql"

#include "udfs/bsonquery_btree/bsonquery_btree--0.10-0.sql"
#include "operators/bsonquery_btree--0.10-0.sql"
#include "schema/btree_operator_class--0.10-0.sql"
#include "schema/btree_opclass_members--0.19-0.sql"

/*
 * Region: Bson Hash Operator Class
 */
#include "udfs/bson_hash/bson_hash_functions--0.15-0.sql"
#include "schema/bson_hash_operator_class--0.15-0.sql"
