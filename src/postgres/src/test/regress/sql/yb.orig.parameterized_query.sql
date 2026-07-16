-- Tests for the yb_commands/parameterized_query.sql infrastructure itself.
-- Basic iteration behavior is asserted by the many parameterized tests.  This
-- file covers behavior not observable there.

\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/parameterized_query.sql'
\i :filename

CREATE TABLE pq_test (k int PRIMARY KEY, v text);
INSERT INTO pq_test VALUES (1, 'one'), (2, 'two'), (3, 'three');

\set P1 1
\set P2 2
\set query 'SELECT v FROM pq_test WHERE k = :P;'

-- A value defined above a gap in a used dimension raises an error.
\set P4 4
\i :run_query
\unset P4

-- A gap in an unused dimension is not checked.
\set Q4 4
\i :run_query
\unset Q4

-- A used dimension with fewer than two values raises an error.
\set query 'SELECT :Q FROM pq_test;'
\i :run_query
\set Q1 1
\i :run_query
\unset Q1

-- A query with no placeholders raises an error.
\set query 'SELECT count(*) FROM pq_test;'
\i :run_query

\unset P1
\unset P2

DROP TABLE pq_test;
