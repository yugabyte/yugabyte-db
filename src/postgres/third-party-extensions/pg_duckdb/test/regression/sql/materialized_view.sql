CREATE TABLE t(a INT, b VARCHAR);
INSERT INTO t SELECT g % 100, MD5(g::VARCHAR) FROM generate_series(1,1000) g;
SELECT COUNT(*) FROM t WHERE a % 10 = 0;
CREATE MATERIALIZED VIEW tv AS SELECT * FROM t WHERE a % 10 = 0;
SELECT COUNT(*) FROM tv;

INSERT INTO t SELECT g % 100, MD5(g::TEXT) FROM generate_series(1,1000) g;
SELECT COUNT(*) FROM t WHERE a % 10 = 0;

REFRESH MATERIALIZED VIEW tv;
SELECT COUNT(*) FROM tv;

SELECT COUNT(*) FROM t WHERE (a % 10 = 0) AND (a < 3);
SELECT COUNT(*) FROM tv WHERE a < 3;

DROP MATERIALIZED VIEW tv;
DROP TABLE t;

-- materialized view where duckdb execution changes result types of query
CREATE TABLE t_jsonb(data jsonb);
INSERT INTO t_jsonb VALUES ('{"a": 1, "b": 2}');
CREATE MATERIALIZED VIEW mv_json AS SELECT * FROM t_jsonb;
SELECT * from mv_json;
-- Should return json, because that's the return type of the query duckdb query
-- (since it does not have the jsonb type).
SELECT atttypid::regtype FROM pg_attribute WHERE attrelid = 'mv_json'::regclass AND attname = 'data';
INSERT INTO t_jsonb VALUES ('{"a": 3, "b": 4}');
REFRESH MATERIALIZED VIEW mv_json;
SELECT * from mv_json;
SET duckdb.force_execution = false;
INSERT INTO t_jsonb VALUES ('{"a": 5, "b": 6}');
REFRESH MATERIALIZED VIEW mv_json;
SET duckdb.force_execution = true;

DROP MATERIALIZED VIEW mv_json;

-- Materialized view created without duckdb execution, and then refresh with
-- duckdb execution enabled.
SET duckdb.force_execution = false;
CREATE MATERIALIZED VIEW mv_jsonb AS SELECT * FROM t_jsonb;
SELECT * from mv_jsonb;
SELECT atttypid::regtype FROM pg_attribute WHERE attrelid = 'mv_jsonb'::regclass AND attname = 'data';
REFRESH MATERIALIZED VIEW mv_jsonb;
SELECT * from mv_jsonb;
SET duckdb.force_execution = true;
REFRESH MATERIALIZED VIEW mv_jsonb;
SET duckdb.force_execution = false;
SELECT * from mv_jsonb;
SET duckdb.force_execution = true;
DROP MATERIALIZED VIEW mv_jsonb;

-- materialized view from duckdb execution

CREATE TABLE t_csv(a INT, b INT);
INSERT INTO t_csv VALUES (1,1),(2,2),(3,3);

\set pwd `pwd`
\set csv_file_path '\'' :pwd '/tmp_check/t_csv.csv'  '\''

COPY t_csv TO :csv_file_path (FORMAT CSV, HEADER TRUE, DELIMITER ',');

CREATE MATERIALIZED VIEW mv_csv AS SELECT * FROM read_csv(:csv_file_path);

SELECT COUNT(*) FROM mv_csv;
SELECT * FROM mv_csv;

INSERT INTO t_csv VALUES (4,4);
COPY t_csv TO :csv_file_path (FORMAT CSV, HEADER TRUE, DELIMITER ',');
REFRESH MATERIALIZED VIEW mv_csv;

SELECT COUNT(*) FROM mv_csv;
SELECT * FROM mv_csv;

DROP MATERIALIZED VIEW mv_csv;
DROP TABLE t_csv;
