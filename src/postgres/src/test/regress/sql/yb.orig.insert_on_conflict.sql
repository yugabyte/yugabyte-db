CREATE TABLE ab_tab (a int, b int);
INSERT INTO ab_tab SELECT g, g + 10 FROM generate_series(1, 10) g;

--- Basic
CREATE UNIQUE INDEX NONCONCURRENTLY ah_idx ON ab_tab (a HASH);
-- Ending with no conflict.
INSERT INTO ab_tab VALUES (generate_series(-4, 5)) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- Ending with conflict.
INSERT INTO ab_tab VALUES (generate_series(6, 15)) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- Reset.
DELETE FROM ab_tab WHERE a < 1 OR a > 10;

--- RETURNING
INSERT INTO ab_tab VALUES (generate_series(-3, 13)) ON CONFLICT DO NOTHING RETURNING (a % 5);
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- Reset.
DELETE FROM ab_tab WHERE a < 1 OR a > 10;

--- DO UPDATE
BEGIN;
INSERT INTO ab_tab VALUES (generate_series(-3, 13)) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.a;
SELECT * FROM ab_tab ORDER BY a, b;
-- Reset.
ROLLBACK;

--- DO UPDATE with existing row
BEGIN;
INSERT INTO ab_tab VALUES (generate_series(-3, 13)) ON CONFLICT (a) DO UPDATE SET b = ab_tab.a + 1;
SELECT * FROM ab_tab ORDER BY a, b;
-- Reset.
ROLLBACK;

--- DO UPDATE with RETURNING
BEGIN;
-- Column b should be returned as NULL for rows a = [-3, 0]
INSERT INTO ab_tab VALUES (generate_series(-3, 5)) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.a RETURNING b, (b % 5);
-- Column b should be returned as a + 1 for rows a = [6, 10] and NULL for a = [11, 13]
INSERT INTO ab_tab AS old VALUES (generate_series(6, 13)) ON CONFLICT (a) DO UPDATE SET b = old.a + 1 RETURNING a, (b % 5);
SELECT * FROM ab_tab ORDER BY a, b;
ROLLBACK;

--- Accessing the EXCLUDED row from the RETURNING clause should be disallowed
INSERT INTO ab_tab VALUES (generate_series(-3, 13)) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.a RETURNING EXCLUDED.b, b, (b % 5);

--- SPI
BEGIN;
SELECT yb_run_spi($$
    INSERT INTO ab_tab VALUES (1) ON CONFLICT DO NOTHING RETURNING a
$$, 1);
SELECT count(*), min(a), max(a) FROM ab_tab;
SELECT yb_run_spi($$
    INSERT INTO ab_tab VALUES (null) ON CONFLICT DO NOTHING RETURNING a
$$, 1);
SELECT count(*), min(a), max(a) FROM ab_tab;
SELECT yb_run_spi($$
    INSERT INTO ab_tab VALUES (generate_series(-10, 20)) ON CONFLICT DO NOTHING RETURNING b
$$, 100);
SELECT count(*), min(a), max(a) FROM ab_tab;
SELECT yb_run_spi($$
    INSERT INTO ab_tab VALUES (generate_series(-20, 30)) ON CONFLICT DO NOTHING RETURNING b, a
$$, 15);
SELECT count(*), min(a), max(a) FROM ab_tab;
SELECT yb_run_spi($$
    INSERT INTO ab_tab VALUES (generate_series(-30, 40)) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.a RETURNING (a + b)
$$, 45);
SELECT count(*), min(a), max(a) FROM ab_tab;
ABORT;

--- Multiple arbiter indexes
CREATE UNIQUE INDEX NONCONCURRENTLY br_idx ON ab_tab (b ASC);
-- No constraint specification.
-- (1, 1): conflict on i
-- (10, 10): conflict on both i and j
-- (15, 15): conflict on j
INSERT INTO ab_tab VALUES (1, 1), (-30, -30), (10, 10), (15, 15), (30, 30) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- Yes constraint specification.
INSERT INTO ab_tab VALUES (2, 2) ON CONFLICT (a) DO NOTHING;
INSERT INTO ab_tab VALUES (2, 2) ON CONFLICT (b) DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;

