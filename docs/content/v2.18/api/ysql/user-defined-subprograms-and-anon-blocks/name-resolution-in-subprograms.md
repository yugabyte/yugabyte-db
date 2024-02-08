---
title: Name resolution within user-defined subprograms and anonymous blocks [YSQL]
headerTitle: Name resolution within user-defined subprograms and anonymous blocks
linkTitle: Name resolution in subprograms
description: Explains how unqualified identifiers, used within user-defined subprograms and anonymous blocks, are resolved [YSQL].
menu:
  v2.18:
    identifier: name-resolution-in-subprograms
    parent: user-defined-subprograms-and-anon-blocks
    weight: 60
type: docs
---

The rules that this section explains apply to

- _language sql_ subprograms
- _language plpgsql_ subprograms
- anonymous blocks (which can be written only in _language plpgsql_)

The principles that the section [Name resolution within top-level SQL statements](../../name-resolution-in-top-level-sql/) explains apply here too. But they're extended with further principles for these contexts.

- An embedded SQL statement in a subprogram, both for _language sql_ and for _language plpgsql_, or in an anonymous block allows an identifier to be used that does not resolve in the scope that the SQL statement defines. When an identifier escapes SQL scope, an attempt is made to resolve it in the scope of names that the code of the containing subprogram or anonymous block establishes.

