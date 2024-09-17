---
title: Doing SQL from PL/pgSQL [YSQL]
headerTitle: Doing SQL from PL/pgSQL
linkTitle: Doing SQL from PL/pgSQL
description: Describes the syntax and semantics of the various PL/pgSQL statements for doing SQL from PL/pgSQL [YSQL].
menu:
  preview:
    identifier: doing-sql-from-plpgsql
    parent: basic-statements
    weight: 50
type: docs
showRightNav: true
---

## Syntax

See these sections for other dedicated ways to loop over the rows that a _select_ statement defines:

- **[Cursor manipulation in PL/pgSQL—the "open", "fetch", and "close" statements](../cursor-manipulation/)**
- **[The "query for loop"](../../compound-statements/loop-exit-continue/query-for-loop/)**
- **[The "infinite loop"](../../compound-statements/loop-exit-continue/infinite-and-while-loops/#infinite-loop-over-cursor-results)**

Here are the remaining basic PL/pgSQL statements for doing SQL:

{{%ebnf%}}
  plpgsql_static_bare_sql_stmt,
  plpgsql_static_dml_returning_stmt,
  plpgsql_static_select_into_stmt,
  plpgsql_dynamic_sql_stmt,
  plpgsql_perform_stmt
{{%/ebnf%}}

## Semantics

### The bare SQL statement

_[See the syntax rule above](./#plpgsql-static-bare-sql-stmt)_

This example shows four _embedded_ SQL statements encapsulated within a single PL/pgSQL procedure. The first three are DDL statements. And the fourth is a DML statement—i.e. a statement that can be used by a _prepare_ statement. The three DDL statements are written exactly as they would be as top-level SQL statements. The _insert_ statement is also syntactically correct as a top-level SQL statement. But if you do this, you get a _42703_ semantic error:

```plpgsql
insert into s.t(v) select generate_series(lo, hi);
```

This is the error text:

```output
column "lo" does not exist
```

This is what the term of art _embedded_ means: the SQL statement text is simply part of the program text, every bit as much as is, say, an assignment statement like this:

```plpgsql
a := b + c;
```

In other words, the SQL statement is syntactically analyzed at the time that the subprogram is created—and a syntax error turns the attempt into a no-op. (A corresponding account holds for a _do_ statement. An attempt to execute it isn't made if its defining PL/pgSQL block statement has a syntax error.)

The term of art _dynamic_ (see the section ["execute" statement](./#the-execute-statement) below) is used when SQL statement text is presented as a _text value_. (Some people prefer the term of art _static_, instead of _embedded_, to contrast with _dynamic_.)

Here is the example. _Not only_ does it serve the purpose of illustrating the _plpgsql_static_bare_sql_stmt_; but _also_ it is a useful tool for re-creating a clean start before running each of the remaining examples on this page.

```plpgsql
drop schema if exists admin cascade;
create schema admin;

create procedure admin.init(lo in int, hi in int)
  set search_path = pg_catalog, pg_temp
  set client_min_messages = error
  language plpgsql
as $body$
begin
  drop schema if exists s cascade;
  create schema s;
  create table s.t(k serial primary key, v int);
  insert into s.t(v) select generate_series(lo, hi);
end;
$body$;
```

It finishes silently without error—leaving you set up for the remaining examples.

### The "select into" statement

_[See the syntax rule above](./#plpgsql-static-select-into-stmt)_

The following example illustrates:

- both the _"select into"_ statement
- and the _["insert, update, delete into"](./#the-insert-update-delete-into-statement)_ statement

Do this:

```plpgsql
call admin.init(lo=>11, hi=>20);

create procedure s.p(key in int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  v_into  int not null := 0;
  v_ret   int not null := 0;
begin
  select v into strict v_into from s.t where k = key;
  delete from s.t where k = key returning v into strict v_ret;
  assert v_ret = v_into;
end;
$body$;

call s.p(5);
```

It finishes silently, showing that the _assert_ holds.

Notice the use of the _strict_ keyword in both of the embedded SQL statements. This is the common case when you do a single row _select into_ or _delete returning_ where the row is identified by the value of the primary key. The effect of _strict_ is that the SQL statement causes a dedicated _P0002_ error with the message _query returned no rows_.

### The "insert, update, delete into" statement

_[See the syntax rule above](./#plpgsql-static-dml-returning-stmt)_

The previous _["select into"](./#the-select-into-statement)_ section illustrates the _"insert, update, delete into"_ statement. Look for this:

```plpgsql
delete from s.t where k = 5 returning v into strict v_ret;
```

You might wonder how to handle the case that the _delete_ restriction identifies more than one row. The clue is the _[common_table_expression](../../../../../../syntax_resources/grammar_diagrams/#common-table-expression)_ hereinafter _CTE_. It lets you use the _delete_ statement that you want in the definition of a _CTE_ in a _with clause_ in an overall _select_ statement. Then you use that in a _["query for loop" with a subquery](../../compound-statements/loop-exit-continue/query-for-loop/#query-for-loop-with-a-subquery)_.

Do this:

```plpgsql
call admin.init(lo=>11, hi=>20);

create function s.f(lo in int, hi in int)
  returns table(del int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  v_into  int not null := 0;
  v_ret   int not null := 0;
begin
  for del in
    (
      with c as (delete from s.t where k between lo and hi returning v)
      select v from c
    )
  loop
    return next;
  end loop;
end;
$body$;

select del as "Deleted rows" from s.f(5, 7);
```

This is the result:

```output
 Deleted rows 
--------------
           15
           16
           17
```

### The "execute" statement

_[See the syntax rule above](./#plpgsql-dynamic-sql-stmt)_

Simply transform each of the _embedded_ SQL statements from the **["select into" statement](./#the-select-into-statement)** section above into a _dynamic_ SQL statement.

Do this:

```plpgsql
call admin.init(lo=>11, hi=>20);

create procedure s.p(key in int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  select_stmt constant text not null := 'select v from s.t where k = $1';
  delete_stmt constant text not null := 'delete from s.t where k = $1 returning v';
  v_into  int not null := 0;
  v_ret   int not null := 0;
begin
  execute select_stmt into strict v_into using key;
  execute delete_stmt into strict v_ret  using key;
  assert v_ret = v_into;
end;
$body$;

call s.p(5);
```

It finishes silently, showing that the _assert_ holds.

Of course, the SQL statement text can be a DDL, too. Dynamic DDLs are especially useful when you don't know identifier names until run-time. Indeed, this is the canonical, and overwhelmingly common, use of Dynamic DDLs.

Dynamic DDLs are best illustrated by deriving the function _admin.init_dynamic()_ from the function _admin.init()_ shown in the **[bare (static) SQL statement](./#the-bare-sql-statement)** section above. Do this:

```plpgsql
create procedure admin.init_dynamic(
  schema_name  in text,
  table_name   in text,
  lo           in int,
  hi           in int)
  set search_path = pg_catalog, pg_temp
  set client_min_messages = error
  language plpgsql
as $body$
declare
  drop_schema    constant text not null :=
    'drop schema if exists %I cascade';
  create_schema  constant text not null :=
    'create schema %I';
  create_table   constant text not null :=
    'create table %I.%I(k serial primary key, v int)';
  insert_rows    constant text not null :=
    'insert into %I.%I(v) select generate_series(%L::int, %L::int)';
begin
  execute format(drop_schema,   schema_name);
  execute format(create_schema, schema_name);
  execute format(create_table,  schema_name, table_name);
  execute format(insert_rows,   schema_name, table_name, lo, hi);
end;
$body$;
```

Test it like this:

```plpgsql
call admin.init_dynamic('My Schema', 'My Table', lo=>11, hi=>20);
```

It finishes silently without error. Check that you can see the expected new schema in the catalog:

```plpgsql
select n.nspname as "Schema name"
from
  pg_namespace as n
  inner
  join pg_roles as r
  on n.nspowner = r.oid
where
  r.rolname = :quoted_u and
  n.nspname ~'^My';
```

This is the result:

```output
 Schema name 
-------------
 My Schema
```

Now check that the expected table is found in the schema _My Schema_:

```plpgsql
select c.relname as "Table name"
from
  pg_class as c
  inner join
  pg_namespace as n
  on c.relnamespace = n.oid
  inner join
  pg_roles as r
  on n.nspowner = r.oid
where
  c.relkind = 'r' and
  r.rolname = :quoted_u and
  n.nspname ~'^My';
```

This is the result:

```output
 Table name 
------------
 My Table
```

Notice that the procedure _admin.init_dynamic()_ adds these _text_ formal arguments to _admin.init()_:

- _schema_name_
- _table_name_

And notice how all of the formal arguments are used, in the _execute_ statement, as actual arguments to the _format()_ built-in SQL function.

- The placeholder _%I_ means that the actual argument will be taken as the text of an object name—and then, according to the emergent status of the name as _common_ or _exotic_, this actual argument will replace the _%I_ placeholder, respectively, either _as is_ or tightly surrounded with double-quotes. The section **[Names and identifiers](../../../../../../names-and-identifiers/)** explains the _common name_ and the _exotic name_ notions and what these notions imply for the need for double-quoting.
- The placeholder _%L_ means that the actual argument will be taken as the text of a literal value. The replaced _%L_ placeholder will always be surrounded by single-quotes. You must therefore take account of the data type that you know that you want by writing the appropriate typecast after _%L_.

Try these simple tests—first using _%I_:

```plpgsql
select
  (select format('select * from %I',  'employees')  as "common name"),
  (select format('select * from %I',  'Pay Grade')  as "exotic name");
```

This is the result:

```output
       common name       |        exotic name        
-------------------------+---------------------------
 select * from employees | select * from "Pay Grade"
```

And next using _%L_:

```plpgsql
select
  (select format('select %L::int',           42)    as "int literal"),
  (select format('select %L::boolean',       true)  as "boolean literal"),
  (select format('select %L',         'My Schema')  as "text literal");

```

This is the result:

```output
   int literal    |   boolean literal   |    text literal    
------------------+---------------------+--------------------
 select '42'::int | select 't'::boolean | select 'My Schema'
```

{{< tip title="Always use the 'format()' function to add object names or values into the text of a dynamic SQL statement" >}}
An Internet search for "SQL injection" reports that it finds about _7 million_ hits. The risk of SQL injection is considered to be a huge security threat. You'll soon find articles with code examples that illustrate how the risk can occur. This [Wikipedia piece](https://en.wikipedia.org/wiki/SQL_injection) is a good place to start.

Briefly, the risk occurs when you want to construct the text of a SQL statement programmatically and you want it to contain identifiers and values that become known only at run-time. Naïve programmers simply assemble the text using the ordinary _text_ concatenation operator—but you must _never_ do this. The examples that Internet search finds show you what can go wrong if you do this.

(You can reduce the size of the hit-list to about _300 thousand_ by searching for _PostgreSQL "format()" "SQL injection"_. )

You must _always_ use the _format()_ built-in SQL function when you need to define the text of a dynamic SQL statement at run-time. The example above shows you how to do this.
{{< /tip >}}

### The "perform" statement

_[See the syntax rule above](./#plpgsql-perform-stmt)_

The _perform_ statement lets you execute a select statement that, as a top-level SQL statement, would produce one or many rows. But, critically, it lets you avoid using the _select into_ statement for one row or the _[query for loop](../../compound-statements/loop-exit-continue/query-for-loop/)_ (or the _[infinite loop](../../compound-statements/loop-exit-continue/infinite-and-while-loops/#infinite-loop-over-cursor-results)_) for many rows. It's the moral equivalent of this:

```output
select ... into null;
```

analogous, in Unix, to sending output to _/Dev/Null_. Try this top-level SQL statement:

```plpgsql
with s(val) as (select g.val from generate_series(1, 11) as g(val))
select s.val as k, s.val*10 as v from s;
```

This, of course, is the result:

```output
 k | v  
---+----
 1 | 10
 2 | 20
 ......
 8 | 80
 9 | 90
```

To use this as the argument for _perform_, you must encapsulate as a subquery in an outer _select_ statement, thus:

```plpgsql\
select k, v from (
    with s(val) as (select g.val from generate_series(1, 11) as g(val))
    select s.val as k, s.val*10 as v from s
  ) as subqry;
```

You can now use this in a PL/pgSQL block statement simply by replacing the starting _select_ with _perform_. Do this:

```plpgsql
do $body$
begin
  perform k, v from (
      with s(val) as (select g.val from generate_series(1, 11) as g(val))
      select s.val as k, s.val*10 as v from s
    ) as subqry;
end;
$body$;
```

It finishes silently without error.

This _do_ statement immediately above shows the general case, where the _select_ statement whose results you want to throw away must use a _with clause_.

This need arises, for example, when you perform multi-table _insert_, _update_ or _delete_ by encapsulating these statements each with a pro-forma _returning_ clause, and in its own _[CTE](../../../../../../syntax_resources/grammar_diagrams/#common-table-expression)_. Then you cause them to execute by selecting the union of these pro-forma results  from the various _CTEs_. But the results are of no interest. They were simply a necessary device.

<!--- _to_do_ --->
{{< note title="Coming soon" >}}
A self-contained, working example will follow.
{{< /note >}}

By far the most common use of _perform_ is when you need to use a SQL built-in, like _pg_sleep()_ that is morally a procedure but that, for implementation reasons, has to be a function. Try this:

```plpgsql
do $body$
begin
  perform pg_sleep(2.0);
end;
$body$;
```

It finishes silently after two seconds.
