# Readers that first lock the partitioned table after DETACH PARTITION
# CONCURRENTLY marked the partition pending detach.
#
# Session sA reads the parent before the detach starts, so the detach's second
# phase (WaitForLockersMultiple on the parent) waits for it until sAc.  sX then
# locks the parent for the first time while that wait is in progress, so the
# detach never waits for sX through the parent.  sC holds a lock on the
# partition so that the finalize step's AccessExclusiveLock on it queues.
#
# A pending partition stays visible to a REPEATABLE READ transaction only if
# that transaction read the parent's partition list before the partition went
# pending; every other reader omits it.

setup
{
  CREATE TABLE parent (a int) PARTITION BY LIST (a);
  CREATE TABLE p1 PARTITION OF parent FOR VALUES IN (1);
  CREATE TABLE p2 PARTITION OF parent FOR VALUES IN (2);
  INSERT INTO parent VALUES (1), (2);
  CREATE TABLE other (x int);
  CREATE TABLE mparent (a int) PARTITION BY LIST (a);
  CREATE TABLE mp1 PARTITION OF mparent FOR VALUES IN (1);
  CREATE TABLE mp2 PARTITION OF mparent FOR VALUES IN (2) PARTITION BY LIST (a);
  CREATE TABLE mp2a PARTITION OF mp2 FOR VALUES IN (2);
  INSERT INTO mparent VALUES (1), (2);
}

teardown
{
  DROP TABLE IF EXISTS parent, p2, other, mparent, mp2;
}

session sA
step sAb	{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step sAs	{ SELECT * FROM parent ORDER BY a; }
step sAms	{ SELECT * FROM mparent ORDER BY a; }
step sAc	{ COMMIT; }

session sX
setup		{ SET plan_cache_mode = force_generic_plan; }
step sXb	{ BEGIN ISOLATION LEVEL REPEATABLE READ; }
step sXbs	{ BEGIN ISOLATION LEVEL SERIALIZABLE; }
step sXsnap	{ SELECT count(*) FROM other; }
step sXs	{ SELECT * FROM parent ORDER BY a; }
step sXp	{ SELECT * FROM parent WHERE a = 1; }
step sXprep	{ PREPARE q(int) AS SELECT * FROM parent WHERE a = $1; }
step sXexec	{ EXECUTE q(1); }
step sXi	{ INSERT INTO parent VALUES (2); }
step sXms	{ SELECT * FROM mparent ORDER BY a; }
step sXc	{ COMMIT; }
step sXdealloc	{ DEALLOCATE q; }

session sC
step sCb	{ BEGIN; }
step sCs	{ SELECT * FROM p2; }
step sCms	{ SELECT * FROM mp2; }
step sCc	{ COMMIT; }
step sCcheck	{ SELECT 'parent' AS t, count(*) FROM parent UNION ALL SELECT 'p2', count(*) FROM p2; }

session sR
step sRs	{ SELECT * FROM parent ORDER BY a; }

session sD
step sDdetach	{ ALTER TABLE parent DETACH PARTITION p2 CONCURRENTLY; }
step sDmdetach	{ ALTER TABLE mparent DETACH PARTITION mp2 CONCURRENTLY; }

# sX's snapshot (sXsnap) predates the pending marker, but sX first reads the
# parent after it, so p2 is omitted and the detach never waits for sX.
permutation sAb sAs sXb sXsnap sDdetach sXs sAc sXs sXc
# Same, with finalize queued on p2 behind sC: sX does not block.
permutation sAb sAs sXb sXsnap sDdetach sCb sCs sAc sXs sCc sXs sXc
# sX's first read prunes p2 at plan time.
permutation sAb sAs sXb sXsnap sDdetach sXp sAc sXs sXc
# sX first reads the parent after the detach committed.
permutation sAb sAs sXb sXsnap sDdetach sAc sXs sXc
# A generic plan with a parameter keeps p2 out of sX's partition list.
permutation sAb sAs sXb sXsnap sDdetach sXprep sXexec sAc sXs sXc sXdealloc
# sX's snapshot is taken after the marker.
permutation sAb sAs sDdetach sXb sXs sAc sXs sXc
permutation sAb sAs sDdetach sCb sCs sXb sXsnap sAc sXs sCc sXs sXc
# sX first reads the parent after the marker, so tuple routing omits p2 and
# the INSERT fails.  Upstream routes it into p2, where it waits behind
# finalize's lock and lands in the detached table.
permutation sAb sAs sXb sXsnap sDdetach sCb sCs sAc sXi sCc sXs sXc sCcheck
# Read committed statements omit p2.
permutation sAb sAs sDdetach sCb sCs sAc sRs sCc sRs
# Sub-partitioned partition, finalize queued behind sC.
permutation sAb sAms sXb sXsnap sDmdetach sCb sCms sAc sXms sCc sXms sXc
# SERIALIZABLE transactions that started before the marker are aborted on
# their first read of the parent after it.
permutation sAb sAs sXbs sXsnap sDdetach sXs sAc sXs sXc
permutation sAb sAs sXbs sXsnap sDdetach sXp sAc sXs sXc
permutation sAb sAs sXbs sXsnap sDdetach sCb sCs sAc sXs sCc sXs sXc
