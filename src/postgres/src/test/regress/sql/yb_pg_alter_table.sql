--
-- ALTER_TABLE
--

-- Clean up in case a prior regression run failed
SET client_min_messages TO 'warning';
DROP ROLE IF EXISTS regress_alter_table_user1;
RESET client_min_messages;

CREATE USER regress_alter_table_user1;

--
-- add attribute
--

CREATE TABLE attmp (initial int4);

COMMENT ON TABLE attmp_wrong IS 'table comment';
COMMENT ON TABLE attmp IS 'table comment';
COMMENT ON TABLE attmp IS NULL;

ALTER TABLE attmp ADD COLUMN xmin integer; -- fails

ALTER TABLE attmp ADD COLUMN a int4 default 3;

ALTER TABLE attmp ADD COLUMN b name;

ALTER TABLE attmp ADD COLUMN c text;

ALTER TABLE attmp ADD COLUMN d float8;

ALTER TABLE attmp ADD COLUMN e float4;

ALTER TABLE attmp ADD COLUMN f int2;

ALTER TABLE attmp ADD COLUMN g polygon;

ALTER TABLE attmp ADD COLUMN h abstime;

ALTER TABLE attmp ADD COLUMN i char;

ALTER TABLE attmp ADD COLUMN j abstime[];

ALTER TABLE attmp ADD COLUMN k int4;

ALTER TABLE attmp ADD COLUMN l tid;

ALTER TABLE attmp ADD COLUMN m xid;

ALTER TABLE attmp ADD COLUMN n oidvector;

--ALTER TABLE attmp ADD COLUMN o lock;
ALTER TABLE attmp ADD COLUMN p smgr;

ALTER TABLE attmp ADD COLUMN q point;

ALTER TABLE attmp ADD COLUMN r lseg;

ALTER TABLE attmp ADD COLUMN s path;

ALTER TABLE attmp ADD COLUMN t box;

ALTER TABLE attmp ADD COLUMN u tinterval;

ALTER TABLE attmp ADD COLUMN v timestamp;

ALTER TABLE attmp ADD COLUMN w interval;

ALTER TABLE attmp ADD COLUMN x float8[];

ALTER TABLE attmp ADD COLUMN y float4[];

ALTER TABLE attmp ADD COLUMN z int2[];

-- This is the original query. This should be uncommented after we support abstime type (col h) and
-- tinterval type (col u)
-- INSERT INTO attmp (a, b, c, d, e, f, g, h, i, j, k, l, m, n, p, q, r, s, t, u,
-- 	v, w, x, y, z)
--    VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
--         'Mon May  1 00:30:30 1995', 'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
-- 	314159, '(1,1)', '512',
-- 	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
-- 	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)', '["epoch" "infinity"]',
-- 	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');

INSERT INTO attmp (a, b, c, d, e, f, g, i, j, k, l, m, n, p, q, r, s, t,
	v, w, x, y, z)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
        'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
	314159, '(1,1)', '512',
	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)',
	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');
SELECT * FROM attmp;

DROP TABLE attmp;

-- the wolf bug - schema mods caused inconsistent row descriptors
CREATE TABLE attmp (
	initial 	int4
);

ALTER TABLE attmp ADD COLUMN a int4;

ALTER TABLE attmp ADD COLUMN b name;

ALTER TABLE attmp ADD COLUMN c text;

ALTER TABLE attmp ADD COLUMN d float8;

ALTER TABLE attmp ADD COLUMN e float4;

ALTER TABLE attmp ADD COLUMN f int2;

ALTER TABLE attmp ADD COLUMN g polygon;

ALTER TABLE attmp ADD COLUMN h abstime;

ALTER TABLE attmp ADD COLUMN i char;

ALTER TABLE attmp ADD COLUMN j abstime[];

ALTER TABLE attmp ADD COLUMN k int4;

ALTER TABLE attmp ADD COLUMN l tid;

ALTER TABLE attmp ADD COLUMN m xid;

ALTER TABLE attmp ADD COLUMN n oidvector;

--ALTER TABLE attmp ADD COLUMN o lock;
ALTER TABLE attmp ADD COLUMN p smgr;

ALTER TABLE attmp ADD COLUMN q point;

ALTER TABLE attmp ADD COLUMN r lseg;

ALTER TABLE attmp ADD COLUMN s path;

ALTER TABLE attmp ADD COLUMN t box;

ALTER TABLE attmp ADD COLUMN u tinterval;

