--
-- Colocation
--

-- CREATE TABLE on non-colocated database

CREATE TABLE tab_colo (a INT) WITH (colocation = true);
CREATE TABLE tab_noco (a INT) WITH (colocation = false);
DROP TABLE tab_noco;

-- CREATE DATABASE colocated

CREATE DATABASE colocation_test colocation = true;
\c colocation_test
CREATE TABLE e (id int PRIMARY KEY, first_name TEXT) WITH (colocation = true) SPLIT INTO 10 TABLETS;

-- CREATE TABLE

CREATE TABLE tab_nonkey (a INT);
\d tab_nonkey
CREATE TABLE tab_key (a INT PRIMARY KEY);
\d tab_key
CREATE TABLE tab_range (a INT, PRIMARY KEY (a ASC));
CREATE TABLE tab_range_nonkey (a INT, b INT, PRIMARY KEY (a ASC));
-- opt out of using colocated tablet
CREATE TABLE tab_nonkey_noco (a INT) WITH (colocation = false);
-- colocated tables with no primary keys should not be hash partitioned
CREATE TABLE split_table ( a integer, b text ) SPLIT INTO 4 TABLETS;
-- multi column primary key table
CREATE TABLE tab_range_range (a INT, b INT, PRIMARY KEY (a, b DESC));
CREATE TABLE tab_range_colo (a INT, PRIMARY KEY (a ASC)) WITH (colocation = true);

INSERT INTO tab_range (a) VALUES (0), (1), (2);
INSERT INTO tab_range (a, b) VALUES (0, '0'); -- fail
INSERT INTO tab_range_nonkey (a, b) VALUES (0, '0'), (1, '1');
INSERT INTO tab_nonkey_noco (a) VALUES (0), (1), (2), (3);
INSERT INTO tab_range_range (a, b) VALUES (0, 0), (0, 1), (1, 0), (1, 1);
INSERT INTO tab_range_colo (a) VALUES (0), (1), (2), (3);

SELECT * FROM tab_range;
SELECT * FROM tab_range WHERE a = 2;
SELECT * FROM tab_range WHERE n = '0'; -- fail
SELECT * FROM tab_range_nonkey;
SELECT * FROM tab_nonkey_noco ORDER BY a ASC;
SELECT * FROM tab_range_range;
SELECT * FROM tab_range_colo;

BEGIN;
INSERT INTO tab_range_colo VALUES (4);
SELECT * FROM tab_range_colo;
ROLLBACK;
BEGIN;
INSERT INTO tab_range_colo VALUES (5);
COMMIT;
SELECT * FROM tab_range_colo;

INSERT INTO tab_range_colo VALUES (6), (6);

-- CREATE INDEX

-- table with index
CREATE TABLE tab_range_nonkey2 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX idx_range ON tab_range_nonkey2 (a);
\d tab_range_nonkey2
INSERT INTO tab_range_nonkey2 (a, b) VALUES (0, 0), (1, 1), (2, 2), (3, 3), (4, 4), (5, 5);
EXPLAIN (COSTS OFF) SELECT * FROM tab_range_nonkey2 WHERE a = 1;
SELECT * FROM tab_range_nonkey2 WHERE a = 1;
/*+IndexScan(tab_range_nonkey2 idx_range)*/EXPLAIN (COSTS OFF) SELECT * FROM tab_range_nonkey2 WHERE a <= 3;
/*+IndexScan(tab_range_nonkey2 idx_range)*/SELECT * FROM tab_range_nonkey2 WHERE a <= 3;
UPDATE tab_range_nonkey2 SET b = b + 1 WHERE a > 3;
SELECT * FROM tab_range_nonkey2;
DELETE FROM tab_range_nonkey2 WHERE a > 3;
SELECT * FROM tab_range_nonkey2;

-- colocated table with non-colocated index
CREATE TABLE tab_range_nonkey3 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX idx_range_colo ON tab_range_nonkey3 (a) WITH (colocation = true);

-- colocated table with colocated index
CREATE TABLE tab_range_nonkey4 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX idx_range_noco ON tab_range_nonkey4 (a) WITH (colocation = false);

-- non-colocated table with index
CREATE TABLE tab_range_nonkey_noco (a INT, b INT, PRIMARY KEY (a ASC)) WITH (colocation = false);
CREATE INDEX idx_range2 ON tab_range_nonkey_noco (a);
INSERT INTO tab_range_nonkey_noco (a, b) VALUES (0, 0), (1, 1), (2, 2), (3, 3), (4, 4), (5, 5);
EXPLAIN (COSTS OFF) SELECT * FROM tab_range_nonkey_noco WHERE a = 1;
SELECT * FROM tab_range_nonkey_noco WHERE a = 1;
EXPLAIN (COSTS OFF) SELECT * FROM tab_range_nonkey_noco WHERE a <= 3;
SELECT * FROM tab_range_nonkey_noco WHERE a <= 3;
UPDATE tab_range_nonkey_noco SET b = b + 1 WHERE a > 3;
SELECT * FROM tab_range_nonkey_noco;
DELETE FROM tab_range_nonkey_noco WHERE a > 3;
SELECT * FROM tab_range_nonkey_noco;

