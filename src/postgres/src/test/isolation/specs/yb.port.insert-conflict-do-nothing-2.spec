# INSERT...ON CONFLICT DO NOTHING test with multiple rows
# in higher isolation levels

setup
{
  CREATE TABLE ints (key int, val text, PRIMARY KEY (key) INCLUDE (val));
}

teardown
{
  DROP TABLE ints;
}

session "s1"
step "beginrr1" { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "begins1" { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step "donothing1" { INSERT INTO ints(key, val) VALUES(1, 'donothing1') ON CONFLICT DO NOTHING; }
step "c1" { COMMIT; }
step "show" { SELECT * FROM ints; }

session "s2"
step "beginrr2" { BEGIN ISOLATION LEVEL REPEATABLE READ; }
step "begins2" { BEGIN ISOLATION LEVEL SERIALIZABLE; }
step "donothing2" { INSERT INTO ints(key, val) VALUES(1, 'donothing2'), (1, 'donothing3') ON CONFLICT DO NOTHING; }
step "c2" { COMMIT; }

permutation "beginrr1" "beginrr2" "donothing1" "c1" "donothing2" "c2" "show"
permutation "beginrr1" "beginrr2" "donothing2" "c2" "donothing1" "c1" "show"
permutation "beginrr1" "beginrr2" "donothing1" "donothing2" "c1" "c2" "show"
permutation "beginrr1" "beginrr2" "donothing2" "donothing1" "c2" "c1" "show"
permutation "begins1" "begins2" "donothing1" "c1" "donothing2" "c2" "show"
permutation "begins1" "begins2" "donothing2" "c2" "donothing1" "c1" "show"

# For the below permutations, YB has improved behavior over PG. In PG, the second transaction
# conflicts, but in YB there is no conflict error.

# For serializable isolation level, YB sets the read point to kMax. The txn performing the first
# insert blocks the latter insert until commit time. When the blocker commits, the waiter wakes up,
# takes in-memory locks and retries conflict resolution with read time as the latest time. So, it
# doesn't face a conflict and goes on to the next phases of the operation to find a unique key
# violation. But this doesn't error due to the 'ON CONFLICT DO NOTHING' clause. Note that we neither
# check nor face conflicts with regular db in case of serialization isolation because the read point
# is set to kMax. This order of execution is essentially equal to the following serial order
# txn performing first insert -> txn performing second insert.
#
# In case of PG, the waiter faces a 40001 on resumption, because PG couldn't find a serial schedule
# for execution.
permutation "begins1" "begins2" "donothing1" "donothing2" "c1" "c2" "show"
permutation "begins1" "begins2" "donothing2" "donothing1" "c2" "c1" "show"