ALTER TABLE attmp ADD COLUMN v timestamp;

ALTER TABLE attmp ADD COLUMN w interval;

ALTER TABLE attmp ADD COLUMN x float8[];

ALTER TABLE attmp ADD COLUMN y float4[];

ALTER TABLE attmp ADD COLUMN z int2[];

-- This is the original query. This should be uncommented after we support abstime type (col h) and
-- tinterval type (col u)
-- INSERT INTO attmp (a, b, c, d, e, f, g, h, i, j, k, l, m, n, p, q, r, s, t, u,
-- 	v, w, x, y, z)
--    VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
--         'Mon May  1 00:30:30 1995', 'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
-- 	314159, '(1,1)', '512',
-- 	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
-- 	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)', '["epoch" "infinity"]',
-- 	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');

INSERT INTO attmp (a, b, c, d, e, f, g, i, j, k, l, m, n, p, q, r, s, t,
	v, w, x, y, z)
   VALUES (4, 'name', 'text', 4.1, 4.1, 2, '(4.1,4.1,3.1,3.1)',
        'c', '{Mon May  1 00:30:30 1995, Monday Aug 24 14:43:07 1992, epoch}',
	314159, '(1,1)', '512',
	'1 2 3 4 5 6 7 8', 'magnetic disk', '(1.1,1.1)', '(4.1,4.1,3.1,3.1)',
	'(0,2,4.1,4.1,3.1,3.1)', '(4.1,4.1,3.1,3.1)',
	'epoch', '01:00:10', '{1.0,2.0,3.0,4.0}', '{1.0,2.0,3.0,4.0}', '{1,2,3,4}');

SELECT * FROM attmp;

CREATE INDEX attmp_idx ON attmp (a, (d + e), b);

ALTER INDEX attmp_idx ALTER COLUMN 0 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 1 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS 1000;

\d+ attmp_idx

ALTER INDEX attmp_idx ALTER COLUMN 3 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 4 SET STATISTICS 1000;

ALTER INDEX attmp_idx ALTER COLUMN 2 SET STATISTICS -1;

DROP TABLE attmp;

--
-- rename - check on both non-temp and temp tables
--
CREATE TABLE attmp (regtable int);
CREATE TEMP TABLE attmp (attmptable int);

ALTER TABLE attmp RENAME TO attmp_new;

SELECT * FROM attmp;
SELECT * FROM attmp_new;

ALTER TABLE attmp RENAME TO attmp_new2;

SELECT * FROM attmp;		-- should fail
SELECT * FROM attmp_new;
SELECT * FROM attmp_new2;

DROP TABLE attmp_new;
DROP TABLE attmp_new2;

-- check rename of partitioned tables and indexes also
CREATE TABLE part_attmp (a int primary key) partition by range (a);
CREATE TABLE part_attmp1 PARTITION OF part_attmp FOR VALUES FROM (0) TO (100);
ALTER INDEX part_attmp_pkey RENAME TO part_attmp_index;
ALTER INDEX part_attmp1_pkey RENAME TO part_attmp1_index;
ALTER TABLE part_attmp RENAME TO part_at2tmp;
ALTER TABLE part_attmp1 RENAME TO part_at2tmp1;
SET ROLE regress_alter_table_user1;
ALTER INDEX part_attmp_index RENAME TO fail;
ALTER INDEX part_attmp1_index RENAME TO fail;
ALTER TABLE part_at2tmp RENAME TO fail;
ALTER TABLE part_at2tmp1 RENAME TO fail;
RESET ROLE;
DROP TABLE part_at2tmp;

