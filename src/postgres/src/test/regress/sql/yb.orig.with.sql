\getenv abs_srcdir PG_ABS_SRCDIR
\set filename :abs_srcdir '/yb_commands/explainrun.sql'
\i :filename
\set explain 'EXPLAIN (COSTS OFF)'

/* Test A */
drop table if exists a;
drop table if exists b;
create table a (i int unique);
create table b (i int unique);
insert into a values (1);
insert into b values (2);

SELECT 'with w(i) as (
    insert into a values (1) on conflict on constraint a_i_key do update set i = 10 returning i
) insert into b values (2) on conflict on constraint b_i_key do update set i = (select 20 from w)'
AS query \gset
:explain1run1

/* Test B */
drop table if exists a;
create table a (i int unique);
insert into a values (1), (2);

SELECT 'with w(i) as (
    insert into a values (1) on conflict on constraint a_i_key do update set i = 10 returning i
) insert into a values (2) on conflict on constraint a_i_key do update set i = (select 20 from w)'
AS query \gset
:explain1run1

/* Test C */
drop table if exists a;
create table a (i int unique);
insert into a values (1), (2), (3);

SELECT 'with w(i) as (
    insert into a values (1) on conflict on constraint a_i_key do update set i = 10 returning i
), x(i) as (
    insert into a values (2) on conflict on constraint a_i_key do update set i = 20 returning i
) insert into a values (3) on conflict on constraint a_i_key do update set i = (select 30 from w)'
AS query \gset
:explain1run1
