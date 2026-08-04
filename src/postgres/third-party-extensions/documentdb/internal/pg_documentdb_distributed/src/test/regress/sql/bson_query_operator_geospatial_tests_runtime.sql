SET search_path TO documentdb_api_catalog, postgis_public;
SET citus.next_shard_id TO 168300;
SET documentdb.next_collection_id TO 16830;
SET documentdb.next_collection_index_id TO 16830;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT documentdb_api.drop_collection('db', 'geoquerytest') IS NOT NULL;
SELECT documentdb_api.create_collection('db', 'geoquerytest') IS NOT NULL;

\o
\set ECHO :prevEcho

-- Tests without sharding

-- Geometries
BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_geospatial_core.sql

ROLLBACK;

-- Geographies
BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_geospatial_geography_core.sql
ROLLBACK;


-- Again testing with shards
-- Shard the collection and run the tests
SELECT documentdb_api.shard_collection('db', 'geoquerytest', '{ "_id": "hashed" }', false);

-- Geometries
BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_geospatial_core.sql

ROLLBACK;

-- Geographies
BEGIN;
set local enable_seqscan TO on;
\i sql/bson_query_operator_geospatial_geography_core.sql
ROLLBACK;