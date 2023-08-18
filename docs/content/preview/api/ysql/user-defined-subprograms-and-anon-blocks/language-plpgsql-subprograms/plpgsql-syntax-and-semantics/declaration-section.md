---
title: PL/pgSQL declaration section [YSQL]
headerTitle: The PL/pgSQL declaration section
linkTitle: Declaration section
description: Describes the syntax and semantics of the PL/pgSQL declaration section. [YSQL].
menu:
  preview:
    identifier: declaration-section
    parent: plpgsql-syntax-and-semantics
    weight: 10
type: docs
showRightNav: true
---

Every identifier that occurs within a PL/pgSQL block statement must be defined. Name resolution of such identifiers is attempted first in the most tightly-enclosing declaration section (if present), and if that fails, in the next most tightly-enclosing declaration section (if present)—and so on up through the outermost declaration section (if present). Only if all of these attempts fail are outer scopes tried. These scopes are first the list of formal arguments (for a subprogram) and then the schemas along the search path. See the section [Name resolution within user-defined subprograms and anonymous blocks](../../../name-resolution-in-subprograms/).

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <img src="/icons/file-lines.svg" alt="Grammar Icon">
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link active" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <img src="/icons/diagram.svg" alt="Diagram Icon">
      Diagram
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="grammar" class="tab-pane fade" role="tabpanel" aria-labelledby="grammar-tab">
  {{% includeMarkdown "../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/plpgsql_declaration,plpgsql_regular_declaration,plpgsql_bound_refcursor_declaration,plpgsql_cursor_arg.grammar.md" %}}
  </div>
  <div id="diagram" class="tab-pane fade show active" role="tabpanel" aria-labelledby="diagram-tab">
  {{% includeMarkdown "../../../syntax_resources/user-defined-subprograms-and-anon-blocks/language-plpgsql-subprograms/plpgsql-syntax-and-semantics/plpgsql_declaration,plpgsql_regular_declaration,plpgsql_bound_refcursor_declaration,plpgsql_cursor_arg.diagram.md" %}}
  </div>
</div>

## Semantics for the "plpgsql_regular_declaration" rule

This rule governs the overwhelmingly common case.

Use any convenient sandbox database and a role that has no special attributes and that has _create_ and _connect_ on that database. Do this first:

```plpgsql
\set db <the sandbox database>
\set u  <the ordinary role>
```

Here's an example. It relies on the **[plpgsql_open_cursor_stmt](../executable-section/basic-statements/cursor-manipulation/#plpgsql-open-cursor-stmt)** and **[plpgsql_fetch_from_cursor_stmt](../executable-section/basic-statements/cursor-manipulation/#plpgsql-fetch-from-cursor-stmt)** statements.

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create table s.t1(k int primary key, v text not null);
create table s.t2(k int primary key, v text not null);
insert into s.t1(k, v) values(1, 'cat');
insert into s.t2(k, v) values(1, 'dog');

create function s.f(x in boolean)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$

-- The declare section of interest.
declare
  a  int;
  b  int not null := 17;
  c  constant text not null := case x
                                 when true then
                                   (select v from s.t1 where k = 1)
                                 else
                                   (select v from s.t2 where k = 1)
                               end;

  r  constant refcursor not null := 'cur';

  rec record;

-- The executable section (sanity check).
begin
  assert (a is null);

  z := 'b: '||b::text;                                                return next;
  z := 'c: '||c::text;                                                return next;

  open r for execute $$
      with c(source, v) as (
          select 'from t1', v from s.t1 where k = 1
          union all
          select 'from t2', v from s.t2 where k = 1
        )
      select source, v from c order by source
    $$;

  loop
    fetch r into rec;
    exit when not found;
    z := 'r: '|| rec.source||' | '||rec.v;                            return next;
  end loop;
  close r;

end;
$body$;

select s.f(true);
```

This is the result:

```output
 b: 17
 c: cat
 r: from t1 | cat
 r: from t2 | dog
```

Notice that this declaration:

```plpgsql
r  constant refcursor not null := 'cur';
```

establishes the variable _r_ as a potential pointer to a _[cursor](../../../../cursors/)_ with the fixed name _cur_. A _refcursor_ variable that is declared in this way (using the _plpgsql_regular_declaration_ syntax) is referred to as an _unbound refcursor variable_. Here, _unbound_ captures the idea that the _open_ executable statement can use an arbitrary _[subquery](../../../../syntax_resources/grammar_diagrams/#subquery)_ that defines a result set whose shape emerges at run-time, thus:

```plpgsql
open r for execute $$
    with c(source, v) as (
        select 'from t1', v from s.t1 where k = 1
        union all
        select 'from t2', v from s.t2 where k = 1
      )
    select source, v from c order by source
  $$;
```

This has the same effect as this top-level _[declare](../../../../the-sql-language/statements/dml_declare/)_ SQL statement:

```plpgsql
declare cur no scroll cursor without hold for
  with c(source, v) as (
      select 'from t1', v from s.t1 where k = 1
      union all
      select 'from t2', v from s.t2 where k = 1
    )
