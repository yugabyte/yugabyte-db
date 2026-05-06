-- Test MAP functions
SET duckdb.force_execution = false;

-- Basic functions
SELECT cardinality(r['map_col']) as cardinality_result FROM duckdb.query($$ SELECT MAP(['key1', 'key2', 'key3'], ['value1', 'value2', 'value3']) as map_col $$) r;
SELECT element_at(r['map_col'], 'key1') as element_at_result FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_concat(r1['map1'], r2['map2']) as map_concat_result FROM duckdb.query($$ SELECT MAP(['a', 'b'], [1, 2]) as map1 $$) r1, duckdb.query($$ SELECT MAP(['b', 'c'], [3, 4]) as map2 $$) r2;

-- Contains functions
SELECT map_contains(r['map_col'], 'key1') as contains_key_true FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_contains(r['map_col'], 'key3') as contains_key_false FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_contains_entry(r['map_col'], 'key1', 'value1') as contains_entry_true FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_contains_entry(r['map_col'], 'key1', 'value2') as contains_entry_false FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_contains_value(r['map_col'], 'value1') as contains_value_true FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_contains_value(r['map_col'], 'value3') as contains_value_false FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;

-- Extract functions
SELECT map_extract(r['map_col'], 'key1') as map_extract_result FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_extract_value(r['map_col'], 'key1') as map_extract_value_result FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;
SELECT map_extract_value(r['map_col'], 'key3') as map_extract_value_null FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;

-- Keys and values
SELECT map_from_entries(r['entries']) as map_from_entries_result FROM duckdb.query($$ SELECT [{'k': 'a', 'v': 1}, {'k': 'b', 'v': 2}] as entries $$) r;
SELECT map_keys(r['map_col']) as map_keys_result FROM duckdb.query($$ SELECT MAP(['key1', 'key2', 'key3'], ['value1', 'value2', 'value3']) as map_col $$) r;
SELECT map_values(r['map_col']) as map_values_result FROM duckdb.query($$ SELECT MAP(['key1', 'key2', 'key3'], ['value1', 'value2', 'value3']) as map_col $$) r;

-- Different data types
SELECT cardinality(r['map_col']) as cardinality_numeric_keys FROM duckdb.query($$ SELECT MAP([1, 2, 3], ['a', 'b', 'c']) as map_col $$) r;
SELECT map_contains(r['map_col'], 1) as contains_numeric_key FROM duckdb.query($$ SELECT MAP([1, 2, 3], ['a', 'b', 'c']) as map_col $$) r;

-- Edge cases
SELECT cardinality(r['map_col']) as cardinality_empty FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;
SELECT map_keys(r['map_col']) as keys_empty FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;
SELECT map_values(r['map_col']) as values_empty FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;
SELECT map_extract(r['map_col'], 'non_existent') as extract_non_existent FROM duckdb.query($$ SELECT MAP(['key1'], ['value1']) as map_col $$) r;
SELECT map_contains(r['map_col'], 'non_existent') as contains_non_existent FROM duckdb.query($$ SELECT MAP(['key1'], ['value1']) as map_col $$) r;
