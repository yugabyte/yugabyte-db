# Similar to yb.orig.avoid_stale_catalog_reads_with_object_locking-1.spec, this test would fail if
# we don't process invalidation messages after acquiring table locks (i.e., without commit 9891ccbf).

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_create_table { create table part1(id int) partition by range(id); }
step s1_create_default_partition { create table part1def partition of part1 default; }
step s1_drop_part1 { drop table part1; }
step s1_commit { COMMIT; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_select_table { select * from part1; }
step s2_create_another_default_partition { create table part2def partition of part1 default; }
step s2_commit { COMMIT; }

session s3
step s3_select_part_info { SELECT partrelid::regclass, partdefid::regclass FROM pg_partitioned_table; }

# Note that we need s2_select_table before the CREATE TABLE in S2 to load the cache with data before
# a default partition is set.
permutation s1_create_table s2_select_table s1_begin_rr s2_begin_rr s1_create_default_partition s2_create_another_default_partition s1_commit s2_commit s3_select_part_info s1_drop_part1

# Misc case that results in a deadlock
# TODO: Mask the txn_start_us in the error string in the .out file
# permutation s1_create_table s1_begin_rr s2_begin_rr s2_select_table s1_create_default_partition s2_create_another_default_partition s1_commit s2_commit s3_select_part_info s1_drop_part1
