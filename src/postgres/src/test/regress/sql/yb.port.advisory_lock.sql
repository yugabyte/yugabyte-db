--
-- ADVISORY LOCKS
--

SELECT oid AS datoid FROM pg_database WHERE datname = current_database() \gset

BEGIN;

SELECT
	pg_advisory_xact_lock(1), pg_advisory_xact_lock_shared(2),
	pg_advisory_xact_lock(1, 1), pg_advisory_xact_lock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;


-- pg_advisory_unlock_all() shouldn't release xact locks
SELECT pg_advisory_unlock_all();

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;


-- can't unlock xact locks
SELECT
	pg_advisory_unlock(1), pg_advisory_unlock_shared(2),
	pg_advisory_unlock(1, 1), pg_advisory_unlock_shared(2, 2);


-- automatically release xact locks at commit
COMMIT;

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;


BEGIN;

-- holding both session and xact locks on the same objects, xact first
SELECT
	pg_advisory_xact_lock(1), pg_advisory_xact_lock_shared(2),
	pg_advisory_xact_lock(1, 1), pg_advisory_xact_lock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;

SELECT
	pg_advisory_lock(1), pg_advisory_lock_shared(2),
	pg_advisory_lock(1, 1), pg_advisory_lock_shared(2, 2);

ROLLBACK;

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;


-- unlocking session locks
SELECT
	pg_advisory_unlock(1), pg_advisory_unlock(1),
	pg_advisory_unlock_shared(2), pg_advisory_unlock_shared(2),
	pg_advisory_unlock(1, 1), pg_advisory_unlock(1, 1),
	pg_advisory_unlock_shared(2, 2), pg_advisory_unlock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;


BEGIN;

-- holding both session and xact locks on the same objects, session first
SELECT
	pg_advisory_lock(1), pg_advisory_lock_shared(2),
	pg_advisory_lock(1, 1), pg_advisory_lock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;

SELECT
	pg_advisory_xact_lock(1), pg_advisory_xact_lock_shared(2),
	pg_advisory_xact_lock(1, 1), pg_advisory_xact_lock_shared(2, 2);

ROLLBACK;

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;


-- releasing all session locks
SELECT pg_advisory_unlock_all();

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;


BEGIN;

-- grabbing txn locks multiple times

SELECT
	pg_advisory_xact_lock(1), pg_advisory_xact_lock(1),
	pg_advisory_xact_lock_shared(2), pg_advisory_xact_lock_shared(2),
	pg_advisory_xact_lock(1, 1), pg_advisory_xact_lock(1, 1),
	pg_advisory_xact_lock_shared(2, 2), pg_advisory_xact_lock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
-- YB: TODO(GH#26179): ensure pg_locks shows only one advisory lock per key
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;

COMMIT;

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;

-- grabbing session locks multiple times

SELECT
	pg_advisory_lock(1), pg_advisory_lock(1),
	pg_advisory_lock_shared(2), pg_advisory_lock_shared(2),
	pg_advisory_lock(1, 1), pg_advisory_lock(1, 1),
	pg_advisory_lock_shared(2, 2), pg_advisory_lock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
-- YB: TODO(GH#26179): ensure pg_locks shows only one advisory lock per key
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;

SELECT
	pg_advisory_unlock(1), pg_advisory_unlock(1),
	pg_advisory_unlock_shared(2), pg_advisory_unlock_shared(2),
	pg_advisory_unlock(1, 1), pg_advisory_unlock(1, 1),
	pg_advisory_unlock_shared(2, 2), pg_advisory_unlock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;

-- .. and releasing them all at once

SELECT
	pg_advisory_lock(1), pg_advisory_lock(1),
	pg_advisory_lock_shared(2), pg_advisory_lock_shared(2),
	pg_advisory_lock(1, 1), pg_advisory_lock(1, 1),
	pg_advisory_lock_shared(2, 2), pg_advisory_lock_shared(2, 2);

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
-- YB: TODO(GH#26179): ensure pg_locks shows only one advisory lock per key
SELECT locktype, classid, objid, objsubid, mode, granted
	FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid
	ORDER BY classid, objid, objsubid;

SELECT pg_advisory_unlock_all();

SELECT pg_sleep(2);  -- YB: sleep 2 second to ensure advisory lock tablets are propagated(via the transaction heartbeat)
SELECT count(*) FROM pg_locks WHERE locktype = 'advisory' AND database = :datoid;