--- GH-25240 (multiple arbiter indexes)
CREATE TABLE t_multiple (c CHAR, n NUMERIC);
CREATE UNIQUE INDEX NONCONCURRENTLY i1 ON t_multiple (c) WHERE n % 2 = 0;
CREATE UNIQUE INDEX NONCONCURRENTLY i2 ON t_multiple (c) WHERE n < 10;
INSERT INTO t_multiple VALUES ('a', 1), ('b', 2);
-- ('c', false::INT::NUMERIC): not found in either index and satisfies predicate of both indexes.
-- Result: inserted in main table and both indexes.
-- ('d', 7), ('e', 14): not found in either index and satisfies predicate of only one index.
-- Result: inserted in main table and only one index.
INSERT INTO t_multiple VALUES ('c', false::INT::NUMERIC), ('d', 7), ('e', 14) ON CONFLICT DO NOTHING;
-- ('d', 6), ('e', 4): found in one index and satisfies predicate of both indexes.
-- Result: not inserted.
INSERT INTO t_multiple VALUES ('d', 6), ('e', 4) ON CONFLICT DO NOTHING;
-- ('d', 13), ('e', 15): found in one index and satisfies predicate of neither index.
-- Result: inserted into main table, but not indexes.
INSERT INTO t_multiple VALUES ('d', 13), ('e', 15) ON CONFLICT DO NOTHING;
-- ('b', 4): found in both indexes and satisfies predicate of both indexes.
-- Result: not inserted.
-- ('b', 12), ('b', 1): found in both indexes and satisfies predicate of one index.
-- Result: not inserted.
-- ('b', 13): found in both indexes and satisfies predicate of neither index.
-- Result: inserted into main table, but not indexes.
INSERT INTO t_multiple VALUES ('b', 4), ('b', 12), ('b', 1), ('b', 13) ON CONFLICT DO NOTHING;
-- ('a', 12), ('e', 7): found in one index and satisfies predicate of other index.
-- Result: inserted into main table and 'other' index.
INSERT INTO t_multiple VALUES ('a', 12), ('e', 7) ON CONFLICT DO NOTHING;
-- ('f', 7), ('g', 12): satisfies predicate of only one index.
-- Result: inserted in main table and only one index.
-- ('f', 8), ('g', 8): found in one index and satisfies predicate of both indexes.
-- Result: not inserted.
-- ('f', 5), ('g', 14): found in one index and satisfies predicate of same index.
-- Result: not inserted.
-- ('f', 16), ('g', 7): found in one index and satisfies predicate of other index.
-- Result: inserted in main table and 'other' index.
-- ('f', 13), ('g', 13): found in both indexes and satisfies predicate of neither index.
-- Result: inserted into main table, but not indexes.
-- ('f', 2), ('g', 2): found in both indexes and satisfies predicate of both indexes.
-- Result: not inserted.
INSERT INTO t_multiple VALUES
	('f', 7), ('g', 12),
	('f', 8), ('g', 8),
	('f', 5), ('g', 14),
	('f', 16), ('g', 7),
	('f', 13), ('g', 13),
	('f', 2), ('g', 2)
	ON CONFLICT DO NOTHING;
SELECT * FROM t_multiple WHERE n % 2 = 0 ORDER BY c, n;
SELECT * FROM t_multiple WHERE n < 10 ORDER BY c, n;
SELECT * FROM t_multiple ORDER BY c, n;
TRUNCATE t_multiple;
DROP INDEX i2;
-- Create a duplicate index to exercise the multiple arbiter index code path for
-- DO UPDATE in the context of unique indexes.
CREATE UNIQUE INDEX NONCONCURRENTLY i1_copy ON t_multiple (c) WHERE n % 2 = 0;
INSERT INTO t_multiple VALUES ('a', 1), ('b', 2);
-- ('a', 1): not found in the indexes, and proposed updated value into does not satisfy the predicate.
-- Result: inserted into main table, but not the indexes.
-- ('b', 2): found in the indexes, and updated value satisfies the predicate.
-- Result: updated in main table and indexes.
EXPLAIN (COSTS OFF) INSERT INTO t_multiple VALUES ('a', 1), ('b', 2) ON CONFLICT (c) WHERE n % 2 = 0 DO UPDATE SET n = EXCLUDED.n + 2;
INSERT INTO t_multiple VALUES ('a', 1), ('b', 2) ON CONFLICT (c) WHERE n % 2 = 0 DO UPDATE SET n = EXCLUDED.n + 2;
SELECT * FROM t_multiple WHERE n % 2 = 0 ORDER BY c, n;
-- ('a', 1): not found in the indexes, and proposed updated value satisfies the predicate.
-- Result: inserted in main table, but not the indexes.
-- ('b', 2): found in the indexes, and updated value does not satisfy the predicate.
-- Result: updated in main table and deleted from the indexes.
INSERT INTO t_multiple VALUES ('a', 1), ('b', 2) ON CONFLICT (c) WHERE n % 2 = 0 DO UPDATE SET n = EXCLUDED.n + 3;
-- first ('b', 2): not found in the indexes.
-- Result: inserted into main table and indexes.
-- second ('b', 2): found in the indexes, and contains same index key as the first row.
-- Result: not inserted.
INSERT INTO t_multiple VALUES ('b', 2), ('b', 2) ON CONFLICT (c) WHERE n % 2 = 0 DO UPDATE SET n = EXCLUDED.n + 3;
SELECT * FROM t_multiple WHERE n % 2 = 0 ORDER BY c, n;
SELECT * FROM t_multiple ORDER BY c, n;
DROP TABLE t_multiple;

