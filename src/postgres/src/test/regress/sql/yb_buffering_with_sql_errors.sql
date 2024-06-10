--
-- Tests to ensure that YSQL buffering (i.e., logic in pg_operation_buffer.cc and
-- related files) doesn't break any semantics of error handling in functions
-- and procedures.
--
-- *****************************************************************************
-- * Exception handling in PLPGSQL
-- *****************************************************************************
--
-- PLPGSQL exception blocks allow a user to execute a block of statements such
-- that if an error occurs, the changes to the database are undone/ reverted and
-- user-specified error-handling is invoked.
--
-- The changes to the database are undone as follows - an internal savepoint is
-- registered before any code in the exception block is executed. If any error
-- occurs, it is caught and the savepoint is rolled back and released. This
-- helps revert any modifications to the database.
--
-- However, there are some statements which don't modify the database, but have
-- other side-effects. These are not undone even if an exception occurs. A
-- simple example of this is the "return next;" statement in plpgsql. Once data
-- is sent to the user it can't be undone.
-- *****************************************************************************
--
-- *****************************************************************************
-- * YSQL Buffering
-- *****************************************************************************
-- YSQL buffers operations to tserver (writes specifically) unless it hits some
-- condition that forces it to flush the buffer and wait for the response. Some
-- conditions that force waiting for a buffer response are - completion of a txn,
-- completion of an exception handling block in plpgsql, a read operation, or a
-- write to a key which already has a buffered write.
--
-- With buffering, execution can move on to later statements unless a flush and
-- response wait is required based on the conditions above. For example - with
-- autocommit mode off, writes for a statement (like INSERT/UPDATE) are not
-- flushed until required. Instead, they are buffered. This is okay because,
-- even before flushing, we would know the number of inserts/updates to be done
-- and return that number to the user client (as "INSERT x"). If an error occurs
-- in the rpc, it will anyway be caught in some later flush, but before the txn
-- commits.
--
-- Allowing execution to move on to later statements without waiting for the
-- actual work to be done on the tablet servers helps improve performance by
-- buffering and reduce latency.
-- *****************************************************************************
--
--
-- As seen in gh issue #12184, incorrect behaviour is observed with YSQL
-- buffering when an exception that occurs due to some statement's rpcs is seen
-- after a later statement which has non-reversible side-effect(s) has also been
-- executed. Buffered operations should be flushed and waited for before
-- executing any statements with non-reversible side effects in the same
-- transaction. The following tests ensure this for the various cases.
--
-- 1(a) PL/pgsql: ensure statements with non-reversible side effects (i.e., non
--      transactional work) are not executed if an ealier statement caused an
--      exception.
--
-- (this test case is one of Bryn's example in Github issue #12184)
--
-- TODO: The constraint_name is not populated for unique constraint violations in stacked
-- diagnostics. This is being tracked by github issue #13501
drop function if exists f(text) cascade;
drop table if exists t cascade;
create table t(k serial primary key, v varchar(5) not null);
create unique index t_v_unq on t(v);
insert into t(v) values ('dog'), ('cat'), ('frog');

create function f(new_v in text)
  returns table(z text)
  security definer
  language plpgsql
as $body$
declare
  sqlstate_    text not null := '';
  constraint_  text not null := '';
  msg          text not null := '';
begin
  z := 'Inserting "'||new_v||'"'; return next;
  begin
    insert into t(v) values (new_v);
    z := 'No exception'; return next;
  exception
    when others then
      get stacked diagnostics
        sqlstate_ = returned_sqlstate,
        constraint_ = constraint_name,
        msg = message_text;
      if sqlstate_ = '23505' then
        z := 'unique_violation'; return next;
        z := 'constraint: '||constraint_; return next;
        z := 'message:    '||msg; return next;
      else
        z := 'others'; return next;
        z := 'sqlstate:   '||sqlstate_; return next;
        z := 'message:    '||msg; return next;
    end if;
  end;
end;
$body$;

select z from f('ant');
select z from f('dog');
select z from f('elephant');

-- 1(b) PL/pgsql: another example from Bryn in Github issue #12184. This one has
--      a for loop within the exception block.

drop table if exists t cascade;
create table t(k serial primary key, v varchar(3) not null unique);

drop function if exists f(boolean);
create function f(bug in boolean)
  returns table(z text)
  language plpgsql
as $body$
declare
  n int  not null := 0;
begin
  z := ''; return next;
  begin
    for i in 1..3 loop
      n := n + 1;
      z := n::text;
      return next;
      insert into t(v) values(z);
    end loop;
  exception
    when others then null;
  end;

  z := ''; return next;
  begin
    for i in 1..3 loop
      n := n + 1;
      z := case
             when      bug  and (i = 2) then '1'
             when (not bug) and (i = 2) then 'abcd'
             else                             n::text
           end;
      return next;
      insert into t(v) values(z);
    end loop;
  exception
    when string_data_right_truncation then
      z := 'string_data_right_truncation handled';
      return next;
    when unique_violation then
      z := 'unique_violation handled';
      return next;
  end;

  z := ''; return next;
  begin
    for i in 1..3 loop
      n := n + 1;
      z := n::text;
      return next;
      insert into t(v) values(z);
    end loop;
  exception
    when others then null;
  end;
end;
$body$;

select z from f(bug=>false);
select v from t order by k;
truncate t;
select z from f(bug=>true);
select v from t order by k;

-- 1(c) PL/pgsql: the statement that does non-reversible side effects (i.e.,
--      non-transactional work) is in a nested function.

drop table if exists t cascade;
create table t(k serial primary key, v varchar(500) not null unique);
insert into t(v) values ('dog');
create or replace function f_inner_that_handles_exception(new_v in text)
  returns table(z text)
  language plpgsql
as $body$
declare
  sqlstate_    text not null := '';
  constraint_  text not null := '';
  msg          text not null := '';
begin
  begin
    z := 'return next was executed after insert, this was not expected';
    insert into t(v) values (new_v);
    return next;
  exception
    when unique_violation then
      get stacked diagnostics
        sqlstate_ = returned_sqlstate,
        constraint_ = constraint_name,
        msg = message_text;
      z := 'unique_violation in f_inner_that_handles_exception constraint: ' || constraint_ || ' sqlstate: ' || sqlstate_ || ' message: '|| msg;
      return next;
    when others then
      raise;
  end;
end;
$body$;

create or replace function f_inner_that_passes_exception(new_v in text)
  returns table(z text)
  language plpgsql
as $body$
begin
    insert into t(v) values (new_v);
end;
$body$;

create or replace function f_outer()
  returns table(z text)
  language plpgsql
as $body$
declare
  sqlstate_    text not null := '';
  constraint_  text not null := '';
  msg          text not null := '';
begin
  begin -- (1) ensure that the statement after "insert" in the inner function is not executed
    insert into t(v) select f_inner_that_handles_exception('dog');
    z := 'No exception'; return next;
  exception
    when others then
      raise;
  end;
  begin -- (2) similar to (1), the inner function handles the exception an returns the same string. But that will should lead to another unique index violation in the below INSERT.
    insert into t(v) select f_inner_that_handles_exception('dog');
    z := 'No exception'; return next;
  exception
    when unique_violation then
      get stacked diagnostics
        sqlstate_ = returned_sqlstate,
        constraint_ = constraint_name,
        msg = message_text;
      z := 'unique_violation in f_outer constraint: ' || constraint_ || ' sqlstate: ' || sqlstate_ || ' message: '|| msg;
      return next;
    when others then
      raise;
  end;
  begin -- (3) ensure that the statement after below "insert" is not executed since inner function passes on the exception
    insert into t(v) select f_inner_that_passes_exception('dog');
    z := 'No exception'; return next;
  exception
    when unique_violation then
      get stacked diagnostics
        sqlstate_ = returned_sqlstate,
        constraint_ = constraint_name,
        msg = message_text;
      z := 'unique_violation in f_outer constraint: ' || constraint_ || ' sqlstate: ' || sqlstate_ || ' message: '|| msg;
      return next;
    when others then
      raise;
  end;
end;
$body$;

select f_outer();
select * from t;

-- 2. SQL functions: ensure statements with non-reversible side effects (i.e.,
--    non transactional work) are not executed if an earlier statement caused an
--    exception.

prepare dummy_query as select * from t;

create or replace function f(new_v in text)
  returns table(z text)
  language sql
as $body$
  insert into t(v) values ('dog');
  deallocate dummy_query;
  select v from t;
$body$;

select f('dog');
execute dummy_query; -- this should find the prepared statement and run fine

-- 3. Miscellaneous test cases from #6429. Just ensures that a function ends
--    silently when expected to, if a raised exception is ignored in the
--    handler.

drop table if exists t cascade;
create table t(k int primary key, v int not null);
create unique index t_v_unq on t(v);
insert into t(k, v) values (1, 1);

insert into t(k, v) values (2, 1);

do $body$
begin
  insert into t(k, v) values (2, 1);
exception
  when unique_violation then null;
end;
$body$;

do $body$
begin
  insert into t(k, v) values (2, 1);
  assert false, 'Logic error. "unique_violation" not caught.';
exception
  when unique_violation then null;
end;
$body$;

-- 4. Miscellaneous test cases from #8326.

drop table if exists t;
create table t(k varchar(3) primary key);
insert into t(k) values('dog');

do $body$
begin
  insert into t(k) values('frog');
exception when others then
  raise info 'exception caught';
end;
$body$;

do $body$
begin
  raise unique_violation;
exception when others then
  raise info 'exception caught';
end;
$body$;

do $body$
begin
  insert into t(k) values('dog');
exception when others then
  raise info 'exception caught';
end;
$body$;