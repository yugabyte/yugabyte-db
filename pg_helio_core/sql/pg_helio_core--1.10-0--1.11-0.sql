
SET search_path TO helio_core;

/*
 * Region: BSON Type and IO
 */
 #include "udfs/bson_io/bson_io--1.11-0.sql"

/*
 * Region: Bson BTree Operator Class
 */
 #include "udfs/bson_btree/bson_btree--1.11-0.sql"

RESET search_path;