-- more tables and indexes
CREATE TABLE tab_range_nonkey_noco2 (a INT, b INT, PRIMARY KEY (a ASC)) WITH (colocation = false);
CREATE INDEX idx_range3 ON tab_range_nonkey_noco2 (a);
INSERT INTO tab_range_nonkey_noco2 (a, b) VALUES (0, 0);
CREATE TABLE tab_range_nonkey_noco3 (a INT, b INT, PRIMARY KEY (a ASC)) WITH (colocation = false);
CREATE INDEX idx_range4 ON tab_range_nonkey_noco3 (a);
CREATE TABLE tab_range_nonkey5 (a INT, b INT, PRIMARY KEY (a ASC));
CREATE INDEX idx_range5 ON tab_range_nonkey5 (a);
CREATE TABLE tbl (r1 INT, r2 INT, v1 INT, v2 INT,
PRIMARY KEY (r1, r2));
CREATE INDEX idx_hash1 on tbl (r1 HASH);
CREATE INDEX idx_hash2 on tbl ((r1, r2) HASH);
CREATE INDEX idx_hash3 on tbl (r1 HASH, r2 ASC);
CREATE UNIQUE INDEX unique_idx_hash1 on tbl (r1 HASH);
CREATE UNIQUE INDEX unique_idx_hash2 on tbl ((r1, r2) HASH);
CREATE UNIQUE INDEX unique_idx_hash3 on tbl (r1 HASH, r2 ASC);
\d tbl
-- Make sure nothing bad happens to UNIQUE constraints after disabling HASH columns
-- for colocated indexes
CREATE TABLE tbl2 (r1 int PRIMARY KEY, r2 int, v1 int, v2 int, UNIQUE(v1));
ALTER TABLE tbl2 ADD CONSTRAINT unique_v2_tbl2 UNIQUE(v2);
\d tbl2

DROP TABLE tbl, tbl2;

-- colocated table with unique index
CREATE TABLE tab_nonkey2 (a INT) WITH (colocation = true);
CREATE UNIQUE INDEX idx_range6 ON tab_nonkey2 (a);

\dt
\di

-- TRUNCATE TABLE

-- truncate colocated table with default index
TRUNCATE TABLE tab_range;
SELECT * FROM tab_range;
INSERT INTO tab_range VALUES (4);
SELECT * FROM tab_range;
INSERT INTO tab_range VALUES (1);
INSERT INTO tab_range VALUES (2), (5);
SELECT * FROM tab_range;
DELETE FROM tab_range WHERE a = 2;
TRUNCATE TABLE tab_range;
SELECT * FROM tab_range;
INSERT INTO tab_range VALUES (2);
SELECT * FROM tab_range;

TRUNCATE TABLE tab_range;

-- truncate non-colocated table without index
TRUNCATE TABLE tab_nonkey_noco;
SELECT * FROM tab_nonkey_noco;

-- truncate colocated table with explicit index
TRUNCATE TABLE tab_range_nonkey2;
SELECT * FROM tab_range_nonkey2;

-- truncate non-colocated table with explicit index
TRUNCATE TABLE tab_range_nonkey_noco2;
SELECT * FROM tab_range_nonkey_noco2;

-- insert and truncate colocated table with explicit index
INSERT INTO tab_nonkey2 VALUES (1), (2), (3);
EXPLAIN (COSTS OFF) SELECT a FROM tab_nonkey2 ORDER BY a;
SELECT a FROM tab_nonkey2 ORDER BY a;
TRUNCATE TABLE tab_nonkey2;
SELECT a FROM tab_nonkey2 ORDER BY a;
INSERT INTO tab_nonkey2 VALUES (2), (4);
SELECT a FROM tab_nonkey2 ORDER BY a;

\dt
\di

-- ALTER TABLE
INSERT INTO tab_range (a) VALUES (0), (1), (2);
INSERT INTO tab_range_nonkey2 (a, b) VALUES (0, 0), (1, 1);

SELECT * FROM tab_range;
SELECT * FROM tab_range_nonkey2;

-- Alter colocated tables
ALTER TABLE tab_range ADD COLUMN x INT;
ALTER TABLE tab_range_nonkey2 DROP COLUMN b;

