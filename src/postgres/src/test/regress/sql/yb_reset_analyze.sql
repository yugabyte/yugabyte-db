\c yugabyte yugabyte
SET client_min_messages = warning;
DROP DATABASE IF EXISTS reset_analyze_test;
DROP USER IF EXISTS yb_su;
DROP USER IF EXISTS yb_user1;
DROP USER IF EXISTS yb_user2;
DROP USER IF EXISTS yb_user3;

-- Create test users
CREATE USER yb_su superuser;
CREATE USER yb_user1;
CREATE USER yb_user2;
CREATE USER yb_user3;

-- Create a database
CREATE DATABASE reset_analyze_test OWNER = yb_user1;

-- Grant permissions for this database.
\c reset_analyze_test yugabyte
GRANT ALL ON SCHEMA public TO yb_user1;
GRANT ALL ON SCHEMA public TO yb_user2;
GRANT ALL ON SCHEMA public TO yb_user3;

-- Create tables owned by each user
\c - yb_user1
CREATE SEQUENCE seq_u1;
CREATE TABLE table_u1 (id int, c1 int, c2 int);
INSERT INTO table_u1
    SELECT nextval('seq_u1'), i % 4, i / 2 FROM generate_series(1, 10) i;
CREATE INDEX table_u1_c2_idx on table_u1 (c2, c1);
CREATE TABLE partitioned_u1 (id int, c1 int, c2 int) PARTITION BY RANGE (c2);
CREATE TABLE part1_u1 PARTITION OF partitioned_u1 FOR VALUES FROM (minvalue) TO (5);
CREATE TABLE part2_u1 PARTITION OF partitioned_u1 FOR VALUES FROM (5) TO (maxvalue);
CREATE INDEX part_u1_c2_c1_idx ON partitioned_u1 (c2, c1);
INSERT INTO partitioned_u1 SELECT * FROM table_u1;
CREATE MATERIALIZED VIEW mv_u1 AS SELECT t1.c1, t2.c2 FROM table_u1 t1 JOIN table_u1 t2 ON t1.id = t2.c1;
CREATE INDEX mv_u1_c1 ON mv_u1 (c1);
CREATE STATISTICS sta_table_u1 ON c2, c1 FROM table_u1;

\c - yb_user2
CREATE SEQUENCE seq_u2;
CREATE TABLE table_u2 (id int, c1 int, c2 int);
INSERT INTO table_u2
    SELECT nextval('seq_u2'), i % 4, i / 2 FROM generate_series(1, 10) i;
CREATE INDEX table_u2_c2_idx on table_u2 (c2, c1);
CREATE TABLE partitioned_u2 (id int, c1 int, c2 int) PARTITION BY RANGE (c2);
CREATE TABLE part1_u2 PARTITION OF partitioned_u2 FOR VALUES FROM (minvalue) TO (5);
CREATE TABLE part2_u2 PARTITION OF partitioned_u2 FOR VALUES FROM (5) TO (maxvalue);
CREATE INDEX part_u2_c2_c1_idx ON partitioned_u2 (c2, c1);
INSERT INTO partitioned_u2 SELECT * FROM table_u2;
CREATE MATERIALIZED VIEW mv_u2 AS SELECT t1.c1, t2.c2 FROM table_u2 t1 JOIN table_u2 t2 ON t1.id = t2.c1;
CREATE INDEX mv_u2_c1 ON mv_u2 (c1);
CREATE STATISTICS sta_table_u2 ON c1, c2 FROM table_u2;

\c - yb_user3
CREATE SEQUENCE seq_u3;
CREATE TABLE table_u3 (id int, c1 int, c2 int);
INSERT INTO table_u3
    SELECT nextval('seq_u3'), i % 4, i / 2 FROM generate_series(1, 10) i;
CREATE INDEX table_u3_c2_idx on table_u3 (c2, c1);
CREATE TABLE partitioned_u3 (id int, c1 int, c2 int) PARTITION BY RANGE (c2);
CREATE TABLE part1_u3 PARTITION OF partitioned_u3 FOR VALUES FROM (minvalue) TO (5);
CREATE TABLE part2_u3 PARTITION OF partitioned_u3 FOR VALUES FROM (5) TO (maxvalue);
CREATE INDEX part_u3_c2_c1_idx ON partitioned_u3 (c2, c1);
INSERT INTO partitioned_u3 SELECT * FROM table_u3;
CREATE MATERIALIZED VIEW mv_u3 AS SELECT t1.c1, t2.c2 FROM table_u3 t1 JOIN table_u3 t2 ON t1.id = t2.c1;
CREATE INDEX mv_u3_c1 ON mv_u3 (c1);
CREATE STATISTICS sta_table_u3 ON c1, c2 FROM table_u3;

\c - yb_su
CREATE SEQUENCE seq_su;
CREATE TABLE table_su (id int, c1 int, c2 int);
INSERT INTO table_su
    SELECT nextval('seq_su'), i % 4, i / 2 FROM generate_series(1, 10) i;
CREATE INDEX table_su_c2_idx on table_su (c2, c1);
CREATE TABLE partitioned_su (id int, c1 int, c2 int) PARTITION BY RANGE (c2);
CREATE TABLE part1_su PARTITION OF partitioned_su FOR VALUES FROM (minvalue) TO (5);
CREATE TABLE part2_su PARTITION OF partitioned_su FOR VALUES FROM (5) TO (maxvalue);
CREATE INDEX part_su_c2_c1_idx ON partitioned_su (c2, c1);
INSERT INTO partitioned_su SELECT * FROM table_su;
CREATE MATERIALIZED VIEW mv_su AS SELECT t1.c1, t2.c2 FROM table_su t1 JOIN table_su t2 ON t1.id = t2.c1;
CREATE INDEX mv_su_c1 ON mv_su (c1);
CREATE STATISTICS sta_table_su ON c2, c1 FROM table_su;