- A subprogram allows the _[search_path](../../name-resolution-in-top-level-sql/#the-search-path-run-time-parameter)_ to be defined as one of its attributes. When this possibility is used, the reigning value of the _search_path_ is saved on entry to the subprogram and is restored on exit. However, an anonymous block has no such feature.

{{< tip title="Name resolution for unqualified identifiers in a user-defined subprogram is independent of its 'security' setting." >}}
Name resolution for unqualified identifiers in a user-defined subprogram works the same for the _language sql_ and _language plpgsql_ variants and the same for the _security invoker_ and _security definer_ variants.
{{< /tip >}}

## Name resolution in 'language sql' subprograms

A _language sql_ subprogram is defined by a list of one or several SQL statements.

Name resolution for unqualified identifiers that denote schema-objects is done exactly as it is for such identifiers in top-level SQL statements. But the subprogram encapsulation provides an additional way to set the _search_path_. The section [Alterable subprogram attributes](../subprogram-attributes/alterable-subprogram-attributes/) explains that an optional _[alterable_fn_and_proc_attribute](../../syntax_resources/grammar_diagrams/#alterable-fn-and-proc-attribute)_ clause allows the _search_path_ to be set declaratively for the subprogram so that the value that is in force just before the call is saved on entry and then, when control returns to the caller, the saved value is restored. This has the huge benefit over relying on the _search_path_ value that's defined at session level that it cannot be changed except by re-creating the subprogram—and therefore cannot be changed by possibly nefarious client-side code.

Recall that the top-level _prepare_ statement allows the use of a placeholder at a syntax spot where an expression is legal. Then, the _execute_ statement lets you supply actual values for such placeholders so that they can be bound in before execution. The SQL statements that jointly define the body of a _language sql_ user-defined subprogram allow the use of a user-defined identifier at a syntax spot where a statement that is the target of _prepare_ allows a placeholder—in other words, an identifier that denotes a name that cannot be resolved in the scope that the SQL statement itself defines. Such an escaping name must be resolved by a name that one of the subprogram's formal arguments identifies—else the _create_ statement fails.

## Name resolution in 'language plpgsql' subprograms

A _language plpgsql_ subprogram embeds SQL statements or refers to user-defined functions in an expression in its declaration section or in ordinary procedural code in its executable section and its exception section.

Connect as an ordinarily privileged user to a database that has a schema, _s_, on which the user has the _create_ privilege and try this:

```plpgsql
create table s.t(k int primary key, c1 int not null, c2 int not null);
insert into s.t(k, c1, c2) values
  (1, 17, 17),
  (2, 31, 19),
  (3, 42, 42);

create function s.f()
  returns text
  language plpgsql
  set search_path = pg_catalog, s, pg_temp
as $body$
declare
  c3 constant int not null := 31;
  ks_1 int[] := (select array_agg(k) from t where c1 = c2);
  ks_2 int[] := (select array_agg(k) from t where c1 = c3);
begin
  return 'ks_1: '||ks_1::text||' | ks_2: '||ks_2::text;
end;
$body$;

select s.f();
```

This is the result:

```output
 ks_1: {1,3} | ks_2: {2}
```

Now try to evaluate the SQL subqueries at top-level:

```plpgsql
select (select array_agg(k) from s.t where c1 = c2)::text as ks_1;
```

and

```plpgsql
select (select array_agg(k) from s.t where c1 = c3)::text as ks_2;
```

The first finishes without error and produces this result: 

```output
 {1,3}
```

But the second causes the _42703_  error:

```output
column "c3" does not exist
```

Of course, the identifier _c3_, at a syntax spot where a column identifier is expected, cannot be resolved within the scope of the SQL statement and the schema objects that it references. And top-level has nowhere else to look—so it gives up with the _42703_  error. But when the same subquery is embedded in PL/pgSQL source text, and name resolution for _c3_ fails in SQL scope, _c3_ escapes to PL/pgSQL scope, and name resolution is attempted there—and succeeds by resolving to the variable with that name. The statement is than transformed behind the scenes to the functional equivalent of the prepared statement that's executed by binding in the value of _c3_. (Notice that this is not visible in the _pg_prepared_statements_ catalog view.)

Not only can schema-identifiers be used at more syntax spots in a _language plpgsql_ subprogram than in a _language sql_ subprogram (for example to invoke a function on the right-hand side of a variable assignment) but also are there more opportunities for names that escape SQL scope to be resolved in the body of a _language plpgsql_ subprogram than there are in a _language sql_ one. They can be resolved both by names that the subprogram's list of formal arguments identifies and by the names of local variables that are defined within the subprogram's implementation.

Try this. It is to be hoped that nobody would write such code—because the proliferation of names that are spelled identically but that are used in different contexts is confusing for the human reader. Nevertheless, the meaning is well-defined and unambiguous:

```plpgsql
create schema y;
create table y.x(x int);

create procedure y.x(x in int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  y constant int not null := x + 1;
begin
  insert into y.x(x) values (x), (y);
end;
$body$;
```

The name _x_ denotes a table, a column in that table, a procedure, and a formal argument of that procedure. And the name _y_ denotes a schema and a local variable that's declared in a procedure. There's no collision or ambiguity because when each is defined and referenced, the context determines the kind of phenomenon that it must be and therefore what space must be searched for collision (at definition time) or for resolution (at reference time).

In this declaration:

```output
y constant int not null := x + 1;
```

_y_ can be only a newly-defined local variable and _x_ can be (in general) an already-defined local variable or (as it is here) a formal argument. And in this _insert_ statement:

```output
insert into y.x(x) values (x), (y);
```

_x_ and _y_ in the _values_ clause (in syntax spots that could be placeholders in the target statement for _prepare_) can each be only a formal argument or a local variable.

Test it thus:

```plpgql
call y.x(42);
select * from y.x order by 1;
```

It runs without error and produces this result:

```output
 x  
----
 42
 43
```

## Name resolution in anonymous blocks

The body of an anonymous block (necessarily _language plpgsql_) is governed by the identical syntax rules that govern a _language plpgsql_ procedure and has identical semantics. The only difference, with respect to name resolution, is that an anonymous block has no formal parameters and cannot set the _search_path_. This means that:

- Only a local variable may be used in static SQL statements where a placeholder is legal when that statement is the target of _prepare_.
- It's critical to use only fully qualified identifiers to ensure that the intention of your code cannot be subverted.

## Demonstrating how a temporary table, created on-the-fly, can capture an application's intended functionality

This demonstration focuses on a "hard-shell" application design where all data manipulation is done with user-defined subprograms and where client-side code has no privileges on the application's tables and has only the _execute_ privileges on the subprograms that manipulate the contents of these tables. The overall paradigm aims to ensure that only the intended operations are allowed on only the intended tables.

Obviously, if the subprograms use only unqualified identifiers and rely on the _search_path_ that the session defines to ensure the intended name resolution, then the application is vulnerable because nothing can stop client-side code changing the _search_path_ at any time. (A hacker who has discovered how to exploit a SQL injection risk might be able to do this—and to remain undetected.) Therefore, leaving a subprogram's _search_path_ attribute unset is a notorious security anti-pattern. This demonstration shows that if a subprogram's _search_path_ attribute _is_ set, but is done so _naïvely_, then a risk of subversion remains.

{{< tip title="See the section 'Writing security definer functions safely' in the PostgreSQL documentation." >}}
[Here is the section](https://www.postgresql.org/docs/11/sql-createfunction.html#id-1.9.3.67.10.2) that this tip's title mentions. It explains the risk that the anti-pattern brings and recommends protecting  against it by setting the intended _search_path_ as an attribute of the subprogram.
{{< /tip >}}

This demonstration relies on two ordinarily privileged users, _d0$u0_ and _d0$u1_ that are able to connect to the database _d0_. Here, _d0$u0_ models the role that owns the tables and the _security definer_ subprograms that access these. These schema objects live in the schema _s_, also owned by _d0$u0_. _d0$u1_ models the role as which the client-side application code connects and it has only the _connect_ and (because of a serious mistake) it still has the _temporary_ privilege, via _public_, on database _d0_. (When a database is created, the _connect_ and _temporary_ privileges on it are automatically granted to _public_.) By proper design, it does _not_ have the _create_ privilege on _d0_.

First, do this set-up:

```plpgsql
\c d0 d0$u0

create schema s;
revoke usage on schema s from public;
grant  usage on schema s to   d0$u1;

create table s.salaries(ename text primary key, sal int not null);
insert into  s.salaries(ename, sal) values
  ('Jane', 6000),
  ('Mary', 6500),
  ('Fred', 5500),
  ('Bert', 5000);
revoke all on table s.salaries from public;

create function s.salaries_list(e out text, s out int)
  returns setof record
  set search_path = s
  security definer
  language sql
as $body$
  select ename, sal from salaries order by 1;
$body$;

revoke all     on function s.salaries_list() from public;
grant  execute on function s.salaries_list() to   d0$u1;
```

Notice that the function's _search_path_ attribute mistakenly specifies only _s_. The author forgot that _pg_catalog_ and _pg_temp_ are inevitably included in the _search_path_ and that when they aren't explicitly mentioned, _pg_temp_ is searched first. See the section [No matter what value you set for 'search_path', 'pg_temp' and 'pg_catalog' are always searched](../../name-resolution-in-top-level-sql/#no-matter-what-value-you-set-for-search-path-pg-temp-and-pg-catalog-are-always-searched).

Next test it just as the designed expected:

```plpgsql
\c d0 d0$u1

select e, s::text from s.salaries_list() order by e;
```

This is the result:

```output
  e   |  s   
------+------
 Bert | 5000
 Fred | 5500
 Jane | 6000
 Mary | 6500
```

So far, then, _d0$d1_ (the client) sees exactly the data that the table that it cannot query directly holds. Now exploit the mistake that the designers made:

```plpgsql
create table pg_temp.stage as select e as ename, s as sal from s.salaries_list();
update pg_temp.stage set sal = sal * 1.15 where ename = 'Jane';
create table pg_temp.salaries as select ename, sal from pg_temp.stage;
drop table pg_temp.stage;
grant select on pg_temp.salaries to d0$u0;

select e, s::text from s.salaries_list();
```

This is the new result:

```output
  e   |  s   
------+------
 Bert | 5000
 Fred | 5500
 Jane | 6900
 Mary | 6500
```

Jane is wrongly listed with the salary _6900_ rather than _6000_. The security hole opened by:

- forgetting to put _pg_temp_ explicitly at the right hand end of the _search_path_ that the subprogram's attribute defines
- _and_ failing to revoke the _temporary_ privilege on database _d0_ from _public_.

has allowed the nefarious client-side code to see results that don't reflect the content of the table, as was intended, but rather to see Jane's salary elevated by 15%,

## Recommendation: always set the "search_path" in a subprogram attribute explicitly to start with "pg_catalog" and explicitly to end with pg_temp"

{{< tip title="Use fully qualified names in the SQL that subprograms issue." >}}

Yugabyte recommends that, for the typical case, you use fully qualified names in the SQL statements that subprograms issue and that, to reinforce your plan, you set the _search_path_ to just _pg_catalog_, _pg_temp_ for every subprogram. If you decide not to follow this recommendation, then your design documentation should explain the reasoning for this decision.
{{< /tip >}}

As with many recommendations, opinions might vary on this point. You might prefer to set the _search_path_ attribute, for example, thus:

```output
set search_path = pg_catalog, s1, s2,... sN, pg_temp
```

This will have the effect that your code will be less cluttered. In the special case that only a single schema, _s_, intervenes between _pg_catalog_ and _pg_temp_, the meaning will be clear (as long as you follow a convention always to schema-qualify the identifiers for temporary objects) and you avoid choosing names for user-created objects that collide with those of objects in _pg_catalog_: unqualified identifiers denote objects in the schema _s_ or the schema _pg_catalog_. However, if more than one schema is listed between _pg_catalog_ and _pg_temp_, then the reader will be unable to discern the code's meaning without studying the environment in which it executes. Yugabyte therefore recommends that you favor self-evident meaning over clutter reduction. (You might relax this strict advice if you rely on extensions and if you follow a general convention always to install these in a dedicated _extensions_ schema or to install each extension in a schema whose name is that of the extension. This would allow you, at least, to use unqualified identifiers like _gen_random_uuid_ in, for example, table declarations.)

There's no risk that you might create objects in the _pg_catalog_ schema. Connect as the _postgres_ role and try this:

```postgres
set search_path = pg_catalog, pg_temp;
create table pg_catalog.t(n int);
```

It causes this error:

```output
42501: permission denied to create "pg_catalog.t"
```

Notice that it _is_ possible, by setting a special configuration parameter, to allow the _postgres_ role to create objects in the _pg_catalog_ schema. But even then, only the _postgres_ role can create objects there. If a bad actor can manage to connect as the _postgres_ role, then all bets are anyway already off.
