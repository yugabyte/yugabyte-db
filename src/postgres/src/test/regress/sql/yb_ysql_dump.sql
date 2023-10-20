CREATE TABLESPACE tsp1 LOCATION '/data';

CREATE TABLE tbl1 (a SERIAL, b INT);
INSERT INTO tbl1 (b) VALUES (100);

CREATE TABLE tbl2 (a SERIAL);

CREATE TABLE tbl3(a INT, b INT, PRIMARY KEY(a ASC));

CREATE TABLE tbl4 (a INT, b INT, PRIMARY KEY(a HASH, b));

CREATE TABLE tbl5 (a INT PRIMARY KEY, b INT, c INT);
INSERT INTO tbl5 (a, b, c) VALUES (4, 7, 16);

CREATE TABLE tbl6 (a INT, PRIMARY KEY(a HASH));

CREATE TABLE tbl7 (a INT, b INT, c INT, d INT, PRIMARY KEY(b HASH, c));

CREATE TABLE tbl8 (a INT, b INT, c INT, d INT, PRIMARY KEY(a HASH, d));
CREATE INDEX tbl8_idx ON tbl8 ((b,c) HASH);
CREATE INDEX tbl8_idx2 ON tbl8 (a HASH, b);
CREATE INDEX tbl8_idx3 ON tbl8 (b ASC);
CREATE INDEX tbl8_idx4 ON tbl8 (b DESC);
CREATE INDEX tbl8_idx5 ON tbl8 (c);

CREATE TABLE tbl9 (a INT, b INT, c INT, PRIMARY KEY((a,b) HASH));

CREATE TABLE tbl10 (a INT, b INT, c INT, d INT, PRIMARY KEY((a,c) HASH, b));

CREATE TABLE tbl11 (a INT, b INT, c INT, PRIMARY KEY(a DESC, b ASC));

CREATE TABLE tbl12 (a INT, b INT, c INT, d INT, PRIMARY KEY(a ASC, d DESC, c DESC));

CREATE TABLE tbl13 (a INT, b INT, c INT, d INT, PRIMARY KEY((b,c) HASH));

CREATE USER tablegroup_test_user SUPERUSER;
CREATE USER rls_user NOLOGIN;

CREATE TABLE rls_public(k INT PRIMARY KEY, v TEXT);
CREATE TABLE rls_private(k INT PRIMARY KEY, v TEXT);

GRANT ALL ON rls_public TO public;
GRANT SELECT ON rls_private TO rls_user;

ALTER TABLE rls_public ENABLE ROW LEVEL SECURITY;
ALTER TABLE rls_private ENABLE ROW LEVEL SECURITY;
ALTER TABLE rls_private FORCE ROW LEVEL SECURITY;

CREATE POLICY p1 ON rls_public FOR ALL TO PUBLIC USING (k % 2 = 0);
CREATE POLICY p2 ON rls_private FOR INSERT WITH CHECK (k % 2 = 1);
CREATE POLICY p3 ON rls_private FOR UPDATE USING (k % 2 = 1);

CREATE TABLE chat_user("chatID" text NOT NULL, PRIMARY KEY("chatID"));

DROP USER IF EXISTS regress_rls_alice;
CREATE USER regress_rls_alice NOLOGIN;
GRANT CREATE ON SCHEMA public TO regress_rls_alice;
SET SESSION AUTHORIZATION regress_rls_alice;
CREATE TABLE uaccount (pguser      name, seclv       int, PRIMARY KEY(pguser ASC));
ALTER TABLE uaccount ENABLE ROW LEVEL SECURITY;

CREATE POLICY account_policies ON uaccount USING (pguser = current_user);

SET SESSION AUTHORIZATION tablegroup_test_user;

CREATE TABLEGROUP grp1;
CREATE TABLEGROUP grp2;
CREATE TABLEGROUP grp_with_spc TABLESPACE tsp1;

CREATE TABLE tgroup_no_options_and_tgroup (a INT) TABLEGROUP grp1;
CREATE TABLE tgroup_one_option (a INT) WITH (autovacuum_enabled = true);
CREATE TABLE tgroup_one_option_and_tgroup (a INT) WITH (autovacuum_enabled = true) TABLEGROUP grp2;
CREATE TABLE tgroup_options (a INT) WITH (autovacuum_enabled=true, parallel_workers=2);
CREATE TABLE tgroup_options_and_tgroup (a INT) WITH (autovacuum_enabled=true, parallel_workers=2) TABLEGROUP grp2;
CREATE TABLE tgroup_options_tgroup_and_custom_colocation_id (a INT) WITH (autovacuum_enabled=true, colocation_id=100500, parallel_workers=2) TABLEGROUP grp2;
CREATE TABLE tgroup_after_options (a INT) TABLEGROUP grp1;
CREATE TABLE tgroup_in_between_options (a INT) WITH (autovacuum_enabled = true) TABLEGROUP grp1;
CREATE TABLE tgroup_empty_options (a INT);
CREATE TABLE tgroup_with_spc (a INT) TABLEGROUP grp_with_spc;
BEGIN;
    SET LOCAL yb_non_ddl_txn_for_sys_tables_allowed TO true;
    UPDATE pg_class SET reloptions = ARRAY[]::TEXT[] WHERE relname = 'tgroup_empty_options';
    UPDATE pg_class SET reloptions = array_prepend('parallel_workers=2', reloptions) WHERE relname = 'tgroup_after_options';
    UPDATE pg_class SET reloptions = array_prepend('parallel_workers=2', reloptions) WHERE relname = 'tgroup_in_between_options';