--- Multiple unique indexes but single arbiter index
INSERT INTO ab_tab VALUES (21, 21), (22, 23);
-- (24, 21) conflicts on b but not a and should produce a unique constraint violation.
INSERT INTO ab_tab VALUES (24, 21) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
-- (22, 22) conflicts on a but not b and should produce a unique constraint violation.
INSERT INTO ab_tab VALUES (22, 22) ON CONFLICT (b) DO UPDATE SET b = EXCLUDED.b;
-- Reset.
DELETE FROM ab_tab WHERE a < 1 OR a > 10;
DROP INDEX ah_idx;
DROP INDEX br_idx;

--- Multicolumn index
CREATE UNIQUE INDEX NONCONCURRENTLY bharbr_idx ON ab_tab (b HASH, a DESC, b ASC);
BEGIN;
INSERT INTO ab_tab VALUES (0, 10), (1, 11), (2, 12), (2, 13) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- Reset.
ROLLBACK;
DROP INDEX bharbr_idx;

--- Many-to-one expression index
CREATE UNIQUE INDEX NONCONCURRENTLY expr_idx ON ab_tab (((a + b) % 100));
-- Conflicts:
-- 12: existing (1, 11), several values mapping to that are inserted
-- 77: two such values are inserted, only one goes through
-- 98: no conflict
BEGIN;
INSERT INTO ab_tab VALUES (12, 0), (112, 0), (99, 99), (100, 12), (77, 0), (78, -1) ON CONFLICT (((a + b) % 100)) DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
ROLLBACK;
-- Same with DO UPDATE.
BEGIN;
INSERT INTO ab_tab VALUES (12, 0), (112, 0), (99, 99), (100, 12), (77, 0), (78, -1) ON CONFLICT (((a + b) % 100)) DO UPDATE SET b = 1010 + EXCLUDED.b;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
ROLLBACK;
-- Reset.
DROP INDEX expr_idx;

--- Partial indexes
CREATE UNIQUE INDEX NONCONCURRENTLY b1_idx ON ab_tab (b) WHERE a % 10 = 1;
CREATE UNIQUE INDEX NONCONCURRENTLY b2_idx ON ab_tab (b) WHERE a % 10 = 2;
-- Conflicts:
-- b1_idx: existing (1, 11), conflicts with (101, 11)
-- b2_idx: existing (2, 12), conflicts with (202, 12)
INSERT INTO ab_tab VALUES (100, 11), (101, 11), (102, 11), (201, 201), (202, 12) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- No partial index matches.
INSERT INTO ab_tab VALUES (55, 55), (66, 66), (77, 77), (88, 88) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
-- Reset.
DELETE FROM ab_tab WHERE a < 1 OR a > 10;

--- Index predicate matching
CREATE UNIQUE INDEX NONCONCURRENTLY bfull_idx ON ab_tab (b);
BEGIN;
INSERT INTO ab_tab VALUES (101, 101), (102, 102);
-- Index predicate should satisfy both the partial index (b1_idx) as well as the full index (bfull_idx).
EXPLAIN (COSTS OFF) INSERT INTO ab_tab VALUES (101, 101), (201, 201) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING;
INSERT INTO ab_tab VALUES (101, 101), (201, 201) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING RETURNING *, a % 10 AS modulo;
-- Index predicate should satisfy b1_idx and bfull_idx but the inserted row should satisfy only bfull_idx.
EXPLAIN (COSTS OFF) INSERT INTO ab_tab VALUES (102, 102), (202, 202) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING;
INSERT INTO ab_tab VALUES (102, 102) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING RETURNING *, a % 10 AS modulo; -- should return nothing
INSERT INTO ab_tab VALUES (202, 202) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING RETURNING *, a % 10 AS modulo; -- should return row
SELECT * FROM ab_tab WHERE a < 1 OR a > 10 ORDER BY a, b;
ROLLBACK;
-- Exclude the full index by specifying the partial index as a constraint.
-- Unique constraints cannot be defined on partial indexes.
CREATE UNIQUE INDEX NONCONCURRENTLY b3_idx ON ab_tab (b) WHERE a % 10 = 3;
ALTER TABLE ab_tab ADD CONSTRAINT b3_idx_constr UNIQUE USING INDEX b3_idx;
DROP INDEX b3_idx;
DROP INDEX bfull_idx;
INSERT INTO ab_tab VALUES (101, 101), (102, 102);
BEGIN;
-- Index predicate corresponding to b1_idx does not satisfy the inserted row
INSERT INTO ab_tab VALUES (103, 103) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING RETURNING *, a % 10 AS modulo;
-- However, a different partial index and should produce a unique constraint violation.
INSERT INTO ab_tab VALUES (102, 102) ON CONFLICT (b) WHERE a % 10 = 1 DO NOTHING;
ROLLBACK;
-- Reset.
DELETE FROM ab_tab WHERE a < 1 OR a > 10;
DROP INDEX b1_idx;
DROP INDEX b2_idx;

