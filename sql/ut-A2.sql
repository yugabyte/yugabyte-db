LOAD 'pg_hint_plan';
SET pg_hint_plan.enable TO on;
SET pg_hint_plan.debug_print TO on;
SET client_min_messages TO LOG;
SET search_path TO public;

----
---- No. A-13 call planner recursively
----

CREATE OR REPLACE FUNCTION nested_planner(cnt int) RETURNS int AS $$
DECLARE
    new_cnt int;
BEGIN
    RAISE NOTICE 'nested_planner(%)', cnt;

    /* 再帰終了の判断 */
    IF cnt <= 1 THEN
        RETURN 0;
    END IF;

    EXECUTE '/*+ IndexScan(t_1) */'
            ' SELECT nested_planner($1) FROM s1.t1 t_1'
            ' JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)'
            ' ORDER BY t_1.c1 LIMIT 1'
        INTO new_cnt USING cnt - 1;

    RETURN new_cnt;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

----
---- No. A-13-2 use hint of main query
----

--No.13-2-1
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_1)*/
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;

----
---- No. A-13-3 output number of times of debugging log
----

--No.13-3-1
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_2)*/
EXPLAIN (COSTS false) SELECT nested_planner(1) FROM s1.t1 t_1 ORDER BY t_1.c1;

--No.13-3-2
EXPLAIN (COSTS false) SELECT nested_planner(2) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_2)*/
EXPLAIN (COSTS false) SELECT nested_planner(2) FROM s1.t1 t_1 ORDER BY t_1.c1;

--No.13-3-3
EXPLAIN (COSTS false) SELECT nested_planner(5) FROM s1.t1 t_1 ORDER BY t_1.c1;
/*+SeqScan(t_2)*/
EXPLAIN (COSTS false) SELECT nested_planner(5) FROM s1.t1 t_1 ORDER BY t_1.c1;

----
---- No. A-13-4 output of debugging log on hint status
----

--No.13-4-1
/*+HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.13-4-2
/*+HashJoin(st_1 st_2)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 st_1
   JOIN s1.t2 st_2 ON (st_1.c1 = st_2.c1)
  ORDER BY st_1.c1;

--No.13-4-3
/*+HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 st_1
   JOIN s1.t2 st_2 ON (st_1.c1 = st_2.c1)
  ORDER BY st_1.c1;

--No.13-4-4
/*+HashJoin(st_1 st_2)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.13-4-5
/*+HashJoin(t_1 t_1)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 t_1
  ORDER BY t_1.c1;

--No.13-4-6
CREATE OR REPLACE FUNCTION nested_planner_one_t(cnt int) RETURNS int AS $$
DECLARE
    new_cnt int;
BEGIN
    RAISE NOTICE 'nested_planner_one_t(%)', cnt;

    IF cnt <= 1 THEN
        RETURN 0;
    END IF;

    EXECUTE '/*+ IndexScan(t_1) */'
            ' SELECT nested_planner_one_t($1) FROM s1.t1 t_1'
            ' ORDER BY t_1.c1 LIMIT 1'
        INTO new_cnt USING cnt - 1;

    RETURN new_cnt;
END;
$$ LANGUAGE plpgsql IMMUTABLE;

EXPLAIN (COSTS false)
 SELECT nested_planner_one_t(2) FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;
/*+HashJoin(t_1 t_1)*/
EXPLAIN (COSTS false)
 SELECT nested_planner_one_t(2) FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

DROP FUNCTION nested_planner_one_t(int);

--No.13-4-7
/*+HashJoin(t_1 t_1)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;

--No.13-4-8
/*+MergeJoin(t_1 t_2)HashJoin(t_1 t_2)*/
EXPLAIN (COSTS false)
 SELECT nested_planner(2) FROM s1.t1 t_1
   JOIN s1.t2 t_2 ON (t_1.c1 = t_2.c1)
  ORDER BY t_1.c1;
