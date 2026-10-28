# A DROP that finds a dependent object in pg_depend, then waits for the
# lock on that object while a concurrent DDL deletes it and commits.
#
# findDependentObjects() re-checks the pg_depend row after the wait
# (systable_recheck_tuple) and skips the dependent when the row is gone.
# Without a working recheck the DROP proceeds against the deleted object
# and fails with an internal error such as
#   ERROR:  could not find tuple for rule <oid>
#   ERROR:  cache lookup failed for attribute <n> of relation <oid>
#   ERROR:  could not find tuple for attrdef <oid>
#
# Cases:
#   - DROP TABLE ... CASCADE vs DROP MATERIALIZED VIEW built on the table.
#     Both cascade to the MV's _RETURN rule in pg_rewrite (GH #31447).
#   - DROP TYPE ... CASCADE vs DROP TABLE of a table with a column of that
#     type. The cascade targets the column (GH #34106).
#   - DROP SEQUENCE vs ALTER TABLE ... DROP DEFAULT of a default that
#     calls nextval() on the sequence. The dependent is the pg_attrdef row
#     (GH #34092).
#   - DROP SCHEMA ... CASCADE vs ALTER TABLE ... SET SCHEMA moving a table
#     out of the schema. The pg_depend row is updated rather than deleted,
#     so the recheck must also notice a changed row: the table must survive
#     in its new schema.

setup
{
  CREATE TABLE test (k int primary key, v int);
  INSERT INTO test SELECT i, i FROM generate_series(1, 10) AS i;
  CREATE MATERIALIZED VIEW test_mv AS SELECT k, v FROM test;
  CREATE TYPE test_ct AS (a int);
  CREATE TABLE test_with_ct (k int PRIMARY KEY, c test_ct);
  CREATE SEQUENCE test_seq;
  CREATE TABLE test_with_seq (k int PRIMARY KEY, c bigint DEFAULT nextval('test_seq'));
  CREATE SCHEMA test_old_schema;
  CREATE SCHEMA test_new_schema;
  CREATE TABLE test_old_schema.test_moved (k int PRIMARY KEY);
}

teardown
{
  DROP TABLE IF EXISTS test CASCADE;
  DROP MATERIALIZED VIEW IF EXISTS test_mv;
  DROP TABLE IF EXISTS test_with_ct;
  DROP TYPE IF EXISTS test_ct CASCADE;
  DROP TABLE IF EXISTS test_with_seq;
  DROP SEQUENCE IF EXISTS test_seq CASCADE;
  DROP SCHEMA IF EXISTS test_old_schema CASCADE;
  DROP SCHEMA IF EXISTS test_new_schema CASCADE;
}

session s1
step s1_begin          { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s1_drop_mv        { DROP MATERIALIZED VIEW test_mv; }
step s1_drop_table_ct  { DROP TABLE test_with_ct; }
step s1_drop_default   { ALTER TABLE test_with_seq ALTER COLUMN c DROP DEFAULT; }
step s1_set_schema     { ALTER TABLE test_old_schema.test_moved SET SCHEMA test_new_schema; }
step s1_commit         { COMMIT; }

session s2
step s2_begin          { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step s2_drop_table     { DROP TABLE test CASCADE; }
step s2_drop_type      { DROP TYPE test_ct CASCADE; }
step s2_drop_seq       { DROP SEQUENCE test_seq; }
step s2_drop_schema    { DROP SCHEMA test_old_schema CASCADE; }
step s2_commit         { COMMIT; }
step s2_check_mv       { SELECT count(*) FROM pg_class WHERE relname IN ('test', 'test_mv'); }
step s2_check_type     { SELECT count(*) FROM pg_type WHERE typname = 'test_ct'; }
step s2_check_seq      { SELECT count(*) FROM pg_class WHERE relname = 'test_seq'; }
step s2_check_schema   { SELECT nspname FROM pg_class JOIN pg_namespace ON relnamespace = pg_namespace.oid WHERE relname = 'test_moved'; }

# s1 holds the MV's lock and deletes its _RETURN rule (uncommitted). s2's
# cascade walk still sees the rule, so it targets the rule for deletion,
# then blocks on the MV lock. s1 commits, removing the rule; s2 must finish
# the drop without "could not find tuple for rule".
permutation s1_begin s2_begin s1_drop_mv s2_drop_table s1_commit s2_commit s2_check_mv

# s1 holds the table's lock. s2's cascade walk finds column test_with_ct.c
# depending on the type and blocks on the table lock. s1 commits, removing
# the table and the column; s2 must finish the drop without "cache lookup
# failed for attribute".
permutation s1_begin s2_begin s1_drop_table_ct s2_drop_type s1_commit s2_commit s2_check_type

# s1 holds the lock on the pg_attrdef object while deleting it. s2 finds the
# default depending on the sequence and blocks on that lock. s1 commits,
# removing the default; s2 must finish the drop without "could not find
# tuple for attrdef", which reportDependentObjects() raises when it tries to
# describe the deleted default in the RESTRICT error path.
permutation s1_begin s2_begin s1_drop_default s2_drop_seq s1_commit s2_commit s2_check_seq

# s1 holds the table's lock and repoints its pg_depend row from the old
# schema to the new one (uncommitted). s2's cascade walk still sees the
# table under the old schema, so it targets the table for deletion, then
# blocks on the table lock. s1 commits; s2 must notice the pg_depend row
# changed and leave the table alone, so it survives in the new schema.
permutation s1_begin s2_begin s1_set_schema s2_drop_schema s1_commit s2_commit s2_check_schema
