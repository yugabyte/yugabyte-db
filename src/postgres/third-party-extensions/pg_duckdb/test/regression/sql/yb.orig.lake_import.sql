-- YB lake_io import: parquet/CSV/JSON -> YugabyteDB tables via CTAS. See pgduckdb_guc.hpp.
\getenv abs_srcdir PG_ABS_SRCDIR
\set parq :abs_srcdir '/data/iris.parquet'
\set csv :abs_srcdir '/data/iris.csv'
\set js :abs_srcdir '/data/table.json'

-- Import a local Parquet file into a YugabyteDB table via CTAS.
CREATE TABLE yb_iris_parquet AS
  SELECT r['sepal.length']::float8 AS sepal_length, r['variety']::text AS variety
  FROM read_parquet(:'parq') AS r;
SELECT count(*) AS parquet_rows FROM yb_iris_parquet;
SELECT variety, count(*) AS n FROM yb_iris_parquet GROUP BY variety ORDER BY variety;
SELECT round(avg(sepal_length)::numeric, 4) AS avg_sepal_length FROM yb_iris_parquet;

-- Import a local CSV file the same way.
CREATE TABLE yb_iris_csv AS
  SELECT r['sepal.length']::float8 AS sepal_length, r['variety']::text AS variety
  FROM read_csv(:'csv') AS r;
SELECT count(*) AS csv_rows FROM yb_iris_csv;
SELECT variety, count(*) AS n FROM yb_iris_csv GROUP BY variety ORDER BY variety;
SELECT round(avg(sepal_length)::numeric, 4) AS avg_sepal_length FROM yb_iris_csv;

-- Import a local JSON file the same way.
CREATE TABLE yb_json AS
  SELECT r['a']::text AS a, r['b']::text AS b, r['c']::float8 AS c
  FROM read_json(:'js') AS r;
SELECT count(*) AS json_rows FROM yb_json;
SELECT a, b, c FROM yb_json ORDER BY a::int LIMIT 3;
SELECT round(avg(c)::numeric, 4) AS avg_c FROM yb_json;

-- Cleanup
DROP TABLE yb_iris_parquet;
DROP TABLE yb_iris_csv;
DROP TABLE yb_json;
