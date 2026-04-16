setup
{
 create table tbl(c1 int, c2 int);
 insert into tbl values (1, 2);
}

teardown
{
  DROP TABLE IF EXISTS tbl;
}

session s1
step s1_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step s1_commit { COMMIT; }
step s1_rewrite_tbl { alter table tbl alter column c2 type bigint; }
step s1_select_tbl { select c1, c2 from tbl order by c1 asc; }

session s2
step s2_begin_rr { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
# This insert should make it to the rewritten table.
step s2_insert_into_tbl { insert into tbl values (2, 3); }
step s2_commit { COMMIT; }

session s3
step s3_select_tbl { select c1, c2 from tbl order by c1 asc; }


permutation s1_begin_rr s1_select_tbl s2_begin_rr s2_insert_into_tbl s1_rewrite_tbl s2_commit s1_commit s3_select_tbl
