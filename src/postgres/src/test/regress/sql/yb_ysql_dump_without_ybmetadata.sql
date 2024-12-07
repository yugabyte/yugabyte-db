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