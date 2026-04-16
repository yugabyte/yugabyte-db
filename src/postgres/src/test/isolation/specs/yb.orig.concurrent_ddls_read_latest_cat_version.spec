
setup
{
  CREATE TABLE test (k int, v int);
  CREATE TABLE test2 (k int primary key, v int);
  CREATE TABLE catalog_version(k int primary key, v int);
  INSERT INTO catalog_version VALUES (1, 0);
}

teardown
{
  DROP TABLE IF EXISTS test, test2, catalog_version;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_drop_table_test { DROP TABLE test; }
step s1_commit { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_drop_table_test2 { DROP TABLE test2; -- if this reads the catalog version as of the transaction snapshot, it will read a stale value}
step s2_select { SELECT * FROM test2; }
step s2_commit { COMMIT; }
step s2_store_catalog_version { UPDATE catalog_version SET v = (SELECT current_version FROM pg_yb_catalog_version WHERE db_oid = (SELECT oid FROM pg_database WHERE datname = current_database())); }
step s2_get_catalog_version_diff { SELECT current_version - (SELECT v FROM catalog_version) FROM pg_yb_catalog_version WHERE db_oid = (SELECT oid FROM pg_database WHERE datname = current_database()); }

permutation s2_store_catalog_version s1_begin_rr s2_begin_rr s2_select s1_drop_table_test s2_drop_table_test2 s1_commit s2_commit s2_get_catalog_version_diff
# TODO: Add a test where an autonomous DDL transaction like CREATE INDEX reads the latest catalog version too.
