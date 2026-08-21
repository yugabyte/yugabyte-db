-- All the queries below should work even if duckdb.force_execution is turned off.
SET duckdb.force_execution = false;
CREATE TEMP TABLE t(
    bool BOOLEAN,
    i2 SMALLINT,
    i4 INT DEFAULT 1,
    i8 BIGINT NOT NULL,
    fl4 REAL DEFAULT random() + 1,
    fl8 DOUBLE PRECISION CHECK(fl8 > 0),
    t1 TEXT,
    t2 VARCHAR,
    t3 BPCHAR,
    d DATE,
    ts TIMESTAMP,
    json_obj JSON,
    CHECK (i4 > i2)
) USING duckdb;


INSERT INTO t VALUES (true, 2, 4, 8, 4.0, 8.0, 't1', 't2', 't3', '2024-05-04', '2020-01-01T01:02:03', '{"a": 1}');
SELECT * FROM t;

CREATE TEMP TABLE t_heap (a int);
INSERT INTO t_heap VALUES (2);

SELECT * FROM t JOIN t_heap ON i2 = a;

-- The default_table_access_method GUC should be honored.
set default_table_access_method = 'duckdb';
CREATE TEMP TABLE t2(a int);

INSERT INTO t2 VALUES (1), (2), (3);
SELECT * FROM t2 ORDER BY a;

DELETE FROM t2 WHERE a = 2;
SELECT * FROM t2 ORDER BY a;

UPDATE t2 SET a = 5 WHERE a = 3;
SELECT * FROM t2 ORDER BY a;

TRUNCATE t2;
SELECT * FROM t2 ORDER BY a;

-- Writing to a DuckDB table in a transaction is allowed
BEGIN;
    INSERT INTO t2 VALUES (1), (2), (3);
END;

-- We should be able to run DuckDB DDL in transactions
BEGIN;
    CREATE TEMP TABLE t3(a int);
END;

BEGIN;
    DROP TABLE t3;
END;

-- Plain postgres DDL and queries should work fine too
BEGIN;
    CREATE TEMP TABLE t4(a int) USING heap;
    INSERT INTO t4 VALUES (1);
    SELECT * FROM t4;
    DROP TABLE t4;
END;

-- Even if duckdb.force_execution is turned on
BEGIN;
    SET LOCAL duckdb.force_execution = true;
    CREATE TEMP TABLE t4(a int) USING heap;
    INSERT INTO t4 VALUES (1);
    SELECT * FROM t4;
    DROP TABLE t4;
END;

-- ANALYZE should not fail on our tables. For now it doesn't do anything
-- though. But it should not fail, otherwise a plain "ANALYZE" of all tables
-- will error.
ANALYZE t;

SELECT duckdb.raw_query($$ SELECT database_name, schema_name, sql FROM duckdb_tables() $$);

-- Ensure that we can drop the table with all the supported features inside a
-- transaction.
BEGIN;
DROP TABLE t;
END;

DROP TABLE t_heap, t2;

SELECT duckdb.raw_query($$ SELECT database_name, schema_name, sql FROM duckdb_tables() $$);

CREATE TABLE t(a int);

-- XXX: A better error message would be nice here, but for now this is acceptable.
CREATE TEMP TABLE t(a int PRIMARY KEY);
-- XXX: A better error message would be nice here, but for now this is acceptable.
CREATE TEMP TABLE t(a int UNIQUE);
CREATE TEMP TABLE t(a int, b int GENERATED ALWAYS AS (a + 1) STORED);
CREATE TEMP TABLE t(a int GENERATED ALWAYS AS IDENTITY);
CREATE TEMP TABLE theap(b int PRIMARY KEY) USING heap;
CREATE TEMP TABLE t(a int REFERENCES theap(b));
DROP TABLE theap;
-- allowed but all other collations are not supported
CREATE TEMP TABLE t(a text COLLATE "default");
DROP TABLE t;
CREATE TEMP TABLE t(a text COLLATE "C");
DROP TABLE t;
CREATE TEMP TABLE t(a text COLLATE "POSIX");
DROP TABLE t;
CREATE TEMP TABLE t(a text COLLATE "de-x-icu");

CREATE TEMP TABLE t(A text COMPRESSION "pglz");

CREATE TEMP TABLE t(a int) WITH (fillfactor = 50);

