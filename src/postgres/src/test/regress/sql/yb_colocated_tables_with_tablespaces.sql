-- Colocated Tests with Tablespaces
CREATE TABLESPACE x WITH (replica_placement='{"num_replicas" : 1, "placement_blocks" : [{"cloud" : "cloud1" , "region" : "region1", "zone" : "zone1", "min_num_replicas" : 1}]}');
CREATE DATABASE colocation_test colocation = true;
\c colocation_test

CREATE TABLESPACE tsp1 WITH (replica_placement = '{"num_replicas": 3, "placement_blocks" : [{"cloud" : "cloud1", "region" : "region1", "zone" : "zone1", "min_num_replicas" : 1}, {"cloud" :  "cloud1", "region": "region1", "zone" : "zone2", "min_num_replicas" : 1}, {"cloud" : "cloud1", "region": "region1", "zone" : "zone3", "min_num_replicas" : 1}]}');

CREATE TABLESPACE tsp2 WITH (replica_placement = '{"num_replicas": 3, "placement_blocks" : [{"cloud" : "cloud1", "region" : "region2", "zone" : "zone1", "min_num_replicas" : 1}, {"cloud" :  "cloud1", "region": "region2", "zone" : "zone2", "min_num_replicas" : 1}, {"cloud" : "cloud1", "region": "region2", "zone" : "zone3", "min_num_replicas" : 1}]}');

CREATE TABLESPACE tsp3 WITH (replica_placement = '{"num_replicas": 3, "placement_blocks" : [{"cloud" : "cloud1", "region" : "region3", "zone" : "zone1", "min_num_replicas" : 1}, {"cloud" :  "cloud1", "region": "region3", "zone" : "zone2", "min_num_replicas" : 1}, {"cloud" : "cloud1", "region": "region3", "zone" : "zone3", "min_num_replicas" : 1}]}');

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
SELECT string_agg(c.relname, ', ' ORDER BY c.relname) AS relation_names
FROM pg_class c
JOIN pg_depend d ON c.oid = d.objid
JOIN pg_yb_tablegroup tg ON d.refobjid = tg.oid
WHERE c.relname IN ('table_1', 'coloc_index1', 'coloc_index2', 'coloc_index3')
GROUP BY tg.grpname
ORDER BY relation_names;

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

-- Test ALTER TABLE/INDEX/MATERIALIZED VIEW ALL IN TABLESPACE ... SET TABLESPACE ... CASCADE
-- Since all three variations behave similarly we expect similar output
CREATE TABLE table1(a INT PRIMARY KEY, b INT, c INT) TABLESPACE tsp1;
CREATE TABLE table2(a INT PRIMARY KEY, b INT, c INT) WITH (colocation=false) TABLESPACE tsp1;
CREATE INDEX index1 on table1(b) TABLESPACE tsp1;
CREATE MATERIALIZED VIEW col_mv TABLESPACE tsp1 AS SELECT table1.a,table2.c FROM table1,table2  WHERE table1.c = table2.c;
CREATE INDEX index2 on table1(c) TABLESPACE tsp2;
CREATE MATERIALIZED VIEW m TABLESPACE tsp2 AS SELECT * FROM table1;
CREATE TABLE table3(a int PRIMARY KEY, b int, c int) TABLESPACE tsp2;

-- Since we are moving colocated relations, this ALTER should fail
ALTER TABLE ALL IN TABLESPACE tsp1 SET TABLESPACE tsp3;

-- Since there are existing relations present in tsp2, this ALTER should fail
ALTER TABLE ALL IN TABLESPACE tsp1 SET TABLESPACE tsp2 CASCADE;

-- View all relations present in pg_class in tablespace tsp1, tsp2 and tsp3
SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

-- There are no relations present in tsp3, this would succeed and move all relations to tsp3.
ALTER TABLE ALL IN TABLESPACE tsp1 SET TABLESPACE tsp3 CASCADE;

--Verify if the tablespaces for the queries have been updated.
SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

ALTER INDEX ALL IN TABLESPACE tsp2 SET TABLESPACE tsp1 CASCADE;

SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

ALTER MATERIALIZED VIEW ALL IN TABLESPACE tsp3 SET TABLESPACE tsp2 CASCADE;

SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

-- Verify that dependency entries from tablegroup->tablespace, table->tablegroup are getting updated properly, only entries for colocated relations are added in pg_shdepend
SELECT u.relname, s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
v.objid = u.oid AND
v.refclassid = 'pg_yb_tablegroup'::regclass::oid AND
x.objid = v.refobjid AND
x.refclassid = 'pg_tablespace'::regclass::oid AND
s.oid = x.refobjid
ORDER BY u.relname;

DROP MATERIALIZED VIEW m;
DROP MATERIALIZED VIEW col_mv;
DROP INDEX index1;
DROP INDEX index2;
DROP TABLE table1;
DROP TABLE table2;
DROP TABLE table3;

-- Test ALTER TABLE ALL IN TABLESPACE ... COLLOCATED WITH ... SET TABLESPACE ... CASCADE
CREATE TABLE table1(a INT PRIMARY KEY, b INT, c INT) TABLESPACE tsp1;
CREATE TABLE table2(a INT PRIMARY KEY, b INT, c INT) WITH (colocation=false) TABLESPACE tsp1;
CREATE INDEX index1 on table1(b) TABLESPACE tsp1;
CREATE INDEX index2 on table1(c) TABLESPACE tsp1;
CREATE MATERIALIZED VIEW col_mv TABLESPACE tsp1 AS SELECT table1.a,table2.c FROM table1,table2  WHERE table1.c = table2.c;
CREATE MATERIALIZED VIEW m TABLESPACE tsp2 AS SELECT * FROM table1;
CREATE TABLE table3(a int PRIMARY KEY, b int, c int) TABLESPACE tsp2;

