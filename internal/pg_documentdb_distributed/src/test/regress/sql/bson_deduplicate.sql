
SET search_path TO helio_api,helio_api_internal,helio_core;
SET citus.next_shard_id TO 4600000;
SET helio_api.next_collection_id TO 4600;
SET helio_api.next_collection_index_id TO 4600;

SELECT helio_api_internal.bson_deduplicate_fields(null);

SELECT helio_api_internal.bson_deduplicate_fields('{}') = '{}';

SELECT helio_api_internal.bson_deduplicate_fields('{"a":1, "a": 1}') = '{"a":1}';
SELECT helio_api_internal.bson_deduplicate_fields('{"a":1, "a": 1}') = '{"a":1}';

SELECT helio_api_internal.bson_deduplicate_fields('{"a": 1, "b": [{"c": 1, "c": 2}, {"c": {"e": 1, "z": [], "e": 2}}], "a": null}') =
                                                  '{"a": null, "b" : [{"c": 2}, {"c": {"e": 2, "z": []}}]}';
SELECT helio_api_internal.bson_deduplicate_fields('{"a": 1, "b": [{"c": 1, "c": [1, 1, "text", {"d": 1, "d": 2}]}, {"c": {"e": 1, "e": 2}}], "a": 2}') =
                                                  '{"a": 2, "b": [{"c": [1, 1, "text", {"d": 2}]}, {"c": {"e": 2}}]}';
SELECT helio_api_internal.bson_deduplicate_fields('{"a": 1, "a.b": 2, "a.b.c": "text"}') = '{"a": 1, "a.b": 2, "a.b.c": "text"}';
