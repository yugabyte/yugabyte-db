---
title: Cursors [YSQL]
headerTitle: Cursors
linkTitle: Cursors
description: Explains what a cursor is and how you create and use a cursor with either SQL or PL/pgSQL. [YSQL].
menu:
  v2.18:
    identifier: cursors
    parent: api-ysql
    weight: 50
type: docs
---

{{< warning title="YSQL currently supports only fetching rows from a cursor consecutively in the forward direction." >}}
See the section [Beware Issue #6514](#beware-issue-6514) below.
{{< /warning >}}

<!--- to_do Fix up the x-ref's when targets exist in the PL/pgSQL section for the 'bound refcursor' and open --->

This section explains:

- what a _cursor_ is;
- how you can manipulate a _cursor_ explicitly, when your use case calls for this, using either a SQL API or a PL/pgSQL API.

The SQL API is exposed by the _[declare](../the-sql-language/statements/dml_declare/)_, _[move](../the-sql-language/statements/dml_move/)_, _[fetch](../the-sql-language/statements/dml_fetch/)_, and _[close](../the-sql-language/statements/dml_close/)_ statements. Each of these specifies its _cursor_ using an identifier for the _cursor's_ name.

The functionally equivalent PL/pgSQL API is exposed by the executable statements _open_, _move_, _fetch_, and _close_.  Each of these specifies its _cursor_ using an identifier for the name of a value of the dedicated data type _refcursor_. You can declare a _refcursor_, just as you declare other variables, in the PL/pgSQL source code's _[plpgsql_declaration_section](../syntax_resources/grammar_diagrams/#plpgsql-declaration-section)_. Notice that one flavor of the declaration syntax lets you specify the defining _subquery_ (see below) for the underlying _cursor_ that it denotes. Alternatively,  you can specify the data type of a PL/pgSQL subprogram's formal argument as _refcursor_. The value of a variable or argument whose data type is _refcursor_ is _text_ and is simply the name of the underlying _cursor_ that it denotes.

## What is a cursor?

A _cursor_ is an artifact that you create with the _[declare](../the-sql-language/statements/dml_declare/)_ SQL statement—or with the equivalent PL/pgSQL _open_ statement. A _cursor's_ duration is limited to the lifetime of the session that creates it, it is private to that session, and it is identified by a bare name that must conform to the usual rules for SQL names like those of tables, schemas, and so on. In the sense that it's session-private, that its name isn't schema-qualified, that it has at most session duration, and that it has no explicit owner, it resembles a prepared statement. A session's currently extant _cursors_ are listed in the _[pg_cursors](https://www.postgresql.org/docs/11/view-pg-cursors.html)_ catalog view.

A _cursor_ is defined by a _subquery_ (typically a _select_ statement, but you can use a _values_ statement) and it lets you fetch the rows from the result set that the _subquery_ defines one-at-a-time without (in favorable cases) needing ever to materialize the entire result set in your application's client backend server process—i.e. the process that this query lists:

```plpgsql
select application_name, backend_type
from pg_stat_activity
where pid = pg_backend_pid();
```

{{< note title="The internal implementation of a cursor." >}}
&nbsp;

You don't need to understand this. But it might help you to see how _cursors_ fit into the bigger picture of how  the processing of SQL statements like _select_, _values_, _insert_, _update_, and _delete_ is implemented. (These statements can all be used as the argument of the _prepare_ statement—and for this reason, they will be referred to here as _preparable_ statements.

At the lowest level, the implementation of all of SQL processing is implemented in PostgreSQL using C. And YSQL uses the same C code. The execution of a preparable statement uses C structure(s) that hold information, in the backend server process, like the SQL statement text, its parsed representation, its execution plan, and so on. Further, when the statement is currently being executed, other information is held, like the values of actual arguments that have been bound to placeholders in the SQL text and the position of the current row in the result set (when the statement produces one). You can manipulate these internal structures, from client side code, using the _[libpq - C Library](https://www.postgresql.org/docs/11/libpq.html)_ API or, at a level of abstraction above that, the _[Embedded SQL in C](https://www.postgresql.org/docs/11/ecpg.html)_ API (a.k.a. the _ECPG_). Moreover, engineers who implement PostgreSQL itself can use the [Server Programming Interface](https://www.postgresql.org/docs/current/spi.html). These APIs have schemes that let the programmer ask for the entire result set from a _subquery_ in a single round trip. And they also have schemes that let you ask for the result set row-by-row, or in batches of a specified size. See, for example, the _libpq_ subsection [Retrieving Query Results Row-By-Row](https://www.postgresql.org/docs/11/libpq-single-row-mode.html).

In a more abstract, RDBMS-independent, discussion of the SQL processing of the statements that correspond to PostgreSQL's preparable statements, the term "cursor" is used to denote these internal structures. (You'll notice this, for example, with Oracle Database.) 

But in PostgreSQL, and therefore in YSQL, a _cursor_ is a direct exposure into SQL and PL/pgSQL (as _language features_) of just a _subset_ of the internal mechanisms for the processing of preparable statements: the _values_ statement and the _select_ statement when it has no data-modifying side-effects.

Here's a counter-example where the _select_ statement _does_ modify data. You can begin a _select_ statement using a [_with_ clause](../the-sql-language/with-clause/) that can include a data-modifying statement like _insert_ as long as it has a _returning_ clause. (This ability lets you implement, for example, multi-table insert.) First create a temporary table as the destination for an _insert_:

```plpgsql
create table pg_temp.count(n int not null);
```

Prepare and execute a _subquery_ thus:

```plpgsql
prepare stmt as
with
  c(n) as (
    insert into pg_temp.count(n)
    select count(*)
    from pg_class
    where relkind = 'v'
    returning n)
select relname from pg_class where relkind = 'v';

execute stmt;
```

This works fine—so far. It shows lots of view names and populates the _pg_temp.count_ table with the count. Now try to declare a cursor using the same _subquery_:

```plpgsql
declare cur cursor for
with
  c(n) as (
    insert into pg_temp.count(n)
    select count(*)
    from pg_class
    where relkind = 'v'
    returning n)
select relname from pg_class where relkind = 'v';
```

It fails with the _0A000_  error: _DECLARE CURSOR must not contain data-modifying statements in WITH_.
{{< /note >}}

Tautologically, then, a _cursor_ is an artifact that is characterized thus:

- It is created with the _declare_ SQL statement (or the equivalent _open_ PL/pgSQL statement).
- Its maximum duration is _either_ the session in which it's created _or_ the transaction in which it's created, according to a choice that you make when you create it.
- Its lifetime can be terminated deliberately with the _close_ SQL statement or the same-spelled PL/pgSQL statement.
- It is defined by its name, its _subquery_ and some other boolean attributes.
- Its name and its other attributes are listed in the _pg_cursors_ catalog view.
- It lets you fetch consecutive rows, either one at a time, or in batches whose size you choose, from the result set that its _subquery_ defines;
- It supports the _move_ SQL statement, and the same-spelled PL/pgSQL statement, to let you specify any row, by its position, within the result set as the current row.
- It supports the _fetch_ SQL statement, and the same-spelled PL/pgSQL statement, that, in one variant, lets you fetch the row at the current position or a row at any other position relative to the current position (_either_ ahead of it _or_ behind it).
- It supports another variant of the _fetch_ statement (but here only in SQL) that lets you fetch  a specified number of rows _either_ forward from and including the row immediately after the current position _or_ backward from and including the row immediately before the current position.

The "current position" notion is defined by imagining that the cursor's defining _subquery_ always includes this _select_ list item:

```plpgsql
row_number() over() as current_position
```

Here, _over()_ spans the entire result set. If the overall query has no _order by_ clause, then _over()_ has no such clause either. But if the overall query does have an _order by_ clause, then _over()_ has the same _order by_ clause within its parentheses.

When you execute the _move_ statement, the current position is left at the result to which you moved. And execute the _fetch_ statement (fetching one or several rows in either the forward or the backward direction) the current position is left at the last-fetched result.

## Simple demonstration

Set the _psql_ variables _db_ and _u_ to, respectively, a convenient sandbox database and a convenient test role that has _connect_ and _create_ on that database. Then create a trivial helper that will delay the delivery of each row from a _subquery_'s result set:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;

create function s.sleep_one_second()
  returns boolean
  set search_path = pg_catalog, pg_text
  language plpgsql
as $body$
begin
  perform pg_sleep(1.0);
  return true;
end;
$body$;
```

Use it to define a view whose result set is ten rows:

```plpgsql
create view s.ten_rows(v) as
select a.v from generate_series(1, 10) as a(v) where s.sleep_one_second();
```

Now execute a query in the obvious way at the _ysqlsh_ prompt:

```plpgsql
select v from s.ten_rows;
```

It takes about ten seconds before you see any results—and then you see all ten rows effectively instantaneously. (Use the \\_timing on_ meta-command to time it.) In other words, all ten rows were first materialized in the backend server process's memory before being passed, in a single round trip, to the application.

Create another helper function to fetch one row from a _cursor_ and to return its value together with the time taken to fetch it;

```plpgsql
create function s.next_row(cur in refcursor)
  returns text
  set search_path = pg_catalog, pg_text
  language plpgsql
as $body$
declare
  t0  float8 not null := 0;
  t1  float8 not null := 0;
  t   int    not null := 0;
  v   int;
begin
  t0 := extract(epoch from clock_timestamp());
  fetch next  from cur into v;
  t1 := extract(epoch from clock_timestamp());

  t := (round(t1 - t0)*1000.0)::int;
  return rpad(coalesce(v::text, '<no row>'), 9)||'-- '||to_char(t, '9999')||' ms';
end;
$body$;
```

Now use a _cursor_ to fetch the ten rows one by one:

```plpgsql
\t on
start transaction;
  declare "My Cursor" no scroll cursor without hold for
  select v from s.ten_rows;

  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');
  select s.next_row('My Cursor');

rollback;
\t off
```

Now you see the rows delivered one by one, every second. This is the result. (Blank lines were removed manually.)

```output
 1        --  1000 ms
 2        --  1000 ms
 3        --  1000 ms
 4        --  1000 ms
 5        --  1000 ms
 6        --  1000 ms
 7        --  1000 ms
 8        --  1000 ms
 9        --  1000 ms
 10       --  1000 ms
 <no row> --     0 ms
```

When you execute _"select v from ten_rows"_ ordinarily using _ysqlsh_, you have to wait until the entire result set has been materialized in the memory of its backend server process before it's delivered to the client application as a unit. This incurs a memory usage cost as well as a time-delay irritation. But when you declare a _cursor_ for that _select_ statement, you materialize the results one row at a time and deliver each to the client as soon as its available. When you use this approach, no more than a single row needs ever to be concurrently materialized in the backend server process's memory.

In real applications, you'll use the piecewise result set delivery that a _cursor_ supports only when the result set is vast; and you'll fetch it in batches of a suitable size: small enough that the backend server process's memory isn't over-consumed; but large enough that the round-trip time doesn't dominate the overall cost of fetching a batch. 

## Transactional behavior — holdable and non-holdable cursors

A cursor can be declared either as so-called _holdable_—or not. See the account of the _with hold_ or _without hold_ choice in the section for the _[declare](../the-sql-language/statements/dml_declare/)_ statement. Try this:

```plpgsql
\c :db :u
select count(*) from pg_cursors;

start transaction;
  declare "Not Holdable" cursor without hold for select 17;
  declare "Is Holdable"  cursor with    hold for select 42;
  select name, is_holdable::text from pg_cursors order by name;
commit;
select name, is_holdable::text from pg_cursors order by name;

\c :db :u
select count(*) from pg_cursors;
```

An invocation of _"select count(*) from pg_cursors"_, immediately after starting a session, will inevitably report that no _cursors_ exist. The first _pg_cursors_ query (within the ongoing transaction) produces this result:

```output
     name     | is_holdable 
--------------+-------------
 Is Holdable  | true
 Not Holdable | false
```

And the _pg_cursors_ query immediately after committing the transaction produces this result:

```output
     name     | is_holdable 
--------------+-------------
 Is Holdable  | true
```

In other words, a _non-holdable_ _cursor_ will vanish when the transaction within which it was declared ends—even if the transaction is committed. Because a _non-holdable_ _cursor_ cannot exist outside of an ongoing transaction, this attempt:

```plpgsql
declare "Not Holdable" cursor without hold for select 17;
```

causes the _25P01_ error: _DECLARE CURSOR can only be used in transaction blocks_. The wording is slightly confusing because this causes no such error:

```output
declare "Is Holdable"  cursor with    hold for select 42;
```

See the section [The transaction model for top-level SQL statements](../txn-model-for-top-level-sql/). The assumption is that you're running _ysqlsh_ with the default setting of _'on'_ for the _psql_ variable _AUTOCOMMIT_.

Notice that the transactional behavior of a _cursor_ differs critically from that of a prepared statement:

```plpgsql
\c :db :u
select count(*) from pg_prepared_statements;

start transaction;
  prepare stmt as select 42;
rollback;
select name from pg_prepared_statements;
```

Like is the case for _cursors_, an invocation of _"select count(*) from pg_prepared_statements"_, immediately after starting a session, will inevitably report that no prepared statements exist. But even when a statement is prepared within a transaction that is rolled back, it continues to exist after that until either the session ends or it is _deallocated_. (If you create a _holdable cursor_, within an on going transaction and then roll back the transaction, then it vanishes.)

{{< tip title="Open a holdable cursor in its own transaction and close it as soon as you have finished using it." >}}
When, as is the normal practice, you don't subvert the behavior that automatically commits a SQL statement that is not executed within an explicitly started transaction, you'll probably _declare_,  _move in_ and _fetch from_ a _holdable cursor_ "ordinarily"—i.e. without explicitly starting, and ending, transactions.

A _holdable cursor_ consumes resources because it always caches its defining _subquery_'s entire result set. Therefore (and especially in a connection-pooling scheme, you should close a _holdable cursor_ as soon as you have finished using it.
{{< /tip >}}

{{< note title="A holdable cursor is most useful when you intend to move or to fetch in the backward direction — but YSQL does not yet support this." >}}
See the section [Beware Issue #6514](#beware-issue-6514) below.
{{< /note >}}

## Scrollable cursors

When you choose, at _cursor_ creation time, either the _scroll_ or the _no scroll_ options, the result of your choice is shown in the _is_scrollable_ column in the _pg_cursors_ view. Try this:

```plpgsql
start transaction;
  declare "Not Scrollable" no scroll cursor without hold for
    select g.v from generate_series(1, 5) as g(v);
  declare "Is Scrollable"     scroll cursor without hold for
    select g.v from generate_series(1, 5) as g(v);

  select name, is_scrollable::text from pg_cursors order by name;
rollback;
```

This is the result:

```output
      name      | is_scrollable 
----------------+---------------
 Is Scrollable  | true
 Not Scrollable | false
```

The term of art _scrollable_ reflects a rather unusual meaning of scrollability. In, for example, discussions about GUIs, scrolling means moving forwards or backwards within a window or, say, a list. However:

- When _pg_cursors.is_scrollable_ is _false_, this means that you can change the current position in the _cursor_'s result set (using either _move_ or as a consequence of _fetch_) only in the _forward_ direction.
- When _pg_cursors.is_scrollable_ is _true_, this means that you can change the current position in the _cursor_'s result set both in the _forward_ direction and in the _backward_ direction.

In other words:

- When you create a cursor and specify _no scroll_, you're saying that you will allow changing the current position in the result set in only the _forward_ direction.
- When you create a cursor and specify _scroll_, you're saying that you will allow changing the current position in the result set in both the _forward_ direction and the _backward_ direction.

Notice that your choice with the _move_ statement, to change the current position by just a single row or by many rows is an orthogonal choice to the direction in which you move. Similarly, your choice with the _fetch_ statement, to fetch just a single row or many rows is an orthogonal choice to the direction in which you fetch. 

- There is no way to create a _cursor_ so that changing the current position in the result set by more than one row, except by consecutive fetches, is prevented.

<a name="specify-no-scroll-or-scroll-explicitly"></a>
{{< tip title="Always specify either 'no scroll' or 'scroll' explicitly." >}}
If you specify neither _no scroll_ nor _scroll_ when you create a _cursor_, then you don't get an error. However, the outcome is that sometimes _backwards_ movement in the result set is allowed, and sometimes it causes the _55000_ error: _cursor can only scan forward_.

Yugabyte recommends that you always specify your scrollability choice explicitly to honor the requirements that you must meet. Notice that while [Issue #6514](https://github.com/yugabyte/yugabyte-db/issues/6514) remains open, your only viable choice is _no scroll_.
{{< /tip >}}

### "no scroll" cursor demonstration

Do this:

```plpgsql
set client_min_messages = error;
start transaction;
  declare cur no scroll cursor without hold for
    select g.v from generate_series(1, 5) as g(v);
  fetch next       from cur;
  move relative 2  in   cur;
  fetch forward 2  from cur;
```

So far, this runs without error and produces these results, as expected:

```output
 1

 4
 5
```

Now do this:

```plpgsql
  fetch relative 0 from cur;
rollback;
```

The _fetch_ causes the _55000_ error: _cursor can only scan forward_ because fetching a _cursor_'s current row tautologically does not move the current position forward and is therefore not allowed when you create the _cursor_ using _no scroll_.

### "scroll" cursor demonstration

Now do this:

```plpgsql
set client_min_messages = error;
start transaction;
  declare cur scroll cursor without hold for
    select g.v from generate_series(1, 5) as g(v);

  -- These are the same four statements that the "no scroll" demo used.
  -- But now, "fetch relative 0" succeeds.
  fetch next         from cur;
  move  relative 2   in   cur;
  fetch forward 2    from cur;
  fetch relative 0   from cur;

  -- Add these statements.
  fetch backward     from cur;
  fetch backward all from cur;
  move  last         in  cur;
  fetch relative 0   from cur;
  move  first        in   cur;
  fetch relative 0   from cur;
rollback;
```

Now every _fetch_ and _move_ statement succeeds. These are the results:

```output
 1

 4
 5

 5

 4

 3
 2
 1

 5

 1
```

## Caching a cursor's result set

- The result set for a _with hold_ _cursor_ is always cached when the transaction that creates it commits.

- The result set for a _without hold_ _cursor_ might, or might not, be cached.

  - If the execution plan for the _cursor_'s defining _subquery_ can be executed in either the forward or the backward direction, then the result set will not be cached.

  - But if the plan can be executed only in the forward direction, then the result set must be cached if you specify _scroll_ when you create the cursor.

PostgreSQL, and therefore YSQL, do not expose metadata to report whether or not a _cursor_'s result set is cached. Nor does the documentation for either RDBMS attempt to specify the rules that determine whether caching will be done. However, it's possible to reason, about certain specific _select_ statements, that their plans cannot be executed backward. For example the plan for a query that includes _row_number()_ in the _select_ list cannot be run backward because the semantics of _row_number()_ is to assign an incrementing rank to each new row in the result set as it is produced when the plan is executed in the forward direction—and the planner cannot predict how many rows will be produced to allow _row_number()_ to be calculated by decrementing from this for each successive row when the plan is run backward. (If the _select_ statement has no _order by_, then the rows are produced in _physical order_ (i.e. in an order that's determined by how the table data is stored).

<a name="physical-order-cannot-be-predicted"></a>
{{< note title="The physical order cannot be predicted." >}}
The physical order cannot be predicted; and it might even change between repeat executions of the same _select_ statement. However, if you use a cluster on a developer laptop and ensure that the backend process that supports the session that you use to do the tests is the _only_ process whose type is _client backend_, then it's very likely indeed that successive repeats of the same _select_ statement will produce the same physical order—at least over the timescale of typical _ad hoc_ experiments.
{{< /note >}}

You can, however, support pedagogy by including a user-defined function in the _where_ clause that always returns _true_ and that uses _raise info_ to report when it's invoked.

First, do this set-up. All the caching tests will use it:

```plpgsql
set client_min_messages = error;
drop schema if exists s cascade;
create schema s;

create table s.t(k serial primary key, v int not null);
insert into s.t(v) select generate_series(1, 5);
create view s.v(pos, v) as select row_number() over(), v from s.t;

create function s.f(i in int)
  returns boolean
  set search_path = pg_catalog, pg_temp
  volatile
  language plpgsql
as $body$
begin
  raise info 'f() invoked';
  return i = i;
end;
$body$;
```

### "with hold", "no scroll" cursor

Do this:

```plpgsql
start transaction;
  declare cur no scroll cursor with hold for
    select pos, v from s.v where s.f(v);
```

It completes silently without error. Now do this:


```plpgsql
commit;
```

This is when you see the _raise info_ output—five times in total, i.e. once for each row that's tested:

```output
INFO:  f() invoked
INFO:  f() invoked
INFO:  f() invoked
INFO:  f() invoked
INFO:  f() invoked
```

This demonstrates that the result set has been cached. Now fetch all the rows and close the cursor;

```plpgsql
fetch all from cur;
close cur;
```

The _fetch all_ statement brings this SQL output:

```output
   1 | 5
   2 | 1
   3 | 4
   4 | 2
   5 | 3
```

### "without hold", "no scroll" cursor

Do this:

```
start transaction;
  declare cur no scroll cursor without hold for
    select pos, v from s.v where s.f(v);

    fetch next from cur;
    fetch next from cur;
    fetch next from cur;
    fetch next from cur;
    fetch next from cur;
rollback;
```

It's easier to distinguish the _raise info_ output and the SQL output from the statements that bring these if you save this code to, say, _t.sql_, and then execute it at the _ysqlsh_ prompt. This is what you see. 

```output
INFO:  f() invoked
   1 | 5

INFO:  f() invoked
   2 | 1

INFO:  f() invoked
   3 | 4

INFO:  f() invoked
   4 | 2

INFO:  f() invoked
   5 | 3
```

Notice that the same _v_ values are paired with the same _pos_ values as with the _"with hold", "no scroll" cursor_ test. But here, nothing suggests that results are cached—and they don't need to be because the _cursor_ doesn't allow moving backwards in the result set. (However, you can't design a test to demonstrate that results are not cached.)

### "without hold", "scroll" cursor

Do this:

```
start transaction;
  declare cur scroll cursor without hold for
    select pos, v from s.v where s.f(v);

  -- Moving forward.
  fetch next       from cur;
  fetch next       from cur;
  fetch next       from cur;
  fetch next       from cur;
  fetch next       from cur;

  -- Moving backward.
  fetch prior      from cur;
  move  absolute 3 in   cur;
  fetch relative 0 from cur;
  move  absolute 1 in   cur;
  fetch relative 0 from cur;
rollback;
```

Apart from replacing _no scroll_ with _scroll_ in the declaration of the _cursor_, the code is identical, through all the _"Moving forward_ fetches, to that for the _"without hold", "no scroll" cursor_ test. And the output is identical, too.

Now continue with the tests that move backwards: You no longer see any _raise info_ output because the entire result set is now cached. Here are the SQL results:

```output
   4 | 2

   3 | 4

   1 | 5
```

Notice that you see the same _v_ values for the same _pos_ values as before. This is required by the semantics of moving forward and backward through the unique result set that the _cursor_'s subquery defines.

### "without hold", "scroll" cursor — variant

The output from the previous test shows that the result set is cached incrementally. This variant demonstrates it more vividly by interleaving forward and backward fetches and moves. First, increase the number of rows in _s.t_ and inspect _s.v_:

```plpgsql
delete from s.t;
insert into s.t(v) select generate_series(1, 10);
select pos, v from s.v;
```

This is the result:

```output
   1 |  6
   2 |  8
   3 |  7
   4 | 10
   5 |  1
   6 |  2
   7 |  4
   8 |  5
   9 |  3
  10 |  9
```

You might see the _v_ values in a different order. See the note [The physical order cannot be predicted](#physical-order-cannot-be-predicted) above.

Now do this:

```plpgsql
start transaction;
  declare cur scroll cursor without hold for
    select pos, v from s.v where s.f(v);
```

This succeeds silently without error. Now do this:

```plpgsql
  fetch forward  3 from cur;
  fetch backward 2 from cur;
```

It causes this output:

```output
INFO:  f() invoked
INFO:  f() invoked
INFO:  f() invoked
   1 | 6
   2 | 8
   3 | 7

   2 | 8
   1 | 6
```

We don't see any _raise info_ output when we fetch the rows with _pos = 2_ and _pos = 1_ again because they're already cached. Now do this:

```plpgsql
  fetch forward  5 from cur;
  fetch backward 3 from cur;
```

It causes this output:

```
INFO:  f() invoked
INFO:  f() invoked
INFO:  f() invoked
   2 |  8
   3 |  7
   4 | 10
   5 |  1
   6 |  2

   5 |  1
   4 | 10
   3 |  7
```

Because we're now seeing the three rows with _pos = 4_, _pos = 5_, and _pos = 6_ for the first time, we see the _raise info_ output three times. Then, when we revisit the rows with _pos = 5_, _pos = 4_, and _pos = 3_, we don't see any _raise info_ output because, again, they're already cached. Now do this:

```plpgsq
  fetch forward  5 from cur;
  fetch backward 1 from cur;
```

It causes this output:

```plpgsql
INFO:  f() invoked
INFO:  f() invoked
   4 | 10
   5 |  1
   6 |  2
   7 |  4
   8 |  5

   7 | 4
```

Here, we're seeing the rows with _pos = 7_ and _pos = 8_ for the first time—which brings the _raise info_ output twice. And then, because we've already (just) seen the row with _pos 7_, there's now no _raise info_ output.

Finally, do this:

```plpgsql
  move  absolute 3 in cur;
  fetch forward  7 from cur;
rollback;
```

It causes this output:

```outpit
INFO:  f() invoked
INFO:  f() invoked
   4 | 10
   5 |  1
   6 |  2
   7 |  4
   8 |  5
   9 |  3
  10 |  9
```

At this stage, we've seen all rows in the result set but those with _pos = 9_ and _pos = 10_, so we see the _raise info_ for just each of those two rows.

## Beware Issue #6514

{{< warning title="YSQL currently supports only fetching rows from a cursor consecutively in the forward direction." >}}
&nbsp;

[Issue 6514](https://github.com/yugabyte/yugabyte-db/issues/6514) tracks the problem that the SQL statements _fetch_ and _move_, together with their PL/pgSQL counterparts, don't work reliably. This is reflected by warnings that are drawn under these circumstances:

- Every _move_ flavor causes the _0A000_ warning with messages like _"MOVE not supported yet"_.

- Many _fetch_ flavors draw the _0A000_ warning with messages like _"FETCH FIRST not supported yet"_, _"FETCH LAST not supported yet"_, _"FETCH BACKWARD not supported yet"_, and the like.

These are the _only_ _fetch_ flavors that do not draw a warning:

- _fetch next_
- bare _fetch_
- _fetch :N_
- bare _fetch forward_
-  _fetch forward :N_
- _fetch all_
- and _fetch forward all_

_:N_ must be a positive integer.

Notice that in many tests, and warning messages notwithstanding, the code has exactly the same effect in YSQL as it does in vanilla PostgreSQL. And if you set the _client_min_messages_ run-time parameter to _error_, then the apparently spurious warnings are suppressed. This is the case for all the tests shown on this page. However, you must not trust this for production code while [Issue 6514](https://github.com/yugabyte/yugabyte-db/issues/6514) remains open.

**You should not suppress warnings, and you should not use any operation that draws the _0A000_  warning.**
{{< /warning >}}