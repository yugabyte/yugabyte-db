-- Create Tablespace API tests
-- Create Tablespace should not work if neither LOCATION nor options
-- are specified
CREATE TABLESPACE x;
CREATE TABLESPACE x OWNER yugabyte;

-- Ill formed JSON
CREATE TABLESPACE x WITH (replica_placement='[{"cloud"}]');

-- num_replicas field missing.
CREATE TABLESPACE x WITH (replica_placement='{"placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":3}]}');

-- placement_blocks field missing.
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas":3}');

-- Invalid value for num_replicas.
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas":"three", "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":3}]}');

-- Invalid value for min_num_replicas.
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":"three"}]}');

-- Invalid keys in the placement policy.
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":3,"invalid_key":"invalid_value"}]}');

-- Sum of min_num_replicas greater than num_replicas.
CREATE TABLESPACE y WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":2},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":2}]}');

-- Positive cases
-- Tablespace without replica_placement option.
CREATE TABLESPACE x LOCATION '/data';

-- Sum of min_num_replicas lesser than num_replicas.
CREATE TABLESPACE y WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":1}]}');

-- Sum of min_num_replicas equal to num_replicas.
CREATE TABLESPACE z WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":2}]}');

-- describe command
\db

CREATE TABLEGROUP grp TABLESPACE x;
-- Fail, not empty
\set VERBOSITY terse \\ -- suppress dependency details.
DROP TABLESPACE x;
\set VERBOSITY default
DROP TABLEGROUP grp;
-- Should succeed, empty now
DROP TABLESPACE x;
DROP TABLESPACE y;

-- create a tablespace using WITH clause
CREATE TABLESPACE regress_tblspacewith WITH (some_nonexistent_parameter = true); -- fail
CREATE TABLESPACE regress_tblspacewith WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":2}]}'); -- ok

-- check to see if WITH clause parameter was used
SELECT spcoptions FROM pg_tablespace WHERE spcname = 'regress_tblspacewith';

DROP TABLESPACE regress_tblspacewith;

-- create a tablespace we can use
CREATE TABLESPACE regress_tblspace LOCATION '/data';
CREATE TABLESPACE regress_tblspace_2 LOCATION '/data';

-- try setting and resetting some properties for the new tablespace
ALTER TABLESPACE regress_tblspace SET (random_page_cost = 1.0, seq_page_cost = 1.1);

-- Enable these tests after ALTER is supported.
/*
ALTER TABLESPACE regress_tblspace SET (some_nonexistent_parameter = true);  -- fail
ALTER TABLESPACE regress_tblspace RESET (random_page_cost = 2.0); -- fail
ALTER TABLESPACE regress_tblspace RESET (random_page_cost, effective_io_concurrency); -- ok
*/

-- create a schema we can use
CREATE SCHEMA testschema;

-- create a tablespace not satisfied by the cluster
CREATE TABLESPACE invalid_tablespace WITH (replica_placement='{"num_replicas": 1, "placement_blocks":[{"cloud":"nonexistent","region":"nowhere","zone":"area51","min_num_replicas":1}]}');

-- try using unsatisfiable tablespace
CREATE TABLE testschema.foo (i int) TABLESPACE invalid_tablespace; -- fail
CREATE TABLE testschema.foo (i int) TABLESPACE regress_tblspace;
ALTER TABLE testschema.foo SET TABLESPACE invalid_tablespace; -- fail
DROP TABLE testschema.foo;
DROP TABLESPACE invalid_tablespace;

-- try a table
CREATE TABLE testschema.foo (i int) TABLESPACE regress_tblspace;
SELECT relname, spcname FROM pg_catalog.pg_tablespace t, pg_catalog.pg_class c
    where c.reltablespace = t.oid AND c.relname = 'foo';

INSERT INTO testschema.foo VALUES(1);
INSERT INTO testschema.foo VALUES(2);

-- tables from dynamic sources
CREATE TABLE testschema.asselect TABLESPACE regress_tblspace AS SELECT 1;
SELECT relname, spcname FROM pg_catalog.pg_tablespace t, pg_catalog.pg_class c
    where c.reltablespace = t.oid AND c.relname = 'asselect';

