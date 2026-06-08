-- Test MAP functions
-- These tests verify that the MAP functions are properly exposed and working

-- Test map_extract function
SELECT map_extract(r['map_col'], 'a') as extracted_value FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
SELECT map_extract(r['map_col'], 'd') as missing_key FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
SELECT map_extract(r['map_col'], 'key1') as string_value FROM duckdb.query($$ SELECT MAP(['key1', 'key2'], ['value1', 'value2']) as map_col $$) r;

-- Test map_keys function
SELECT map_keys(r['map_col']) as keys FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
SELECT map_keys(r['map_col']) as empty_keys FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;

-- Test map_values function
SELECT map_values(r['map_col']) as values FROM duckdb.query($$ SELECT MAP(['a', 'b', 'c'], [1, 2, 3]) as map_col $$) r;
SELECT map_values(r['map_col']) as empty_values FROM duckdb.query($$ SELECT MAP([], []) as map_col $$) r;

-- Test with unresolved_type for flexibility
SELECT map_extract(r['map_col']::duckdb.unresolved_type, 'x') as flexible_extract FROM duckdb.query($$ SELECT MAP(['x', 'y'], [10, 20]) as map_col $$) r;
SELECT map_keys(r['map_col']::duckdb.unresolved_type) as flexible_keys FROM duckdb.query($$ SELECT MAP(['p', 'q'], [100, 200]) as map_col $$) r;
SELECT map_values(r['map_col']::duckdb.unresolved_type) as flexible_values FROM duckdb.query($$ SELECT MAP(['m', 'n'], [1000, 2000]) as map_col $$) r;
