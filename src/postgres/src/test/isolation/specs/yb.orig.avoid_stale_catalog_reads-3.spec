# Similar to yb.orig.avoid_stale_catalog_reads_with_object_locking-1.spec, this test would fail if
# we don't process invalidation messages after acquiring table locks (i.e., without commit 9891ccbf).

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_create_table { create table parent(c1 int); }
step s1_alter_table_add_col { alter table parent add column c2 int; }
step s1_drop_parent { drop table parent cascade; }
step s1_commit { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_select_parent { SELECT * from parent; }
step s2_create_child_table_via_inheritance { create table child(chc1 int) inherits(parent); }
step s2_commit { COMMIT; }

session s3
step s3_select_child { SELECT * from child; }

# Note that we need s2_select_parent before the ALTER in S2 to load the cache with data before the
# new column is added.
permutation s1_create_table s2_select_parent s1_begin_rr s2_begin_rr s1_alter_table_add_col s2_create_child_table_via_inheritance s1_commit s2_commit s3_select_child s1_drop_parent

# Misc case that results in a deadlock
# TODO: Mask the txn_start_us in the error string in the .out file
# permutation s1_create_table s1_begin_rr s2_begin_rr s2_select_parent s1_alter_table_add_col s2_create_child_table_via_inheritance s1_commit s2_commit s3_select_child s3_select_parent s1_drop_parent