PREPARE selectsource(int) AS SELECT $1;
CREATE TABLE testschema.asexecute TABLESPACE regress_tblspace
    AS EXECUTE selectsource(2);
/*
SELECT relname, spcname FROM pg_catalog.pg_tablespace t, pg_catalog.pg_class c
    where c.reltablespace = t.oid AND c.relname = 'asexecute';
*/

-- Create table with primary key.
CREATE TABLE testschema.foo_pk (i int, PRIMARY KEY(i)) TABLESPACE regress_tblspace;
\d testschema.foo_pk_pkey
\d testschema.foo_pk;
-- Fail, cannot ALTER INDEX SET TABLESPACE for primary key index.
ALTER INDEX testschema.foo_pk_pkey SET TABLESPACE regress_tblspace_2;

-- Create table with primary key after default tablespace is changed.
SET default_tablespace TO regress_tblspace;
CREATE TABLE testschema.foo_pk_default_tblspc (i int, PRIMARY KEY(i));
\d testschema.foo_pk_default_tblspc_pkey;
\d testschema.foo_pk_default_tblspc;
-- Fail, cannot ALTER INDEX SET TABLESPACE for primary key index.
ALTER INDEX testschema.foo_pk_pkey SET TABLESPACE pg_default;
SET default_tablespace TO '';

-- Verify that USING INDEX TABLESPACE is not supported for primary keys.
CREATE TABLE testschema.using_index1 (a int PRIMARY KEY USING INDEX TABLESPACE regress_tblspace);
CREATE TABLE testschema.using_index1 (a int, PRIMARY KEY(a) USING INDEX TABLESPACE regress_tblspace);

-- Verify that USING INDEX TABLESPACE is supported for other constraints.
CREATE TABLE testschema.using_index2 (a int UNIQUE USING INDEX TABLESPACE regress_tblspace);
CREATE TABLE testschema.using_index3 (a int, UNIQUE(a) USING INDEX TABLESPACE regress_tblspace);
\d testschema.using_index2;
\d testschema.using_index3;

ALTER INDEX testschema.using_index2_a_key SET TABLESPACE pg_default;
\d testschema.using_index2;

-- index
CREATE INDEX foo_idx on testschema.foo(i) TABLESPACE regress_tblspace;
SELECT relname, spcname FROM pg_catalog.pg_tablespace t, pg_catalog.pg_class c
    where c.reltablespace = t.oid AND c.relname = 'foo_idx';

-- partitioned table
CREATE TABLE testschema.part (a int) PARTITION BY LIST (a);
CREATE TABLE testschema.part12 PARTITION OF testschema.part FOR VALUES IN(1,2) PARTITION BY LIST (a) TABLESPACE regress_tblspace;
CREATE TABLE testschema.part12_1 PARTITION OF testschema.part12 FOR VALUES IN (1);
ALTER TABLE testschema.part12 SET TABLESPACE pg_default;
CREATE TABLE testschema.part12_2 PARTITION OF testschema.part12 FOR VALUES IN (2);
-- Ensure part12_1 defaulted to regress_tblspace and part12_2 defaulted to pg_default.
SELECT relname, spcname FROM pg_catalog.pg_class c
    LEFT JOIN pg_catalog.pg_tablespace t ON c.reltablespace = t.oid
    where c.relname LIKE 'part%' order by relname;
DROP TABLE testschema.part;

-- temporary table
-- Fail, cannot set tablespaces for temp tables
CREATE TEMPORARY TABLE temptest (a INT) TABLESPACE regress_tblspace;
CREATE TEMPORARY TABLE temptest (a INT);
-- Fail, cannot set tablespaces for temp tables
ALTER TABLE temptest SET TABLESPACE regress_tblspace;
-- Fail, cannot set tablespace for temp indexes
CREATE INDEX temp_idx_tblspc ON temptest(a) TABLESPACE regress_tblspace;
CREATE INDEX temp_idx ON temptest(a);
-- Fail, cannot set tablespaces for temp tables
ALTER INDEX temp_idx SET TABLESPACE regress_tblspace;
DROP TABLE temptest;

