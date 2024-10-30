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
DROP INDEX b1_idx;
DROP INDEX b2_idx;

--- Nulls
-- NULLS DISTINCT
CREATE UNIQUE INDEX NONCONCURRENTLY ah_idx ON ab_tab (a HASH);
INSERT INTO ab_tab VALUES (null, null);
INSERT INTO ab_tab VALUES (null, null) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a IS NULL ORDER BY b;
-- Reset.
DELETE FROM ab_tab WHERE a IS null;
-- NULLS NOT DISTINCT
DROP INDEX ah_idx;
CREATE UNIQUE INDEX NONCONCURRENTLY ah_idx ON ab_tab (a HASH) NULLS NOT DISTINCT;
/* TODO(jason): uncomment when NULLS NOT DISTINCT is supported
INSERT INTO ab_tab VALUES (null, null);
INSERT INTO ab_tab VALUES (null, null) ON CONFLICT DO NOTHING;
SELECT * FROM ab_tab WHERE a IS NULL ORDER BY b;
*/

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
INSERT INTO pp (j) SELECT g * 7 % 40 FROM generate_series(1, 40) g ON CONFLICT (j) DO UPDATE SET i = EXCLUDED.i + 100;
SELECT * FROM pp ORDER BY i % 100;

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
SELECT * FROM varlen ORDER BY t;

--- ON CONFLICT DO UPDATE edge cases
CREATE TABLE ioc (i int, PRIMARY KEY (i ASC));
BEGIN;
INSERT INTO ioc VALUES (1), (1) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 20;
ROLLBACK;
INSERT INTO ioc VALUES (1);
BEGIN;
INSERT INTO ioc VALUES (1), (1) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 20;
TABLE ioc;
ROLLBACK;
BEGIN;
INSERT INTO ioc VALUES (20), (20) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + 20;
ROLLBACK;
BEGIN;
INSERT INTO ioc VALUES (20), (1) ON CONFLICT (i) DO UPDATE SET i = 20;
ROLLBACK;
BEGIN;
INSERT INTO ioc VALUES (1), (20) ON CONFLICT (i) DO UPDATE SET i = 20;
ROLLBACK;

--- UPDATE SET edge case
CREATE TABLE texts (t text PRIMARY KEY);
CREATE FUNCTION agg_texts() RETURNS text AS $$SELECT 'agg=[' || string_agg(t, ',') || ']' FROM texts$$ LANGUAGE sql;
INSERT INTO texts VALUES ('i'), ('j') ON CONFLICT (t) DO UPDATE SET t = agg_texts();
INSERT INTO texts VALUES ('i'), ('j') ON CONFLICT (t) DO UPDATE SET t = agg_texts();
SELECT * FROM texts ORDER BY t;

--- ON CONFLICT DO UPDATE YbExecDoUpdateIndexTuple
CREATE TABLE index_update (a int PRIMARY KEY, b int);
INSERT INTO index_update VALUES (1, 2);
INSERT INTO index_update VALUES (1, 3) ON CONFLICT (a) DO UPDATE SET b = EXCLUDED.b;

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
CREATE TABLE parent_table (n numeric, t text, b bool, PRIMARY KEY (t, n));
CREATE TABLE child_table (b bool PRIMARY KEY, n numeric, t text, FOREIGN KEY (t, n) REFERENCES parent_table);
INSERT INTO parent_table VALUES (1, '1', true), (2, '2', true);
INSERT INTO child_table VALUES (false, 1, '1') ON CONFLICT DO NOTHING;
INSERT INTO child_table VALUES (false, 1, '1') ON CONFLICT (b) DO UPDATE SET b = true;
TABLE child_table;
INSERT INTO child_table VALUES (false, 2, '1') ON CONFLICT (b) DO UPDATE SET b = true;
INSERT INTO child_table VALUES (true, 2, '1') ON CONFLICT (b) DO UPDATE SET t = '2';
TABLE child_table;

--- WITH
CREATE TABLE with_a (i int, PRIMARY KEY (i DESC));
CREATE TABLE with_b (i int, PRIMARY KEY (i ASC));
INSERT INTO with_a VALUES (generate_series(1, 10));
INSERT INTO with_b VALUES (generate_series(11, 20));
BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(1, 11)) ON CONFLICT (i) DO NOTHING RETURNING i
) INSERT INTO with_b VALUES (generate_series(1, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
TABLE with_b;
ABORT;
BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(1, 11)) ON CONFLICT (i) DO NOTHING RETURNING i
) INSERT INTO with_a VALUES (generate_series(1, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;
BEGIN;
WITH w(i) AS (
    INSERT INTO with_a VALUES (generate_series(6, 11)) ON CONFLICT (i) DO NOTHING RETURNING i
) INSERT INTO with_a VALUES (generate_series(10, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;
BEGIN;
WITH w(i) AS (
    DELETE FROM with_a WHERE i = 10 RETURNING i
) INSERT INTO with_a VALUES (generate_series(9, 15)) ON CONFLICT (i) DO UPDATE SET i = EXCLUDED.i + (SELECT i FROM w);
TABLE with_a;
ABORT;
