--
-- ALTER_TABLE
--

-- Clean up in case a prior regression run failed
SET client_min_messages TO 'warning';
DROP ROLE IF EXISTS regress_alter_table_user1;
RESET client_min_messages;

CREATE USER regress_alter_table_user1;

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
        from pg_locks
        where transactionid = txid_current()::integer)
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
