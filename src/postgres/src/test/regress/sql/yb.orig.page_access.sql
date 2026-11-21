--
-- Code that reads PostgreSQL pages, the visibility map, the free space map or
-- heap scan state must reject YB relations, which have none of these, and
-- keep working on temp tables.
--
CREATE EXTENSION amcheck;
CREATE EXTENSION pg_freespacemap;
CREATE EXTENSION pg_prewarm;
CREATE EXTENSION pg_surgery;
CREATE EXTENSION pg_visibility;

CREATE TABLE yb_t (k int PRIMARY KEY, v int);
INSERT INTO yb_t SELECT g, g FROM generate_series(1, 10) g;

-- pg_visibility: the visibility map
SELECT * FROM pg_visibility_map('yb_t', 0);
SELECT * FROM pg_visibility('yb_t', 0);
SELECT count(*) FROM pg_visibility('yb_t');
SELECT * FROM pg_visibility_map_summary('yb_t');
SELECT * FROM pg_check_frozen('yb_t');
SELECT pg_truncate_visibility_map('yb_t');

-- pg_freespacemap: the free space map
SELECT pg_freespace('yb_t', 0);

-- amcheck and pg_surgery: heap pages
SELECT * FROM verify_heapam('yb_t');
SELECT heap_force_kill('yb_t', ARRAY['(0,1)']::tid[]);
SELECT heap_force_freeze('yb_t', ARRAY['(0,1)']::tid[]);

-- pg_prewarm: smgr
SELECT pg_prewarm('yb_t');
SELECT pg_prewarm('yb_t', 'prefetch');
SELECT pg_prewarm('yb_t_pkey');

-- currtid2: heap scan state
SELECT currtid2('yb_t', '(0,1)');

-- ANALYZE skips the visibility map of YB relations
ANALYZE yb_t;
SELECT relallvisible FROM pg_class WHERE relname = 'yb_t';
-- With the cost-based optimizer on, CREATE INDEX updates the table's stats,
-- which also skips the visibility map
SET yb_enable_cbo = on;
CREATE INDEX yb_t_v_idx ON yb_t (v);
RESET yb_enable_cbo;

-- Temp tables are heap tables and keep working
CREATE TEMP TABLE tmp_t (k int PRIMARY KEY, v int);
INSERT INTO tmp_t SELECT g, g FROM generate_series(1, 10) g;
SELECT * FROM pg_visibility_map('tmp_t', 0);
SELECT count(*) FROM pg_visibility('tmp_t');
SELECT * FROM pg_visibility_map_summary('tmp_t');
SELECT pg_freespace('tmp_t', 0);
SELECT * FROM verify_heapam('tmp_t');
SELECT heap_force_freeze('tmp_t', ARRAY['(0,1)']::tid[]);
SELECT pg_prewarm('tmp_t');
SELECT currtid2('tmp_t', '(0,1)');
ANALYZE tmp_t;

DROP TABLE tmp_t;
DROP TABLE yb_t;
DROP EXTENSION pg_visibility;
DROP EXTENSION pg_surgery;
DROP EXTENSION pg_prewarm;
DROP EXTENSION pg_freespacemap;
DROP EXTENSION amcheck;
