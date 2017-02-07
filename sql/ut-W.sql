LOAD 'pg_hint_plan';
ALTER SYSTEM SET session_preload_libraries TO 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;
SET max_parallel_workers_per_gather TO 0;
SET enable_indexscan to false;
SET enable_bitmapscan to false;
SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
SET max_parallel_workers_per_gather to 0;

CREATE TABLE s1.tl (a int);
INSERT INTO s1.tl (SELECT a FROM generate_series(0, 100000) a);

EXPLAIN (COSTS false) SELECT * FROM s1.t1;

/*+Parallel(t1 10)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1;

/*+Parallel(t1 10 soft)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1;

/*+Parallel(t1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1;

-- Inheritnce tables
/*+Parallel(p1 10 soft)*/
EXPLAIN (COSTS false) SELECT * FROM p1;

/*+Parallel(p1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1;

-- Joins
EXPLAIN (COSTS false) SELECT * FROM p1_c1 join p2_c1 on p1_c1.id = p2_c1.id;

/*+Parallel(p1_c1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1_c1 join p2_c1 on p1_c1.id = p2_c1.id;

/*+Parallel(p2_c1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1_c1 join p2_c1 on p1_c1.id = p2_c1.id;

/*+Parallel(p1_c1 10 hard) Parallel(p2_c1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1_c1 join p2_c1 on p1_c1.id = p2_c1.id;

-- Joins on inheritance tables
/*+Parallel(p1 10)*/
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;

/*+Parallel(p2 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;

/*+Parallel(p2 10 hard) Parallel(p1 5 hard) */
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;

-- Negative hint
SET max_parallel_workers_per_gather to 5;
EXPLAIN (COSTS false) SELECT * FROM p1;

/*+Parallel(p1 0 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1;

ALTER SYSTEM SET session_preload_libraries TO DEFAULT;
SELECT pg_reload_conf();