--- Defaults
CREATE TABLE ioc_defaults (a INT, b INT DEFAULT 42, c INT DEFAULT NULL);
INSERT INTO ioc_defaults (a) VALUES (1);
CREATE UNIQUE INDEX NONCONCURRENTLY ioc_defaults_b_idx ON ioc_defaults (b);
INSERT INTO ioc_defaults VALUES (1) ON CONFLICT DO NOTHING RETURNING *;
INSERT INTO ioc_defaults VALUES (1), (1) ON CONFLICT (b) DO UPDATE SET b = ioc_defaults.b + 1 RETURNING *;
-- Not modifying the default value should produce an error.
-- TODO(kramanathan): Uncomment when RETURNING is supported by batch insert on conflict.
-- INSERT INTO ioc_defaults VALUES (1), (1) ON CONFLICT (b) DO UPDATE SET c = ioc_defaults.a + 1 RETURNING *;
SELECT * FROM ioc_defaults ORDER BY b, c;
DROP INDEX ioc_defaults_b_idx;
TRUNCATE ioc_defaults;

--- NULLS DISTINCT
CREATE UNIQUE INDEX NONCONCURRENTLY ah_idx ON ab_tab (a HASH);
INSERT INTO ab_tab VALUES (null, null);
INSERT INTO ab_tab VALUES (null, null) ON CONFLICT DO NOTHING;
-- Multiple rows with NULL values should semantically be treated as distinct rows.
INSERT INTO ab_tab VALUES (null, 1), (null, 2) ON CONFLICT DO NOTHING;
INSERT INTO ab_tab VALUES (null, 1), (null, 2) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
SELECT * FROM ab_tab WHERE a IS NULL ORDER BY b;
-- Similarly, columns with default NULL values should be treated as distinct rows.
CREATE UNIQUE INDEX NONCONCURRENTLY ioc_defaults_bc_idx ON ioc_defaults (b, c);
INSERT INTO ioc_defaults (a) VALUES (1);
INSERT INTO ioc_defaults VALUES (1), (1) ON CONFLICT (b, c) DO UPDATE SET a = EXCLUDED.a;
SELECT * FROM ioc_defaults ORDER BY b, c;
-- Reset.
DELETE FROM ab_tab WHERE a IS null;
DROP INDEX ioc_defaults_bc_idx;
TRUNCATE ioc_defaults;

--- NULLS NOT DISTINCT
CREATE TABLE ab_tab2 (a int, b int);
CREATE UNIQUE INDEX NONCONCURRENTLY ah_idx2 ON ab_tab2 (a HASH) NULLS NOT DISTINCT;
INSERT INTO ab_tab2 VALUES (null, 1);
INSERT INTO ab_tab2 VALUES (null, 2) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab2;
INSERT INTO ab_tab2 VALUES (null, 3) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
SELECT * FROM ab_tab2;
INSERT INTO ab_tab2 VALUES (null, 4), (null, 5) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab2;
INSERT INTO ab_tab2 VALUES (null, 6), (null, 7) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;
SELECT * FROM ab_tab2 ORDER BY a, b;
INSERT INTO ab_tab2 VALUES (null, 8), (null, 9) ON CONFLICT (a) DO UPDATE SET a = EXCLUDED.b;
SELECT * FROM ab_tab2 ORDER BY a, b;
INSERT INTO ab_tab2 VALUES (null, 10), (null, 11), (null, 12) ON CONFLICT (a) DO UPDATE SET a = EXCLUDED.b;
SELECT * FROM ab_tab2 ORDER BY a, b;
DROP TABLE ab_tab2;
-- Index key attributes > 1
CREATE TABLE abc_tab (a int, b int, c int);
CREATE UNIQUE INDEX NONCONCURRENTLY abh_idx ON abc_tab ((a, b) HASH) NULLS NOT DISTINCT;
INSERT INTO abc_tab VALUES (123, null, 1), (456, null, 1), (null, null, 1);
INSERT INTO abc_tab VALUES (123, null, 2), (456, null, 2), (null, null, 2) ON CONFLICT DO NOTHING;
SELECT * FROM abc_tab ORDER BY a, b;
INSERT INTO abc_tab VALUES (123, null, 2), (456, null, 2), (null, null, 2) ON CONFLICT (a, b) DO UPDATE SET c = EXCLUDED.c;
SELECT * FROM abc_tab ORDER BY a, b;
DROP TABLE abc_tab;
-- Default NULL values
CREATE UNIQUE INDEX NONCONCURRENTLY ioc_defaults_bc_idx ON ioc_defaults (b, c) NULLS NOT DISTINCT;
INSERT INTO ioc_defaults VALUES (1);
INSERT INTO ioc_defaults VALUES (2) ON CONFLICT (b, c) DO UPDATE SET a = EXCLUDED.a;
SELECT * FROM ioc_defaults;
INSERT INTO ioc_defaults VALUES (3), (4) ON CONFLICT (b, c) DO UPDATE SET a = EXCLUDED.a;
SELECT * FROM ioc_defaults ORDER BY a;
DROP INDEX ioc_defaults_bc_idx;
TRUNCATE ioc_defaults;