SELECT * FROM tab_range;
SELECT * FROM tab_range_nonkey2;

ALTER TABLE tab_range_nonkey2 RENAME TO tab_range_nonkey2_renamed;
SELECT * FROM tab_range_nonkey2_renamed;
SELECT * FROM tab_range_nonkey2;

-- Alter colocated table ADD PRIMARY KEY
CREATE TABLE tbl_no_pk (k INT, v INT) WITH (colocation = true);
\d tbl_no_pk
INSERT INTO tbl_no_pk (k, v) VALUES (1, 1), (2, 2), (3, 3);
ALTER TABLE tbl_no_pk ADD PRIMARY KEY (k ASC);
\d tbl_no_pk
SELECT * FROM tbl_no_pk ORDER BY k;

-- Alter colocated table "DROP PRIMARY KEY"
ALTER TABLE tbl_no_pk DROP CONSTRAINT tbl_no_pk_pkey;
\d tbl_no_pk
SELECT * FROM tbl_no_pk ORDER BY k;
DROP TABLE tbl_no_pk;

-- DROP TABLE

-- drop colocated table with default index
DROP TABLE tab_range;
SELECT * FROM tab_range;

-- drop non-colocated table without index
DROP TABLE tab_nonkey_noco;
SELECT * FROM tab_nonkey_noco;

--- drop colocated table with explicit index
DROP TABLE tab_range_nonkey2_renamed;
SELECT * FROM tab_range_nonkey2_renamed;

-- drop non-colocated table with explicit index
DROP TABLE tab_range_nonkey_noco2;
SELECT * FROM tab_range_nonkey_noco2;

-- DROP INDEX

-- drop index on non-colocated table
DROP INDEX idx_range2;
EXPLAIN SELECT * FROM tab_range_nonkey_noco WHERE a = 1;

-- drop index on colocated table
DROP INDEX idx_range5;
EXPLAIN SELECT * FROM tab_range_nonkey5 WHERE a = 1;

\dt
\di

-- Test colocated tables/indexes with SPLIT INTO/SPLIT AT
CREATE TABLE invalid_tbl_split_into (k INT) SPLIT INTO 10 TABLETS;
CREATE TABLE invalid_tbl_split_at (k INT) SPLIT AT VALUES ((100));
CREATE TABLE test_tbl (k INT);
CREATE INDEX invalid_idx_split_into ON test_tbl (k) SPLIT INTO 10 TABLETS;
CREATE INDEX invalid_idx_split_at ON test_tbl (k) SPLIT AT VALUES ((100));
DROP TABLE test_tbl;

-- Test colocated partitioned table and partition tables
CREATE TABLE partitioned_table (
    k1 INT,
    v1 INT,
    v2 TEXT
)
PARTITION BY HASH (k1)
WITH (colocation_id='123456');
SELECT * FROM yb_table_properties('partitioned_table'::regclass::oid);

CREATE TABLE table_partition PARTITION OF partitioned_table
FOR VALUES WITH (modulus 2, remainder 0)
WITH (colocation_id='234567');
SELECT * FROM yb_table_properties('table_partition'::regclass::oid);

-- drop database
\c yugabyte
DROP DATABASE colocation_test;

-- Test syntax change as a result of Colocation GA change
-- Fail: only one of 'colocation' and 'colocated' options can be specified in CREATE DATABASE
CREATE DATABASE colocation_test colocated = true colocation = true;
-- Succeed with deprecated warning: create a colocated database using old syntax
CREATE DATABASE colocation_test colocated = true;
DROP DATABASE colocation_test;
-- Succeed: create a colocated database using new syntax
CREATE DATABASE colocation_test colocation = true;
\c colocation_test

-- Fail: only one of 'colocation' and 'colocated' options can be specified in CREATE TABLE
CREATE TABLE tbl_colocated_colocation (k INT, v INT)
WITH (colocation = true, colocated = true);
-- Succeed with deprecated warning: create a colocated table using old syntax
CREATE TABLE tbl_colocated (k INT, v INT) WITH (colocated = true);
-- Succeed: create a colocated table using new syntax
CREATE TABLE tbl_colocation (k INT, v INT) WITH (colocation = true);
-- Check colocated table footer
\d tbl_colocation
-- Create and describe a table opt out of colocation
CREATE TABLE tbl_no_colocation (k INT, v INT) WITH (colocation = false);
\d tbl_no_colocation

-- Drop database
\c yugabyte
DROP DATABASE colocation_test;

-- Test Colocation GA special characteristics different from legacy colocated database
CREATE DATABASE colocation_test colocation = true;
\c colocation_test

