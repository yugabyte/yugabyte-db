# A foreign key has the following set of triggers:
#
#     RI_FKey_noaction_del
#     RI_FKey_restrict_del
#     RI_FKey_noaction_upd
#     RI_FKey_restrict_upd
#     RI_FKey_cascade_del
#     RI_FKey_cascade_upd
#     RI_FKey_setnull_del
#     RI_FKey_setnull_upd
#     RI_FKey_setdefault_del
#     RI_FKey_setdefault_upd
#     RI_FKey_check_ins
#     RI_FKey_check_upd
#
# These triggers (except the last 2) operate on the latest snapshot instead of the transaction
# snapshot. If they operated on the transaction snapshot, it would result in fk violations going
# undetected.

setup
{
  DROP TABLE IF EXISTS pk_rel, fk_rel;
  CREATE TABLE pk_rel (h INT PRIMARY KEY, v_no_action VARCHAR UNIQUE, v_restrict VARCHAR UNIQUE, v_cascade VARCHAR UNIQUE);
  CREATE TABLE fk_rel (
      h INT PRIMARY KEY,
      v_no_action VARCHAR REFERENCES pk_rel(v_no_action) DEFERRABLE,
      v_restrict VARCHAR REFERENCES pk_rel(v_restrict) ON DELETE RESTRICT ON UPDATE RESTRICT DEFERRABLE,
      v_cascade VARCHAR REFERENCES pk_rel(v_cascade) ON DELETE CASCADE ON UPDATE CASCADE DEFERRABLE);
  INSERT INTO pk_rel VALUES (1, 'g', NULL, NULL);
  INSERT INTO pk_rel VALUES (2, NULL, 'g', NULL);
  INSERT INTO pk_rel VALUES (3, NULL, NULL, 'g');
}

teardown
{
  DROP TABLE fk_rel, pk_rel;
}

session "s1"
step "s1_begin" { BEGIN; }
step "s1_commit"	{ COMMIT; }
step "s1_ins"	{ INSERT INTO fk_rel VALUES (1, 'g', 'g', 'g'); }

session "s2"
step "s2_rc" { BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED; }
step "s2_rr" { BEGIN TRANSACTION ISOLATION LEVEL REPEATABLE READ; }
step "s2_sr" { BEGIN TRANSACTION ISOLATION LEVEL SERIALIZABLE; }
step "s2_set_deferred" { SET CONSTRAINTS ALL DEFERRED; }
step "s2_commit"	{ COMMIT; }

step "s2_del_no_action_col"	{ DELETE FROM pk_rel WHERE v_no_action='g'; }
step "s2_ins_no_action_col" { INSERT INTO pk_rel VALUES (5000, 'g'); }
step "s2_upd_no_action_col"	{ UPDATE pk_rel SET v_no_action='a' WHERE v_no_action='g'; }

step "s2_del_restrict_col"	{ DELETE FROM pk_rel WHERE v_restrict='g'; }
step "s2_upd_restrict_col"	{ UPDATE pk_rel SET v_restrict='a' WHERE v_restrict='g'; }

step "s2_del_cascade_col"	{ DELETE FROM pk_rel WHERE v_cascade='g'; }
step "s2_upd_cascade_col"	{ UPDATE pk_rel SET v_cascade='a' WHERE v_cascade='g'; }

step "s2_read_fk_rel"	{ SELECT * FROM fk_rel; }
step "s2_read_pk_rel"	{ SELECT * FROM pk_rel; }

