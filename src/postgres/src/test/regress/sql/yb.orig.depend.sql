CREATE TABLE test_table(k INT PRIMARY KEY, v INT);
CREATE VIEW test_view AS SELECT k FROM test_table WHERE v = 10;
CREATE INDEX ON test_view (k);
CREATE TYPE type_pair AS (f1 INT, f2 INT);
CREATE TYPE type_pair_with_int AS (f1 type_pair, f2 int);
DROP TABLE test_table; -- should fail
DROP TABLE test_table CASCADE;
DROP TYPE type_pair; -- should fail
DROP TYPE type_pair CASCADE;
DROP TYPE type_pair_with_int;