-- Lazily create the default implicit tablegroup
SELECT * FROM pg_yb_tablegroup;
CREATE TABLE tbl (k INT, v INT);
SELECT * FROM pg_yb_tablegroup;

-- Check for dependency between colocated table and default tablegroup
SELECT * FROM pg_depend, pg_yb_tablegroup WHERE classid = 'pg_class'::regclass
AND objid = 'tbl'::regclass AND refclassid = 'pg_yb_tablegroup'::regclass
AND refobjid = pg_yb_tablegroup.oid AND grpname = 'default';
DROP TABLEGROUP "default";

-- The default tablegroup cannot be dropped
DROP TABLEGROUP "default" CASCADE;
DROP TABLE tbl;
DROP TABLEGROUP "default";

-- Cannot set privileges of an implicit tablegroup
CREATE ROLE test_role;
GRANT CREATE ON TABLEGROUP "default" TO test_role;
REVOKE CREATE ON TABLEGROUP "default" FROM test_role;

-- Any user can create tables/indexes in an implicit tablegroup
SET SESSION AUTHORIZATION test_role;
CREATE TABLE test_role_table (k INT PRIMARY KEY, v TEXT);
SELECT * FROM pg_tables WHERE tablename = 'test_role_table';
CREATE UNIQUE INDEX unique_idx ON test_role_table(v);
SELECT rolname FROM pg_roles JOIN pg_class
ON pg_roles.oid = pg_class.relowner WHERE pg_class.relname = 'unique_idx';
RESET SESSION AUTHORIZATION;

-- The default tablegroup cannot be used explicitly
CREATE TABLE invalid_tbl (k int) TABLEGROUP "default";

-- Test distinct pushdown for colocated table
CREATE TABLE tbl_colo(r1 INT, r2 INT, r3 INT, r4 INT, r5 INT) WITH (colocation = TRUE);
CREATE INDEX ON tbl_colo(r1 asc, r3 asc, r5 asc);
INSERT INTO tbl_colo (SELECT 1, i, i, i, i FROM generate_series(1, 100) AS i);
INSERT INTO tbl_colo (SELECT 2, i, i, i, i FROM generate_series(1, 100) AS i);
INSERT INTO tbl_colo (SELECT 3, i, i, i, i FROM generate_series(1, 100) AS i);
EXPLAIN (COSTS OFF) SELECT DISTINCT r1 FROM tbl_colo WHERE r3 <= 1;
SELECT DISTINCT r1 FROM tbl_colo WHERE r3 <= 1;
/*+Set(enable_hashagg false)*/ EXPLAIN (COSTS OFF) SELECT DISTINCT r1 FROM tbl_colo WHERE r3 <= 1;
/*+Set(enable_hashagg false)*/ SELECT DISTINCT r1 FROM tbl_colo WHERE r3 <= 1;

-- Drop database
\c yugabyte
DROP DATABASE colocation_test;

-- Test Colocated Materialized View describe
CREATE DATABASE colocation_test WITH colocation = true;
\c colocation_test
CREATE TABLE t1 (a INT PRIMARY KEY);
CREATE MATERIALIZED VIEW m1 AS SELECT * FROM t1;
CREATE MATERIALIZED VIEW m2 with (colocation = true) AS SELECT * FROM t1;
CREATE MATERIALIZED VIEW m3 with (colocation = false) AS SELECT * FROM t1;

\d m1
\d m2
\d m3

\c yugabyte
DROP DATABASE colocation_test;

-- Test Colocated Materialized View 
CREATE DATABASE colocation_test WITH colocation = true;
\c colocation_test
CREATE TABLE t1 (a INT PRIMARY KEY) WITH (colocation = true);
CREATE TABLE t2 (b INT PRIMARY KEY) WITH (colocation = false);
CREATE MATERIALIZED VIEW m0 WITH (colocation = true) as SELECT * FROM t1;
CREATE MATERIALIZED VIEW m1 WITH (colocation = false) as SELECT * FROM t1;
CREATE MATERIALIZED VIEW m2 WITH (colocation = true) as SELECT * FROM t2;
CREATE MATERIALIZED VIEW m3 WITH (colocation = false) as SELECT * FROM t2;
CREATE MATERIALIZED VIEW m4 WITH (colocation = true) as SELECT * FROM t1, t2;
CREATE MATERIALIZED VIEW m5 WITH (colocation = false) as SELECT * FROM t1, t2;
select is_colocated from yb_table_properties('m0'::regclass);
select is_colocated from yb_table_properties('m1'::regclass);
select is_colocated from yb_table_properties('m2'::regclass);
select is_colocated from yb_table_properties('m3'::regclass);
select is_colocated from yb_table_properties('m4'::regclass);
select is_colocated from yb_table_properties('m5'::regclass);
