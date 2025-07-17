\set ON_ERROR_STOP on

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
GRANT CREATE ON SCHEMA public TO regress_rls_alice;
SET SESSION AUTHORIZATION regress_rls_alice;
CREATE TABLE uaccount (pguser      name, seclv       int, PRIMARY KEY(pguser ASC));
ALTER TABLE uaccount ENABLE ROW LEVEL SECURITY;

CREATE POLICY account_policies ON uaccount USING (pguser = current_user);


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

------------------------------------------------
-- Test unique constraint on partitioned tables.
------------------------------------------------

-- Check that index OIDs are assigned correctly in include-yb-metadata mode for partition primary keys
CREATE TABLE part_uniq_const(v1 INT, v2 INT, v3 INT, PRIMARY KEY (v1, v3)) PARTITION BY RANGE(v1);

CREATE TABLE part_uniq_const_50_100 PARTITION OF part_uniq_const FOR VALUES FROM (50) TO (100);

CREATE TABLE part_uniq_const_30_50 PARTITION OF part_uniq_const FOR VALUES FROM (30) TO (50);

CREATE TABLE part_uniq_const_default PARTITION OF part_uniq_const DEFAULT;

INSERT INTO part_uniq_const VALUES (51, 151, 151), (31, 231, 231), (1, 1001, 1001);

-- Constraints should be output without a separate CREATE INDEX
-- because partitions do not support ALTER TABLE .. ADD CONSTRAINT .. USING INDEX
ALTER TABLE part_uniq_const ADD CONSTRAINT part_uniq_const_unique UNIQUE (v1, v2);

-- However, range partitioned index on the child partition table alone should be
-- output with the specific CREATE INDEX to retain range partitioning
CREATE UNIQUE INDEX part_uniq_const_50_100_v2_idx ON part_uniq_const_50_100 (v2 ASC);

ALTER TABLE part_uniq_const_50_100 ADD CONSTRAINT part_uniq_const_50_100_v2_uniq  UNIQUE USING INDEX part_uniq_const_50_100_v2_idx;


-- Test inheritance
CREATE TABLE level0(c1 int, c2 text not null, c3 text, c4 text);

CREATE TABLE level1_0(c1 int, primary key (c1 asc)) inherits (level0);
CREATE TABLE level1_1(c2 text primary key) inherits (level0);
CREATE INDEX  level1_1_c3_idx ON level1_1 (c3 DESC);

CREATE TABLE level2_0(c1 int not null, c2 text, c3 text not null) inherits (level1_0, level1_1);
ALTER TABLE level2_0 NO INHERIT level1_1;

CREATE TABLE level2_1(c1 int not null, c2 text not null, c3 text not null, c4 text primary key);
ALTER TABLE level2_1 inherit level1_0;
ALTER TABLE level2_1 inherit level1_1;
CREATE INDEX level2_1_c3_idx ON level2_1 (c3 ASC);

INSERT INTO level0 VALUES (NULL, '0', NULL, NULL);
INSERT INTO level1_0 VALUES (2, '1_0', '1_0', NULL);
INSERT INTO level1_1 VALUES (NULL, '1_1', NULL, '1_1');
INSERT INTO level2_0 VALUES (1, '2_0', '2_0', NULL);
INSERT INTO level2_1 VALUES (2, '2_1', '2_1', '2_1');

ALTER TABLE level0 ADD CONSTRAINT level0_c1_cons CHECK (c1 > 0);
ALTER TABLE level0 ADD CONSTRAINT level0_c1_cons2 CHECK (c1 IS NULL) NO INHERIT;
ALTER TABLE level1_1 ADD CONSTRAINT level1_1_c1_cons CHECK (c1 >= 2);

-- Test enum type sortorder
CREATE TYPE overflow AS ENUM ( 'A', 'Z' );
ALTER TYPE overflow ADD VALUE 'B' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'C' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'D' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'E' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'F' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'G' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'H' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'I' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'J' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'K' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'L' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'M' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'N' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'O' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'P' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'Q' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'R' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'S' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'T' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'U' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'V' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'W' BEFORE 'Z';
ALTER TYPE overflow ADD VALUE 'X' BEFORE 'Z';

