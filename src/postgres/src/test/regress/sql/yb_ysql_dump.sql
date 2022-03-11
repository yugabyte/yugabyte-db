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
SET SESSION AUTHORIZATION regress_rls_alice;
CREATE TABLE uaccount (pguser      name, seclv       int, PRIMARY KEY(pguser ASC));
ALTER TABLE uaccount ENABLE ROW LEVEL SECURITY;

CREATE POLICY account_policies ON uaccount USING (pguser = current_user);

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
