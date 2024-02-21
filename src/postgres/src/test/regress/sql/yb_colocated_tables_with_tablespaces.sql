-- Colocated Tests with Tablespaces
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas":1, "placement_blocks":[{"cloud":"cloud1","region":"region1","zone":"zone1","min_num_replicas":1}]}');
CREATE DATABASE colocation_test colocation = true;
\c colocation_test

CREATE TABLESPACE tsp1 WITH (replica_placement = '{
"num_replicas": 3,
"placement_blocks": [
  {
    "cloud" :  "cloud1",
    "region":  "region1",
    "zone"  :  "zone1",
    "min_num_replicas" : 1
  },
  {
    "cloud" :  "cloud1",
    "region":  "region1",
    "zone"  :  "zone2",
    "min_num_replicas" : 1
  },
  {
    "cloud" :  "cloud1",
    "region":  "region1",
    "zone"  :  "zone3",
    "min_num_replicas" : 1
  }
]
}');

CREATE TABLESPACE tsp2 WITH (replica_placement = '{
"num_replicas": 3,
"placement_blocks": [
  {
    "cloud" :  "cloud1",
    "region":  "region2",
    "zone"  :  "zone1",
    "min_num_replicas" : 1
  },
  {
    "cloud" :  "cloud1",
    "region":  "region2",
    "zone"  :  "zone2",
    "min_num_replicas" : 1
  },
  {
    "cloud" :  "cloud1",
    "region":  "region2",
    "zone"  :  "zone3",
    "min_num_replicas" : 1
  }
]
}');

CREATE TABLESPACE tsp3 WITH (replica_placement = '{
"num_replicas": 3,
"placement_blocks": [
  {
    "cloud" :  "cloud1",
    "region":  "region3",
    "zone"  :  "zone1",
    "min_num_replicas" : 1
  },
  {
    "cloud" :  "cloud1",
    "region":  "region3",
    "zone"  :  "zone2",
    "min_num_replicas" : 1
  },
  {
    "cloud" :  "cloud1",
    "region":  "region3",
    "zone"  :  "zone3",
    "min_num_replicas" : 1
  }
]
}');

-- Should succeed in creating colocated partitioned tables with tablespace
CREATE TABLE partitioned_table (name varchar, region varchar, PRIMARY KEY(name, region)) PARTITION BY LIST(region);
CREATE TABLE partition_in_dc1 PARTITION OF partitioned_table FOR VALUES IN ('region1') TABLESPACE tsp1;
CREATE TABLE partition_in_dc2 PARTITION OF partitioned_table FOR VALUES IN ('region2') TABLESPACE tsp2;
CREATE TABLE partition_in_dc3 PARTITION OF partitioned_table FOR VALUES IN ('region3') TABLESPACE tsp3;
-- 4 implicit tablegroups must be created
SELECT COUNT(*) FROM pg_yb_tablegroup;
-- 6 tablespaces(1 Default + 1 Global + 4 Defined) should have been created
SELECT COUNT(*) FROM pg_tablespace;
-- The created implicit tablegroups must use the specified tablespaces
SELECT y.spcname FROM pg_tablespace as y JOIN pg_yb_tablegroup as x ON x.grptablespace = y.oid;
DROP TABLE partitioned_table;
-- Implicit tablegroups must be deleted since it has no tables
SELECT COUNT(*) FROM pg_yb_tablegroup;

-- Users must be able to create tables in the implicit tablegroup as long as they have required permission.
-- Requires permission to create table in schema and to create a table in the tablespace
CREATE ROLE user1;
CREATE ROLE user2;
CREATE SCHEMA s1;
CREATE SCHEMA s2;
ALTER SCHEMA s1 OWNER TO user1;
ALTER SCHEMA s2 OWNER TO user2;
GRANT CREATE ON TABLESPACE tsp1 TO user1;
GRANT CREATE ON TABLESPACE tsp2 TO user2;
SELECT COUNT(*) FROM pg_yb_tablegroup;
SET SESSION ROLE user1;
CREATE TABLE s1.t1 (a INT) TABLESPACE tsp1;
CREATE TABLE s1.t2 (a INT) TABLESPACE tsp2;
SELECT COUNT(*) FROM pg_yb_tablegroup;
SET SESSION ROLE user2;
CREATE TABLE s2.t3 (a INT) TABLESPACE tsp2;
CREATE TABLE s2.t4 (a INT) TABLESPACE tsp1;
CREATE TABLE s1.t4 (a INT) TABLESPACE tsp2;
SELECT COUNT(*) FROM pg_yb_tablegroup;
SET SESSION ROLE user1;
DROP TABLE s1.t1;
-- Implicit tablegroup must be deleted since it has no tables
SELECT COUNT(*) FROM pg_yb_tablegroup;
SET SESSION ROLE user2;
DROP TABLE s2.t3;
-- Implicit tablegroup must be deleted since it has no tables
SELECT COUNT(*) FROM pg_yb_tablegroup;
RESET ROLE;

-- Must be able to create colocated materialized view with tablespace
CREATE TABLE base_table (a INT PRIMARY KEY);
INSERT INTO base_table SELECT * FROM generate_series(1, 100);
CREATE MATERIALIZED VIEW m TABLESPACE tsp1 AS SELECT * FROM base_table;
-- 2 implicit tablegroup must be created
SELECT COUNT(*) FROM pg_yb_tablegroup;
-- The implicit tablegroup must use tablespace tsp1
SELECT y.spcname FROM pg_tablespace as y JOIN pg_yb_tablegroup as x ON x.grptablespace = y.oid;
DROP MATERIALIZED VIEW m;
DROP TABLE base_table;
-- Implicit tablegroup must be deleted since it has no tables
SELECT COUNT(*) FROM pg_yb_tablegroup;