--- Partitioned table
CREATE TABLE pp (i serial, j int, UNIQUE (j)) PARTITION BY RANGE (j);
CREATE TABLE pp1 PARTITION OF pp FOR VALUES FROM (0) TO (10);
CREATE TABLE pp2 PARTITION OF pp FOR VALUES FROM (10) TO (20);
CREATE TABLE pp3 PARTITION OF pp FOR VALUES FROM (20) TO (30);
CREATE TABLE pp4 (i serial, j int, UNIQUE (j)) PARTITION BY RANGE (j);
CREATE TABLE pp44 PARTITION OF pp4 FOR VALUES FROM (30) TO (35);
CREATE TABLE pp49 PARTITION OF pp4 FOR VALUES FROM (35) TO (40);
ALTER TABLE pp ATTACH PARTITION pp4 FOR VALUES FROM (30) TO (40);
INSERT INTO pp (j) SELECT g * 17 % 40 FROM generate_series(1, 20) g;
SELECT * FROM pp ORDER BY i;
BEGIN;
INSERT INTO pp (j) SELECT g * 7 % 40 FROM generate_series(1, 40) g ON CONFLICT DO NOTHING;
SELECT * FROM pp ORDER BY i;
ABORT;
BEGIN;
INSERT INTO pp (j) SELECT g * 7 % 40 FROM generate_series(1, 40) g ON CONFLICT (j) DO UPDATE SET i = EXCLUDED.i + 100;
SELECT * FROM pp ORDER BY i % 100;
ABORT;

--- Partitioned table with TEXT partition key
CREATE TABLE staff (id SERIAL, name TEXT, department TEXT, PRIMARY KEY (name HASH, department ASC)) PARTITION BY LIST (department);
CREATE TABLE staff_sales PARTITION OF staff FOR VALUES IN ('Sales');
CREATE TABLE staff_engineering PARTITION OF staff FOR VALUES IN ('Engineering');
CREATE TABLE staff_finance PARTITION OF staff FOR VALUES IN ('Finance');
INSERT INTO staff (name, department) VALUES ('Eve Adams', 'Sales'), ('Frank Green', 'Engineering');
INSERT INTO staff (name, department) VALUES ('Eve Adams', 'Sales'), ('Frank Green', 'Engineering') ON CONFLICT (name, department) DO NOTHING;
-- Cross-partition updates should be disallowed.
INSERT INTO staff (name, department) VALUES ('Eve Adams', 'Sales'), ('Frank Green', 'Engineering') ON CONFLICT (name, department) DO UPDATE SET department = 'Finance';
INSERT INTO staff (name, department) VALUES ('Eve Adams', 'Sales'), ('Gwen Smith', 'Engineering') ON CONFLICT (name, department) DO UPDATE SET name = staff.name || ' (CONFLICT)';
SELECT name, department FROM staff ORDER BY id;

--- Complex types
CREATE TYPE complex_enum AS ENUM ('bob', 'cob', 'hob');
CREATE TABLE complex_table (t timestamp UNIQUE,
                            b box,
                            v1 varchar(5) UNIQUE,
                            v2 varchar UNIQUE,
                            x text,
                            n numeric UNIQUE,
                            d decimal,
                            e complex_enum,
                            PRIMARY KEY (d, x, e DESC));
CREATE UNIQUE INDEX NONCONCURRENTLY ON complex_table (n, e);
INSERT INTO complex_table VALUES ('2024-08-22 07:00:00+07'::timestamptz,
                                  '(1, 2, 3, 4)',
                                  'abc',
                                  'def',
                                  'hij',
                                  12.34,
                                  56.78,
                                  'cob');
INSERT INTO complex_table VALUES ('2024-08-22 06:00:00+06'::timestamptz,
                                  '(5, 6, 7, 8)',
                                  'def',
                                  'hij',
                                  'abc',
                                  56.78,
                                  12.34,
                                  'bob') ON CONFLICT DO NOTHING;
SELECT count(*) FROM complex_table;

--- ON CONFLICT DO UPDATE varlen type
CREATE TABLE varlen (t text, b bytea GENERATED ALWAYS AS (bytea(t)) STORED, UNIQUE (t));
CREATE INDEX NONCONCURRENTLY ON varlen (b);
INSERT INTO varlen VALUES ('a');
INSERT INTO varlen VALUES ('a'), ('a') ON CONFLICT (t) DO UPDATE SET t = 'b';
INSERT INTO varlen VALUES ('a'), ('a') ON CONFLICT (t) DO UPDATE SET t = EXCLUDED.t || 'z';
INSERT INTO varlen VALUES ('az'), ('az') ON CONFLICT (t) DO UPDATE SET t = varlen.t || 'z';
SELECT * FROM varlen ORDER BY t;
-- Reset.
TRUNCATE varlen;

