
setup
{
  CREATE TABLE test1 (k int, v int);
  INSERT INTO test1 SELECT i, i FROM generate_series(1, 10) AS i;
  CREATE TABLE test2 (k int, v int);
  INSERT INTO test2 SELECT i, i FROM generate_series(1, 10) AS i;
  CREATE ROLE role2;
  CREATE ROLE role3;
}

teardown
{
  DROP TABLE IF EXISTS test1;
  DROP TABLE IF EXISTS test2;
  DROP ROLE IF EXISTS role2;
  DROP ROLE IF EXISTS role3;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_alter_table_test1_add_col_rewrite { ALTER TABLE test1 ADD COLUMN t1_a INT default random(); }
step s1_grant_test1 { GRANT SELECT ON TABLE test1 TO role2; }
step s1_create_index_test1 { CREATE INDEX NONCONCURRENTLY idx_test11 ON test1(v); }
step s1_commit { COMMIT; }


session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_alter_table_test2_add_col_rewrite { ALTER TABLE test2 ADD COLUMN t2_a INT default random(); }
step s2_grant_ins_test1 { GRANT INSERT ON TABLE test1 TO role3; }
step s2_grant_upd_test1 { GRANT UPDATE ON TABLE test1 TO role3; }
step s2_grant_del_test1 { GRANT DELETE ON TABLE test1 TO role3; }
step s2_create_index_test1 { CREATE INDEX NONCONCURRENTLY idx_test21 ON test1(v); }
step s2_commit { COMMIT; }

session s3
step s3_check_relacl { SELECT relname, relacl FROM pg_class WHERE relname = 'test1'; }
step s3_check_pgattr_test1 { SELECT attname, attnum, atttypid FROM pg_attribute WHERE attrelid = 'test1'::regclass::oid and attnum > 0 ORDER BY attnum ASC; }
step s3_check_pgattr_test2 { SELECT attname, attnum, atttypid FROM pg_attribute WHERE attrelid = 'test2'::regclass::oid and attnum > 0 ORDER BY attnum ASC; }
step s3_check_relhasindex { SELECT relname, relhasindex FROM pg_class WHERE relname = 'test1'; }
step s3_show_pg_index { SELECT indexrelid::regclass, indrelid::regclass, indisvalid, indisready FROM pg_index WHERE indrelid = 'test1'::regclass ORDER BY indexrelid::regclass::text; }
step s3_add_pkey_test1 { ALTER TABLE test1 ADD PRIMARY KEY (k); }

# "Inplace" update of relhasindex by CREATE INDEX needs to survive a concurrent GRANT on the main table
# This is yet to be implemented in YB, tracked by #29638
permutation s3_check_relhasindex s1_begin_rr s2_begin_rr  s1_create_index_test1 s2_grant_del_test1 s1_commit s2_commit s3_check_relhasindex s3_check_relacl

# GRANT on main table needs to work concurrently with "inplace" update by CREATE INDEX on the same table
# This is yet to be implemented in YB, tracked by #29638
permutation s3_check_relhasindex s2_begin_rr  s1_begin_rr s2_grant_del_test1 s1_create_index_test1 s1_commit s2_commit s3_check_relhasindex s3_check_relacl

# "Inplace" update of relhasindex by CREATE INDEX needs to not conflict with a concurrent CREATE INDEX on the same table
# This is yet to be implemented in YB, tracked by #29638
permutation s3_check_relhasindex s1_begin_rr s2_begin_rr s1_create_index_test1 s2_create_index_test1 s1_commit s2_commit s3_check_relhasindex s3_show_pg_index

# If the table already has an index, typically a primary key, concurrent CREATE INDEX NONCONCURRENTLY
# should be able to go through
permutation s3_add_pkey_test1 s3_check_relhasindex  s1_begin_rr s2_begin_rr s1_create_index_test1 s2_create_index_test1 s1_commit s2_commit s3_check_relhasindex s3_show_pg_index

# The rest of the tests do not verify inplace updates but check possible issues that arise from yb object locks for tuples not matching PG semantics exactly.

# Verify fix for GH issue #30095 where table rewrites on different tables deadlocked.
permutation s1_begin_rr s2_begin_rr s1_alter_table_test1_add_col_rewrite s2_alter_table_test2_add_col_rewrite s1_commit s2_commit s3_check_pgattr_test1 s3_check_pgattr_test2

# GRANT permissions on a table conflicts with an ALTER TABLE rewrite in PG15 as well as YB, as no object locks are acquired on the table during GRANT.
permutation s1_begin_rr  s2_begin_rr s1_alter_table_test1_add_col_rewrite s2_grant_ins_test1 s1_commit s2_commit s3_check_relacl s3_check_pgattr_test1

# GRANTs on same table conflict with each other in PG15 as well as YB, as no object locks are acquired on the table during GRANT.
permutation s1_begin_rr s2_begin_rr s1_grant_test1 s2_grant_upd_test1 s1_commit s2_commit s3_check_relacl
