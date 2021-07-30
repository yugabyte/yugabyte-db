---
title: Common code for traversing all kinds of graph
headerTitle: Common code for traversing all kinds of graph
linkTitle: common code
description: This section presents common helper code for traversing all kinds of graph
menu:
  latest:
    identifier: common-code
    parent: traversing-general-graphs
    weight: 20
isTocNested: true
showAsideToc: true
---

Make sure that you have run [`cr-edges.sql`](../graph-representation/#cr-edges-sql) before you run the following code. Once you've done this, you can leave all of the common code that you create by following the steps described below in place while you run the various code examples in the following sections that show how to traverse the various different kinds of graph.

## Create wrapper functions to return the start and the terminal node of a path

The main value of this function is to bring readability. Without it, all of the SQL implementations described in the following sections would be cluttered with, in some cases, three occurrences of this expression:

```
a.path[cardinality(a.path)]
```

It's a little shorter, and much easier to discern the meaning of, this expression:

```
terminal(a.path)
```

Moreover, in some cases, an index will be needed on a table of _"path"_ values but this statement:

```plpgsql
create unique index terminal_unq on <the path table>(path[cardinality(path)])
```

fails with this generic error:

```
42601: syntax error
```

But this works just fine:

```
create unique index terminal_unq on paths(terminal(path));
```

So it's essential to use the function _"terminal()"_ here. The same thinking applies for this:

```
create unique index filtered_paths_shortest_path_unq on shortest_paths(start(path), terminal(path));
```

#####  `cr-start-and-terminal.sql`

```plpgsql
drop function if exists start cascade;
drop function if exists terminal cascade;

create function start(path in text[])
  returns text
  immutable
  language plpgsql
as $body$
begin
  return path[1];
end;
$body$;

create function terminal(path in text[])
  returns text
  immutable
  language plpgsql
as $body$
begin
  return path[cardinality(path)];
end;
$body$;
```

## Create a procedure to create the set of tables into which to insert paths and filtered paths

It will be useful to insert the paths that the code examples for the various kinds of graph generate into a table so that subsequent ad _hoc_ queries can be run on these results. For example, it is interesting to generate the set of shortest paths to the distinct reachable nodes (see _["restrict_to_shortest_paths()"](#cr-restrict-to-shortest-paths-sql)_). Because all these tables have the same shape and constraints, it is best to use dynamic SQL issued from a procedure to create them.

Notice that, in order to ensure that every computed path is unique, a unique index is created on the _"path"_ column. However, [GitHub Issue #6606](https://github.com/yugabyte/yugabyte-db/issues/6606) currently prevents the direct creation of an index on an array. The workaround is to create the index on the `text` typecast of the array. However,  the naïve attempt:

```
create index i1 on t1((arr::text));
```

fails for a different reason:

```
ERROR:  42P17: functions in index expression must be marked IMMUTABLE
```

(The _"typecast"_ functionality is given by a special kind of built-in function that is _not_ considered to be immutable.) So a jacket function, _"path_as_text()"_, that _can_ be marked as immutable is used. This is risk free because the `text` typecast of an array is reliably deterministic and free of side-effects.

#####  `cr-cr-path-table.sql`

```plpgsql
drop function if exists path_as_text(text[]) cascade;

create function path_as_text(path in text[])
  returns text
  immutable
  language plpgsql
as $body$
begin
  return path::text;
end;
$body$;

drop procedure if exists create_path_table(text, boolean) cascade;

create procedure create_path_table(name in text, temp in boolean)
  language plpgsql
as $body$
declare
  drop_table constant text := '
    drop table if exists ? cascade';

  create_table constant text := '
    create table ?(
      k     serial  primary key,
      path  text[]  not null)';

  create_temp_table constant text := '
    create temporary table ?(
      k     serial  primary key,
      path  text[]  not null)';

  cache_sequence constant text := '
    alter sequence ?_k_seq  cache 100000';
begin
  execute replace(drop_table,     '?', name);
  case temp
    when true then execute replace(create_temp_table, '?', name);
    else           execute replace(create_table,      '?', name);
  end case;
  execute replace(cache_sequence, '?', name);
end;
$body$;
```

## Create the set of tables into which to insert paths and filtered paths

You'll use the procedure _["create_path_table()"](#cr-cr-path-table-sql)_ to create the following tables:

- _"raw_paths"_. This is the target table for the paths that each of the code examples for the various kinds of graph generates.
- _"shortest_paths"_. This is the target table for the paths produced by procedure _["restrict_to_shortest_paths()"](#cr-restrict-to-shortest-paths-sql)_.
- _"unq_containing_paths"_. This is the target table for the paths produced by procedure _["restrict_to_unq_containing_paths()"](#cr-restrict-to-unq-containing-paths-sql)_.
- _"temp_paths"_, and _"working_paths"_. These tables are used by the approach that uses direct SQL, issued from a PL/pgsql stored procedure, that implements what the recursive CTE does rather than an actual `WITH` clause. See the section [How to implement early path pruning](../undirected-cyclic-graph/#how-to-implement-early-path-pruning).

First create the _"raw_paths"_ table and optionally add a column and create a trigger on the table so that the outcome of each successive repeat of the code that implements the _recursive term_ can be traced to help the developer see how the code works. (It has this effect only for the implementation of the _"find_paths()"_ procedure that implements early-path-pruning.) 

Because the purpose of the trigger is entirely pedagogical, because it is not needed by the substantive _"find_paths()"_ logic, and because its presence brings a theoretical performance drag, you should _either_ use the script that adds the extra tracing column and trigger for the _"raw_paths"_ table _or_ use the script that simply creates the _"raw_paths"_ table bare—according to your purpose.

**EITHER:**

##### `cr-raw-paths-with-tracing.sql`

```plpgsql
call create_path_table('raw_paths', false);

alter table raw_paths add column repeat_nr int;

drop function if exists raw_paths_trg_f() cascade;
create function raw_paths_trg_f()
  returns trigger 
  language plpgsql
as $body$
declare
  max_iteration constant int := (
    select coalesce(max(repeat_nr), null, -1) + 1 from raw_paths);
begin
  update raw_paths set repeat_nr = max_iteration where repeat_nr is null;
  return new;
end;
$body$;

create trigger raw_paths_trg after insert on raw_paths
for each statement
execute function raw_paths_trg_f();
```

**OR:**

##### `cr-raw-paths-no-tracing.sql`

```plpgsql
call create_path_table('raw_paths', false);
drop function if exists raw_paths_trg_f() cascade;
```

You can see that you can run either one of `cr-raw-paths-with-tracing.sql` or `cr-raw-paths-no-tracing.sql` at any time to get the regime that you need. Normally, you'll set up the _"raw_paths"_ table for tracing. But when your purpose is to compare the times for different approaches, you'll set it up without tracing code.

Now create the other tables:

##### `cr-supporting-path-tables.sql`

```plpgsql
call create_path_table('shortest_paths',        false);
call create_path_table('unq_containing_paths',  false);
call create_path_table('temp_paths',            true);
call create_path_table('working_paths',         true);

create unique index shortest_paths_start_terminal_unq on shortest_paths(start(path), terminal(path));
```

## Create a procedure to restrict a set of paths to leave only a shortest path to each distinct terminal node

The solution to the [Bacon Numbers](../../bacon-numbers/) problem needs only the _shortest path_ to all those actors who have a transitive "both acted in the same movie" relationship to Kevin Bacon. But, in general, there will be _many_ paths that reflect this transitive relationship. Indeed, there might even be two or more paths that each has the same shortest length.

The _"restrict_to_shortest_paths()"_ procedure finds just one shortest path to each reachable node from the set of all paths to reachable nodes. When there do exist two or more shortest paths to the same node, it selects the first one in the path sorting order. The advantage of this scheme over picking one of the contenders randomly is that the result is deterministic. This allows for a meaningful comparison between the result from running two overall analyses in two different databases. This is crucial when the aim is to confirm that PostgreSQL and YugabyteDB produce the same result from the same starting data, where, without a reliable ordering scheme, differences in physical data storage might produce different actual orders of results.

Notice how ordinary (non-recursive) CTEs are used, just as functions and procedures are used in procedural programming, to encapsulate and name distinct steps in the code. Try to implement the logic without using this technique. The exercise will very vividly highlight the expressive value that the `WITH` clause provides.

##### `cr-restrict-to-shortest-paths.sql`

```plpgsql
drop procedure if exists restrict_to_shortest_paths(text, text, boolean) cascade;

-- Restrict the paths from the start node to every other node
-- to just the shortest path to each distinct node.
create procedure restrict_to_shortest_paths(
  in_tab in text, out_tab in text, append in boolean default false)
  language plpgsql
as $body$
declare
  stmt constant text := '
    with
      -- For readability. Define cardinality and end_node as view columns.
      a1(k, cardinality, end_node, path) as (
        select
          k,
          cardinality(path),
          path[cardinality(path)],
          path
        from ?in_tab),

      -- In general, there is more than one path to any end_node. Define the
      -- minumum cardinality for each end_node as a pair of view columns.
      a2(cardinality, end_node) as (
        select
          min(cardinality),
          end_node
        from a1
        group by end_node),

      -- There might still be more than one path to a particular end_node
      -- where each of these has the minimum cardinality. Pick up the path
      -- for each of these following the GROUP BY to allow just one of these
      -- to be picked arbitrarily.
      a3(path, cardinality, end_node) as (
        select
          a1.path,
          cardinality,
          end_node
        from a1 inner join a2 using(cardinality, end_node)),

      -- Pick just one path among the possibly several minimum cardinality paths
      -- to each end_node.
      a4(path, cardinality, end_node) as (
        select
          min(path),
          cardinality,
          end_node
        from a3 group by cardinality, end_node)

    -- Finally, pick up the actual path to each arbitrily selected end_node to which the
    -- path has the minimum cardinality. No need to include the end_node column now because
    -- it is anyway shown as the final node in the path.
    insert into ?out_tab(path)
    select path
    from a4 inner join a1 using(path, cardinality, end_node)';
begin
  case append
    when false then execute 'delete from '||out_tab;
    else            null;
  end case;
  execute replace(replace(stmt, '?in_tab', in_tab), '?out_tab', out_tab);
end;
$body$;
```

## Create a procedure to restrict a set of paths to leave only a longest path to each distinct terminal node

This procedure is derived trivially from the procedure _["restrict_to_shortest_paths()"](#cr-restrict-to-shortest-paths-sql)_ by just a few obvious substitutions: _"longest"_ for _"shortest"_, _"maximum"_ for _"minimum"_ , and _"max()"_ for _"min()"_:

##### `cr-restrict-to-longest-paths.sql`

```plpgsql
drop procedure if exists restrict_to_longest_paths(text, text, boolean) cascade;

-- Restrict the paths from the start node to every other node
-- to just the longest path to each distinct node.
create procedure restrict_to_longest_paths(
  in_tab in text, out_tab in text, append in boolean default false)
  language plpgsql
as $body$
declare
  stmt constant text := '
    with
      -- For readability. Define cardinality and end_node as view columns.
      a1(k, cardinality, end_node, path) as (
        select
          k,
          cardinality(path),
          path[cardinality(path)],
          path
        from ?in_tab),

      -- In general, there is more than one path to any end_node. Define the
      -- maximum cardinality for each end_node as a pair of view columns.
      a2(cardinality, end_node) as (
        select
          max(cardinality),
          end_node
        from a1
        group by end_node),

      -- There might still be more than one path to a particular end_node
      -- where each of these has the maximum cardinality. Pick up the path
      -- for each of these following the GROUP BY to allow just one of these
      -- to be picked arbitrarily.
      a3(path, cardinality, end_node) as (
        select
          a1.path,
          cardinality,
          end_node
        from a1 inner join a2 using(cardinality, end_node)),

      -- Pick just one path among the possibly several maximum cardinality paths
      -- to each end_node.
      a4(path, cardinality, end_node) as (
        select
          max(path),
          cardinality,
          end_node
        from a3 group by cardinality, end_node)

    -- Finally, pick up the actual path to each arbitrily selected end_node to which the
    -- path has the maximum cardinality. No need to include the end_node column now because
    -- it is anyway shown as the final node in the path.
    insert into ?out_tab(path)
    select path
    from a4 inner join a1 using(path, cardinality, end_node)';
begin
  case append
    when false then execute 'delete from '||out_tab;
    else            null;
  end case;
  execute replace(replace(stmt, '?in_tab', in_tab), '?out_tab', out_tab);
end;
$body$;
```

## Create a procedure to restrict a set of paths to leave only the set of longest unique paths that jointly contain each member of the starting set

Suppose that the paths _"n1 > n2"_, _"n1 > n2 > n3"_, _"n1 > n2 > n3 > n4"_, and _"n1 > n2 > n3 > n4 > n5"_ were all found by a call like this:

```
call find_paths(start_node => 'n1');
```

See, for example, [`cr-find-paths-with-nocycle-check.sql`](../undirected-cyclic-graph/#cr-find-paths-with-nocycle-check-sql). By construction, all of the paths that it finds start at the node _"n1"_. You can see that the path _"n1 > n2 > n3 > n4 > n5"_ contains each of the shorter paths  _"n1 > n2"_, _"n1 > n2 > n3"_, and _"n1 > n2 > n3 > n4"_. This means that all of the useful information about the paths that have been found, in this example, is conveyed by just the longest containing path. It can sometimes be useful, therefore, to summarize the many results produced by a call to `find_paths()` by listing only the set of unique longest containing paths. The procedure `restrict_to_unq_containing_paths()` finds this set. It relies on the [`@>` built-in array "contains" operator](../../../../datatypes/type_array/functions-operators/comparison/#the-160-160-160-160-and-160-160-160-160-operators-1).

##### `cr-restrict-to-unq-containing-paths.sql`

```plpgsql
drop procedure if exists restrict_to_unq_containing_paths(text, text, boolean) cascade;

-- Filter the input set of paths to set of longest paths
-- that jointly contain all the other paths.
create procedure restrict_to_unq_containing_paths(
  in_tab in text, out_tab in text, append in boolean default false)
  language plpgsql
as $body$
declare
  stmt constant text := '
    with
      -- Cartesian product restricted to give all possible
      -- longer path with shorter path combinations.
      each_path_with_all_shorter_paths as (
        select a1.path as longer_path, a2.path as shorter_path
        from ?in_tab as a1, ?in_tab as a2
        where cardinality(a1.path) > cardinality(a2.path)),

      -- Identify each shorter path that is contained by
      -- its longer path partner.
      contained_paths as (
        select
        shorter_path as contained_path
        from each_path_with_all_shorter_paths
        where longer_path @> shorter_path)

    -- Filter out the contained paths.
    insert into ?out_tab(path)
    select path
    from ?in_tab
    where path not in (
      select contained_path from contained_paths
      )';
begin
  case append
    when false then execute 'delete from '||out_tab;
    else            null;
  end case;
  execute replace(replace(stmt, '?in_tab', in_tab), '?out_tab', out_tab);
end;
$body$;
```

## Create a table function to list paths from the table of interest

The table function _"list_paths()"_ achieves what you could achieve with an ordinary top-level SQL statement if you edited it before each execution to  use the name of the target table of interest. This is a canonical use case for dynamic SQL.

[GitHub issue #3286](https://github.com/yugabyte/yugabyte-db/issues/3286) prevents you using dynamic SQL for a `SELECT` statement that returns more than one row. The function [`array_agg()`](../../../../datatypes/type_array/functions-operators/array-agg-unnest/#array-agg) enables a simple workaround.

Notice the use of "translate" and "replace" on the text literal representation of a `text[]` value to produce a nice human-readable path display. This crude approach is sufficient only when the individual `text` array elements have no interior commas, curly braces, or double-quotes—as is the case with the names of actors. A robust approach needs to start with the proper `text[]` value and step along using, say, a `FOREACH` loop to assign each element in turn to a `text` variable. This technique is used in the [`cr-decorated-paths-report.sql`](../../bacon-numbers/#cr-decorated-paths-report-sql) script.


#####  `cr-list-paths.sql`

```plpgsql
drop function if exists list_paths(text) cascade;

create function list_paths(tab in text)
  returns table(t text)
  language plpgsql
as $body$
declare
  -- Recall that when you address an array element that falls outside of its bounds,
  -- you get a `NULL` result. And recall that `NULLS FIRST` is the default sorting order.
  stmt constant text := $$
    with a(r, c, p) as (
      select
        row_number() over w,
        cardinality(path),
        path
      from ?
      window w as
        (order by path[1], cardinality(path), path[2], path[3], path[4], path[5], path[6]))

    select
      array_agg(
        lpad(r::text,  6) ||'   '||
        lpad(c::text, 11) ||'   '||
        replace(translate(p::text, '{}', ''), ',', ' > ')
        order by p[1], cardinality(p), p[2], p[3], p[4], p[5], p[6])
    from a
    $$;

  results text[] not null := '{}';
begin
  t := 'path #   cardinality   path'; return next;
  t := '------   -----------   ----'; return next;

  execute replace(stmt, '?', tab)  into results;
  foreach t in array results loop
    return next;
  end loop;
end;
$body$;
```

## Create a procedure to assert that the contents of the "shortest_paths" and the "raw_paths" tables are identical.

In some cases, the result from one of the implementations of the procedure that finds all the paths from a specified start node finds only the shortest paths. In these cases, the output of the procedure _["restrict_to_shortest_paths()"](#cr-restrict-to-shortest-paths-sql)_ is identical to its input. The following procedure tests that this is the case.

##### `cr_assert-shortest-paths-same-as-raw-paths.sql`

```plpgsql
drop procedure if exists assert_shortest_paths_same_as_raw_paths() cascade;

create procedure assert_shortest_paths_same_as_raw_paths()
  language plpgsql
as $body$
declare
  n int not null := 999999;
begin
  with
    restricteded_except_raw as (
      select path
      from shortest_paths
      except
      select path
      from raw_paths),

    raw_except_restricted as (
      select path
      from raw_paths
      except
      select path
      from shortest_paths),

    filtered_except_raw_union_raw_except_filtered as (
      select path
      from restricteded_except_raw
      union all
      select path
      from raw_except_restricted)

  select
    count(*) into n
  from filtered_except_raw_union_raw_except_filtered;

  assert n = 0, 'unexpected';
end;
$body$;
```

## Create a procedure and a function for measuring elapsed wall-clock time

Create the script `t.sql` with this content:

```plain
\o t-o.txt

select 'Selecting some text';

\timing on

do $body$
begin
  perform pg_sleep(1.0);
  raise info 'done sleeping';
end;
$body$;

\timing off
\echo Echoing some text

\o
\q
```

Now invoke it from the operating system prompt like this (replacing _"demo"_ and _"u1"_ with the names of your test database and test user):

```plain
ysqlsh -h localhost -p 5433 -d demo -U u1 < t.sql 1> t-1.txt 2> t-2.txt
```

This invokes `t.sql` and sends the output from the `SELECT` statement to the `t-o.txt` spool file, the output from the `timing off` and `\echo` metacommands to `t-1.txt`, and the output from `raise info` to `t-2.txt`.

This outcome is no good when you want a coherent report. Moreover, it turns out that if you send the `1>` and `2>` redirects to the same `t-o.txt` spool file, then you get a surprising outcome: the three streams are interleaved in an unhelpful order, thus:

```
INFO:  done sleeping
     
--------------------------
 Selecting some text

Time: 1006.902 ms (00:01.007)
Echoing some text
```

Notice, too, that the caption for the `SELECT` output has vanished.

Here is the best practical approach to producing a coherent report:

- Program your own stopwatch explicitly when you do systematic timing tests.
- Program a function (and especially a table function) from which you can `SELECT` to produce output from a PL/pgSQL execution.

The explicitly programmed stopwatch needs to read the wall-clock time. Here's how to do it:

```plpgsql
select extract(epoch from clock_timestamp());
```

This returns the number of seconds, as a _double precision_ value, since the start of the so-called epoch—_00:00_ on _1-Jan-1970_. Tautologically, _extract(epoch...)_ from this moment returns zero:

```plpgsql
select (
    extract(epoch from '1970-01-01 00:00:00 UTC'::timestamptz) =
    0::double precision
  )::text;
```

The result is _true_. Notice that though the _clock_timestamp()_ built-in function returns a _timestamptz_ value, the value that _extract(epoch from timestamptz_value)_ returns is unaffected by the session's _timezone_ setting. This is explained in the section [The plain timestamp and timestamptz data types](../../../../datatypes/type_datetime/date-time-data-types-semantics/type-timestamp/).

The stopwatch also needs to use a memo for noting the wall-clock time when it's started. While a (temporary) table would work, this would bring an installation-time nuisance cost and a Heisenberg effect. It's better, therefore, to implement the memo as a user-defined run-time parameter:

```plpgsql
do $body$
begin
  execute 'set stopwatch.start_time to '''||extract(epoch from clock_timestamp())::text||'''';
end;
$body$;
```

This looks rather cumbersome. But the operand of the `SET` statement's `TO` keyword can only be a literal.

It's up to you to adopt a naming convention so that no other components of your overall application interfere with the _"stopwatch"_ memo.

Then, later, you can read the stopwatch like this:

```plpgsql
select to_char(
  (
    extract(epoch from clock_timestamp()) -
    current_setting('stopwatch.start_time')::double precision
  ),
  '9999.999')
  as "elapsed seconds";
```

Of course, it's best to encapsulate starting and reading the stopwatch. The bulk of the code formats the elapsed time for best human readability by taking account of the magnitude of the value—just as the output after setting `\timing on` is formatted.

##### `cr-stopwatch.sql`

```plpgsql
drop procedure if exists start_stopwatch() cascade;

create procedure start_stopwatch()
  language plpgsql
as $body$
declare
  -- Make a memo of the current wall-clock time as (real) seconds
  -- since midnight on 1-Jan-1970.
  start_time constant text not null := extract(epoch from clock_timestamp())::text;
begin
  execute 'set stopwatch.start_time to '''||start_time||'''';
end;
$body$;

drop function if exists fmt(numeric, text) cascade;
drop function if exists fmt(int, text) cascade;

create function fmt(n in numeric, template in text)
  returns text
  stable
language plpgsql
as $body$
begin
  return ltrim(to_char(n, template));
end;
$body$;

create function fmt(i in int, template in text)
  returns text
  stable
language plpgsql
as $body$
begin
  return ltrim(to_char(i, template));
end;
$body$;

drop function if exists duration_as_text(numeric) cascade;

create function duration_as_text(t in numeric)
  returns text
  stable
language plpgsql
as $body$
declare
  ms_pr_sec         constant numeric not null := 1000.0;
  secs_pr_min       constant numeric not null := 60.0;
  mins_pr_hour      constant numeric not null := 60.0;
  secs_pr_hour      constant numeric not null := mins_pr_hour*secs_pr_min;
  secs_pr_day       constant numeric not null := 24.0*secs_pr_hour;

  confidence_limit  constant numeric not null := 0.02;
  ms_limit          constant numeric not null := 5.0;
  cs_limit          constant numeric not null := 10.0;

  result                     text    not null := '';
begin
  case
    when t < confidence_limit then
      result := 'less than ~20 ms';

    when t >= confidence_limit and t < ms_limit then
      result := fmt(t*ms_pr_sec, '9999')||' ms';

    when t >= ms_limit and t < cs_limit then
      result := fmt(t, '90.99')||' ss';

    when t >= cs_limit and t < secs_pr_min then
      result := fmt(t, '99.9')||' ss';

    when t >= secs_pr_min and t < secs_pr_hour then
      declare
        ss   constant numeric not null := round(t);
        mins constant int     not null := trunc(ss/secs_pr_min);
        secs constant int     not null := ss - mins*secs_pr_min;
      begin
        result := fmt(mins, '09')||':'||fmt(secs, '09')||' mi:ss';
      end;

    when t >= secs_pr_hour and t < secs_pr_day then
      declare
        mi    constant numeric not null := round(t/secs_pr_min);
        hours constant int     not null := trunc(mi/mins_pr_hour);
        mins  constant int     not null := round(mi - hours*mins_pr_hour);
      begin
        result := fmt(hours, '09')||':'||fmt(mins,  '09')||' hh:mi';
      end;

    when t >= secs_pr_day then
      declare
        days  constant int     not null := trunc(t/secs_pr_day);
        mi    constant numeric not null := (t - days*secs_pr_day)/secs_pr_min;
        hours constant int     not null := trunc(mi/mins_pr_hour);
        mins  constant int     not null := round(mi - hours*mins_pr_hour);
      begin
        result := fmt(days,  '99')||' days '||
                  fmt(hours, '09')||':'||fmt(mins,  '09')||' hh:mi';
      end;
  end case;
  return result;
end;
$body$;

drop function if exists stopwatch_reading_as_dp() cascade;

create function stopwatch_reading_as_dp()
  returns double precision
  -- It's critical to use "volatile" because "clock_timestamp()" is volatile.
  -- "volatile" is the default. Spelled out here for self-doc.
  volatile
language plpgsql
as $body$
declare
  start_time  constant double precision not null := current_setting('stopwatch.start_time');
  curr_time   constant double precision not null := extract(epoch from clock_timestamp());
  diff        constant double precision not null := curr_time - start_time;
begin
  return diff;
end;
$body$;

drop function if exists stopwatch_reading() cascade;

create function stopwatch_reading()
  returns text
  -- It's critical to use "volatile" because "stopwatch_reading()" is volatile.
  -- "volatile" is the default. Spelled out here for self-doc.
  volatile
language plpgsql
as $body$
declare
  t constant text not null := duration_as_text(stopwatch_reading_as_dp()::numeric);
begin
  return t;
end;
$body$;
```

Test the stopwatch like this:

```plpgsl
call start_stopwatch();
select pg_sleep(17.5);
select 'reading after pg_sleep(17.5): '||stopwatch_reading();
```

Here is a typical result:

```
reading after pg_sleep(17.5): 17.5 ss
```

The reported value inevitably suffers from a small client-server round trip delay. But, as this result shows, this is unimportant for readings of a few seconds or longer. More significantly, timings are always subject to a stochastic variability—especially in a distributed SQL database like YugabyteDB.

