-- PostgreSQL instance has data directory set to tmp_check/data so for all read functions argument
-- is relative to that data directory patch

-- read_parquet

SELECT count(r['sepal.length']) FROM read_parquet('../../data/iris.parquet') r;

SELECT r['sepal.length'] FROM read_parquet('../../data/iris.parquet') r ORDER BY r['sepal.length']  LIMIT 5;

SELECT r['sepal.length'], r['file_row_number'], r['filename']
    FROM read_parquet('../../data/iris.parquet', file_row_number => true, filename => true) r
    ORDER BY r['sepal.length'], r['file_row_number']  LIMIT 5;

-- Further subscripting is supported on duckdb.row
SELECT r['jsoncol'][1], r['arraycol'][2] FROM read_parquet('../../data/indexable.parquet') r;

-- And not just on duckdb.row, but also on duckdb.unresolved_type (the parenth
SELECT jsoncol[1], arraycol[2] FROM (
    SELECT r['jsoncol'] jsoncol, r['arraycol'] arraycol
    FROM read_parquet('../../data/indexable.parquet') r
) q;

-- And the same for slice subscripts
SELECT r['arraycol'][1:2] FROM read_parquet('../../data/indexable.parquet') r;
SELECT arraycol[1:2] FROM (
    SELECT r['arraycol'] arraycol
    FROM read_parquet('../../data/indexable.parquet') r
) q;
SELECT r['arraycol'][:] FROM read_parquet('../../data/indexable.parquet') r;
SELECT arraycol[:] FROM (
    SELECT r['arraycol'] arraycol
    FROM read_parquet('../../data/indexable.parquet') r
) q;

-- Subqueries correctly expand *, in case of multiple columns.
SELECT * FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q;

-- NOTE: A single "r" is equivalent to a *. The prefix and postfix columns are
-- not explicitly selected, but still show up in the result. This is
-- considered a bug, but it's one that we cannot easily solve because the "r"
-- reference does not exist in the DuckDB query at all, so there's no way to
-- reference only the columns coming from that part of the subquery. Very few
-- people should be impacted by this though.
SELECT r FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q;

-- ... but if you manually add the expected columns then they are merged into
-- the new star expansion.
SELECT prefix, r FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q;
SELECT prefix, r, postfix FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q;

-- This requires the prefix columns to be there though. If the prefix columns
-- are not there the postfix columns don't get merged into the new star
-- expansion automatically.
-- NOTE: Would be nice to fix this, but it's not a high priority.
SELECT r, postfix FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q;

-- If you swap them around, they will get duplicated though. For the
SELECT postfix, r, prefix FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q;

-- Joining two subqueries a single * works as expected.
SELECT *
FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r
    LIMIT 1
) q1, (
    SELECT * FROM read_parquet('../../data/unsigned_types.parquet') r
) q2;

-- Combining multiple read_parquet calls in a single query also works as
-- expected. They are not expanded to multiple *'s.
-- BUG: This currently doesn't work correctly!
SELECT 'something' as prefix, *, 'something else' as postfix
FROM read_parquet('../../data/iris.parquet') r,
        read_parquet('../../data/unsigned_types.parquet') r2
LIMIT 1;

-- And also when done in a subquery
-- BUG: Broken in the same way as the above query.
SELECT * FROM (
    SELECT 'something' as prefix, *, 'something else' as postfix
    FROM read_parquet('../../data/iris.parquet') r,
         read_parquet('../../data/unsigned_types.parquet') r2
    LIMIT 1
) q;

