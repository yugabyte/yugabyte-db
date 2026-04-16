-- YB note: deferable unique constraints and OIDs are not supported for user tables
CREATE TABLE test_replica_identity (
       id serial primary key,
       keya text not null,
       keyb text not null,
       nonkey text,
    --    CONSTRAINT test_replica_identity_unique_defer UNIQUE (keya, keyb) DEFERRABLE,
       CONSTRAINT test_replica_identity_unique_nondefer UNIQUE (keya, keyb)
);-- WITH OIDS;

CREATE INDEX test_replica_identity_keyab ON test_replica_identity (keya, keyb);

-- default is 'c' (CHANGE) for user created tables
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;
-- but 'none' for system tables
SELECT relreplident FROM pg_class WHERE oid = 'pg_class'::regclass;
SELECT relreplident FROM pg_class WHERE oid = 'pg_constraint'::regclass;

-- YB Note: REPLICA IDENTITY mode INDEX is not supported yet, we will assert the failure message
-- here. Other queries for testing INDEX mode should be ported whenever the support is added.
ALTER TABLE test_replica_identity REPLICA IDENTITY USING INDEX test_replica_identity_keyab;

-- YB Note: Since REPLICA IDENTITY mode INDEX is not supported yet, this will be 'c' (CHANGE)
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;

----
-- Make sure non index cases work
----
ALTER TABLE test_replica_identity REPLICA IDENTITY DEFAULT;
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;
SELECT count(*) FROM pg_index WHERE indrelid = 'test_replica_identity'::regclass AND indisreplident;

ALTER TABLE test_replica_identity REPLICA IDENTITY FULL;
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;
\d+ test_replica_identity
ALTER TABLE test_replica_identity REPLICA IDENTITY NOTHING;
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;

DROP TABLE test_replica_identity;