-- ALTER TABLE ... RENAME on non-table relations
-- renaming indexes (FIXME: this should probably test the index's functionality)
ALTER INDEX IF EXISTS __onek_unique1 RENAME TO attmp_onek_unique1;
ALTER INDEX IF EXISTS __attmp_onek_unique1 RENAME TO onek_unique1;

ALTER INDEX onek_unique1 RENAME TO attmp_onek_unique1;
ALTER INDEX attmp_onek_unique1 RENAME TO onek_unique1;

SET ROLE regress_alter_table_user1;
ALTER INDEX onek_unique1 RENAME TO fail;  -- permission denied
RESET ROLE;

-- renaming index should rename constraint as well
ALTER TABLE onek ADD CONSTRAINT onek_unique1_constraint UNIQUE (unique1);
ALTER INDEX onek_unique1_constraint RENAME TO onek_unique1_constraint_foo;
ALTER TABLE onek DROP CONSTRAINT onek_unique1_constraint_foo;

--
-- lock levels
--
drop type lockmodes;
create type lockmodes as enum (
 'SIReadLock'
,'AccessShareLock'
,'RowShareLock'
,'RowExclusiveLock'
,'ShareUpdateExclusiveLock'
,'ShareLock'
,'ShareRowExclusiveLock'
,'ExclusiveLock'
,'AccessExclusiveLock'
);

drop view my_locks;
create or replace view my_locks as
select case when c.relname like 'pg_toast%' then 'pg_toast' else c.relname end, max(mode::lockmodes) as max_lockmode
from pg_locks l join pg_class c on l.relation = c.oid
where virtualtransaction = (
        select virtualtransaction
        from pg_locks)
and locktype = 'relation'
and relnamespace != (select oid from pg_namespace where nspname = 'pg_catalog')
and c.relname != 'my_locks'
group by c.relname;

create table alterlock (f1 int primary key, f2 text);
insert into alterlock values (1, 'foo');
create table alterlock2 (f3 int primary key, f1 int);
insert into alterlock2 values (1, 1);

begin; alter table alterlock alter column f2 set statistics 150;
select * from my_locks order by 1;
rollback;

begin; alter table alterlock cluster on alterlock_pkey;
select * from my_locks order by 1;
commit;

begin; alter table alterlock set without cluster;
select * from my_locks order by 1;
commit;

begin; alter table alterlock set (fillfactor = 100);
select * from my_locks order by 1;
commit;

begin; alter table alterlock reset (fillfactor);
select * from my_locks order by 1;
commit;

begin; alter table alterlock set (toast.autovacuum_enabled = off);
select * from my_locks order by 1;
commit;

begin; alter table alterlock set (autovacuum_enabled = off);
select * from my_locks order by 1;
commit;

begin; alter table alterlock alter column f2 set (n_distinct = 1);
select * from my_locks order by 1;
rollback;

-- test that mixing options with different lock levels works as expected
begin; alter table alterlock set (autovacuum_enabled = off, fillfactor = 80);
select * from my_locks order by 1;
commit;

begin; alter table alterlock alter column f2 set storage extended;
select * from my_locks order by 1;
rollback;

-- TODO(jason): uncomment when doing issue #9106
-- begin; alter table alterlock alter column f2 set default 'x';
-- select * from my_locks order by 1;
-- rollback;

--
-- alter object set schema
--

create schema alter1;
create schema alter2;

create text search parser alter1.prs(start = prsd_start, gettoken = prsd_nexttoken, end = prsd_end, lextypes = prsd_lextype);
create text search configuration alter1.cfg(parser = alter1.prs);

alter text search parser alter1.prs set schema alter2;
alter text search configuration alter1.cfg set schema alter2;

-- clean up
drop schema alter2 cascade;

--
-- typed tables: OF / NOT OF
--

CREATE TYPE tt_t0 AS (z inet, x int, y numeric(8,2));
ALTER TYPE tt_t0 DROP ATTRIBUTE z;
CREATE TABLE tt0 (x int NOT NULL, y numeric(8,2));	-- OK
CREATE TABLE tt1 (x int, y bigint);					-- wrong base type
CREATE TABLE tt2 (x int, y numeric(9,2));			-- wrong typmod
CREATE TABLE tt3 (y numeric(8,2), x int);			-- wrong column order
CREATE TABLE tt4 (x int);							-- too few columns
CREATE TABLE tt5 (x int, y numeric(8,2), z int);	-- too few columns
CREATE TABLE tt6 () INHERITS (tt0);					-- can't have a parent
CREATE TABLE tt7 (x int, q text, y numeric(8,2)) WITH OIDS;
ALTER TABLE tt7 DROP q;								-- OK

ALTER TABLE tt0 OF tt_t0;
ALTER TABLE tt1 OF tt_t0;
ALTER TABLE tt2 OF tt_t0;
ALTER TABLE tt3 OF tt_t0;
ALTER TABLE tt4 OF tt_t0;
ALTER TABLE tt5 OF tt_t0;
ALTER TABLE tt6 OF tt_t0;
ALTER TABLE tt7 OF tt_t0;

CREATE TYPE tt_t1 AS (x int, y numeric(8,2));
ALTER TABLE tt7 OF tt_t1;			-- reassign an already-typed table
ALTER TABLE tt7 NOT OF;
\d tt7
