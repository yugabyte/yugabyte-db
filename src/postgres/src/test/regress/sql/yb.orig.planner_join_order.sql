CREATE TABLE t_10000 (id int PRIMARY KEY, val int);
CREATE TABLE t_1000 (id int PRIMARY KEY, val int);
CREATE TABLE t_100 (id int PRIMARY KEY, val int);
CREATE TABLE t_1000000 (id int PRIMARY KEY, val int);

INSERT INTO t_10000 SELECT i, i FROM (SELECT generate_series(1, 10000) i) t;
INSERT INTO t_1000 SELECT i, i FROM (SELECT generate_series(1, 1000) i) t;
INSERT INTO t_100 SELECT i, i FROM (SELECT generate_series(1, 100) i) t;
INSERT INTO t_1000000 SELECT i, i FROM (SELECT generate_series(1, 1000000) i) t;

CREATE OR REPLACE PROCEDURE update_sizes(t_10000_size int, t_1000_size int, t_100_size int, t_1000000_size int)
LANGUAGE plpgsql
AS $$
BEGIN
    SET yb_non_ddl_txn_for_sys_tables_allowed=1;
    BEGIN
        UPDATE pg_class SET reltuples=t_10000_size WHERE relname='t_10000';
        UPDATE pg_class SET reltuples=t_1000_size WHERE relname='t_1000';
        UPDATE pg_class SET reltuples=t_100_size WHERE relname='t_100';
        UPDATE pg_class SET reltuples=t_1000000_size WHERE relname='t_1000000';
        UPDATE pg_yb_catalog_version SET current_version=current_version+1 WHERE db_oid=1;
    EXCEPTION
        -- Workaround for [#10405], exception clause makes buffers flushed
        WHEN OTHERS THEN
            SET yb_non_ddl_txn_for_sys_tables_allowed=0;
    END;
    SET yb_non_ddl_txn_for_sys_tables_allowed=0;
END;
$$;
-- Updating table sizes enables the query planner to choose appropriate join orders. In other words,
-- the larger table is chosen as the inner table if the larger table fits in memory.
-- In this example, t_1000 is always being chosen as the the inner table because it's larger.
call update_sizes(10000, 1000, 100, 1000000);
-- Executing a sleep for a small amount of time for the size changes to be reflected.
select pg_sleep(1);
EXPLAIN (COSTS false) SELECT * FROM t_1000, t_100 WHERE t_1000.id = t_100.id;
EXPLAIN (COSTS false) SELECT * FROM t_100, t_1000 WHERE t_100.id = t_1000.id;
EXPLAIN (COSTS false) SELECT * FROM t_10000, t_1000000 WHERE t_10000.id = t_1000000.id;
EXPLAIN (COSTS false) SELECT * FROM t_1000000, t_10000 WHERE t_1000000.id = t_10000.id;
-- Here we, reset the table size to -1. In this situation, query planner is unaware of table sizes
-- Hence, it will choose the inner table based on the order in which it appears in the query
-- irrespective of the actual size.
call update_sizes(-1, -1, -1, -1);
select pg_sleep(1);
EXPLAIN (COSTS false) SELECT * FROM t_1000, t_100 WHERE t_1000.id = t_100.id;
EXPLAIN (COSTS false) SELECT * FROM t_100, t_1000 WHERE t_100.id = t_1000.id;
EXPLAIN (COSTS false) SELECT * FROM t_10000, t_1000000 WHERE t_10000.id = t_1000000.id;
EXPLAIN (COSTS false) SELECT * FROM t_1000000, t_10000 WHERE t_1000000.id = t_10000.id;
-- Updating table sizes arranges join orders based on table sizes. Updating costs enables the query
-- planner to prioritize NestedLoop vs HashJoins during different occasions based on the size of
-- the tables. In the second example, due to varying sizes of the table, the join order changes.
-- The query planner tries to optimize the join by prioritizing joining smaller tables with larger
-- tables. In this example, it joins t_1000 and t_100 and then subsequently joins the result with
-- t_10000. However, when table sizes are unknown, it joins t_1000 and t_10000 and its resultant
-- with t_100.
call update_sizes(10000, 1000, 100, 1000000);
select pg_sleep(1);
EXPLAIN (COSTS false) SELECT * FROM t_1000000 t4 INNER JOIN t_100 t1 ON t4.id = t1.id
                                                 INNER JOIN t_1000 t2 ON t1.id = t2.id
                                                 INNER JOIN t_10000 t3 ON t2.id = t3.id;
EXPLAIN (COSTS false) SELECT * FROM t_10000 t3 INNER JOIN t_1000 t2 ON t3.id = t2.id
                                               INNER JOIN t_100 t1 ON t2.val = t1.val
                                               WHERE t3.id < 10;
call update_sizes(-1, -1, -1, -1);
select pg_sleep(1);
EXPLAIN (COSTS false) SELECT * FROM t_1000000 t4 INNER JOIN t_100 t1 ON t4.id = t1.id
                                                 INNER JOIN t_1000 t2 ON t1.id = t2.id
                                                 INNER JOIN t_10000 t3 ON t2.id = t3.id;
EXPLAIN (COSTS false) SELECT * FROM t_10000 t3 INNER JOIN t_1000 t2 ON t3.id = t2.id
                                               INNER JOIN t_100 t1 ON t2.val = t1.val
                                               WHERE t3.id < 10;