# DELETE NO ACTION
  # Transaction block INSERT and transaction block DELETE/UPDATE
    permutation "s1_begin" "s2_rc" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_rr" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_sr" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
  # Fast path INSERT
    permutation "s2_rc" "s1_ins" "s2_del_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_rr" "s1_ins" "s2_del_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    # Test also with a case where we pick the read time in RR isolation transaction of S2 before the fast-path insert is done.
    # This case is more important, the previous case doesn't have much value: the read time for the RR transaction in that case will
    # anyway be picked after the fast-path INSERT has completed.
    permutation "s2_rr" "s2_read_pk_rel" "s1_ins" "s2_del_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_sr" "s1_ins" "s2_del_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
  # Fast path DELETE/UPDATE
    permutation "s1_begin" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# DELETE NO ACTION with deferrable trigger, another pk row has been added to satisfy fk
  # Transaction block INSERT and transaction block DELETE/UPDATE
    permutation "s1_begin" "s2_rc" "s2_set_deferred" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_rr" "s2_set_deferred" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_sr" "s2_set_deferred" "s1_ins" "s2_del_no_action_col" "s1_commit" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
  # Fast path INSERT
    permutation "s2_rc" "s2_set_deferred" "s1_ins" "s2_del_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_rr" "s2_set_deferred" "s1_ins" "s2_del_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    # Test also with a case where we pick the read time in RR isolation transaction of S2 before the fast-path insert is done.
    # This case is more important, the previous case doesn't have much value: the read time for the RR transaction in that case will
    # anyway be picked after the fast-path INSERT has completed.
    permutation "s2_rr" "s2_set_deferred" "s2_read_pk_rel" "s1_ins" "s2_del_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_sr" "s2_set_deferred" "s1_ins" "s2_del_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# UPDATE NO ACTION
  # Transaction block INSERT and transaction block DELETE/UPDATE
    permutation "s1_begin" "s2_rc" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_rr" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_sr" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
  # Fast path INSERT
    permutation "s2_rc" "s1_ins" "s2_upd_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_rr" "s1_ins" "s2_upd_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    # Test also with a case where we pick the read time in RR isolation transaction of S2 before the fast-path insert is done.
    # This case is more important, the previous case doesn't have much value: the read time for the RR transaction in that case will
    # anyway be picked after the fast-path INSERT has completed.
    permutation "s2_rr" "s2_read_pk_rel" "s1_ins" "s2_upd_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_sr" "s1_ins" "s2_upd_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
  # Fast path DELETE/UPDATE
    permutation "s1_begin" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# UPDATE NO ACTION with deferrable trigger, another pk row has been added to satisfy fk
  # Transaction block INSERT and transaction block DELETE/UPDATE
    permutation "s1_begin" "s2_rc" "s2_set_deferred" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_rr" "s2_set_deferred" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s1_begin" "s2_sr" "s2_set_deferred" "s1_ins" "s2_upd_no_action_col" "s1_commit" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
  # Fast path INSERT
    permutation "s2_rc" "s2_set_deferred" "s1_ins" "s2_upd_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_rr" "s2_set_deferred" "s1_ins" "s2_upd_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    # Test also with a case where we pick the read time in RR isolation transaction of S2 before the fast-path insert is done.
    # This case is more important, the previous case doesn't have much value: the read time for the RR transaction in that case will
    # anyway be picked after the fast-path INSERT has completed.
    permutation "s2_rr" "s2_set_deferred" "s2_read_pk_rel" "s1_ins" "s2_upd_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"
    permutation "s2_sr" "s2_set_deferred" "s1_ins" "s2_upd_no_action_col" "s2_ins_no_action_col" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# DELETE RESTRICT. This option is very similar to DELETE NO ACTION except for a difference: it doesn't allow foreign key triggers to be deferred.
# Just test the basic cases.
  permutation "s1_begin" "s2_rr" "s2_read_pk_rel" "s1_ins" "s2_del_restrict_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

  # A deferrable trigger doesn't really help avoid the error
  permutation "s1_begin" "s2_rr" "s2_set_deferred" "s2_read_pk_rel" "s1_ins" "s2_del_restrict_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# UPDATE RESTRICT. This option is very similar to UPDATE NO ACTION except for a difference: it doesn't allow foreign key triggers to be deferred.
# Just test the basic cases.
  permutation "s1_begin" "s2_rr" "s2_read_pk_rel" "s1_ins" "s2_upd_restrict_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

  # A deferrable trigger doesn't really help avoid the error
  permutation "s1_begin" "s2_rr" "s2_set_deferred" "s2_read_pk_rel" "s1_ins" "s2_upd_restrict_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# DELETE CASCADE
  permutation "s1_begin" "s2_rr" "s2_read_pk_rel" "s1_ins" "s2_del_cascade_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# UPDATE CASCADE
  permutation "s1_begin" "s2_rr" "s2_read_pk_rel" "s1_ins" "s2_upd_cascade_col" "s1_commit" "s2_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# Miscellaneous:
# (1) DELETE on pk_rel blocks an insert on fk_rel.
permutation "s1_begin" "s2_rc" "s2_del_no_action_col" "s1_ins" "s2_commit" "s1_commit" "s2_read_fk_rel" "s2_read_pk_rel"

# TODO:
#   (1) Add test cases for SET NULL and SET DEFAULT
#   (2) The above test cases for RI_FKey_check_ins and RI_FKey_check_upd with partitioned tables. They by-pass MVCC too.
#   (3) Add this complex case of chained fks: delete of a pk causes a delete of a fk, but that second delete further causes a delete on another fk.
#   (4) Add a test case to ensure that rollback of the concurrent INSERT in the above test cases let's the DELETE/ UPDATE make progress.
#   (5) FKs will face a correctness issue for the "USE SNAPSHOT" option for logical replication: after picking a snapshot for use in the session that
#       created the replication slot, we should be allowed to switch snapshots for fk checks. But currently, if we "use snapshot", we just set that
#       snapshot forever.
