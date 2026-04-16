setup
{
 create table parent(c1 int primary key, c2 int) partition by range(c1);
 create table child_1 partition of parent for values from (0) to (100);
 create table child_def partition of parent default;
 insert into parent values (1, 2);
 insert into parent values (201, 202);
}

teardown
{
  DROP TABLE IF EXISTS parent CASCADE;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_commit { COMMIT; }
# This new partition creation should fail because this row is already present in the default partition.
step s1_create_new_partition { create table child_3 partition of parent for values from (100) to (200); }
step s1_select_tbl { select c1, c2 from parent order by c1 asc; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s2_insert_into_tbl { insert into parent values (101, 102); }
step s2_commit { COMMIT; }

session s3
step s3_select_tbl { select tableoid::regclass,c1, c2 from parent order by c1 asc; }


permutation s1_begin_rr s1_select_tbl s2_begin_rr s2_insert_into_tbl s1_create_new_partition s2_commit s1_commit s3_select_tbl
