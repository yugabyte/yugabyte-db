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