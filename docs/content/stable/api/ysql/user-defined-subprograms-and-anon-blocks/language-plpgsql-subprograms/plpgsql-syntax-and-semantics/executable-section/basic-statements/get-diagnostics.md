---
title: The PL/pgSQL "get diagnostics" statement [YSQL]
headerTitle: The PL/pgSQL "get diagnostics" statement
linkTitle: >
  "get diagnostics" statement
description: Describes the syntax and semantics of the PL/pgSQL "get diagnostics" statement. [YSQL].
menu:
  stable:
    identifier: get-diagnostics
    parent: basic-statements
    weight: 20
type: docs
showRightNav: true
---

## Syntax

{{%ebnf%}}
  plpgsql_get_diagnostics_stmt,
  plpgsql_diagnostics_item,
  plpgsql_diagnostics_item_name
{{%/ebnf%}}

## Semantics

The PL/pgSQL _get diagnostics_ statement is typically used, for tracing, during the development process. It's likely, here, that you'd use _raise info_ to display the information that it returns.

The _get diagnostics_ syntax specifies three separate run-time facts—all or some of which can, optionally, be read at once:

- _pg_context_: This reports the subprogram call stack from the top-level server call through the execution of the _get diagnostics_ statement itself.
- _row_count_: This reports the number of rows processed by the most recent SQL DML statement invoked by the subprogram or _do_ statement from whose code _get diagnostics_ is invoked. (DML statements invoked from code that the present subprogram or _do_ statement might invoke are not considered.)
- _result_oid_: This is useful only after an _insert_ statement, (nominally) the OID of the most-recently inserted row.

{{< note title="The returned value for 'result_oid' is always zero." >}}
The syntax is accepted; but the returned value for the _result_oid_ item is always zero. The PostgreSQL documentation says that you get a non-zero result only when the _create table_ statement for target table for the insert uses the _with oids_ clause. But YSQL does not support this. (The attempt causes the _0A000_ error: _"OIDs are not supported for user tables."_)
{{< /note >}}

Typically, _get diagnostics_ will be invoked in the executable section. But it might be usefully invoked in a handler in the exception if this executes a DML statement.

Further information about the outcome of the most recent SQL DML statement, that a subprogram or _do_ statement has processed, is provided by the special, implicitly declared and automatically populated, _boolean_ variable _found_. This variable is always _false_ when evaluated in the declaration section, or by the first statement in the executable section, in the top-level block statement of the subprogram or _do_ statement that issues the DML statements. Later, it will be _true_ if the most recent SQL DML statement affected at least one row. (Just like the value that _get diagnostics_ returns for _row_count_, the value of _found_ is not affected by DML statements issued from code that the present subprogram or _do_ statement might invoke.)

## Example

First, create the function _s.f3()_ that inserts some rows into the table _s.t_ and then invokes _get diagnostics_. It returns a value of the user-defined composite type _s.diagnostics_. This has three attributes:

- _found_: for the value of the special variable _found_
- _rows_: for the value from the _get diagnostics_ invocation's _row_count_
- _ctx_: for the value from the _get diagnostics_ invocation's _pg_context_

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create type s.diagnostics as(found boolean, rows bigint, ctx text);
create table s.t(k int primary key, v int not null);

create function s.f3(lo in int, hi in int)
  returns s.diagnostics
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  diags s.diagnostics;
begin
  insert into s.t(k, v)
  select a.v, a.v*10 from generate_series(lo, hi) as a(v);

  diags.found := found;
  get diagnostics
    diags.rows := row_count,
    diags.ctx  := pg_context;
  return diags;
end;
$body$;
```

Now create the function _s.f2()_ that invokes _s.f3()_ and returns the value of _s.diagnostics_ that _s.f3()_ returns. And, in the same way, create the function _s.f1()_ that invokes _s.f2()_ and returns the value of _s.diagnostics_ that _s.f2()_ returns. The purpose of these two functions is simply to ensure that there's a call stack of some depth above the invocation of _get diagnostics_ so as best to demonstrate the usefulness of the returned value of _pg_context_.

```plpgsql
create function s.f2(n in int)
  returns s.diagnostics
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  lo constant int not null := 7;
  hi constant int not null := lo + n - 1;
begin
  return s.f3(lo, hi);
end;
$body$;

create function s.f1()
  returns s.diagnostics
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  return s.f2(5);
end;
$body$;
```

Finally, create the table function _s.f0()_ that invokes _s.f1()_ and displays the values of the attributes of _s.diagnostics_ that are seen in _s.f3()_:

```plpgsql
create function s.f0()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  diags  s.diagnostics;
  rows   bigint not null := 0;
begin
  -- s.f1() invokes s.f2().
  -- s.f2() invokes s.f3()
  -- s.f3() does the "insert".
  diags := s.f1();
  
  -- Sanity checks.
  get diagnostics rows := row_count;
  assert rows = 0;
  assert not found;

  z := 'diags.found:  '||diags.found::text;                 return next;
  z := 'diags.rows:   '||diags.rows::text;                  return next;
  z := '';                                                  return next;
  z := 'diags.ctx:    '||chr(10)||diags.ctx::text;          return next;
end;
$body$;
```

Now test it:

```plpgsql
select s.f0() as "Values seen in s.f3().";
```

This is the result:

```output
                      Values seen in s.f3().                       
-------------------------------------------------------------------
 diags.found:  true
 diags.rows:   5
 
 diags.ctx:                                                       +
 PL/pgSQL function s.f3(integer,integer) line 9 at GET DIAGNOSTICS+
 PL/pgSQL function s.f2(integer) line 6 at RETURN                 +
 PL/pgSQL function s.f1() line 3 at RETURN                        +
 PL/pgSQL function s.f0() line 9 at assignment
```

You might like to select the rows from _s.t_ as a further sanity check. This is the result:

```output
 k  |  v  
----+-----
  7 |  70
  8 |  80
  9 |  90
 10 | 100
 11 | 110
```