-- Fail, cannot set tablespaces for temp tables
CREATE TEMPORARY TABLE tempparttest (a int) PARTITION BY LIST (a) TABLESPACE regress_tblspace;
CREATE TEMPORARY TABLE tempparttest (a int) PARTITION BY LIST (a);
-- Fail, cannot set tablespaces for temp tables
ALTER TABLE tempparttest SET TABLESPACE regress_tblspace;
DROP TABLE tempparttest;

-- partitioned index
CREATE TABLE testschema.part (a int) PARTITION BY LIST (a);
CREATE TABLE testschema.part1 PARTITION OF testschema.part FOR VALUES IN (1);
CREATE INDEX part_a_idx ON testschema.part (a) TABLESPACE regress_tblspace;
CREATE TABLE testschema.part2 PARTITION OF testschema.part FOR VALUES IN (2);
SELECT relname, spcname FROM pg_catalog.pg_tablespace t, pg_catalog.pg_class c
    where c.reltablespace = t.oid AND c.relname LIKE 'part%_idx' ORDER BY relname;

CREATE TABLE testschema.part34 PARTITION OF testschema.part FOR VALUES IN (3, 4) PARTITION BY LIST (a);
CREATE TABLE testschema.part3 PARTITION OF testschema.part34 FOR VALUES IN (3);
ALTER INDEX testschema.part34_a_idx SET TABLESPACE pg_default;
CREATE TABLE testschema.part4 PARTITION OF testschema.part34 FOR VALUES IN (4);
SELECT relname, spcname FROM pg_catalog.pg_class c LEFT OUTER JOIN pg_catalog.pg_tablespace t
    ON c.reltablespace = t.oid WHERE c.relname LIKE 'part%_idx' ORDER BY relname;

-- check that default_tablespace doesn't affect ALTER TABLE index rebuilds
CREATE TABLE testschema.test_default_tab(id bigint) TABLESPACE regress_tblspace;
INSERT INTO testschema.test_default_tab VALUES (1);
CREATE INDEX test_index1 on testschema.test_default_tab (id);
CREATE INDEX test_index2 on testschema.test_default_tab (id) TABLESPACE regress_tblspace;
\d testschema.test_index1
\d testschema.test_index2
-- use a custom tablespace for default_tablespace
SET default_tablespace TO regress_tblspace;
-- tablespace should not change if no rewrite
ALTER TABLE testschema.test_default_tab ALTER id TYPE bigint;
\d testschema.test_index1
\d testschema.test_index2
SELECT * FROM testschema.test_default_tab;
-- tablespace should not change even if there is an index rewrite
ALTER TABLE testschema.test_default_tab ALTER id TYPE int;
\d testschema.test_index1
\d testschema.test_index2
SELECT * FROM testschema.test_default_tab;
-- now use the default tablespace for default_tablespace
SET default_tablespace TO '';
-- tablespace should not change if no rewrite
ALTER TABLE testschema.test_default_tab ALTER id TYPE int;
\d testschema.test_index1
\d testschema.test_index2
-- tablespace should not change even if there is an index rewrite
ALTER TABLE testschema.test_default_tab ALTER id TYPE bigint;
\d testschema.test_index1
\d testschema.test_index2
DROP TABLE testschema.test_default_tab;
-- check that default_tablespace affects index additions in ALTER TABLE
CREATE TABLE testschema.test_tab(id int) TABLESPACE regress_tblspace;
INSERT INTO testschema.test_tab VALUES (1);
SET default_tablespace TO regress_tblspace;
ALTER TABLE testschema.test_tab ADD CONSTRAINT test_tab_unique UNIQUE (id);
SET default_tablespace TO '';
ALTER TABLE testschema.test_tab ADD CONSTRAINT test_tab_pkey PRIMARY KEY (id);
\d testschema.test_tab_unique
\d testschema.test_tab_pkey
\d testschema.test_tab;
SELECT * FROM testschema.test_tab;
DROP TABLE testschema.test_tab;

-- let's try moving a table from one place to another
CREATE TABLE testschema.atable AS VALUES (1), (2);
CREATE UNIQUE INDEX anindex ON testschema.atable(column1);
ALTER TABLE testschema.atable SET TABLESPACE regress_tblspace;
ALTER INDEX testschema.anindex SET TABLESPACE regress_tblspace;
\d testschema.atable
ALTER INDEX testschema.part_a_idx SET TABLESPACE pg_global;
\d testschema.part
ALTER INDEX testschema.part_a_idx SET TABLESPACE pg_default;
\d testschema.part
ALTER INDEX testschema.part_a_idx SET TABLESPACE regress_tblspace;
\d testschema.part  
INSERT INTO testschema.atable VALUES(3);
INSERT INTO testschema.atable VALUES(1);
SELECT COUNT(*) FROM testschema.atable;

