CREATE DATABASE test_tablegroups_colocation_id;
\c test_tablegroups_colocation_id

CREATE VIEW table_props AS
  SELECT c.relname, props.is_colocated, tg.grpname AS tablegroup, props.colocation_id
  FROM pg_class c, yb_table_properties(c.oid) props
  LEFT JOIN pg_yb_tablegroup tg ON tg.oid = props.tablegroup_oid
  WHERE c.relname LIKE 'tg%'
  ORDER BY c.relname;

CREATE TABLEGROUP tg1;
CREATE TABLEGROUP tg2;

--
-- There should be no collision between:
-- (a) different colocation IDs within the same tablegroup,
-- (b) same colocation IDs across different tablegroups.
--

CREATE TABLE tg1_table1(v1 int PRIMARY KEY)                  WITH (colocation_id=20001)  TABLEGROUP tg1;
CREATE TABLE tg1_table2(v1 int PRIMARY KEY, v2 text)         WITH (colocation_id=20002)  TABLEGROUP tg1;
CREATE TABLE tg1_table3(v1 int PRIMARY KEY, v2 int, v3 text) WITH (colocation_id=100500) TABLEGROUP tg1;

CREATE TABLE tg2_table1(v1 text PRIMARY KEY)                 WITH (colocation_id=20001)  TABLEGROUP tg2;
CREATE TABLE tg2_table2(v1 text PRIMARY KEY, v2 int)         WITH (colocation_id=20002)  TABLEGROUP tg2;

INSERT INTO tg1_table1 VALUES (110),               (111),               (112);
INSERT INTO tg1_table2 VALUES (120, '120t'),       (121, '121t'),       (122, '122t');
INSERT INTO tg1_table3 VALUES (130, 1300, '130t'), (131, 1310, '131t'), (132, 1320, '132t');

INSERT INTO tg2_table1 VALUES ('210t'),            ('211t'),            ('212t');
INSERT INTO tg2_table2 VALUES ('220t', 120),       ('221t', 121),       ('222t', 122);

\d tg1_table3
\d tg2_table1
SELECT * FROM table_props;

SELECT * FROM tg1_table1;
SELECT * FROM tg1_table2;
SELECT * FROM tg1_table3;
SELECT * FROM tg2_table1;
SELECT * FROM tg2_table2;

DROP TABLE tg1_table1, tg1_table2, tg1_table3, tg2_table1, tg2_table2;

--
-- Various invalid cases.
--

CREATE TABLE tg1_table_valid(v1 int PRIMARY KEY) WITH (colocation_id=20001) TABLEGROUP tg1; -- Success
CREATE TABLE tg1_table_invalid(v1 int PRIMARY KEY) WITH (colocation_id=20001) TABLEGROUP tg1;
CREATE TABLE tg1_table_invalid(v1 int UNIQUE WITH (colocation_id=40002)) WITH (colocation_id=40002) TABLEGROUP tg1;

SELECT * FROM table_props;

DROP TABLE IF EXISTS tg1_table_valid, tg1_table_invalid;

--
-- Test colocated tables with indexes.
--

CREATE TABLE tg1_table1(
  v1 int,
  v2 int UNIQUE WITH (colocation_id=20002),
  v3 text
) WITH (colocation_id=20001) TABLEGROUP tg1;

INSERT INTO tg1_table1 VALUES (1, 1, 'v1'),  (1, 1, 'v1'); -- Fail
INSERT INTO tg1_table1 VALUES (1, 1, 'v2'),  (1, 2, 'v2'), (3, 3, 'v2'), (4, 4, 'v2');

CREATE INDEX tg1_table1_v1_idx ON tg1_table1 (v1) WITH (colocation_id=20003);

\d tg1_table1
SELECT * FROM table_props;

-- Make sure we're using index only scans and index scans respectively.
EXPLAIN (COSTS OFF) SELECT v1 FROM tg1_table1 WHERE v1 IN (1, 2, 4);
EXPLAIN (COSTS OFF) SELECT v2 FROM tg1_table1 WHERE v2 IN (1, 2, 4);
EXPLAIN (COSTS OFF) SELECT *  FROM tg1_table1 WHERE v1 IN (1, 2, 4) ORDER BY v1, v2, v3;
EXPLAIN (COSTS OFF) SELECT *  FROM tg1_table1 WHERE v2 IN (1, 2, 4) ORDER BY v1, v2, v3;

SELECT v1 FROM tg1_table1 WHERE v1 IN (1, 2, 4);
SELECT v2 FROM tg1_table1 WHERE v2 IN (1, 2, 4);
SELECT *  FROM tg1_table1 WHERE v1 IN (1, 2, 4) ORDER BY v1, v2, v3;
SELECT *  FROM tg1_table1 WHERE v2 IN (1, 2, 4) ORDER BY v1, v2, v3;

DELETE FROM tg1_table1 WHERE v1 = 1;
DELETE FROM tg1_table1 WHERE v2 = 3;

SELECT v1 FROM tg1_table1 WHERE v1 IN (1, 2, 4);
SELECT v2 FROM tg1_table1 WHERE v2 IN (1, 2, 4);
SELECT *  FROM tg1_table1 WHERE v1 IN (1, 2, 4) ORDER BY v1, v2, v3;
SELECT *  FROM tg1_table1 WHERE v2 IN (1, 2, 4) ORDER BY v1, v2, v3;

TRUNCATE TABLE tg1_table1;

SELECT v1 FROM tg1_table1 WHERE v1 IN (1, 2, 4);
SELECT v2 FROM tg1_table1 WHERE v2 IN (1, 2, 4);
SELECT *  FROM tg1_table1 WHERE v1 IN (1, 2, 4) ORDER BY v1, v2, v3;
SELECT *  FROM tg1_table1 WHERE v2 IN (1, 2, 4) ORDER BY v1, v2, v3;

INSERT INTO tg1_table1 VALUES (1, 1, 'v3'),  (444, 4, 'v3');

SELECT v1 FROM tg1_table1 WHERE v1 IN (1, 2, 4);
SELECT v2 FROM tg1_table1 WHERE v2 IN (1, 2, 4);
SELECT *  FROM tg1_table1 WHERE v1 IN (1, 2, 4) ORDER BY v1, v2, v3;
SELECT *  FROM tg1_table1 WHERE v2 IN (1, 2, 4) ORDER BY v1, v2, v3;

DROP TABLE tg1_table1;

--
-- Cleanup
--

\c yugabyte
DROP DATABASE test_tablegroups_colocation_id;
SELECT count(*) FROM pg_database WHERE datname = 'test_tablegroups_colocation_id';