-- Since there are existing relations present in tsp2, this ALTER should fail
ALTER TABLE ALL IN TABLESPACE tsp1 COLOCATED WITH table1 SET TABLESPACE tsp2 CASCADE;

-- View all relations present in pg_class in tablespace tsp1, tsp2 and tsp3
SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

-- There are no relations present in tsp3, this would succeed and move relations colocated with t1 to tsp3.
ALTER TABLE ALL IN TABLESPACE tsp1 COLOCATED WITH table1 SET TABLESPACE tsp3 CASCADE;

--Verify if the tablespaces for the queries have been updated.
SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

-- Verify that dependency entries are updated accordingly in pg_shdepend
SELECT u.relname, s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
v.objid = u.oid AND
v.refclassid = 'pg_yb_tablegroup'::regclass::oid AND
x.objid = v.refobjid AND
x.refclassid = 'pg_tablespace'::regclass::oid AND
s.oid = x.refobjid
ORDER BY u.relname;

DROP MATERIALIZED VIEW m;
DROP MATERIALIZED VIEW col_mv;
DROP INDEX index1;
DROP INDEX index2;
DROP TABLE table1;
DROP TABLE table2;
DROP TABLE table3;

-- Test non-colocated Tables in a colocated database
CREATE TABLE table1(a INT PRIMARY KEY, b INT, c INT) WITH (colocation=false) TABLESPACE tsp1;
CREATE TABLE table2(a INT PRIMARY KEY, b INT, c INT) WITH (colocation=false) TABLESPACE tsp2;
CREATE TABLE table3(a INT PRIMARY KEY, b INT, c INT) TABLESPACE tsp3;

ALTER TABLE ALL IN TABLESPACE tsp1 SET TABLESPACE tsp2;
--Verify if the tablespaces for the queries have been updated.
SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

ALTER TABLE ALL in TABLESPACE tsp2 SET TABLESPACE tsp3;
--Verify if the tablespaces for the queries have been updated.
SELECT c.relname, ts.spcname FROM
    pg_class c
    JOIN pg_tablespace ts ON c.reltablespace = ts.oid
WHERE
    ts.spcname IN ('tsp1', 'tsp2', 'tsp3')
ORDER BY
    c.relname;

-- Verify that dependency entries are updated accordingly in pg_shdepend, non-colocated relations are not added in the pg_shdepend table
SELECT u.relname, s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
v.objid = u.oid AND
v.refclassid = 'pg_yb_tablegroup'::regclass::oid AND
x.objid = v.refobjid AND
x.refclassid = 'pg_tablespace'::regclass::oid AND
s.oid = x.refobjid
ORDER BY u.relname;

-- This command should fail
ALTER TABLE ALL IN TABLESPACE tsp2 COLOCATED WITH table1 SET TABLESPACE tsp3 CASCADE;

DROP TABLE table1;
DROP TABLE table2;
DROP TABLE table3;

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
    "cloud" : "cloud1",
    "region" : "region1",
    "zone" : "zone1",
    "min_num_replicas" : 1
  }]}');

CREATE TABLESPACE tsp2 WITH (replica_placement = '{
"num_replicas": 1,
"placement_blocks": [
  {
    "cloud" : "cloud1",
    "region" : "region1",
    "zone" : "zone1",
    "min_num_replicas" : 1
  }]}');

CREATE TABLE t1 (a int) TABLESPACE tsp1;
CREATE TABLE t2 (a int) TABLESPACE tsp2;

-- Check that dependency entries from tablegroup->tablespace, table->tablegroup are getting created properly
-- Here we perform joins on pg_class, pg_depends, pg_shdepend and pg_tablespace to find
--  1. Corresponding tablegroup of a colocated table
--  2. Corresponding tablespace of the tablegroup
SELECT s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
-- Relation name is t1
u.relname = 't1' AND
-- Find table -> tablegroup entry in pg_depends by matching table oid and refclassid of tablegroup
v.objid = u.oid AND
v.refclassid = 'pg_yb_tablegroup'::regclass::oid AND
-- Find tablegroup -> tablespace entry in shdepends by matching tablegroup oid from pg_depend and refclassid of tablespace
x.objid = v.refobjid AND
x.refclassid = 'pg_tablespace'::regclass::oid AND
-- Find the tablespace name by matching tablespace oid (found using pg_shdepend) with pg_tablespace.
s.oid = x.refobjid;

SELECT s.spcname FROM pg_class AS u, pg_depend AS v, pg_shdepend AS x, pg_tablespace AS s WHERE
-- Relation name is t2
u.relname = 't2' AND
-- Find table -> tablegroup entry in pg_depends by matching table oid and refclassid of tablegroup
v.objid = u.oid AND
v.refclassid = 'pg_yb_tablegroup'::regclass::oid AND
-- Find tablegroup -> tablespace entry in shdepends by matching tablegroup oid from pg_depend and refclassid of tablespace
x.objid = v.refobjid AND
x.refclassid = 'pg_tablespace'::regclass::oid AND
-- Find the tablespace name by matching tablespace oid (found using pg_shdepend) with pg_tablespace.
s.oid = x.refobjid;
\c yugabyte
DROP DATABASE colocation_test;