-- No such tablespace
CREATE TABLE bar (i int) TABLESPACE regress_nosuchspace;

-- Fail, not empty
\set VERBOSITY terse \\ -- suppress dependency details.
DROP TABLESPACE regress_tblspace;
\set VERBOSITY default

CREATE ROLE regress_tablespace_user1 login;
CREATE ROLE regress_tablespace_user2 login;
GRANT USAGE ON SCHEMA testschema TO regress_tablespace_user2;

ALTER TABLESPACE regress_tblspace OWNER TO regress_tablespace_user1;

CREATE TABLE testschema.tablespace_acl (c int);
-- new owner lacks permission to create this index from scratch
CREATE INDEX k ON testschema.tablespace_acl (c) TABLESPACE regress_tblspace;
ALTER TABLE testschema.tablespace_acl OWNER TO regress_tablespace_user2;

SET SESSION ROLE regress_tablespace_user2;
CREATE TABLE tablespace_table (i int) TABLESPACE regress_tblspace; -- fail
ALTER TABLE testschema.tablespace_acl ALTER c TYPE bigint;
RESET ROLE;

ALTER TABLESPACE regress_tblspace RENAME TO regress_tblspace_renamed;
/*
ALTER TABLE ALL IN TABLESPACE regress_tblspace_renamed SET TABLESPACE pg_default;
ALTER INDEX ALL IN TABLESPACE regress_tblspace_renamed SET TABLESPACE pg_default;

-- Should show notice that nothing was done
ALTER TABLE ALL IN TABLESPACE regress_tblspace_renamed SET TABLESPACE pg_default;

-- Should succeed
DROP TABLESPACE regress_tblspace_renamed;
*/
\set VERBOSITY terse \\ -- suppress cascade details
DROP SCHEMA testschema CASCADE;
\set VERBOSITY default
DROP ROLE regress_tablespace_user1;
DROP TABLESPACE regress_tblspace;
DROP ROLE regress_tablespace_user1;
DROP ROLE regress_tablespace_user2;

-- Colocated Tests
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1}]}');
CREATE DATABASE colocation_test colocation = true;
\c colocation_test
-- Should succeed in setting tablespace on a table in a colocated database
CREATE TABLE tab_key (a INT) TABLESPACE x;
-- Should succeed in setting tablespace on a table in a colocated database when opted out
CREATE TABLE tab_nonkey (a INT) WITH (COLOCATION = false) TABLESPACE x;
-- cleanup
DROP TABLE tab_key;
DROP TABLE tab_nonkey;
\c yugabyte
DROP DATABASE colocation_test;

-- Verify that tablespaces cannot be set on partitioned tables.
CREATE TABLE list_partitioned (partkey char) PARTITION BY LIST(partkey) TABLESPACE x;
-- Cleanup.
DROP TABLE list_partitioned;
DROP TABLESPACE x;

/*
Testing to make sure that an index on a "near" tablespace whose placements are
all on the current cloud/region/zone is preferred over "far" indexes.
*/
CREATE TABLESPACE near WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1}]}');
CREATE TABLESPACE far WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud2","region":"region2", "zone":"zone2", "min_num_replicas":1}]}');
CREATE TABLESPACE regionlocal WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1}]}');
CREATE TABLESPACE cloudlocal WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1}]}');
CREATE TABLE foo(x int, y int);
CREATE UNIQUE INDEX good ON foo(x) INCLUDE (y) TABLESPACE near;
CREATE UNIQUE INDEX regionlocal_ind ON foo(x) INCLUDE (y) TABLESPACE regionlocal;
CREATE UNIQUE INDEX cloudlocal_ind ON foo(x) INCLUDE (y) TABLESPACE cloudlocal;
CREATE UNIQUE INDEX bad ON foo(x) INCLUDE (y) TABLESPACE far;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
SET yb_enable_geolocation_costing = off;
EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
SET yb_enable_geolocation_costing = on;
DROP INDEX good;
EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX regionlocal_ind;
EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;