-- Should fail because user should specify the precision of the NUMERIC.
CREATE TEMP TABLE large_numeric_tbl (a NUMERIC) USING duckdb;
-- But it's fine if the user specifies the precision
CREATE TEMP TABLE large_numeric_tbl_specified (a NUMERIC(38,20)) USING duckdb;
-- CTAS is fine though, it will use duckdb its default
-- TODO: Maybe make this fail too for consistency?
CREATE TEMP TABLE duckdb_numeric_from_pg_bare USING duckdb AS select 1::numeric x;
SELECT format_type(atttypid, atttypmod) FROM pg_attribute WHERE attrelid = 'duckdb_numeric_from_pg_bare'::regclass AND attname = 'x';
-- Except if they are fully specified
CREATE TEMP TABLE duckdb_numeric_from_pg USING duckdb AS select 1::numeric(10,8) x;
SELECT format_type(atttypid, atttypmod) FROM pg_attribute WHERE attrelid = 'duckdb_numeric_from_pg'::regclass AND attname = 'x';
-- Same when the query is forced by duckdb
CREATE TEMP TABLE duckdb_numeric_bare USING duckdb AS select * from duckdb.query($$ select 1::numeric x $$);
SELECT format_type(atttypid, atttypmod) FROM pg_attribute WHERE attrelid = 'duckdb_numeric_bare'::regclass AND attname = 'x';
-- But CTAS with numerics coming from a duckdb query are fine (i.e. we pass on the precision that duckdb uses)
CREATE TEMP TABLE duckdb_numeric USING duckdb AS select * from duckdb.query($$ select 1::numeric(10, 5) x $$);
SELECT format_type(atttypid, atttypmod) FROM pg_attribute WHERE attrelid = 'duckdb_numeric'::regclass AND attname = 'x';

CREATE TEMP TABLE cities_duckdb (
  name       text,
  population real,
  elevation  int
);

CREATE TEMP TABLE cities_heap (
  name       text,
  population real,
  elevation  int
) USING heap;

-- XXX: A better error message would be nice here, but for now this is acceptable.
CREATE TEMP TABLE capitals_duckdb (
  state      char(2) UNIQUE NOT NULL
) INHERITS (cities_duckdb);

-- XXX: A better error message would be nice here, but for now this is acceptable.
CREATE TEMP TABLE capitals_duckdb (
  state      char(2) UNIQUE NOT NULL
) INHERITS (cities_heap);

-- XXX: A better error message would be nice here, but for now this is acceptable.
CREATE TEMP TABLE capitals_heap (
  state      char(2) UNIQUE NOT NULL
) INHERITS (cities_duckdb);

DROP TABLE cities_heap, cities_duckdb;

CREATE TEMP TABLE t(a int) ON COMMIT PRESERVE ROWS;
INSERT INTO t VALUES (1);
SELECT * FROM t;
DROP TABLE t;
CREATE TEMP TABLE t(a int) ON COMMIT DELETE ROWS;
INSERT INTO t VALUES (1);
SELECT * FROM t;
DROP TABLE t;
-- unsupported
CREATE TEMP TABLE t(a int) ON COMMIT DROP;

-- CTAS fully in Duckdb
CREATE TEMP TABLE webpages USING duckdb AS SELECT r['column00'], r['column01'], r['column02'] FROM read_csv('../../data/web_page.csv') r;
SELECT * FROM webpages ORDER BY column00 LIMIT 2;

CREATE TEMP TABLE t_heap(a int) USING heap;
INSERT INTO t_heap VALUES (1);

-- CTAS from postgres table to duckdb table
CREATE TEMP TABLE t(b) USING duckdb AS SELECT * FROM t_heap;
SELECT * FROM t;

-- CTAS from DuckDB table to postgres table
CREATE TEMP TABLE t_heap2(c) USING heap AS SELECT * FROM t;
SELECT * FROM t_heap2;

-- CTAS from postgres table to postgres table (not actually handled by
-- pg_duckdb, but should still work)
CREATE TEMP TABLE t_heap3(c) USING heap AS SELECT * FROM t_heap;
SELECT * FROM t_heap3;

CREATE TEMP TABLE t_jsonb_heap(data jsonb) USING heap;
INSERT INTO t_jsonb_heap VALUES ('{"a": 1, "b": 2}');

-- CTAS from postgres table with type that has a different name in DuckDB
CREATE TEMP TABLE t_json AS SELECT * FROM t_jsonb_heap;
SELECT * FROM t_json;
-- DuckDB table should have
SELECT atttypid::regtype FROM pg_attribute WHERE attrelid = 't_json'::regclass AND attname = 'data';

SELECT duckdb.raw_query($$ SELECT database_name, schema_name, sql FROM duckdb_tables() $$);

-- multi-VALUES
CREATE TEMP TABLE ta (a int DEFAULT 3, b int) USING duckdb;
INSERT INTO ta (b) VALUES (123), (456);
INSERT INTO ta (a, b) VALUES (123, 456), (456, 123);
SELECT * FROM ta;

CREATE TEMP TABLE tb (a int DEFAULT 3, b int, c varchar DEFAULT 'pg_duckdb') USING duckdb;
INSERT INTO tb (a) VALUES (123), (456);
INSERT INTO tb (b) VALUES (123), (456);
INSERT INTO tb (c) VALUES ('ta'), ('tb');
SELECT * FROM tb;

