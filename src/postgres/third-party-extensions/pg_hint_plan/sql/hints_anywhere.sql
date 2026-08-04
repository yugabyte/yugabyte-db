LOAD 'pg_hint_plan';
SET client_min_messages TO log;
\set SHOW_CONTEXT always
SET pg_hint_plan.debug_print TO on;

explain (costs false)
select * from t1 join t2 on t1.id = t2.id where '/*+HashJoin(t1 t2)*/' <> '';

set pg_hint_plan.hints_anywhere = on;
explain (costs false)
select * from t1 join t2 on t1.id = t2.id where '/*+HashJoin(t1 t2)*/' <> '';

set pg_hint_plan.hints_anywhere = off;
explain (costs false)
select * from t1 join t2 on t1.id = t2.id where '/*+HashJoin(t1 t2)*/' <> '';

set pg_hint_plan.hints_anywhere = on;
/*+ MergeJoin(t1 t2) */
explain (costs false)
select * from t1 join t2 on t1.val = t2.val where '/*+HashJoin(t1 t2)*/' <> '';

/*+ HashJoin(t1 t2) */
explain (costs false)
select * from t1 join t2 on t1.val = t2.val where '/*+MergeJoin(t1 t2)*/' <> '';
