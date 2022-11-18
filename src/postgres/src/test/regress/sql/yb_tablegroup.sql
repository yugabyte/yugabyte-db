--
-- YB_TABLEGROUP Testsuite: Testing Statments for TABLEGROUP.
--
\h CREATE TABLEGROUP
\h ALTER TABLEGROUP
\h DROP TABLEGROUP

--
-- pg_catalog alterations. Validate columns of pg_yb_tablegroup and oids.
--
\d pg_yb_tablegroup
SELECT oid, relname, reltype, relnatts FROM pg_class WHERE relname IN ('pg_yb_tablegroup', 'pg_yb_tablegroup_oid_index');
SELECT oid, typname, typrelid FROM pg_type WHERE typname LIKE 'pg_yb_tablegroup';
--
-- CREATE TABLEGROUP
--

CREATE TABLEGROUP tgroup1;
CREATE TABLEGROUP tgroup2;
CREATE TABLEGROUP tgroup3;
CREATE TABLE tgroup_test1 (col1 int, col2 int) TABLEGROUP tgroup1;
CREATE INDEX ON tgroup_test1(col2);
CREATE TABLE tgroup_test2 (col1 int, col2 int) TABLEGROUP tgroup1;
CREATE TABLE nogroup (col1 int) NO TABLEGROUP; -- fail
CREATE TABLE nogroup (col1 int);
-- TODO(alex): Create a table with a primary key
SELECT grpname FROM pg_yb_tablegroup;

SELECT cl.relname, tg.grpname
    FROM pg_class cl, yb_table_properties(cl.oid) props
    LEFT JOIN pg_yb_tablegroup tg ON tg.oid = props.tablegroup_oid
    WHERE cl.oid >= 16384
    ORDER BY cl.relname;

CREATE TABLE tgroup_test3 (col1 int, col2 int) TABLEGROUP tgroup2;
CREATE INDEX ON tgroup_test3(col1);

SELECT cl.relname, tg.grpname
    FROM pg_class cl, yb_table_properties(cl.oid) props
    LEFT JOIN pg_yb_tablegroup tg ON tg.oid = props.tablegroup_oid
    WHERE cl.oid >= 16384
    ORDER BY cl.relname;

-- These should fail.
CREATE TABLEGROUP tgroup1;
CREATE TABLE tgroup_test (col1 int, col2 int) TABLEGROUP bad_tgroupname;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocation=false) TABLEGROUP tgroup1;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocation=true) TABLEGROUP tgroup1;
CREATE TABLE tgroup_optout (col1 int, col2 int) WITH (colocation=false) TABLEGROUP bad_tgroupname;
CREATE TEMP TABLE tgroup_temp (col1 int, col2 int) TABLEGROUP tgroup1;
CREATE INDEX ON tgroup_test3(col1) TABLEGROUP tgroup1; -- fail
CREATE INDEX ON tgroup_test3(col1) NO TABLEGROUP; -- fail

--
-- Cannot drop dependent objects
--
CREATE USER alice;
CREATE USER bob;

CREATE TABLEGROUP alice_grp OWNER alice;
CREATE TABLEGROUP alice_grp_2 OWNER alice;
CREATE TABLEGROUP alice_grp_3 OWNER alice;
CREATE TABLE bob_table (a INT) TABLEGROUP alice_grp;
CREATE TABLE bob_table_2 (a INT) TABLEGROUP alice_grp_2;
CREATE TABLE bob_table_3 (a INT) TABLEGROUP alice_grp_3;
CREATE TABLE alice_table (a INT);
ALTER TABLE bob_table OWNER TO bob;
ALTER TABLE bob_table_2 OWNER TO bob;
ALTER TABLE bob_table_3 OWNER TO bob;
ALTER TABLE alice_table OWNER TO alice;

-- Fails
\set VERBOSITY terse \\ -- suppress dependency details.
DROP TABLEGROUP alice_grp;  -- because bob_table depends on alice_grp
DROP USER alice;            -- because alice still owns tablegroups
DROP OWNED BY alice;        -- because bob's tables depend on alice's tablegroups
\set VERBOSITY default

-- Succeeds
DROP TABLEGROUP alice_grp_3 CASCADE;
DROP TABLE bob_table;

SELECT relname FROM pg_class WHERE relname LIKE 'bob%';

DROP TABLEGROUP alice_grp;    -- bob_table is gone, so alice_grp has no deps
DROP OWNED BY alice CASCADE;  -- CASCADE means we are allowed to drop bob's tables. We'll notify about any entities that she doesn't own.
DROP USER alice;              -- we dropped all of alice's entities, so we can remove her

SELECT relname FROM pg_class WHERE relname LIKE 'bob%';

--
-- Usage of WITH tablegroup_oid reloption, this legacy syntax no longer works.
--

CREATE TABLE tgroup_with1 (col1 int, col2 int) WITH (tablegroup_oid=16385);
CREATE TABLE tgroup_with3 (col1 int, col2 int) WITH (tablegroup_oid=16385) TABLEGROUP tgroup1;

