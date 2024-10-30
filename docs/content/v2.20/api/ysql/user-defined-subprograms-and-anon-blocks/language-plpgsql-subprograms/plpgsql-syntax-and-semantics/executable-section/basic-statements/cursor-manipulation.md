---
title: Cursor manipulation in PL/pgSQL [YSQL]
headerTitle: Cursor manipulation in PL/pgSQL—the "open", "fetch", and "close" statements
linkTitle: Cursor manipulation
description: Describes the syntax and semantics of the PL/pgSQL "open", "fetch", and "close" statements. [YSQL].
menu:
  v2.20:
    identifier: cursor-manipulation
    parent: basic-statements
    weight: 50
type: docs
showRightNav: true
---

## Syntax

{{%ebnf%}}
  plpgsql_regular_declaration,
  plpgsql_bound_refcursor_declaration,
  plpgsql_open_cursor_stmt,
  declare,
  plpgsql_fetch_from_cursor_stmt,
  plpgsql_close_cursor_stmt
{{%/ebnf%}}

## Semantics
The background understanding that you need in order to manipulate cursors in PL/pgSQL is common to both PL/pgSQL and top-level SQL. It's explained in the dedicated **[Cursors](../../../../../../cursors/)** section. Notice that it begins by **[calling out](../../../../../../cursors/#beware-issue-6514)** the fact that, while **[Issue #6514](https://github.com/yugabyte/yugabyte-db/issues/6514)** is open, cursor functionality in YSQL is limited with respect to what vanilla PostgreSQL exposes. In particular, an attempt to use the [_move_ statement](../../../../../../syntax_resources/grammar_diagrams/#plpgsql-move-in-cursor-stmt) in the PL/pgSQL source code of a subprogram causes the _create procedure_ or _create function_ attempt to fail—so that you never get as far as seeing the run-time error that the corresponding attempt in top-level SQL causes. (The outcome is analogous for a _do_ statement.) Try this in a convenient sandbox database:

```plpgsql
drop schema if exists s cascade;
create schema s;
create table s.t(k int primary key, v text not null);

with c(k) as (select generate_series(97, 101))
insert into s.t(k, v)
select k, chr(k) from c;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  cur constant refcursor not null := 'cur';
begin
  open cur for select k, v from s.t order by k;
  move last in cur;
  close cur;
end;
$body$;
```

The attempt fails with the _0A000_ error thus:

```output
MOVE not supported yet
```

