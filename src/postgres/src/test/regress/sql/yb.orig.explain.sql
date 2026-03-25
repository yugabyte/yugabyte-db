CREATE TABLE p1 (k INT PRIMARY KEY);
CREATE TABLE p2 (k INT PRIMARY KEY);
CREATE TABLE t_test (k INT PRIMARY KEY,
					 p1_fk INT REFERENCES p1(k),
					 p2_fk INT REFERENCES p2(k) DEFERRABLE INITIALLY DEFERRED);

-- Load some data into each of the tables
INSERT INTO p1 (SELECT generate_series(1, 15));
INSERT INTO p2 (SELECT generate_series(1, 15));

-- Commit stats are not displayed by default
EXPLAIN (ANALYZE, DIST, COSTS OFF) INSERT INTO t_test VALUES (1, 1, 1);

-- When enabled, commit stats should include read requests indicating the deferred foreign key check
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (2, 2, 2);

-- Commit stats should not be shown if EXPLAIN is invoked within an explicit transaction
BEGIN;
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (3, 3, 3);
ROLLBACK;

-- Commit stats should be displayed for the last statement in a multi-statement query
-- when autocommit is turned on. This is only applicable to the simple query protocol
-- which is used by the regress tests.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (4, 4, 4) \; EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (5, 5, 5);

\set AUTOCOMMIT OFF
-- However, when autocommit is turned off, commit stats should not be displayed as nothing is committed.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (6, 6, 6) \; EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (7, 7, 7);
ROLLBACK;
\set AUTOCOMMIT ON

-- Commit stats should not be displayed if there is nothing to display.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF) INSERT INTO p1 VALUES (101);

-- Commit stats should respect the other options provided to EXPLAIN. No commit stats should be
-- displayed for any of the following queries.
EXPLAIN (COSTS OFF) INSERT INTO t_test VALUES (8, 8, 8);
EXPLAIN (COMMIT, COSTS OFF) INSERT INTO t_test VALUES (8, 8, 8);
EXPLAIN (ANALYZE, COMMIT, COSTS OFF) INSERT INTO t_test VALUES (9, 9, 9);
EXPLAIN (ANALYZE, DIST, COMMIT, SUMMARY OFF, COSTS OFF) INSERT INTO t_test VALUES (10, 10, 10);

-- Commit stats should also respect the format option.
EXPLAIN (ANALYZE, DIST, COMMIT, COSTS OFF, FORMAT JSON) INSERT INTO t_test VALUES (11, 11, 11);

SELECT * FROM t_test ORDER BY k;

-- DEBUG without DIST should produce a warning and no debug metrics
EXPLAIN (ANALYZE, DEBUG, COSTS OFF) SELECT * FROM p1 WHERE k = 1;

-- DEBUG with yb_explain_hide_non_deterministic_fields should produce a warning and no debug metrics
SHOW yb_explain_hide_non_deterministic_fields;
EXPLAIN (ANALYZE, DEBUG, DIST, COSTS OFF) SELECT * FROM p1 WHERE k = 1;

-- Check planner stats fields (#30768 shouldn't be printed twice!)
SET client_min_messages TO 'warning';
drop function if exists explain_filter_to_json_text cascade;
create function explain_filter_to_json_text(text) returns text
language plpgsql as
$$
declare
    data text := '';
    ln text;
begin
    for ln in execute $1
    loop
        -- Replace any numeric word with just '0'
        ln := regexp_replace(ln, '\m\d+\M', '0', 'g');
        data := data || ln;
    end loop;
    return data;
end;
$$;

SET yb_explain_hide_non_deterministic_fields = off;
SET yb_enable_cbo = on;

select explain_filter_to_json_text($$/*+ IndexScan(p1) */EXPLAIN (ANALYZE, DIST, DEBUG, COSTS OFF, SUMMARY OFF, FORMAT JSON) SELECT * from p1 where k = 1$$);

RESET yb_explain_hide_non_deterministic_fields;
RESET yb_enable_cbo;
