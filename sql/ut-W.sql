LOAD 'pg_hint_plan';
ALTER SYSTEM SET session_preload_libraries TO 'pg_hint_plan';
SET pg_hint_plan.enable_hint TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;


CREATE TABLE s1.tl (a int);
INSERT INTO s1.tl (SELECT a FROM generate_series(0, 100000) a);

-- Queries on ordinary tables
EXPLAIN (COSTS false) SELECT * FROM s1.t1;

SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
/*+Parallel(t1 10)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1;

/*+Parallel(t1 10 soft)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1;
SET parallel_setup_cost to DEFAULT;
SET parallel_tuple_cost to DEFAULT;
SET min_parallel_relation_size to DEFAULT;

/*+Parallel(t1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM s1.t1;

-- Queries on inheritance tables
SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
/*+Parallel(p1 10)*/
EXPLAIN (COSTS false) SELECT * FROM p1;
SET parallel_setup_cost to DEFAULT;
SET parallel_tuple_cost to DEFAULT;
SET min_parallel_relation_size to DEFAULT;

/*+Parallel(p1 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1;

-- hinting on children don't work but enables parallel
/*+Parallel(p1_c1 10 hard)*/
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
SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
/*+Parallel(p1 10)*/
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;
SET parallel_setup_cost to DEFAULT;
SET parallel_tuple_cost to DEFAULT;
SET min_parallel_relation_size to DEFAULT;

/*+Parallel(p2 10 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;

/*+Parallel(p2 10 hard) Parallel(p1 5 hard) */
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;


-- Mixture with a scan hint
-- p1 can be parallel
/*+Parallel(p1 10 hard) IndexScan(p2) */
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;

-- seqscan doesn't harm parallelism
/*+Parallel(p1 10 hard) SeqScan(p1) */
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;

-- parallelism is not available for the case
/*+Parallel(p1 10 hard) IndexScan(p1) */
EXPLAIN (COSTS false) SELECT * FROM p1 join p2 on p1.id = p2.id;


-- Parallel on UNION
EXPLAIN (COSTS false) SELECT id FROM p1 UNION ALL SELECT id FROM p2;

-- some of the scans are not parallel, so this cannot be parallel
SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
SET max_parallel_workers_per_gather to 0;
/*+Parallel(p1 10) */
EXPLAIN (COSTS false) SELECT id FROM p1 UNION ALL SELECT id FROM p2;

-- all children are parallel, so this can be parallel
/*+Parallel(p1 10) Parallel(p2 10) */
EXPLAIN (COSTS false) SELECT id FROM p1 UNION ALL SELECT id FROM p2;
SET parallel_setup_cost to DEFAULT;
SET parallel_tuple_cost to DEFAULT;
SET min_parallel_relation_size to DEFAULT;
SET max_parallel_workers_per_gather to DEFAULT;

/*+Parallel(p1 10 hard)Parallel(p2 10 hard) */
EXPLAIN (COSTS false) SELECT id FROM p1 UNION ALL SELECT id FROM p2;


-- num of workers of non-hinted relations should be default value
SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
SET max_parallel_workers_per_gather to 3;

/*+Parallel(p1 10 hard) */
EXPLAIN (COSTS false) SELECT * FROM p1 join t1 on p1.id = t1.id;

-- Negative hint
SET parallel_setup_cost to 0;
SET parallel_tuple_cost to 0;
SET min_parallel_relation_size to 0;
SET max_parallel_workers_per_gather to 5;
EXPLAIN (COSTS false) SELECT * FROM p1;

/*+Parallel(p1 0 hard)*/
EXPLAIN (COSTS false) SELECT * FROM p1;

-- Errors
/*+Parallel(p1 100x hard)Parallel(p1 -1000 hard)Parallel(p1 1000000 hard)
   Parallel(p1 10 hoge)Parallel(p1)Parallel(p1 100 soft x)*/
EXPLAIN (COSTS false) SELECT id FROM p1 UNION ALL SELECT id FROM p2;

ALTER SYSTEM SET session_preload_libraries TO DEFAULT;
SELECT pg_reload_conf();