CREATE TYPE underflow AS ENUM ( 'A', 'Z' );
ALTER TYPE underflow ADD VALUE 'Y' BEFORE 'Z';
ALTER TYPE underflow ADD VALUE 'X' BEFORE 'Y';
ALTER TYPE underflow ADD VALUE 'W' BEFORE 'X';
ALTER TYPE underflow ADD VALUE 'V' BEFORE 'W';
ALTER TYPE underflow ADD VALUE 'U' BEFORE 'V';
ALTER TYPE underflow ADD VALUE 'T' BEFORE 'U';
ALTER TYPE underflow ADD VALUE 'S' BEFORE 'T';
ALTER TYPE underflow ADD VALUE 'R' BEFORE 'S';
ALTER TYPE underflow ADD VALUE 'Q' BEFORE 'R';
ALTER TYPE underflow ADD VALUE 'P' BEFORE 'Q';
ALTER TYPE underflow ADD VALUE 'O' BEFORE 'P';
ALTER TYPE underflow ADD VALUE 'N' BEFORE 'O';
ALTER TYPE underflow ADD VALUE 'M' BEFORE 'N';
ALTER TYPE underflow ADD VALUE 'L' BEFORE 'M';
ALTER TYPE underflow ADD VALUE 'K' BEFORE 'L';
ALTER TYPE underflow ADD VALUE 'J' BEFORE 'K';
ALTER TYPE underflow ADD VALUE 'I' BEFORE 'J';
ALTER TYPE underflow ADD VALUE 'H' BEFORE 'I';
ALTER TYPE underflow ADD VALUE 'G' BEFORE 'H';
ALTER TYPE underflow ADD VALUE 'F' BEFORE 'G';
ALTER TYPE underflow ADD VALUE 'E' BEFORE 'F';
ALTER TYPE underflow ADD VALUE 'D' BEFORE 'E';
ALTER TYPE underflow ADD VALUE 'C' BEFORE 'D';

-- Test inheritance with different col order in child
CREATE TABLE par(parc1 INT, parc2 TEXT DEFAULT 'def', parc25 int, parc3 INT GENERATED ALWAYS AS (parc1 + 5) STORED, parc4 REAL NOT NULL, parc5 TEXT DEFAULT NULL);
CREATE TABLE ch(chc1 TEXT, chc2 int, parc4 REAL, parc5 TEXT, PRIMARY KEY(chc1)) INHERITS (par);
ALTER TABLE par ADD COLUMN parc9 REAL NOT NULL;
ALTER TABLE par ADD COLUMN parc10 INT DEFAULT 5;
ALTER TABLE par DROP COLUMN parc25;
ALTER TABLE ch DROP COLUMN chc2;
INSERT INTO ch VALUES (1, 'FOO', DEFAULT, 1.1, NULL, 'fooch', 2.2);

-- Test inheritance with different col order and multiple parents
CREATE TABLE inh2_par1(parc1 INT, parc2 TEXT, parc3 REAL);
CREATE TABLE inh2_par2(parc3 REAL, parc1 INT, parc2 TEXT);
-- Col order should be parent1, parent2, child
CREATE TABLE inh2_ch(chc1 INT, chc2 TEXT, PRIMARY KEY(chc1)) INHERITS (inh2_par1, inh2_par2);
INSERT INTO inh2_ch VALUES (1, '1_FOO', 1.1, 10, 'CH_1_FOO');

-- Added col inherited from parent2
ALTER TABLE inh2_par2 ADD COLUMN parc5 TEXT;

-- Added col inherited from parent1
ALTER TABLE inh2_par1 ADD COLUMN parc6 numeric(8,1);

-- Added col inherited from parent2 and different default in child
ALTER TABLE inh2_par2 ADD COLUMN parc7 TEXT DEFAULT 'PAR2_C7_DEF';
ALTER TABLE inh2_ch ALTER COLUMN parc7 SET DEFAULT 'CH_C7_DEF';
INSERT INTO inh2_ch values (2, '2_FOO', 2.2, 20, '2_CH_2', '2_CH_5', 2.2);