-- Create views and procedures for verifications
SET client_min_messages = warning;

CREATE OR REPLACE VIEW all_stats AS
    SELECT rolname AS owner, nspname AS schemaname, relname,
           relkind AS kind, relisshared AS shared, reltuples,
           ncolstats, xn_distinct, xdependencies
    FROM pg_class c
         JOIN pg_namespace nc ON nc.oid = relnamespace
         JOIN pg_authid r ON r.oid = relowner
         JOIN (
             SELECT
                 attrelid,
                 count(stadistinct) AS ncolstats
             FROM pg_attribute a
                  LEFT JOIN pg_statistic s
                      ON starelid = attrelid AND staattnum = attnum
             GROUP BY attrelid
         ) AS cstats ON attrelid = c.oid
         LEFT JOIN (
             SELECT
                 stxrelid,
                 string_agg(stxdndistinct::text, ', ' ORDER BY stxkind, stxkeys)
                     AS xn_distinct,
                 string_agg(stxddependencies, ', ' ORDER BY stxkind, stxkeys)
                     AS xdependencies
             FROM pg_statistic_ext e JOIN pg_statistic_ext_data d ON e.oid = d.stxoid
             GROUP BY stxrelid
         ) AS xstats ON stxrelid = attrelid;

GRANT SELECT ON all_stats TO PUBLIC;


DROP TABLE IF EXISTS x_stats;
CREATE TABLE x_stats AS SELECT * FROM all_stats LIMIT 0;

CREATE OR REPLACE PROCEDURE record_stats() AS
$$
BEGIN
    DELETE FROM x_stats;
    INSERT INTO x_stats SELECT * FROM all_stats;
END
$$
LANGUAGE plpgsql
SECURITY DEFINER;

CREATE OR REPLACE VIEW diff_stats(d, owner, schemaname, relname,
    k, s, reltuples, ncolstats, xndv, xdep) AS
(
    SELECT '-' AS d, t.* FROM x_stats AS t
    EXCEPT ALL
    SELECT '-' AS d, t.* FROM all_stats AS t
)
UNION ALL
(
    SELECT '+' AS d, t.* FROM all_stats AS t
    EXCEPT ALL
    SELECT '+' AS d, t.* FROM x_stats AS t
)
ORDER BY 2, 3, 4, 5, 1 DESC LIMIT ALL;

GRANT SELECT ON diff_stats TO PUBLIC;


-- Superuser
\c - yb_su
CALL record_stats();
-- reltuples of some system catalog tables and indexes may be collected during
-- the template DB creation.  Clear them in the recorded stats to pretend they
-- didn't exist.
UPDATE x_stats SET (reltuples, ncolstats) = (-1, 0) WHERE kind IN ('r', 'i') AND reltuples >= 0;

ANALYZE;
SELECT yb_reset_analyze_statistics(null);
SELECT * FROM diff_stats;


-- DB owner
\c - yb_user1
SET client_min_messages = error;
ANALYZE;
SELECT yb_reset_analyze_statistics(null);
SELECT * FROM diff_stats;

-- Non-superuser shouldn't be able to reset stats on shared relations
\c - yb_su
ANALYZE;
\c - yb_user1
CALL record_stats();
SELECT yb_reset_analyze_statistics(null);
SELECT * FROM diff_stats WHERE s = true;


-- Non-DB owner
\c - yb_su
SELECT yb_reset_analyze_statistics(null);
\c - yb_user2
CALL record_stats();
SET client_min_messages = error;
ANALYZE;
SELECT yb_reset_analyze_statistics(null);
SELECT * FROM diff_stats;


-- Non-DB owner shouldn't be able to reset stats on other users' tables
\c - yb_user2
SET client_min_messages = error;
ANALYZE;
\c - yb_user3
CALL record_stats();
SELECT yb_reset_analyze_statistics(null);
SELECT * FROM diff_stats WHERE owner <> 'yb_user3';


--- Single relation, superuser
\c - yb_su
ANALYZE;
\c - yb_su
CALL record_stats();
SELECT yb_reset_analyze_statistics('pg_sequence'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('pg_database'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_su'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u1'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u2'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u3'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');


--- Single relation, DB owner
\c - yb_su
ANALYZE;
\c - yb_user1
CALL record_stats();
SELECT yb_reset_analyze_statistics('pg_sequence'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('pg_database'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_su'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u1'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u2'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u3'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');


-- Single relation, non-DB owner
\c - yb_su
ANALYZE;
\c - yb_user2
CALL record_stats();
SELECT yb_reset_analyze_statistics('pg_sequence'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('pg_database'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_su'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u1'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u2'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');

CALL record_stats();
SELECT yb_reset_analyze_statistics('table_u3'::regclass);
SELECT * FROM diff_stats
    WHERE relname ~* '(pg_database|pg_sequence).*'
          OR owner IN ('yb_su', 'yb_user1', 'yb_user2', 'yb_user3');


-- Error or do nothing
\c - yb_su
ANALYZE;
CALL record_stats();

SELECT yb_reset_analyze_statistics();	-- fail
SELECT * FROM diff_stats;

SELECT yb_reset_analyze_statistics(-1);
SELECT * FROM diff_stats;

SELECT yb_reset_analyze_statistics(0);
SELECT * FROM diff_stats;

SELECT yb_reset_analyze_statistics((SELECT max(oid) FROM pg_class WHERE relkind <> 'r'));
SELECT * FROM diff_stats;


-- Clean up
\c yugabyte yugabyte
DROP DATABASE reset_analyze_test;
DROP USER yb_su;
DROP USER yb_user1;
DROP USER yb_user2;
DROP USER yb_user3;
