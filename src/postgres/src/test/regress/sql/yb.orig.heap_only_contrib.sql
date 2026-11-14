--
-- Extensions that read heap or index pages directly must reject YB relations
-- instead of misreading them, and keep working on temp tables.
--
CREATE EXTENSION pageinspect;
CREATE EXTENSION pgrowlocks;
CREATE EXTENSION pgstattuple;

CREATE TABLE yb_t (k int PRIMARY KEY, v int);
CREATE INDEX yb_t_v_idx ON yb_t (v);
INSERT INTO yb_t SELECT g, g FROM generate_series(1, 10) g;

-- pgrowlocks
SELECT * FROM pgrowlocks('yb_t');
BEGIN;
SELECT k FROM yb_t WHERE k = 1 FOR UPDATE;
SELECT * FROM pgrowlocks('yb_t');
ROLLBACK;

-- pgstattuple
SELECT * FROM pgstattuple('yb_t');
SELECT * FROM pgstattuple('yb_t'::regclass);
SELECT * FROM pgstattuple('yb_t_pkey');
SELECT * FROM pgstattuple_approx('yb_t');
SELECT pg_relpages('yb_t');
SELECT pg_relpages('yb_t_v_idx'::regclass);
SELECT * FROM pgstatindex('yb_t_v_idx');

-- pageinspect stops at the page count, which is 0 for YB relations
SELECT length(get_raw_page('yb_t', 0));
SELECT length(get_raw_page('yb_t_v_idx', 'main', 0));

-- Temp tables are heap tables and keep working
CREATE TEMP TABLE tmp_t (k int PRIMARY KEY, v int);
INSERT INTO tmp_t SELECT g, g FROM generate_series(1, 10) g;
BEGIN;
SELECT k FROM tmp_t WHERE k IN (1, 2) FOR UPDATE;
SELECT locked_row, multi, modes FROM pgrowlocks('tmp_t');
COMMIT;
SELECT tuple_count, dead_tuple_count FROM pgstattuple('tmp_t');
SELECT table_len > 0 AS has_pages FROM pgstattuple_approx('tmp_t');
SELECT pg_relpages('tmp_t');
SELECT leaf_pages FROM pgstatindex('tmp_t_pkey');
SELECT length(get_raw_page('tmp_t', 0));

DROP TABLE tmp_t;
DROP TABLE yb_t;
DROP EXTENSION pgstattuple;
DROP EXTENSION pgrowlocks;
DROP EXTENSION pageinspect;
