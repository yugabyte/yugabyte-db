------------------------------------------------
-- Test alter with add constraint using unique index.
------------------------------------------------

-- Setting default HASH/ASC unique index columns on regular table
CREATE TABLE p1 (a INT, b INT, c INT);

CREATE UNIQUE INDEX idx1 on p1 (a, b);

ALTER TABLE p1 ADD CONSTRAINT c1 UNIQUE USING INDEX idx1;

-- Setting defualt ASC/ASC unique index columns on tablegroup table
CREATE TABLEGROUP t;

CREATE TABLE p2 (a INT, b INT, c INT) TABLEGROUP t;

CREATE UNIQUE INDEX idx2 on p2 (a, b);

ALTER TABLE p2 ADD CONSTRAINT c2 UNIQUE USING INDEX idx2;

-- Setting multi-column unique index
CREATE TABLE p3 (a INT, b INT, c INT);

CREATE UNIQUE INDEX idx3 on p3 ((a,b) HASH, c ASC);

ALTER TABLE p3 ADD CONSTRAINT c3 UNIQUE USING INDEX idx3;

------------------------------------------------
-- Test unique constraint on partitioned tables.
------------------------------------------------

CREATE TABLE part_uniq_const(v1 INT, v2 INT) PARTITION BY RANGE(v1);

CREATE TABLE part_uniq_const_50_100 PARTITION OF part_uniq_const FOR VALUES FROM (50) TO (100);

CREATE TABLE part_uniq_const_30_50 PARTITION OF part_uniq_const FOR VALUES FROM (30) TO (50);

CREATE TABLE part_uniq_const_default PARTITION OF part_uniq_const DEFAULT;

INSERT INTO part_uniq_const VALUES (51, 100), (31, 200), (1, 1000);

ALTER TABLE part_uniq_const ADD CONSTRAINT part_uniq_const_unique UNIQUE (v1, v2);

-- A range partitioned index on the child partition table alone should be output with
-- the specific CREATE INDEX
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
ALTER TYPE overflow ADD VALUE 'Y' BEFORE 'Z';

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
ALTER TYPE underflow ADD VALUE 'B' BEFORE 'C';