--- Generated column as arbiter index
CREATE UNIQUE INDEX NONCONCURRENTLY ON varlen (b);
INSERT INTO varlen VALUES ('a');
INSERT INTO varlen VALUES ('a'), ('a') ON CONFLICT (b) DO UPDATE SET t = 'b';
INSERT INTO varlen VALUES ('a'), ('a') ON CONFLICT (b) DO UPDATE SET t = EXCLUDED.t || 'z';
INSERT INTO varlen VALUES ('az'), ('az') ON CONFLICT (b) DO UPDATE SET t = varlen.t || 'z';

SELECT * FROM varlen ORDER BY t;

--- ON CONFLICT DO UPDATE edge cases with PRIMARY KEY as arbiter index
CREATE TABLE ioc (i int, PRIMARY KEY (i ASC));
BEGIN;
-- INSERT i=1, UPDATE i=1 to 21
INSERT INTO ioc VALUES (1), (1) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 20;
ROLLBACK;
INSERT INTO ioc VALUES (1);
BEGIN;
-- UPDATE i=1 to 21, INSERT i=1
INSERT INTO ioc VALUES (1), (1) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 20;
TABLE ioc;
ROLLBACK;
BEGIN;
-- INSERT i=20, UPDATE i=20 to 40
INSERT INTO ioc VALUES (20), (20) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 20;
ROLLBACK;
BEGIN;
-- INSERT i=20, UPDATE i=1 to 20
INSERT INTO ioc VALUES (20), (1) ON CONFLICT (i) DO UPDATE SET i = 20;
ROLLBACK;
BEGIN;
-- UPDATE i=1 to 20, UPDATE i=20 to 20
INSERT INTO ioc VALUES (1), (20) ON CONFLICT (i) DO UPDATE SET i = 20;
ROLLBACK;
-- Reset.
DROP TABLE ioc;

--- ON CONFLICT DO UPDATE edge cases with secondary index as arbiter index
CREATE TABLE ioc (i TEXT, j INT UNIQUE);
BEGIN;
-- INSERT j=1, UPDATE j=1 to 21
INSERT INTO ioc VALUES ('row-1', 1), ('row-2', 1) ON CONFLICT (j) DO UPDATE SET j = EXCLUDED.j + 20;
ROLLBACK;
INSERT INTO ioc VALUES ('row-1', 1);
BEGIN;
-- UPDATE j=1 to 21, INSERT j=1
INSERT INTO ioc VALUES ('row-1', 1), ('row-2', 1) ON CONFLICT (j) DO UPDATE SET j = EXCLUDED.j + 20;
SELECT * FROM ioc ORDER BY i, j;
ROLLBACK;
BEGIN;
-- INSERT j=20, UPDATE j=20 to 40
INSERT INTO ioc VALUES ('row-2', 20), ('row-2', 20) ON CONFLICT (j) DO UPDATE SET j = EXCLUDED.j + 20;
ROLLBACK;
BEGIN;
-- INSERT j=20, UPDATE j=1 to 20
INSERT INTO ioc VALUES ('row-2', 20), ('row-3', 1) ON CONFLICT (j) DO UPDATE SET j = 20;
ROLLBACK;
BEGIN;
-- UPDATE j=1 to 20, UPDATE j=20 to 20
INSERT INTO ioc VALUES ('row-2', 1), ('row-3', 20) ON CONFLICT (j) DO UPDATE SET j = 20;
ROLLBACK;
-- Reset.
DROP TABLE ioc;

--- UPDATE SET edge case
CREATE TABLE texts (t text PRIMARY KEY);
CREATE FUNCTION agg_texts() RETURNS text AS $$SELECT 'agg=[' || string_agg(t, ',') || ']' FROM texts$$ LANGUAGE sql;
INSERT INTO texts VALUES ('i'), ('j') ON CONFLICT (t) DO UPDATE SET t = agg_texts();
INSERT INTO texts VALUES ('i'), ('j') ON CONFLICT (t) DO UPDATE SET t = agg_texts();
SELECT * FROM texts ORDER BY t;

--- UPDATE SET unmodified primary key
CREATE TABLE table_unmodified_pk (i INT PRIMARY KEY, j INT);
INSERT INTO table_unmodified_pk VALUES (1, 1);
INSERT INTO table_unmodified_pk AS old VALUES (1, 1), (1, 4) ON CONFLICT (i) DO UPDATE SET j = old.j + 1;

