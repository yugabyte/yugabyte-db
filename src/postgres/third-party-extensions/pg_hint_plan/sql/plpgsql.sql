--
-- Scenarios with various PL/pgsql functions
--

SET search_path TO public;
SET client_min_messages TO log;
\set SHOW_CONTEXT always

LOAD 'pg_hint_plan';
SET pg_hint_plan.debug_print TO on;
SELECT setting <> 'off' FROM pg_settings WHERE name = 'compute_query_id';
SHOW pg_hint_plan.enable_hint_table;

-- Internal handling of hints within plpgsql functions.
-- This forces an exception, manipulating internally plpgsql_recurse_level
-- in the resowner cleanup callback.
create or replace function test_hint_exception(level int)
returns void language plpgsql as $$
begin
  level := level + 1;
  raise notice 'Execution of test_hint_exception at level %', level;
  if level > 1 then
    -- This triggers the exception below, ending execution.
    execute 'select ''x''::numeric';
  end if;
  raise notice 'End of test_hint_exception at level %', level;
  execute 'select test_hint_exception(' || level || ')';
  exception when others then end;
$$;
-- Having a transaction context is essential to mess up with the
-- recursion counter and to make sure that the resowner cleanup is called
-- when expected.
begin;
select set_config('compute_query_id','off', true);
-- Show plan without hints
explain (costs false) with test as (select 'z' val)
  select t1.val from test t1, test t2 where t1.val = t2.val;
-- Invoke function that internally throws an exception with two
-- levels of nesting.
select test_hint_exception(0);
-- Show plan with hint, stored as an internal state of plpgsql_recurse_level.
explain (costs false) with test /*+ MergeJoin(t1 t2) */
  as (select 'x' val) select t1.val from test t1, test t2 where t1.val = t2.val;
-- This query should have the same plan as the first one, without hints.
explain (costs false) with test as (select 'y' val)
  select t1.val from test t1, test t2 where t1.val = t2.val;
-- Again, with one level of nesting.
select test_hint_exception(1);
-- Show plan with hint.
explain (costs false) with test /*+ MergeJoin(t1 t2) */
  as (select 'x' val) select t1.val from test t1, test t2 where t1.val = t2.val;
-- This query should have no hints.
explain (costs false) with test as (select 'y' val)
  select t1.val from test t1, test t2 where t1.val = t2.val;
rollback;
-- Still no hints used here.
explain (costs false) with test as (select 'y' val)
  select t1.val from test t1, test t2 where t1.val = t2.val;
drop function test_hint_exception;

-- Test hints with function using transactions internally.
create table test_hint_tab (a int);
-- Function called in a nested loop to check for hints.
create function test_hint_queries(run int, level int) returns void
language plpgsql as $$
declare c text;
begin
  level := level + 1;
  -- Stopping at two levels of nesting should be sufficient..
  if level > 2 then
    return;
  end if;
  -- Mix of queries with and without hints.  The level is mixed in the
  -- query string to show it in the output generated.
  raise notice 'Execution % at level %, no hints', run, level;
  execute 'explain (costs false) with test
    as (select ' || level || ' val)
    select t1.val from test t1, test t2 where t1.val = t2.val;'
    into c;
  raise notice 'Execution % at level %, merge-join hint', run, level;
  execute 'explain (costs false) with test /*+ MergeJoin(t1 t2) */
    as (select ' || level || ' val)
    select t1.val from test t1, test t2 where t1.val = t2.val;'
    into c;
  execute 'select test_hint_queries(' || run || ',' || level || ')';
end; $$;
-- Entry point of this test.  This executes the transaction
-- commands while calling test_hint_queries in a nested loop.
create procedure test_hint_transaction()
language plpgsql as $$
declare c text;
begin
  for i in 0..3 loop
    execute 'select test_hint_queries(' || i || ', 0)';
    insert into test_hint_tab (a) values (i);
    if i % 2 = 0 then
      commit;
    else
      rollback;
    end if;
  end loop;
end; $$;
call test_hint_transaction();
table test_hint_tab;
drop procedure test_hint_transaction;
drop function test_hint_queries;
drop table test_hint_tab;