-- INSERT ... SELECT
TRUNCATE TABLE ta;
INSERT INTO ta (a) SELECT 789;
INSERT INTO ta (b) SELECT 789;
INSERT INTO ta (a) SELECT * FROM t_heap;
INSERT INTO ta (b) SELECT * FROM t_heap;
SELECT * FROM ta;

TRUNCATE TABLE tb;
INSERT INTO tb (a) SELECT 789;
INSERT INTO tb (b) SELECT 789;
INSERT INTO tb (a) SELECT * FROM t_heap;
INSERT INTO tb (b) SELECT * FROM t_heap;
SELECT * FROM tb;

TRUNCATE TABLE tb;
INSERT INTO tb (c) SELECT 'ta';
INSERT INTO tb (c) SELECT 'ta' || 'tb';
INSERT INTO tb (a) SELECT (2)::numeric;
INSERT INTO tb (b) SELECT (3)::numeric;
INSERT INTO tb (c) SELECT t.a FROM (SELECT 'ta' || 'tb' AS a) t;
INSERT INTO tb (b, c) SELECT t.b, t.c FROM (SELECT (3)::numeric AS b, 'ta' || 'tb' AS c) t;
INSERT INTO tb (a, b, c) SELECT 1, 2, 'tb';
INSERT INTO tb  SELECT * FROM (SELECT (3)::numeric AS a, (3)::numeric AS b, 'ta' || 'tb' AS c) t;
SELECT * FROM tb;

CREATE TEMP TABLE tc (a int DEFAULT 3, b int, c varchar DEFAULT 'pg_duckdb', d varchar DEFAULT 'a' || 'b', e int DEFAULT 1 + 2) USING duckdb;
INSERT INTO tc (a) VALUES (123), (456);
INSERT INTO tc (b) VALUES (123), (456);
INSERT INTO tc (c) VALUES ('ta'), ('tb');
SELECT * FROM tc;

TRUNCATE TABLE tc;
INSERT INTO tc (a) SELECT 789;
INSERT INTO tc (b) SELECT 789;
INSERT INTO tc (a) SELECT * FROM t_heap;
INSERT INTO tc (b) SELECT * FROM t_heap;
SELECT * FROM tc;

TRUNCATE TABLE tc;
INSERT INTO tc (c) SELECT 'ta';
INSERT INTO tc (c) SELECT 'ta' || 'tb';
INSERT INTO tc (a) SELECT (2)::numeric;
INSERT INTO tc (b) SELECT (3)::numeric;
INSERT INTO tc (c) SELECT t.a FROM (SELECT 'ta' || 'tb' AS a) t;
INSERT INTO tc (b, c) SELECT t.b, t.c FROM (SELECT (3)::numeric AS b, 'ta' || 'tb' AS c) t;
INSERT INTO tc (a, b, c) SELECT 1, 2, 'tb';
INSERT INTO tc  SELECT * FROM (SELECT (3)::numeric AS a, (3)::numeric AS b, 'ta' || 'tb' AS c) t;
SELECT * FROM tc;

CREATE TEMP TABLE td (a int, ts timestamp default now()) USING duckdb;
INSERT INTO td (a) SELECT 1;
SELECT a FROM td;

-- Single Row Function
TRUNCATE TABLE td;
INSERT INTO td (ts) SELECT now();
SELECT count(*) FROM td;

TRUNCATE TABLE tc;
EXPLAIN VERBOSE INSERT INTO tc(c) SELECT md5('ta');
INSERT INTO tc(c) SELECT md5('ta');
EXPLAIN VERBOSE INSERT INTO tc(d) SELECT md5('test');
INSERT INTO tc(d) SELECT md5('test');
SELECT * FROM tc;

-- Set Returning Function
TRUNCATE TABLE ta;
INSERT INTO ta (a) SELECT generate_series(1, 3); -- failed. DuckDB expects this "INSERT INTO ta (a) FROM generate_series(1, 3)"

INSERT INTO ta (a) SELECT * FROM generate_series(1, 3); -- OK
INSERT INTO ta (b) SELECT * FROM generate_series(1, 3); -- OK
SELECT * FROM ta;

ALTER TABLE ta
    ADD COLUMN xyz int,
    ADD COLUMN "column with spaces" text,
    ALTER COLUMN b TYPE bigint;

select * from duckdb.query( $$ describe pg_temp.ta $$ );

SELECT * FROM ta;

CREATE TEMP TABLE measurement (
    city_id         int not null,
    logdate         date not null,
    peaktemp        int,
    unitsales       int
) PARTITION BY RANGE (logdate);

-- This command should fail because we don't support duckdb partitions yet
CREATE TEMP TABLE measurement_y2006m02 PARTITION OF measurement
    FOR VALUES FROM ('2006-02-01') TO ('2006-03-01') USING duckdb;

DROP TABLE webpages, t, t_heap, t_heap2, t_heap3, ta, tb, tc, td, measurement;
