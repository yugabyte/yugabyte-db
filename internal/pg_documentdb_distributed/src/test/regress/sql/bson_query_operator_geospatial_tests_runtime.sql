SET search_path TO helio_api_catalog, postgis_public;
SET citus.next_shard_id TO 168300;
SET helio_api.next_collection_id TO 16830;
SET helio_api.next_collection_index_id TO 16830;

\set prevEcho :ECHO
\set ECHO none
\o /dev/null

SELECT helio_api.drop_collection('db', 'geoquerytest') IS NOT NULL;
SELECT helio_api.create_collection('db', 'geoquerytest') IS NOT NULL;

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
SELECT helio_api.shard_collection('db', 'geoquerytest', '{ "_id": "hashed" }', false);

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