-- CTEs work fine, both materialized and not materialized ones.
with experiences as (
  select
    r['starts_at'],
    r['company']
  from duckdb.query($$ SELECT '2024-01-01'::date starts_at, 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

with experiences as materialized (
  select
    r['starts_at'],
    r['company']
  from duckdb.query($$ SELECT '2024-01-01'::date starts_at, 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

with experiences as not materialized (
  select
    r['starts_at'],
    r['company']
  from duckdb.query($$ SELECT '2024-01-01'::date starts_at, 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

-- ... also when using a single column
with experiences as (
  select
    r['company']
  from duckdb.query($$ SELECT 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

with experiences as materialized (
  select
    r['company']
  from duckdb.query($$ SELECT 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

with experiences as not materialized (
  select
    r['company']
  from duckdb.query($$ SELECT 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

-- ... and also when adding aliasses
with experiences as (
  select
    r['company'] company
  from duckdb.query($$ SELECT 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

with experiences as materialized (
  select
    r['company'] company
  from duckdb.query($$ SELECT 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;

with experiences as not materialized (
  select
    r['company'] company
  from duckdb.query($$ SELECT 'DuckDB Labs' company $$) r
  limit 100
)
select * from experiences;


-- We show a hint for the new syntax when someone uses the old syntax.
SELECT count("sepal.length") FROM read_parquet('../../data/iris.parquet') AS ("sepal.length" FLOAT);

-- But we don't show that hint for queries that don't use these functions.
SELECT count("sepal.length") FROM generate_series(1, 100) AS ("sepal.length" FLOAT);

-- Show a hint for users trying to use columns as normal instead of using r['column_name']
SELECT count("sepal.length") FROM read_parquet('../../data/iris.parquet');

-- But again only show it when we use functions that return duckdb.rows
SELECT count("sepal.length") FROM generate_series(1, 100) a(x);

-- read_csv

SELECT count(r['sepal.length']) FROM read_csv('../../data/iris.csv') r;

SELECT r['sepal.length'] FROM read_csv('../../data/iris.csv') r ORDER BY r['sepal.length'] LIMIT 5;

SELECT r['sepal.length'], r['filename']
    FROM read_csv('../../data/iris.csv', filename => true, header => true) r
    ORDER BY r['sepal.length']  LIMIT 5;

SELECT * FROM read_csv('../../non-existing-file.csv');

-- We override Postgres its default column name for subscript expressions. In
-- the following example the column would normally be named "r", which is
-- pretty non-descriptive especially when selecting multiple columns from the
-- same row.
--
-- NOTE: Jelte tried to change this behaviour in upstream Postgres, but met
-- with some resistance:
-- https://www.postgresql.org/message-id/flat/CAGECzQRYAFHLnjjymsSPhL-9OExVyNfMQkZMc1hcoUQ6dDHo=Q@mail.gmail.com
SELECT r['column00'] FROM read_csv('../../data/web_page.csv') r limit 1;

-- If an explicit column name is given we
SELECT r['column00'] AS col1 FROM read_csv('../../data/web_page.csv') r limit 1;

-- ...except if that explicit column name is the same as the default column
-- name. In which case we fail to recognize that fact.
-- XXX: It would be nice to fix this, but it's not a high priority.
SELECT r['column00'] AS r FROM read_csv('../../data/web_page.csv') r limit 1;

-- If we use the same trick inside subqueries, then references to columns from
-- that subquery would not use that better name, and thus the query could not
-- be executed. To avoid that we simply don't rename a subscript expression
-- inside a subquery, and only do so in the outermost SELECT list (aka
-- targetlist).
SELECT * FROM (SELECT r['column00'] FROM read_csv('../../data/web_page.csv') r limit 1) q;

-- If you give it a different alias then that alias is propegated though.
SELECT * FROM (SELECT r['column00'] AS col1 FROM read_csv('../../data/web_page.csv') r limit 1) q;

-- Only simple string literals are supported as column names
SELECT r[NULL] FROM read_csv('../../data/web_page.csv') r limit 1;
SELECT r[123] FROM read_csv('../../data/web_page.csv') r limit 1;
SELECT r[3.14] FROM read_csv('../../data/web_page.csv') r limit 1;
SELECT r['abc':'abc'] FROM read_csv('../../data/web_page.csv') r limit 1;
SELECT r[:] FROM read_csv('../../data/web_page.csv') r limit 1;
SELECT r[q.col]
FROM
    read_csv('../../data/web_page.csv') r,
    (SELECT 'abc'::text as col) q
LIMIT 1;

-- delta_scan

SELECT duckdb.install_extension('delta');

SELECT count(r['a']) FROM delta_scan('../../data/delta_table') r;
SELECT * FROM delta_scan('../../data/delta_table') r WHERE (r['a'] = 1 OR r['b'] = 'delta_table_3');


-- iceberg_*

SELECT duckdb.install_extension('iceberg');

SELECT COUNT(r['l_orderkey']) FROM iceberg_scan('../../data/lineitem_iceberg', allow_moved_paths => true) r;

-- TPCH query #6
SELECT
	sum(r['l_extendedprice'] * r['l_discount']) as revenue
FROM
	iceberg_scan('../../data/lineitem_iceberg', allow_moved_paths => true) r
WHERE
	r['l_shipdate'] >= date '1997-01-01'
	AND r['l_shipdate'] < date '1997-01-01' + interval '1' year
	AND r['l_discount'] between 0.08 - 0.01 and 0.08 + 0.01
	AND r['l_quantity'] < 25
LIMIT 1;

SELECT * FROM iceberg_snapshots('../../data/lineitem_iceberg') ORDER BY sequence_number;
SELECT * FROM iceberg_metadata('../../data/lineitem_iceberg',  allow_moved_paths => true);

-- read_json

SELECT COUNT(r['a']) FROM read_json('../../data/table.json') r;
SELECT COUNT(r['a']) FROM read_json('../../data/table.json') r WHERE r['c'] > 50.4;
SELECT r['a'], r['b'], r['c'] FROM read_json('../../data/table.json') r WHERE r['c'] > 50.4 AND r['c'] < 51.2;