CREATE INDEX ON tgroup_test1(col1) WITH (tablegroup_oid=123);

--
-- Usage of SPLIT clause with TABLEGROUP should fail
--
CREATE TABLE tgroup_split (col1 int PRIMARY KEY) SPLIT INTO 3 TABLETS TABLEGROUP tgroup1;
CREATE TABLE tgroup_split (col1 int, col2 text) SPLIT INTO 3 TABLETS TABLEGROUP tgroup1;
CREATE INDEX ON tgroup_test1(col1) SPLIT AT VALUES((10), (20), (30));

--
-- Test CREATE INDEX with hash columns
--
CREATE TABLEGROUP tgroup_hash_index_test;
CREATE TABLE tbl (r1 int, r2 int, v1 int, v2 int,
PRIMARY KEY (r1, r2)) TABLEGROUP tgroup_hash_index_test;
CREATE INDEX idx_tbl ON tbl (r1 hash);
CREATE INDEX idx2_tbl ON tbl ((r1, r2) hash);
CREATE INDEX idx3_tbl ON tbl (r1 hash, r2 asc);
CREATE UNIQUE INDEX unique_idx_tbl ON tbl (r1 hash);
CREATE UNIQUE INDEX unique_idx2_tbl ON tbl ((r1, r2) hash);
CREATE UNIQUE INDEX unique_idx3_tbl ON tbl (r1 hash, r2 asc);
\d tbl

-- Make sure nothing bad happens to UNIQUE constraints after disabling HASH columns
-- for tablegroup indexes
CREATE TABLE tbl2 (r1 int PRIMARY KEY, r2 int, v1 int, v2 int, UNIQUE(v1))
TABLEGROUP tgroup_hash_index_test;
ALTER TABLE tbl2 ADD CONSTRAINT unique_v2_tbl2 UNIQUE(v2);
\d tbl2

DROP TABLE tbl, tbl2;
DROP TABLEGROUP tgroup_hash_index_test;

--
-- Test describes
--
CREATE TABLE tgroup_test4 (col1 int, col2 int) TABLEGROUP tgroup2;
CREATE INDEX ON tgroup_test4(col1);
CREATE INDEX ON tgroup_test4(col2);
-- Add comments
COMMENT ON TABLEGROUP tgroup1 IS 'Comment for Tablegroup 1';
COMMENT ON TABLEGROUP tgroup2 IS 'Comment for Tablegroup 2';
\dgr
\dgr+
\dgrt
\dgrt+
COMMENT ON TABLEGROUP tgroup2 IS NULL;
\dgr+ tgroup2
\dgrt tgroup2

-- Describe table
\d tgroup_test2
\d tgroup_test4
\d tgroup_test4_col1_idx
CREATE TABLEGROUP tgroup_describe1;
CREATE TABLEGROUP tgroup_describe2;
CREATE TABLE tgroup_describe (col1 int) TABLEGROUP tgroup_describe1;
CREATE INDEX ON tgroup_describe(col1);
\d tgroup_describe
--
-- DROP TABLEGROUP
--

DROP TABLEGROUP tgroup3;
-- These should fail. CREATE TABLE is to check that the row entry was deleted from pg_yb_tablegroup.
CREATE TABLE tgroup_test5 (col1 int, col2 int) TABLEGROUP tgroup3;
\set VERBOSITY terse \\ -- suppress dependency details.
DROP TABLEGROUP tgroup1;
DROP TABLEGROUP IF EXISTS tgroup1;
\set VERBOSITY default
DROP TABLEGROUP bad_tgroupname;
-- This drop should work now.
DROP TABLE tgroup_test1;
DROP TABLE tgroup_test2;
DROP TABLEGROUP tgroup1;
DROP TABLEGROUP IF EXISTS tgroup1;
-- Create a tablegroup with the name of a dropped tablegroup.
CREATE TABLEGROUP tgroup1;
DROP TABLEGROUP IF EXISTS tgroup1;
CREATE TABLEGROUP tgroup1;

--
-- Assigning a tablespace to a tablegroup
--
CREATE TABLESPACE tblspc WITH (replica_placement='{"num_replicas": 1, "placement_blocks": [{"cloud":"cloud1","region":"datacenter1","zone":"rack1","min_num_replicas":1}]}');

-- These should fail
CREATE TABLEGROUP grp1 TABLESPACE nonexistentspc;
CREATE TABLEGROUP grp2 TABLESPACE pg_global;
-- These should succeeed
CREATE TABLEGROUP grp3 TABLESPACE tblspc;

SET default_tablespace = "tblspc";
CREATE TABLEGROUP grp4;
SET default_tablespace = '';

CREATE TABLE tgroup_test6 (col1 int, col2 int) TABLEGROUP grp3;
\dgr+
\dgrt+

--
-- Interactions with colocated database.
--

CREATE DATABASE db_colocated colocation=true;
\c db_colocated
-- This should fail.
CREATE TABLEGROUP tgroup1;