--- UPDATE SET unmodified secondary index
CREATE TABLE table_skip_index (i INT UNIQUE, j INT);
INSERT INTO table_skip_index VALUES (1, 1);
SET yb_enable_inplace_index_update TO true;
INSERT INTO table_skip_index AS old VALUES (1, 1), (1, 4) ON CONFLICT (i) DO UPDATE SET j = old.j + 1;
RESET yb_enable_inplace_index_update;

--- ON CONFLICT DO UPDATE YbExecDoUpdateIndexTuple
CREATE TABLE index_update (a int PRIMARY KEY, b int);
INSERT INTO index_update VALUES (1, 2);
INSERT INTO index_update VALUES (1, 3) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;

--- Before row triggers
CREATE OR REPLACE FUNCTION loggingfunc() RETURNS trigger AS $$
    DECLARE
        count int;
    BEGIN
        SELECT count(*) INTO count FROM pp;
        RAISE NOTICE '% % % % i=% count=%', TG_NAME, TG_TABLE_NAME, TG_WHEN, TG_OP, new.i, count;
    RETURN NEW;
    END;
$$ LANGUAGE plpgsql;
-- Trigger on parent table should disable batching for child tables.
CREATE TRIGGER loggingtrig BEFORE INSERT ON pp FOR EACH ROW EXECUTE PROCEDURE loggingfunc();
BEGIN;
INSERT INTO pp (j) SELECT g * 19 % 40 FROM generate_series(1, 5) g ON CONFLICT DO NOTHING;
SELECT * FROM pp ORDER BY i;
ABORT;

--- After row triggers
CREATE TABLE trigger_test (i int2, PRIMARY KEY (i ASC));
-- This test is derived from TestPgUpdatePrimaryKey.java.
CREATE TABLE triggers_fired (name text, fired int, PRIMARY KEY (name));
CREATE OR REPLACE FUNCTION log_trigger() RETURNS trigger AS $$
    BEGIN
        UPDATE triggers_fired SET fired = triggers_fired.fired + 1 WHERE name = TG_NAME;
    RETURN NEW;
    END;
$$ LANGUAGE plpgsql;
CREATE TRIGGER ai AFTER INSERT ON trigger_test FOR EACH ROW EXECUTE PROCEDURE log_trigger();
CREATE TRIGGER au AFTER UPDATE ON trigger_test FOR EACH ROW EXECUTE PROCEDURE log_trigger();
INSERT INTO triggers_fired VALUES ('ai', 0), ('au', 0);
INSERT INTO trigger_test VALUES (1);
INSERT INTO trigger_test VALUES (1), (2), (1), (3) ON CONFLICT DO NOTHING;
TABLE triggers_fired;
TABLE trigger_test;
INSERT INTO trigger_test VALUES (1), (2), (1), (3) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 10;
TABLE triggers_fired;
TABLE trigger_test;

--- Foreign key
CREATE TABLE parent_table (n numeric, t text, b bool, PRIMARY KEY (t, n ASC));
CREATE TABLE child_table (k numeric, n numeric, t text, CONSTRAINT fk FOREIGN KEY (t, n) REFERENCES parent_table, PRIMARY KEY (k ASC));
INSERT INTO parent_table VALUES (1, '1', true), (2, '2', true);
INSERT INTO child_table VALUES (0, 1, '1') ON CONFLICT DO NOTHING;
INSERT INTO child_table VALUES (0, 1, '1') ON CONFLICT (k) DO UPDATE SET k = 1;
TABLE child_table;
INSERT INTO child_table VALUES (0, 2, '1') ON CONFLICT (k) DO UPDATE SET k = 1;
INSERT INTO child_table VALUES (1, 2, '1') ON CONFLICT (k) DO UPDATE SET t = '2';
TABLE child_table;
INSERT INTO parent_table VALUES (2, '2', false), (1, '1', false) ON CONFLICT (t, n) DO UPDATE SET t = EXCLUDED.t || EXCLUDED.t;
INSERT INTO parent_table VALUES (2, '2', false) ON CONFLICT (t, n) DO UPDATE SET t = EXCLUDED.t || EXCLUDED.t;
SELECT * FROM parent_table ORDER BY t, n;
TRUNCATE parent_table CASCADE;
INSERT INTO parent_table (n, t) SELECT g, '' FROM generate_series(0, 5) g;
INSERT INTO child_table (n, t, k) SELECT g, '', g FROM generate_series(0, 4) g;
-- NO ACTION
ALTER TABLE child_table DROP CONSTRAINT fk;
ALTER TABLE child_table ADD CONSTRAINT fk FOREIGN KEY (t, n) REFERENCES parent_table (t, n) ON DELETE NO ACTION ON UPDATE NO ACTION;
BEGIN;
INSERT INTO parent_table (n, t) SELECT g, '' FROM generate_series(0, 5) g
    ON CONFLICT (n, t) DO UPDATE SET n = EXCLUDED.n - 1;
