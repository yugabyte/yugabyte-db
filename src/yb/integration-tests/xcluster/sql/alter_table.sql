--
-- ALTER_TABLE
--

-- Taken from Postgres test/regress/sql/alter_table.sql
-- Test basic commands.

--
-- add attribute
--

CREATE TABLE attmp (initial int4);

ALTER TABLE attmp ADD COLUMN a int4 default 3;

ALTER TABLE attmp ADD COLUMN b name;

ALTER TABLE attmp ADD COLUMN c text;

ALTER TABLE attmp ADD COLUMN d float8;

ALTER TABLE attmp ADD COLUMN e float4;

ALTER TABLE attmp ADD COLUMN f int2;

ALTER TABLE attmp ADD COLUMN g polygon;

-- ALTER TABLE attmp ADD COLUMN h abstime; -- unsupported

ALTER TABLE attmp ADD COLUMN i char;

-- ALTER TABLE attmp ADD COLUMN j abstime[]; -- unsupported

ALTER TABLE attmp ADD COLUMN k int4;

ALTER TABLE attmp ADD COLUMN l tid;

ALTER TABLE attmp ADD COLUMN m xid;

ALTER TABLE attmp ADD COLUMN n oidvector;

-- ALTER TABLE attmp ADD COLUMN o lock; -- unsupported
-- ALTER TABLE attmp ADD COLUMN p smgr; -- unsupported

ALTER TABLE attmp ADD COLUMN q point;

ALTER TABLE attmp ADD COLUMN r lseg;

ALTER TABLE attmp ADD COLUMN s path;

ALTER TABLE attmp ADD COLUMN t box;

-- ALTER TABLE attmp ADD COLUMN u tinterval; -- unsupported

ALTER TABLE attmp ADD COLUMN v timestamp;

ALTER TABLE attmp ADD COLUMN w interval;

ALTER TABLE attmp ADD COLUMN x float8[];

ALTER TABLE attmp ADD COLUMN y float4[];

ALTER TABLE attmp ADD COLUMN z int2[];

ALTER TABLE attmp ADD COLUMN extra text;  -- extra column to be dropped later

INSERT INTO attmp (a, b, c, d, e, f, g, i, k, l, m, n, q, r, s, t, v, w, x, y, z, extra)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
           'c',
           314159, '(1,1)', '512',
           '1 2 3 4 5 6 7 8', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
           '(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)',
           'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}', 'extra');

ALTER TABLE attmp DROP COLUMN extra;

CREATE INDEX attmp_idx ON attmp (a, (d + e), b, (e + d));

ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS 1000;

\d+ attmp_idx

ALTER INDEX attmp_idx ALTER COLUMN 4 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS -1;

--
-- rename - check on both non-temp and temp tables
--
CREATE TABLE attmp_rename (regtable int);
CREATE TEMP TABLE attmp_rename (attmptable int);

ALTER TABLE attmp_rename RENAME TO attmp_rename_new;

SELECT * FROM attmp_rename;
SELECT * FROM attmp_rename_new;

ALTER TABLE attmp_rename RENAME TO attmp_rename_new2;

SELECT * FROM attmp_rename_new;
SELECT * FROM attmp_rename_new2;

-- check rename of partitioned tables and indexes also
CREATE TABLE part_attmp (a int primary key) partition by range (a);
CREATE TABLE part_attmp1 PARTITION OF part_attmp FOR VALUES FROM (0) TO (100);
ALTER INDEX part_attmp_pkey RENAME TO part_attmp_index;
ALTER INDEX part_attmp1_pkey RENAME TO part_attmp1_index;
ALTER TABLE part_attmp RENAME TO part_at2tmp;
ALTER TABLE part_attmp1 RENAME TO part_at2tmp1;


create table atacc1 (test int);
create table atacc2 (test2 int);
create table atacc3 (test3 int) inherits (atacc1, atacc2);
alter table atacc3 no inherit atacc2;
