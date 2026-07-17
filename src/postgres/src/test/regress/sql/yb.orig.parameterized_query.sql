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

-- All parameter combinations yield identical output: the first prints, the
-- rest collapse, regardless of which combination produced them.
\set Q1 1
\set Q2 2
\set query 'SELECT v FROM pq_test WHERE k IN (:P, :Q, 1, 2) ORDER BY v;'
\i :run_query

-- A new \i :run_query never collapses against the previous one: the first
-- combination prints in full again.
\i :run_query

-- Zero-row results output multiple lines and collapse.
\set query 'SELECT v FROM pq_test WHERE k = -:P;'
\i :run_query

-- Zero-line outputs never collapse.
\set query 'UPDATE pq_test SET v = v WHERE k = -:P;'
\i :run_query

-- One-line outputs never collapse.
\set query 'COPY (SELECT 1 WHERE :P > 0) TO STDOUT;'
\i :run_query

-- Bare queries bypass run_query: identical outputs print verbatim.
\set query 'SELECT v FROM pq_test ORDER BY v;'
:query
:query

\unset P1
\unset P2
\unset Q1
\unset Q2

DROP TABLE pq_test;