-- Test colocated indexes
CREATE TABLE table_1(a INT PRIMARY KEY, b INT, c INT) TABLESPACE tsp2;
-- Indexes should use default implicit tablegroup if no tablespace is specified
CREATE INDEX coloc_index1 on table_1(a);
CREATE INDEX coloc_index2 on table_1(b) TABLESPACE tsp1;
CREATE INDEX coloc_index3 on table_1(c) TABLESPACE tsp2;
SELECT COUNT(*) FROM pg_yb_tablegroup;
INSERT INTO table_1 VALUES (1,1,1), (2,2,2), (3,3,3), (4,3,4), (5,5,4);
SELECT * FROM table_1 WHERE a = 2 ORDER BY b;
SELECT * FROM table_1 WHERE b = 3 ORDER BY c;
SELECT * FROM table_1 WHERE c = 4 ORDER BY a;
EXPLAIN (COSTS OFF) SELECT * FROM table_1 WHERE a = 2;
EXPLAIN (COSTS OFF) SELECT * FROM table_1 WHERE b = 3;
EXPLAIN (COSTS OFF) SELECT * FROM table_1 WHERE c = 4;
-- Test if pg_depend entries of the indexes and the table are being updated.
WITH table_groups AS (
  SELECT oid, grpname FROM pg_yb_tablegroup
),
rel_to_grp AS (
  SELECT x.relname, y.grpname
  FROM pg_class x
  JOIN pg_depend z ON x.oid = z.objid
  JOIN table_groups y ON z.refobjid = y.oid
  WHERE x.relname IN ('table_1', 'coloc_index1', 'coloc_index2','coloc_index3')
)
SELECT string_agg(relname, ', ' ORDER BY relname ASC) AS relation_names
FROM rel_to_grp GROUP BY grpname ORDER BY relation_names ASC;
DROP INDEX coloc_index1;
DROP INDEX coloc_index2;
DROP INDEX coloc_index3;
DROP TABLE table_1;
-- Implicit tablegroup must be deleted since it has no tables
SELECT COUNT(*) FROM pg_yb_tablegroup;

-- Describing colocated table with tablespace must show both colocation and tablespace info
CREATE TABLE table_1 (a INT PRIMARY KEY) TABLESPACE tsp1;
\d+ table_1
DROP TABLE table_1;

-- Table Created without colocation should not be colocated
CREATE TABLE table_1 (a INT PRIMARY KEY) with (COLOCATION = false);
\d+ table_1
DROP TABLE table_1;

-- Implicit tablegroup must be deleted since it has no tables
SELECT COUNT(*) FROM pg_yb_tablegroup;

-- A tablespace should not be dropped if any colocated tables are dependent on it
CREATE TABLE t1 (a int) TABLESPACE tsp1;
CREATE TABLE t2 (a int) TABLESPACE tsp1;
DROP TABLESPACE tsp1;
DROP TABLE t1;
DROP TABLESPACE tsp1;
DROP TABLE t2;
DROP TABLESPACE tsp1;
DROP TABLESPACE tsp2;
DROP TABLESPACE tsp3;

CREATE TABLESPACE tsp1 WITH (replica_placement = '{
"num_replicas": 1,
"placement_blocks": [
  {
    "cloud" :  "cloud1",
    "region":  "region1",
    "zone": "zone1",
    "min_num_replicas" : 1
  }]}');

CREATE TABLESPACE tsp2 WITH (replica_placement = '{
"num_replicas": 1,
"placement_blocks": [
  {
    "cloud" :  "cloud1",
    "region":  "region1",
    "zone": "zone1",
    "min_num_replicas" : 1
  }]}');

CREATE TABLE t1 (a int) TABLESPACE tsp1;
CREATE TABLE t2 (a int) TABLESPACE tsp2;

-- Check that dependency entries from tablegroup->tablespace, table->tabelgroup are getting created properly
-- Here we perform joins on pg_class, pg_depends, pg_shdepend and pg_tablespace to find
--  1. Corresponding tablegroup of a colocated table
--  2. Corresponding tablespace of the tablegroup
SELECT s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
-- Relation name is t1
u.relname = 't1' AND
-- Find table -> tablegroup entry in pg_depends by matching table oid and refclassid of tablegroup (8036)
v.objid = u.oid AND
v.refclassid = 8036 AND
-- Find tablegroup -> tablespace entry in shdepends by matching tablegroup oid from pg_depend and refclassid of tablespace (1213)
x.objid = v.refobjid AND
x.refclassid = 1213 AND
-- Find the tablespace name by matching tablespace oid (found using pg_shdepend) with pg_tablespace.
s.oid = x.refobjid;

SELECT s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
-- Relation name is t2
u.relname = 't2' AND
-- Find table -> tablegroup entry in pg_depends by matching table oid and refclassid of tablegroup (8036)
v.objid = u.oid AND
v.refclassid = 8036 AND
-- Find tablegroup -> tablespace entry in shdepends by matching tablegroup oid from pg_depend and refclassid of tablespace (1213)
x.objid = v.refobjid AND
x.refclassid = 1213 AND
-- Find the tablespace name by matching tablespace oid (found using pg_shdepend) with pg_tablespace.
s.oid = x.refobjid;
\c yugabyte
DROP DATABASE colocation_test;