select source, v from c order by source;
```

Notice that the target for the _fetch_ statement, _rec_, is declared as a _record_ so that it can accommodate a result row of any shape. It is the programmer's responsibility to know what the result's column names and data types will be so that the fields (in this example _rec.source_ and _rec.v_) can be referenced correctly.

## Semantics for the "plpgsql_bound_refcursor_declaration" rule

{{< tip title="Make sure that you have read the section 'Cursors' before reading this subsection." >}}
The [Cursors](../../../../cursors/) section is a direct child of the major section [Yugabyte Structured Query Language (YSQL)](../../../../../ysql/) and, as such, is a peer of the [User-defined subprograms and anonymous blocks](../../../../user-defined-subprograms-and-anon-blocks/) section. This reflects the fact that cursor functionality is first and foremost a SQL feature—just as, for example, _select_, _insert_, _update_, and _delete_ are.
{{< /tip >}}

Here is an example. It uses the **["Query for loop" with a bound refcursor variable](../executable-section/compound-statements/loop-exit-continue/query-for-loop/#query-for-loop-with-a-bound-refcursor-variable)**.

```plpgsql
create table s.t3(k int primary key, v text not null);
insert into s.t3(k, v) select g.s, g.s*10 from generate_series(1, 10) as g(s);

drop function if exists s.f() cascade;

create function s.f()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$

-- The declare section of interest.
declare
  r no scroll cursor(lo int, hi int) for
  select v from s.t3 where k between lo and hi order by k;

-- The executable section (sanity check).
begin
  assert (pg_typeof(r)::text = 'refcursor');

  for rec in r(lo := 7, hi := 9) loop
    z := rec.v; return next;
  end loop;
end;
$body$;

select s.f();
```

This is the result:

```output
 f  
----
 70
 80
 90
```

Here,  the _refcursor_ variable _r_  (declared using the _plpgsql_bound_refcursor_declaration_ syntax) is referred to as a _bound refcursor variable_ because its _subquery_ is irrevocably determined at declaration time. This allows it to be used in a _cursor for loop_. A _bound refcursor variable_ can also be used as the argument of an explicit _open_ statement. But here you cannot (re)specify the already-specified _subquery_.

Notice that the way you list the optional formal arguments in the declaration of a _bound refcursor variable_ differs from how this is done for a user-defined subprogram in that you _must_ name each argument and you cannot provide an _in/out_ mode or a default value. This means that all the arguments are mandatory and that you must provide an actual argument value for each: with a _cursor for loop_ as part of its _in_ clause; or in the explicit _open_ statement.

## Syntax errors and semantics errors in the declare section

A syntax error in the declaration section, when the PL/pgSQL block statement is the argument of a _[do](../../../../syntax_resources/grammar_diagrams/#do)_ statement, prevents the attempt to execute it. And a syntax error in the declaration section, when the PL/pgSQL block statement is the argument of a _[create function](../../../../syntax_resources/grammar_diagrams/#create-function)_ or _[create procedure](../../../../syntax_resources/grammar_diagrams/#create-procedure)_ statement (more carefully stated, when it's an [unalterable_fn_attribute](../../../../syntax_resources/grammar_diagrams/#unalterable-fn-attribute) or an [unalterable_proc_attribute](../../../../syntax_resources/grammar_diagrams/#unalterable-proc-attribute)), prevents the to-be-created subprogram from being recorded in the catalog.

A semantic error in the declaration section shows up as a run-time error. Notice that the exception as which the error manifests within PL/pgSQL cannot be handled in the exception section of the _[plpgsql_block_stmt](../../../../syntax_resources/grammar_diagrams/#plpgsql-block-stmt)_ that the declaration section introduces. Rather, the exception can be handled only in the exception section of an enclosing _plpgsql_block_stmt_.

### Syntax errors

Try this:

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a  int := 1  2;
begin
  assert a = 1;
end;
$body$;
```

The attempt causes this error:

```output
42601: syntax error at or near "2"
```

and _call s.p()_ causes the error _42883: procedure s.p() does not exist_.

Now try this. (The syntax rules simply insist that no variable can be declared more than once.)

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a  int;
  a  text;
begin

end;
$body$;
```

The attempt causes this error:

```pitput
42601: duplicate declaration at or near "a"
```

and, again, _call s.p()_ shows that it doesn't exist.

### Semantic errors

Here's a trivial example:

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a  constant numeric not null := swrt(4.0);
begin
  assert (a = 2.0);
end;
$body$;
```

No errors are reported—and _s.p()_ is recorded in the catalog. But the _call s.p()_ attempt causes this error, attributed to the line that declares _a_:

```output
42883: function swrt(numeric) does not exist
```

Now try a more subtle example:

```plpgsql
drop schema if exists s cascade;
create schema s;

create procedure s.p()
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a  constant int not null := 17;
  c           int not null := a + b;
  b  constant int not null := 42;
begin
  assert (c = 17 + 42);
end;
$body$;
```

No errors are reported—and _s.p()_ is recorded in the catalog. Notice that when _b_ is referenced in the expression that initializes _c_, it has not yet been declared. This happens on the next line. But forward references are not allowed—and such an error is considered to be a semantic error and is therefore not  detected until _call s.p()_ is attempted. The attempt causes this error, attributed to the line that declares _c_:

```output
42703: column "b" does not exist
```

