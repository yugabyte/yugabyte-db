--
-- SCHEMA
--

-- Create 2 schemas with table of the same name in each.
CREATE SCHEMA S1;
CREATE SCHEMA S2;

CREATE TABLE S1.TBL (a1 int PRIMARY KEY);
CREATE TABLE S2.TBL (a2 text PRIMARY KEY);

-- Insert values into the tables and verify both can be queried.
INSERT INTO S1.TBL VALUES (1);
INSERT INTO S2.TBL VALUES ('a');

SELECT * FROM S1.TBL;
SELECT * FROM S2.TBL;

-- Drop one table and verify the other still exists.
DROP TABLE S1.TBL;
SELECT * FROM S2.TBL;

DROP TABLE S2.TBL;

-- Test ALTER TABLE [IF EXISTS].. SET SCHEMA..
CREATE TABLE S1.TBL2 (a1 int PRIMARY KEY, a2 int);
CREATE INDEX IDX2 ON S1.TBL2(a2);

INSERT INTO S1.TBL2 VALUES (2, 2);
SELECT * FROM S1.TBL2;
\d S1.TBL2;

ALTER TABLE S1.TBL2 SET SCHEMA S2;
SELECT * FROM S1.TBL2;
SELECT * FROM S2.TBL2;
\d S2.TBL2;
\d S2.IDX2;

DROP TABLE S2.TBL2;
SELECT * FROM S2.TBL2;

ALTER TABLE S2.TBL2 SET SCHEMA S1;            -- the table was deleted
ALTER TABLE IF EXISTS S2.TBL2 SET SCHEMA S1;  -- OK

-- for partitioned table
CREATE TABLE S1.P_TBL (k INT PRIMARY KEY, value TEXT)  PARTITION BY RANGE(k);
CREATE TABLE S1.P_TBL_1 PARTITION OF S1.P_TBL FOR VALUES FROM (1) TO (3);
CREATE TABLE S1.P_TBL_DEFAULT PARTITION OF S1.P_TBL DEFAULT;
CREATE INDEX P_TBL_K_IDX on S1.P_TBL(k);

ALTER TABLE S1.P_TBL SET SCHEMA S2;
\d+ S2.P_TBL

DROP TABLE S2.P_TBL;

-- for temp table
CREATE TEMP TABLE TMP_TBL (a INT PRIMARY KEY);

ALTER TABLE TMP_TBL SET SCHEMA S2;

DROP TABLE TMP_TBL;

-- verify yb_db_admin role can manage schemas like a superuser
CREATE SCHEMA test_ns_schema_other;
CREATE ROLE test_regress_user1;
SET SESSION AUTHORIZATION yb_db_admin;
ALTER SCHEMA test_ns_schema_other RENAME TO test_ns_schema_other_new;
ALTER SCHEMA test_ns_schema_other_new OWNER TO test_regress_user1;
DROP SCHEMA test_ns_schema_other_new;
-- verify that the objects were dropped
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_other_new');
CREATE SCHEMA test_ns_schema_yb_db_admin;
ALTER SCHEMA test_ns_schema_yb_db_admin RENAME TO test_ns_schema_yb_db_admin_new;
ALTER SCHEMA test_ns_schema_yb_db_admin_new OWNER TO test_regress_user1;
DROP SCHEMA test_ns_schema_yb_db_admin_new;
-- verify that the objects were dropped
SELECT COUNT(*) FROM pg_class WHERE relnamespace =
    (SELECT oid FROM pg_namespace WHERE nspname = 'test_ns_schema_yb_db_admin_new');