COMMIT;

RESET SESSION AUTHORIZATION;

------------------------------------------------
-- Test table and index explicit splitting.
------------------------------------------------

------------------------------------
-- Tables

-- Table without a primary key
CREATE TABLE th1 (a int, b text, c float) SPLIT INTO 2 TABLETS;

-- Hash-partitioned table with range components
CREATE TABLE th2 (a int, b text, c float, PRIMARY KEY (a HASH, b ASC)) SPLIT INTO 3 TABLETS;

-- Hash-partitioned table without range components
CREATE TABLE th3 (a int, b text, c float, PRIMARY KEY ((a, b) HASH)) SPLIT INTO 4 TABLETS;

-- Range-partitioned table with single-column key
CREATE TABLE tr1 (a int, b text, c float, PRIMARY KEY (a ASC)) SPLIT AT VALUES ((1), (100));

-- Range-partitioned table with multi-column key
CREATE TABLE tr2 (a int, b text, c float, PRIMARY KEY (a DESC, b ASC, c DESC)) SPLIT AT VALUES ((100, 'a', 2.5), (50, 'n'), (1, 'z', -5.12));

-- Range-partitoned table with a long CREATE TABLE statement
CREATE TABLE pre_split_range (
    id int NOT NULL,
    customer_id int NOT NULL,
    company_name character varying(40) NOT NULL,
    contact_name character varying(30),
    contact_title character varying(30),
    address character varying(60),
    city character varying(15),
    region character varying(15),
    postal_code character varying(10),
    country character varying(15),
    phone character varying(24),
    fax character varying(24),
    more_col1 text,
    more_col2 text,
    more_col3 text,
    PRIMARY KEY (customer_id ASC)
) SPLIT AT VALUES ((1000), (5000), (10000), (15000), (20000), (25000), (30000), (35000), (55000), (85000), (110000), (150000), (250000), (300000), (350000), (400000), (450000), (500000), (1000000));

------------------------------------
-- Indexes

-- Hash-partitioned table with range components
CREATE INDEX on th2(c HASH, b DESC) SPLIT INTO 4 TABLETS;

-- Hash-partitioned table without range components
CREATE INDEX on th3((c, b) HASH) SPLIT INTO 3 TABLETS;

-- Range-partitioned table with single-column key
CREATE INDEX ON tr2(c DESC) SPLIT AT VALUES ((100.5), (1.5));

-- Range-partitioned table with multi-column key
CREATE INDEX ON tr2(c ASC, b DESC, a ASC) SPLIT AT VALUES ((-5.12, 'z', 1), (-0.75, 'l'), (2.5, 'a', 100));

------------------------------------
-- Extensions
CREATE EXTENSION pg_hint_plan;

------------------------------------------------
-- Test alter with add constraint using unique index.
------------------------------------------------

-- Setting range-partitioned unique index with SPLIT AT clause
CREATE TABLE p1 (k INT PRIMARY KEY, v TEXT);

CREATE UNIQUE INDEX c1 ON p1 (v ASC) SPLIT AT VALUES (('foo'), ('qux'));

ALTER TABLE p1 ADD UNIQUE USING INDEX c1;

-- Setting hash partitioned unique index with SPLIT INTO clause
CREATE TABLE p2 (k INT PRIMARY KEY, v TEXT);

CREATE UNIQUE INDEX c2 ON p2 (v) SPLIT INTO 10 TABLETS;

ALTER TABLE p2 ADD UNIQUE USING INDEX c2;

------------------------------------------------
-- Test included columns with primary key index and included columns with non-primary key index
------------------------------------------------
CREATE TABLE range_tbl_pk_with_include_clause (
  k2 TEXT,
  v DOUBLE PRECISION,
  k1 INT,
  PRIMARY KEY (k1 ASC, k2 ASC) INCLUDE (v)
) SPLIT AT VALUES((1, '1'), (100, '100'));

CREATE UNIQUE INDEX unique_idx_with_include_clause ON range_tbl_pk_with_include_clause (k1, k2) INCLUDE (v);

CREATE TABLE hash_tbl_pk_with_include_clause (
  k2 TEXT,
  v DOUBLE PRECISION,
  k1 INT,
  PRIMARY KEY ((k1, k2) HASH) INCLUDE (v)
) SPLIT INTO 8 TABLETS;

CREATE UNIQUE INDEX non_unique_idx_with_include_clause ON hash_tbl_pk_with_include_clause (k1, k2) INCLUDE (v);

CREATE TABLE range_tbl_pk_with_multiple_included_columns (
  col1 INT,
  col2 INT,
  col3 INT,
  col4 INT,
  PRIMARY KEY (col1 ASC, col2 ASC) INCLUDE (col3, col4)
);

CREATE TABLE hash_tbl_pk_with_multiple_included_columns (
  col1 INT,
  col2 INT,
  col3 INT,
  col4 INT,
  PRIMARY KEY (col1 HASH, col2 ASC) INCLUDE (col3, col4)
);
