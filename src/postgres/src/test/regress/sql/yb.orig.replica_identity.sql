CREATE TABLE test_replica_identity (
       id serial primary key,
       keya text not null,
       keyb text not null,
       nonkey text
);

----
-- Make sure yb specific replica identity modes work
----

ALTER TABLE test_replica_identity REPLICA IDENTITY CHANGE;
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;
\d+ test_replica_identity

-- YB Note: Make sure replica identity is retained after table rewrite operations
ALTER TABLE test_replica_identity REPLICA IDENTITY FULL;
ALTER TABLE test_replica_identity DROP CONSTRAINT test_replica_identity_pkey;
SELECT relreplident FROM pg_class WHERE oid = 'test_replica_identity'::regclass;


-- YB Note: Check default replica identity of temp table
CREATE TEMPORARY TABLE test_temporary_replica_identity (
    a INT PRIMARY KEY,
    b INT
);
SELECT relreplident FROM pg_class WHERE oid = 'test_temporary_replica_identity'::regclass;

DROP TABLE test_temporary_replica_identity;
DROP TABLE test_replica_identity;
