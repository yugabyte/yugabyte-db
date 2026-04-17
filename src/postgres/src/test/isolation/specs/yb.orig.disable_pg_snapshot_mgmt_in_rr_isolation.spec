setup
{
  DROP TABLE IF EXISTS pk_rel, fk_rel;
  create table pk_rel (h int primary key, v1 int unique);
  create table fk_rel (h int primary key, v1 int references pk_rel(v1));

  insert into pk_rel values (1, 1);
}

teardown
{
  DROP TABLE fk_rel, pk_rel;
}

session "s1"
step "s1_begin_rr" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s1_commit"	{ COMMIT; }
step "s1_ins_fk_rel"	{ insert into fk_rel values (1, 1); }
step "s1_select_pk_rel" { select * from pk_rel; }
step "s1_select_fk_rel" { select * from fk_rel; }

session "s2"
step "s2_begin_rr" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_select" { select count(*) from pk_rel; }
step "s2_del_from_pk_rel" { delete from pk_rel where h=1; }
step "s2_commit"	{ COMMIT; }
step "s2_disable_pg_snapshot_mgmt" { set yb_disable_pg_snapshot_mgmt_in_repeatable_read=true; }
step "s2_sleep" { SELECT pg_sleep(1); }

permutation "s1_begin_rr" "s2_begin_rr" "s2_select" "s1_ins_fk_rel" "s1_commit" "s2_del_from_pk_rel" "s2_commit" "s1_select_pk_rel" "s1_select_fk_rel"
permutation "s1_begin_rr" "s2_disable_pg_snapshot_mgmt" "s2_begin_rr" "s2_select" "s2_sleep" "s1_ins_fk_rel" "s1_commit" "s2_del_from_pk_rel" "s2_commit" "s1_select_pk_rel" "s1_select_fk_rel"

# Test to ensure GUC can't be set in a transaction block.
permutation "s2_begin_rr" "s2_disable_pg_snapshot_mgmt" "s2_commit"