DROP TABLE foo;

CREATE TABLE foo(id int primary key, val int);
CREATE UNIQUE INDEX bad ON foo(id) INCLUDE (val) TABLESPACE far;
CREATE UNIQUE INDEX good ON foo(id) INCLUDE (val) TABLESPACE near;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE id = 5;

DROP TABLE foo;
DROP TABLESPACE far;
DROP TABLESPACE near;
DROP TABLESPACE regionlocal;
DROP TABLESPACE cloudlocal;

-- Verify yb_db_admin role can use tablespace
-- Create objects not owned by yb_db_admin
CREATE TABLESPACE tblspace_other WITH (replica_placement='{"num_replicas": 1, "placement_blocks": [{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1}]}');
CREATE TABLE tbl_other(x int, y int);
SET SESSION ROLE yb_db_admin;
-- Verify yb_db_admin role can CREATE tablespace
CREATE TABLESPACE tblspace WITH (replica_placement='{"num_replicas": 1, "placement_blocks": [{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1}]}');
-- Verify yb_db_admin role can CREATE table with tablespace
CREATE TABLE tbl (x int, y int) TABLESPACE tblspace;
DROP TABLE tbl;
-- Verify yb_db_admin role can assign tablespace to tables
ALTER TABLE tbl_other SET TABLESPACE tblspace_other;
CREATE TABLE tbl (x int, y int);
ALTER TABLE tbl SET TABLESPACE tblspace;
DROP TABLE tbl;
-- Verify yb_db_admin_role can assign tablespace to index
CREATE TABLE tbl(x int, y int);
CREATE INDEX idx ON tbl(x) TABLESPACE tblspace;
CREATE INDEX idx2 ON tbl_other(x) TABLESPACE tblspace;
-- Verify yb_db_admin role cannot ALTER tablespace
ALTER TABLESPACE tblspace SET (random_page_cost = 1.0, seq_page_cost = 1.1);
-- Verify yb_db_admin role can DROP tablespace
DROP TABLE tbl;
DROP TABLE tbl_other;
DROP TABLESPACE tblspace;
DROP TABLESPACE tblspace_other;

-- Test leader_preference
-- Empty leader_preference
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":}]}');

-- Negative leader_preference
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":-1}]}');

-- Zero as leader_preference
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":0}]}');

-- No leader_preference 1
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":2}]}');

-- No leader_preference 2
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1,"leader_preference":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":1,"leader_preference":1},{"cloud":"cloud2","region":"r2", "zone":"z3", "min_num_replicas":1,"leader_preference":3}]}');

-- No leader_preference 2 and no preference set
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1,"leader_preference":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z3", "min_num_replicas":1,"leader_preference":3}]}');

-- Positive case
-- Some zones with no leader_preference set
CREATE TABLESPACE LP WITH (replica_placement='{"num_replicas":3, "placement_blocks":[{"cloud":"cloud1","region":"r1","zone":"z1","min_num_replicas":1,"leader_preference":1},{"cloud":"cloud2","region":"r2", "zone":"z2", "min_num_replicas":1},{"cloud":"cloud2","region":"r2", "zone":"z3", "min_num_replicas":1}]}');
-- Valid case
CREATE TABLESPACE valid_tablespace WITH (replica_placement='{"num_replicas":2,"placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},{"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1,"leader_preference":2}]}');
CREATE TABLE foo (i int) TABLESPACE valid_tablespace;
CREATE TABLE bar(i int);
ALTER TABLE bar SET TABLESPACE valid_tablespace;
DROP TABLE foo;
DROP TABLE bar;
DROP TABLESPACE valid_tablespace;
DROP TABLESPACE LP;

/*
Testing that an index in a tablespace with leader preference on a closer placement is preferred
over one on a tablespace with leader preference on a farther placement.
*/

CREATE TABLE foo(x int, y int);