TABLE parent_table;
TABLE child_table;
ABORT;
-- RESTRICT
ALTER TABLE child_table DROP CONSTRAINT fk;
ALTER TABLE child_table ADD CONSTRAINT fk FOREIGN KEY (t, n) REFERENCES parent_table (t, n) ON DELETE RESTRICT ON UPDATE RESTRICT;
BEGIN;
INSERT INTO parent_table (n, t) SELECT g, '' FROM generate_series(0, 5) g
    ON CONFLICT (n, t) DO UPDATE SET n = EXCLUDED.n - 1;
TABLE parent_table;
TABLE child_table;
ABORT;
-- CASCADE + self-referential NO ACTION
ALTER TABLE child_table DROP CONSTRAINT fk;
ALTER TABLE child_table ADD CONSTRAINT fk FOREIGN KEY (t, n) REFERENCES parent_table (t, n) ON DELETE CASCADE ON UPDATE CASCADE;
ALTER TABLE child_table ADD CONSTRAINT u UNIQUE (n);
ALTER TABLE child_table ADD CONSTRAINT self_fk FOREIGN KEY (k) REFERENCES child_table (n) ON DELETE NO ACTION ON UPDATE NO ACTION;
BEGIN;
INSERT INTO parent_table (n, t) SELECT g, '' FROM generate_series(0, 5) g
    ON CONFLICT (n, t) DO UPDATE SET n = EXCLUDED.n - 1;
TABLE parent_table;
TABLE child_table;
ABORT;
-- CASCADE + self-referential CASCADE
ALTER TABLE child_table DROP CONSTRAINT self_fk;
ALTER TABLE child_table ADD CONSTRAINT self_fk FOREIGN KEY (k) REFERENCES child_table (n) ON DELETE CASCADE ON UPDATE CASCADE;
BEGIN;
INSERT INTO parent_table (n, t) SELECT g, '' FROM generate_series(0, 5) g
    ON CONFLICT (n, t) DO UPDATE SET n = EXCLUDED.n - 1;
TABLE parent_table;
TABLE child_table;
ABORT;
-- CASCADE + no-update/no-delete self-referential
ALTER TABLE child_table DROP CONSTRAINT self_fk;
ALTER TABLE child_table DROP CONSTRAINT u;
ALTER TABLE child_table ADD CONSTRAINT self_fk FOREIGN KEY (n) REFERENCES child_table (k) ON DELETE CASCADE ON UPDATE CASCADE;
BEGIN;
INSERT INTO parent_table (n, t) VALUES (-1, 'other');
INSERT INTO child_table (n, t, k) VALUES (-1, 'other', -1);
INSERT INTO parent_table (n, t) SELECT g, '' FROM generate_series(0, 5) g
    ON CONFLICT (n, t) DO UPDATE SET n = EXCLUDED.n - 1;
TABLE parent_table;
TABLE child_table;
ABORT;

--- GH-25070
CREATE TABLE main (a INT, b TEXT, PRIMARY KEY (a, b));
CREATE TABLE copy (a INT, b TEXT, PRIMARY KEY (b, a));
INSERT INTO main (SELECT i, 'name_' || i FROM generate_series(1, 10) AS i);
INSERT INTO copy (SELECT a, b FROM main) ON CONFLICT DO NOTHING;
TABLE copy ORDER BY a;
INSERT INTO copy (SELECT a, b FROM main) ON CONFLICT (b, a) DO UPDATE SET b = 'replaced_' || EXCLUDED.b;
TABLE copy ORDER BY a;

--- GH-25296
CREATE TABLE dupidx (a int);
CREATE UNIQUE INDEX ON dupidx (a);
CREATE UNIQUE INDEX ON dupidx (a);
INSERT INTO dupidx VALUES (1);
INSERT INTO dupidx VALUES (1) ON CONFLICT DO NOTHING;
TABLE dupidx;
CREATE TABLE multidx (a int, b int);
CREATE UNIQUE INDEX ON multidx (a);
CREATE UNIQUE INDEX ON multidx (b);
INSERT INTO multidx VALUES (1, 2);
INSERT INTO multidx VALUES (1, 2) ON CONFLICT DO NOTHING;
TABLE multidx;

--- GH-25836
CREATE TABLE it (i int2 PRIMARY KEY, t text);
WITH w AS (
    INSERT INTO it SELECT g, repeat(g::text, g % 1000) FROM generate_series(1, 5000) g
        ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING t
) SELECT * FROM w LIMIT 5;
WITH w AS (
    INSERT INTO it SELECT g, repeat(g::text, g % 1000) FROM generate_series(1001, 4000) g
        ON CONFLICT (i) DO UPDATE SET i = -EXCLUDED.i RETURNING t
) SELECT * FROM w LIMIT 5;
SELECT count(*), sign(i) FROM it GROUP BY (sign(i));
