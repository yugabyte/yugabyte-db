--
-- Subscription options that YugabyteDB does not support
--

-- retain_dead_tuples needs the conflict detection slot, which has no
-- counterpart in DocDB.
-- fail
CREATE SUBSCRIPTION regress_testsub CONNECTION 'dbname=regress_doesnotexist' PUBLICATION testpub WITH (connect = false, retain_dead_tuples = true);
-- ok
CREATE SUBSCRIPTION regress_testsub CONNECTION 'dbname=regress_doesnotexist' PUBLICATION testpub WITH (connect = false, retain_dead_tuples = false);
-- fail
ALTER SUBSCRIPTION regress_testsub SET (retain_dead_tuples = true);
-- ok
ALTER SUBSCRIPTION regress_testsub SET (retain_dead_tuples = false);
SELECT subname, subretaindeadtuples FROM pg_subscription WHERE subname = 'regress_testsub';
ALTER SUBSCRIPTION regress_testsub SET (slot_name = NONE);
DROP SUBSCRIPTION regress_testsub;