CREATE TABLESPACE zone_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE TABLESPACE region_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE TABLESPACE cloud_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE UNIQUE INDEX zone_pref_ind ON foo(x) INCLUDE (y) TABLESPACE zone_pref;
CREATE UNIQUE INDEX region_pref_ind ON foo(x) INCLUDE (y) TABLESPACE region_pref;
CREATE UNIQUE INDEX cloud_pref_ind ON foo(x) INCLUDE (y) TABLESPACE cloud_pref;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
SET yb_enable_geolocation_costing = off;
EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
SET yb_enable_geolocation_costing = on;
DROP INDEX zone_pref_ind;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX region_pref_ind;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX cloud_pref_ind;

DROP TABLE foo;
DROP TABLESPACE zone_pref;
DROP TABLESPACE region_pref;
DROP TABLESPACE cloud_pref;

/*
Testing that a leader preference > 1 is ignored in distance cost calculations.
*/

CREATE TABLE foo(x int, y int);

CREATE TABLESPACE zone_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":2},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1,"leader_preference":3},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1,"leader_preference":4}]}');

CREATE TABLESPACE region_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":4},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1,"leader_preference":2},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1,"leader_preference":3}]}');

CREATE TABLESPACE cloud_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":3},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":4},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1,"leader_preference":2}]}');

CREATE UNIQUE INDEX zone_pref_ind ON foo(x) INCLUDE (y) TABLESPACE zone_pref;
CREATE UNIQUE INDEX region_pref_ind ON foo(x) INCLUDE (y) TABLESPACE region_pref;
CREATE UNIQUE INDEX cloud_pref_ind ON foo(x) INCLUDE (y) TABLESPACE cloud_pref;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX zone_pref_ind;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX region_pref_ind;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX cloud_pref_ind;

DROP TABLE foo;
DROP TABLESPACE zone_pref;
DROP TABLESPACE region_pref;
DROP TABLESPACE cloud_pref;

/*
Testing that a tablespace with leader preference is preferred over one with
no leader preferences, when they have the same placements.
*/

CREATE TABLE foo(x int, y int);

CREATE TABLESPACE no_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE TABLESPACE has_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE UNIQUE INDEX no_pref_ind ON foo(x) INCLUDE (y) TABLESPACE no_pref;
CREATE UNIQUE INDEX has_pref_ind ON foo(x) INCLUDE (y) TABLESPACE has_pref;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;

DROP TABLE foo;
DROP TABLESPACE no_pref;
DROP TABLESPACE has_pref;

/*
Testing that more specific leader-preferenced placements are preferred over
less specific ones.
*/

CREATE TABLE foo(x int, y int);

CREATE TABLESPACE most_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE TABLESPACE some_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE TABLESPACE few_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE UNIQUE INDEX few_pref_ind ON foo(x) INCLUDE (y) TABLESPACE few_pref;
CREATE UNIQUE INDEX some_pref_ind ON foo(x) INCLUDE (y) TABLESPACE some_pref;
CREATE UNIQUE INDEX most_pref_ind ON foo(x) INCLUDE (y) TABLESPACE most_pref;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX few_pref_ind;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX some_pref_ind;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP INDEX most_pref_ind;

DROP TABLE foo;
DROP TABLESPACE most_pref;
DROP TABLESPACE some_pref;
DROP TABLESPACE few_pref;

/*
Testing that a tablespace with leader preference on a close and far placement is NOT
preferred over a tablespace with leader preference on just a medium-distance placement.

We estimate based on the worst case, which means that having a far leader-preferred placement
leads to a large cost.
*/
CREATE TABLE foo(x int, y int);

CREATE TABLESPACE close_far_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE TABLESPACE medium_pref
  WITH (replica_placement='{"num_replicas": 4, "placement_blocks": [
    {"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud1","region":"region1","zone":"zone2","min_num_replicas":1,"leader_preference":1},
    {"cloud":"cloud1","region":"region2","zone":"zone1","min_num_replicas":1},
    {"cloud":"cloud2","region":"region2","zone":"zone2","min_num_replicas":1}]}');

CREATE UNIQUE INDEX close_far_pref_ind ON foo(x) INCLUDE (y) TABLESPACE close_far_pref;
CREATE UNIQUE INDEX medium_pref_ind ON foo(x) INCLUDE (y) TABLESPACE medium_pref;

EXPLAIN (COSTS OFF) SELECT * FROM foo WHERE x = 5;
DROP TABLE foo;
DROP TABLESPACE close_far_pref;
DROP TABLESPACE medium_pref;
