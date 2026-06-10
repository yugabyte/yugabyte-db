-- We should be able to use duckdb-only functions even when
-- duckdb.force_execution is turned off
SET duckdb.force_execution = false;

\set pwd `pwd`

select r['usmallint'] from read_parquet(:'pwd' || '/data/unsigned_types.parquet') r;
select r['usmallint'] from read_parquet(ARRAY[:'pwd' || '/data/unsigned_types.parquet']) r;

select r['column00'] from read_csv(:'pwd' || '/data/web_page.csv') r LIMIT 2;
select r['column00'] from read_csv(ARRAY[:'pwd' || '/data/web_page.csv']) r LIMIT 2;

-- TODO: Add a test for scan_iceberg once we have a test table
