setup
{
  CREATE TABLE foo (k INT PRIMARY KEY, v1 INT, v2 INT);
  INSERT INTO foo VALUES (1, 1, 1), (2, 2, 2), (3, 3, 3), (4, 4, 4);
}

teardown
{
  DROP TABLE foo;
}

session "s1"
setup                     { BEGIN; }
step "s1_update_v1_4"     { UPDATE foo SET v1=v1+10 WHERE k=4; }
step "s1_savepoint_a"     { SAVEPOINT a; }
step "s1_update_v1_1"     { UPDATE foo SET v1=v1+10 WHERE k=1; }
step "s1_rollback_to_a"   { ROLLBACK TO a; }
step "s1_update_v2_3"     { UPDATE foo SET v2=v2+100 WHERE k=3; }
step "s1_commit"          { COMMIT; }

session "s2"
setup                     { BEGIN; }
step "s2_update_v2_3"     { UPDATE foo SET v2=v2+300 WHERE k=3; }
step "s2_update_v1_v2_1"  { UPDATE foo SET v1=v1+30, v2=v2+300 WHERE k=1; }
step "s2_commit"          { COMMIT; }

session "s3"
setup                     { BEGIN; }
step "s3_update_v1_1"     { UPDATE foo SET v1=v1+20 WHERE k=1; }
step "s3_commit"          { COMMIT; }

# Drop received wait-for probe(s) if none of the subtxns in the blocker txn's blocking subtxn set is
# active.
#
# txn3 initially blocks on txn1. txn1 then rolls back to a savepoint, unblocking txn3. txn1 now
# blocks on txn2, and txn2 blocks on txn3. This tests whether the deadlock detector drops incoming 
# wait-for probes when the blocker txn has no active subtxn in the blocking subtxn set. If the 
# detector instead forwards them, it could lead to detection of false deadlocks (if txn1's 
# coordinator doesn't drop the probe txn3 -> txn1 and forwards it instead, it would lead to a false 
# deadlock detection at txn3's coordinator. as txn1 -> txn2, and txn2 -> txn3).
permutation "s1_update_v1_4" "s1_savepoint_a" "s1_update_v1_1" "s3_update_v1_1" "s1_rollback_to_a" "s2_update_v2_3" "s1_update_v2_3" "s2_update_v1_v2_1" "s3_commit" "s2_commit" "s1_commit"

# Drop wait-for probe(s) at originator if blocking subtxn is rolled back.
#
# txn2 initially blocks on txn1. txn1 then rolls back to a savepoint, unblocking txn2. txn1 now 
# blocks on txn2. This tests whether the deadlock detector stores the underlying blocking subtxn(s)
# for each wait-for edge and can ignore edges that no longer have any active subtxns. Else, it would
# lead to detection of false deadlocks (when the old wait-for edge from txn2 -> txn1 isn't ignored, 
# it would lead to a false deadlock. as txn1 now blocks on txn2).
permutation "s1_update_v1_4" "s1_savepoint_a" "s1_update_v1_1" "s2_update_v2_3" "s2_update_v1_v2_1" "s1_rollback_to_a" "s1_update_v2_3" "s2_commit" "s1_commit"