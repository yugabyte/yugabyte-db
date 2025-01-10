SET search_path TO helio_api,helio_core,helio_api_internal;
SET helio_api.next_collection_id TO 5500;
SET helio_api.next_collection_index_id TO 5500;

\d helio_api_catalog.helio_index_queue;
SELECT helio_api.create_indexes_background('db', NULL);
SELECT helio_api.create_indexes_background('db', '{}');