The hint tells you simply to report this by raising an issue—but this is (long) out of date. It should direct you to **[Issue #6514](https://github.com/yugabyte/yugabyte-db/issues/6514)** and ask you to vote to raise its priority.

Because of the current restrictions that Issue #6514 describes, and because of the fact that _fetch all_ is anyway not supported in PL/pgSQL in vanilla PostgreSQL, the only viable cursor operation in PL/pgSQL besides _open_ and _close_ is _fetch next... into_.

{{< note title="Empirical tests show that unsupported 'fetch' variants work in PL/pgSQL without error." >}}
Look for the section entitled "Yet more anomalies: functionality that causes the 0A000 "not supported" error in top-level SQL works fine in PL/pgSQL" in the report for  **[Issue #6514](https://github.com/yugabyte/yugabyte-db/issues/6514)**. The following function illustrates a couple of examples:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create table s.t(k serial primary key, v int not null);
insert into s.t(v) select generate_series(0, 99, 5);

create function s.f()
  returns table(k int, v int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  cur refcursor not null := 'cur';
begin
  open cur scroll for (
    select t.k, t.v
    from s.t
    order by t.k);

  fetch last       in   cur into k, v;  return next;
  fetch absolute 2 from cur into k, v;  return next;
  fetch relative 2 from cur into k, v;  return next;

close cur;
end;
$body$;

select k, v from s.f();
```
Notice that the cursor is opened in _scrollable_ mode. The function is created without error and then runs without error to produce this result:

```
 k  | v  
----+----
 20 | 95
  2 |  5
  4 | 15
```

Try the same test in vanilla PostgreSQL. It produces the identical result there. Nevertheless, you must regard the apparent success of _fetch absolute_ and _fetch relative_ as unsupported. In other words:

- **Use only _fetch next... into_. Yugabyte recommends that you strengthen this recommendation by always ensuring that a PL/pgSQL _refcursor_ is defined as _no scroll_.**
  {{< /note >}}

### Iterating over the rows that a cursor defines using a "query for loop"

See the section **["Query for loop" with a bound refcursor variable](../../compound-statements/loop-exit-continue/query-for-loop/#query-for-loop-with-a-bound-refcursor-variable)**. The code example demonstrates the syntax and shows that it produces the expected results. But notice this at the end:

> ...a *query for loop* with a bound refcursor variable brings very limited added value beyond what a [query for loop with a subquery](../../compound-statements/loop-exit-continue/query-for-loop/#query-for-loop-with-a-subquery) brings. 

### Iterating over the rows that a cursor defines using an "infinite loop"

See the code example at the end of the section **["Infinite loop"](../../compound-statements/loop-exit-continue/infinite-and-while-loops/#infinite-loop-over-cursor-results)**.

### Using the "hard-shell" approach to separate the code that opens a cursor from the code that fetches the rows

YSQL's support for **[cursors](../../../../../../cursors/)**, together with their exposure in PL/pgSQL, bring a special advantage when a query's result set might be vast. In such a case, the best design for client-side code delegates the secure definition of the result set to a user-defined function that returns the name of a cursor that has been opened for the query of interest. This allows successive slices of the total result set to be fetched, one slice at a time, so that the results in each slice can be processed—without restarting the query for each slice.

Compare this with the "offset/limit" approach:

```plpgsql

prepare slice10(int) as
select v from data.t order by v
offset $1
limit 10;

execute slice10( 0);
execute slice10(10);
execute slice10(20);
```

Not only does this penalize performance; it also brings a semantic error if the table is undergoing changes from other concurrent sessions because, when the default (and preferred) isolation level _"read committed"_ is used, successive slices will each see a different committed state of the table.

#### Ensure that the required roles are in place

The approach that this section describes relies on the ideas that the section **[Case study: PL/pgSQL procedures for provisioning roles with privileges on only the current database](../../../../provisioning-roles-for-current-database/)** describes. Run each of the code blocks on that page, in order. (You can do that time and again. You might like to save the code to a single _.sql_ script so that you can start again with a clean slate whenever you want to. (Having said this, and as long as the roles are in place, you can re-run the code blocks that follow, in order, time and again without needing to re-create the roles.)

Notice that the final code block on the referenced page is this:

```plpgsql
\c :db :mgr
set client_min_messages = warning;

call mgr.re_create_role('data',   'd', true);
call mgr.re_create_role('code',   'c', true);
call mgr.re_create_role('api',    'a', true);
call mgr.re_create_role('client', 'k', false);
```

When all of the previous code-blocks on that page have been run, it can be run time and again.

Now define _psql_ variables, in the way that _:db_ and _:mgr_ were defined, for these four roles:

```plpgsql
select :quoted_db||'$data' as data
\gset

select :quoted_db||'$code' as code
\gset

select :quoted_db||'$api' as api
\gset

select :quoted_db||'$client' as client
\gset
```

#### Create the objects that the "data" role owns

This is a minimal example, Its pedagogy its served by just a single table. Do this:

```plpgsql
\c :db :data
set client_min_messages = warning;
drop schema if exists data cascade;
create schema data;
create table data.t(k serial primary key, v text not null);

with c(k) as (select generate_series(65, 65 + 25))
insert into data.t(v)
select chr(k) from c
union
select lower(chr(k)) from c;

call mgr.grant_priv('usage',    'schema', 'data',    'code');
call mgr.revoke_all_from_public('table',  'data.t');
call mgr.grant_priv('select',   'table',  'data.t',  'code');
```

Notice that this code block, and each of those that follow, starts by dropping and (re)creating single schema that it needs.

#### Create the "security definer" function, "opened_cur_name()" that opens a cursor for the specified query

Do this:

```pkpgsql
\c :db :code
set client_min_messages = warning;
drop schema if exists code cascade;
create schema code;

create function code.opened_cur_name()
  returns text
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $body$
declare
  cur constant refcursor not null := 'cur';
  stmt constant text not null :=
    format('declare %I no scroll cursor without hold for select k, v from data.t order by v', cur);    

  v_statement text not null := '';
  v_is_holdable boolean not null := true;
begin
  begin
    close cur;
  exception when invalid_cursor_name then null;
  end;

  execute stmt;

  select statement, is_holdable
  into strict v_statement, v_is_holdable
  from pg_cursors
  where name = 'cur';

  assert (v_statement = stmt) and (not v_is_holdable);
  return cur::text;
end;
$body$;

call mgr.grant_priv('usage',    'schema',   'code',                 'api');
call mgr.revoke_all_from_public('function', 'code.opened_cur_name'       );
call mgr.grant_priv('execute',  'function', 'code.opened_cur_name', 'api');
```

<a name="when-to-invoke-subprogram-in-ongoing-txn"></a>
Look back at the sections **[The transaction model for top-level SQL statements](../../../../../../txn-model-for-top-level-sql/)** and **[Issuing "commit" in user-defined subprograms and anonymous blocks](../../../../../commit-in-user-defined-subprograms/)**. Notice that the latter section makes the recommendation _almost_ always to invoke a user-defined subprogram using _single statement automatic transaction mode_. This use case demonstrates one of the rare situations where a user-defined subprogram must be invoked within an ongoing explicitly started transaction.

Notice that the code drops and re-creates the needed cursor with the hard-coded name _cur_ and that the top-level SQL statement _declare_  is used to create the cursor as a _non-holdable_ cursor. This is usually the preferred choice. But it means that the cursor will vanish automatically when the transaction in which the function _opened_cur_name()_ is invoked finally ends. But, the aim here is to delegate the opening of the cursor to a dedicated _security definer_ subprogram and then to fetch the rows from it in ordinary client-side code. This means that the client-side code must start the transaction, then invoke _opened_cur_name()_, then fetch from it using top-level SQL, and then close the cursor. This scheme ensures that the client-side code cannot see the table, as is required, but nevertheless can fetch the rows from the result set that has been defined according to the overall application's requirements in manageably small batches.

This is the main (and arguably only) benefit of using a cursor: you can avoid the need ever to completely materialize a vast result set, client-side, before you can start to process the results one at a time.

#### Create the "security definer" wrapper for the function "opened_cur_name()"

This wrapper follows the paradigm that the section **[Case study: PL/pgSQL procedures for provisioning roles with privileges on only the current database](../../../../provisioning-roles-for-current-database/#the-role-provisioning-paradigm)** explains. Do this:

```plpgsql
\c :db :api
set client_min_messages = warning;

drop schema if exists api cascade;
create schema api;

create function api.opened_cur_name()
  returns text
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $body$
begin
  return code.opened_cur_name();
end;
$body$;

call mgr.grant_priv('usage',    'schema',   'api',                 'client');
call mgr.revoke_all_from_public('function', 'api.opened_cur_name'          );
call mgr.grant_priv('execute',  'function', 'api.opened_cur_name', 'client');
```

#### Simulate the client-side use of the function "opened_cur_name()

```plpgsql
\c :db :client
set search_path = api, pg_catalog, pg_temp;

start transaction;
  select opened_cur_name() as cur
  \gset
  \set quoted_cur '\'':cur'\''

  select statement
  from pg_cursors
  where (not is_holdable) and (not is_scrollable) and name = :quoted_cur;

  fetch 5 from :cur;
  fetch 5 from :cur;
  fetch 5 from :cur;

  -- This is now the client-side code's responsibility.
  close :cur;
rollback;
```

This is the result:

```output
                                    statement                                     
----------------------------------------------------------------------------------
 declare cur no scroll cursor without hold for select k, v from data.t order by v

 k  | v 
----+---
 47 | A
 40 | B
 25 | C
  1 | D
 49 | E

 k  | v 
----+---
 52 | F
 13 | G
 28 | H
  3 | I
 15 | J

 k  | v 
----+---
 20 | K
  9 | L
 32 | M
 43 | N
 44 | O
```

While still connected as the _client_ role, issue the SQL statement that defines the cursor by hand:

```plpgsql
select k, v from data.t order by v;
```

You get the error _"permission denied for schema data"_. This dramatically demonstrates that sessions that connect as the _client_ role can fetch only the results from the queries that the specification of the overall application allows.

#### Simulate a use-case where the responsibility for opening the cursor and for fetching from it are separated but where everything is done behind the hard-shell

The larger point is that the code for executing a query and the code for processing the results are to be distributed between two roles. This could be done by encapsulating the code for executing a query within a table function. But the problem, when the result set is vast, is that the entire result set must be materialized first, and only then does the table function deliver this entire result set to the consumer. (Unlike Oracle Database's PL/SQL, PG/pgSQL does not support so-called _pipelined_ table functions.) This is where the on-demand fetching that a cursor supports comes to the rescue.

The consumer is implemented using the _internal_client_ role. And this can use the already-created function, _api.opened_cur_name()_. First, create the _internal_client_ role and allow it to use the function, _api.opened_cur_name()_:

```plpgsql
select :quoted_db||'$mgr' as mgr
\gset

select :quoted_db||'$internal_client' as internal_client
\gset

\c :db :mgr
set client_min_messages = warning;
call mgr.re_create_role('internal_client', 'i', true);

\c :db :api
call mgr.grant_priv('usage',    'schema',   'api',                 'internal_client');
call mgr.grant_priv('execute',  'function', 'api.opened_cur_name', 'internal_client');
```

Now connect as the _internal_client_ role, simulate the consumer code, and run it:

```plpgsql
\c :db :internal_client
set client_min_messages = warning;
drop schema if exists internal_client cascade;
create schema internal_client;

create function internal_client.five_rows_from_cursor(cur in refcursor)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $body$
declare
  v_k int;
  v_v text;
begin
  for j in 1..5 loop
    fetch cur into v_k, v_v;
    exit when not found;
    z := lpad(v_k::text, 5)||': '||v_v; return next;
  end loop;
end;
$body$;

create function internal_client.three_batches()
  returns table(zz text)
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $body$
declare
  cur constant refcursor not null := api.opened_cur_name();
begin
  for j in 1..3 loop
    for zz in (select z from internal_client.five_rows_from_cursor(cur)) loop
      return next;
    end loop;
    zz := '';  return next;
  end loop;
end;
$body$;

select internal_client.three_batches();
```

This is the result:

```output
 three_batches 
---------------
    47: A
    40: B
    25: C
     1: D
    49: E
 
    52: F
    13: G
    28: H
     3: I
    15: J
 
    20: K
     9: L
    32: M
    43: N
    44: O
```

Of course, in a real use, the function _internal_client.three_batches()_ would be replaced by a subprogram that processed all of the rows that the cursor's query defines, using as many batches as needed, in an infinite loop, until no more results were found. The larger point is that native top-level SQL is phenomenally functionally rich. (Consider, for example, aggregate functions and window functions.) But nevertheless, now and then, a use case arises where notwithstanding this, you have to resort to procedural logic to meet the processing requirements.
