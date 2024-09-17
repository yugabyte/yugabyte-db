---
title: SQL subprograms (a.k.a. "language sql" subprograms) [YSQL]
headerTitle: SQL subprograms (a.k.a. "language sql" subprograms)
linkTitle: >
  "language sql" subprograms
description: Describes SQL functions and procedures. These are also known as "language sql" subprograms. [YSQL].
menu:
  preview:
    identifier: language-sql-subprograms
    parent: user-defined-subprograms-and-anon-blocks
    weight: 20
type: docs
---

{{< tip title="Regard the PostgreSQL documentation as the canonical definitional reference on this topic." >}}
Make sure that you read the section [Query Language (SQL) Functions](https://www.postgresql.org/docs/11/xfunc-sql.html) in the PostgreSQL documentation.
{{< /tip >}}

You can define both functions and procedures using _language sql_. Each of these lets you encapsulate two or more SQL statements. Notice that a function, or a procedure with an _inout_ formal argument, returns the result of only the _last_ SQL statement. See the section [The result is delivered by the last "select" (or "values") statement](./#the-result-is-delivered-by-the-last-select-or-values-statement) below. The encapsulation lets you do no more than list independent SQL statements in the order that they are to be executed. This means that there's no way within the _language sql_ encapsulation to inform the next statement that's executed about the outcome of the previous statements. However, using two or more SQL statements in concert typically implies the need for a control structure—and this is what _language plpgsql_ subprograms provide.

Yugabyte recommends that you respect the convention that the introductory section [User-defined subprograms](../#user-defined-subprograms) outlines:

- use a function to produce a single _value_, a _record value_, or a set of such  _values_
- use a procedure to _do something_.

The upshot is that the body of a _language sql_ function will normally be a single _select_ (or _values_) statement and that the body of a _language sql_ procedure will normally be a single _insert_, update_, or delete_ statement.

## The execution model for 'language sql' subprograms

Because the body of a _language sql_ subprogram is nothing more than a list of SQL statements, the analysis that's done at _create_ time is different from what's done at _create_ time for a _language plpgsql_ subprogram. (See the section [PL/pgSQL's execution model](../language-plpgsql-subprograms/plpgsql-execution-model/).)

- For a _language sql_ subprogram, at _create_ time, each SQL statement undergoes the same kind of analysis that the _prepare_ statement carries out. In particular, every schema-object that each encapsulated SQL statement references must exist, and have the correct sub-object structure, at _create_ time for the subprogram. Then at run-time, each statement is executed in a way that corresponds to how a prepared statement is executed: actual values brought by the subprogram's arguments are bound to corresponding placeholders and then it is executed. (Notice that there's no evidence of this in what the _pg_prepared_statements_ catalog view lists.)
- For a _language plpgsql_ subprogram, the body's procedural source code is syntax checked. Further, some (but by no means not all possible) semantic analysis is done. But embedded SQL statements receive no more than a syntax check. Then at run-time, and only when the subprogram is first referenced, its so-called AST (abstract syntax tree) is derived and stored in the session's memory. Therefore, full semantic analysis of embedded SQL statements (followed by the equivalent of _prepare_, binding of actual values, and execution) happens first at run-time. (Notice that, here too, there's no evidence of this in what the _pg_prepared_statements_ catalog view lists.)

The difference between the two models means that a _language plpgsql_ subprogram can use objects that it creates just before use—while a _language sql_ subprogram can use only objects that already exist at the moment that it's created. Try the following comparison. First, using _language plpgsql_:

```plpgsql
create schema s;

create procedure s.p(v1 in int, v2 in int, r inout int[])
  set search_path = pg_catalog, s, pg_temp
  language plpgsql
as $body$
begin
  create table if not exists pg_temp.t(v int not null) on commit delete rows;
  with c as (
    insert into pg_temp.t(v) values (v1), (v2) returning v)
  select array_agg(v) into r from c;
end;
$body$;

call s.p(17, 19, null::int[]);
```

The _create procedure_ succeeds and the _call_ produces this result:

```output
    r    
---------
 {17,19}
```

Now attempt to rewrite it using _language sql_:

```plpgsql
drop table if exists pg_temp.t;
drop procedure if exists s.p(int, int, int[]);

create procedure s.p(v1 in int, v2 in int, r inout int[])
  set search_path = pg_catalog, s, pg_temp
  language sql
as $body$
  create table if not exists pg_temp.t(v int not null) on commit delete rows;
  with c as (
    insert into pg_temp.t(v) values (v1), (v2) returning v)
  select array_agg(v) from c;
$body$;
```

The _create procedure_ fails with the _42P01_ error:

```output
relation "pg_temp.t" does not exist
```

You can make the error go away by moving the _create table_ statement to top-level and by executing it before you execute the _create procedure_:

```plpgsql
drop table if exists pg_temp.t;
create table pg_temp.t(v int not null) on commit delete rows;

drop procedure if exists s.p(int, int, int[]);
create procedure s.p(v1 in int, v2 in int, r inout int[])
  set search_path = pg_catalog, s, pg_temp
  language sql
as $body$
  with c as (
    insert into pg_temp.t(v) values (v1), (v2) returning v)
  select array_agg(v) from c;
$body$;
```

The _create procedure_ now runs without error and the _call_ produces the same result as it did for the _language plpgsql_ procedure.

The difference between how _language plpgsql_ subprograms and _language sql_ subprograms are created and executed brings some functional disadvantage to the latter kind of subprogram with respect to the former kind. With the former kind, a common pattern for using a temporary table is to include the _create table if not exists_ statement within the encapsulation of the procedure itself so that it's guaranteed that the table exists when it's first accessed. (A more carefully designed pattern is needed if the temporary table's purpose is to represent session-duration state that several different subprograms share. Here, it will be created using _on commit preserve rows_. And specific initialization code must ensure that the table is created at session start—or at least before it's first needed.) If a _language sql_ subprogram is to be used, then it inevitably must rely on specific initialization code outside of the subprogram's encapsulation to create it before the subprogram uses it.

{{< tip title="A 'language sql' subprogram might need to typecast values where its 'language plpgsql' counterpart does not." >}}
Try this:

```plpgsql
create schema s;

create function s.f()
  returns int
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select count(*) from pg_class;
$body$;
```

The _create_ fails with the _42P13_ error thus:
```output
return type mismatch in function declared to return integer
```

and the "detail" says _Actual return type is bigint_. The \\_df count_ meta-command shows that its return data type is indeed _bigint_. So you must either create the function with _returns bigint_ or use a typecast thus:

```plpgsql
drop function if exists s.f() cascade;

create function s.f()
  returns int
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select count(*)::int from pg_class;
$body$;
```

Now the _create_ succeeds without error (and _select s.f()_ produces the expected result).

The _language plpgsql_ counterpart is more forgiving. Try this:

```plpgsql
drop function if exists s.f() cascade;

create function s.f()
  returns int
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  return (select count(*) from pg_class);
end;
$body$;
```

Here, the create succeeds without error (and _select s.f()_ again produces the expected result).
{{< /tip >}}

## Using a "language sql" function to produce different kinds of return value

You can use a _language sql_ function to return:

- a single value—which can be either a single scalar value or a single value of a composite type of arbitrary complexity
- a value of a single (anonymous) _record_ value—each named field of which can be a single scalar value or a single value of a composite type
- a set of single values or a set of _record_ values.

### Returning a single scalar value

Connect as an ordinary user that can create objects in a test database and do this:

```plpgsql
create schema s;
create table s.t(k serial primary key, c1 int not null, c2 int not null);
insert into s.t(c1, c2) values (1, 17), (2, 42), (3, 57), (1, 19);

create function s.f(c1_in in int)
  returns int
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  select c2 from t where c1 = c1_in order by c2 desc;
$body$;
```

The example is contrived to make a point: the function is specified to return a single scalar value. Yet, by looking at the table's content, you can see that the _select_ that the function encapsulates will produce two rows when _c1_in = 2_. However, the function does not cause an error when invoked with this actual argument—and the outcome is well-defined. The function's result is simply the _first_ result that the _select_ statement defined. (The _order by_ clause brings a well-defined result in the usual way.) Now execute it:

```plpgsql
select s.f(1);
```

This is the result"

```output
 f  
----
 19
```

Compare this with how the corresponding _language plpgsql_ function behaves:

```plpgsql
create function s.f_plpgsql(c1_in in int)
  returns int
  set search_path = pg_catalog, s, pg_temp
  security definer
  language plpgsql
as $body$
begin
  return (select c2 from t where c1 = c1_in order by c2 desc);
end;
$body$;
```

Now execute it:

```plpgsql
select s.f_plpgsql(1);
```

It causes the _21000_ error:

```output
more than one row returned by a subquery used as an expression
```

The difference in outcome for the _language sql_ function compared to the _language plpgsql_ function that each encapsulates the same _select_ statement is explained thus:

- The _language sql_ encapsulation, when the header's _returns_ clause specifies a single row, is simply _defined_ to return the first row.
- But the corresponding _language plpgsql_ encapsulation is designed to return the value that follows the _return_ keyword. And, in the present example, that is a subquery that may, or may not, return a single row.

This implies that you can provoke the same error in ordinary top-level SQL, thus:

```plpgsql
select (select c2 from s.t where c1 = 1 order by c2 desc) as result;
```

The key question that informs the choice between a _language sql_ and a _language plpgsql_ subprogram when the _returns_ argument is to be a single value is your confidence that the code is guaranteed to produce a single value. If the code does no more than evaluate an arithmetic expression, then choosing _language sql_ can be completely safe. But if the result is produced by a SQL statement, then the safety must rely on the existence of a suitable unique index (which is not the case in the present example).

### Returning a single record value

The syntax here depends on using the keyword _record_ as the operand of the _returns_ keyword in the function's header together with listing the names and data types of the record's fields as _out_ formal arguments. Try this:

```plpgsql
create schema s;
create table s.t(k int primary key, c1 int not null, c2 int not null);
insert into s.t(k, c1, c2) values (1, 3, 17), (2, 5, 42), (3, 7, 57), (4, 9, 19);

create function s.f(k_in in int, c1 out int, c2 out int)
  returns record
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  select c1, c2 from t where k = k_in;
$body$;
```

Test it thus:

```plpgsql
select c1, c2 from s.f(3);
```

This is the result:

```output
 c1 | c2 
----+----
  7 | 57
```

Compare this approach with defining a function that returns a single composite type value:

```plpgsq
create type s.ct as (c1 int, c2 int);

create function s.f2(k_in in int)
  returns s.ct
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  select (c1, c2)::s.ct from t where k = k_in;
$body$;
```

The syntax to test this is identical to what you used to test _s.f()_:

```plpgsql
select c1, c2 from s.f2(3);
```

And it produces the identical result. The choice between the two approaches will be dictated by how the result is to be consumed.

### Returning a set of scalar values

The syntax here is a trivial extension of the syntax for returning a single scalar value. Try this:

```plpgsql
create schema s0;

create function s0.my_subprogram_names(kinds_in text[] = null)
  returns setof name
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  with s(name, kind) as (
    select p.proname, p.prokind::text
    from
      pg_proc as p
      inner join pg_roles as r
      on p.proowner = r.oid
    where r.rolname = current_role
    and   p.prokind = any (array['f'::"char", 'p'::"char"]))
  select name
  from s
  where kind = any(
                    case kinds_in
                      when array['f'] then array['f']
                      when array['p'] then array['p']
                      else                       array['f', 'p']
                    end
                  )
  order by name;
$body$;
```

Now create another function and a procedure simply to give the function _s.my_subprograms()_ more records to return:

```plpgsql
create schema s1;
create function s1.f()
  returns int
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select 1;
$body$;

create schema s2;
create table s2.t(k serial primary key, v int not null);
create procedure s2.p(v_in in int)
  set search_path = pg_catalog, pg_temp
  security definer
  language sql
as $body$
  insert into s2.t(v) values (v_in);
$body$;
```

Now test _s.my_subprogram_names()_. First, list the current role's functions:

```plpgsql
select t.v as func_name
from s0.my_subprogram_names(array['f']) as t(v);
```

This is the result:

```output
      func_name      
---------------------
 f
 my_subprogram_names
```

Next, list the current role's procedures:

```plpgsql
select t.v as proc_name
from s0.my_subprogram_names(array['p']) as t(v);
```

This is the result:

```output
 proc_name 
-----------
 p
```

Finally, list the current role's functions and procedures:

```plpgsql
select t.v as subprogram_name
from s0.my_subprogram_names() as t(v);
```

This is the result:

```output
   subprogram_name   
---------------------
 f
 my_subprogram_names
 p
```

### Returning a set of record values

The syntax here is a trivial extension of the syntax for returning a single record value. Try this:

```plpgsql
create function s0.my_subprograms(
  kinds_in text[] = null,
  schema out name, name out name, kind out text)
  returns setof record
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  with s(schema, name, kind) as (
    select n.nspname, p.proname, p.prokind::text
    from
      pg_proc as p
      inner join
      pg_namespace as n
      on p.pronamespace = n.oid
      inner join pg_roles as r
      on p.proowner = r.oid
    where r.rolname = current_role
    and   p.prokind = any (array['f'::"char", 'p'::"char"]))
  select schema, name, case kind
                          when 'f' then 'function'
                          when 'p' then 'procedure'
                        end
  from s
  where kind = any(
                    case kinds_in
                      when array['f'] then array['f']
                      when array['p'] then array['p']
                      else                       array['f', 'p']
                    end
                  )
  order by schema, name;    
$body$;
```

Now test _s.my_subprograms()_. First, list the current role's functions:

```plpgsql
select schema, name as func_name
from s0.my_subprograms(array['f']);
```

This is the result:

```output
 schema |      func_name      
--------+---------------------
 s0     | my_subprogram_names
 s0     | my_subprograms
 s1     | f
```

Next, list the current role's procedures:

```plpgsql
select schema, name as proc_name
from s0.my_subprograms(array['p']);
```

This is the result:

```output
 schema | proc_name 
--------+-----------
 s2     | p
```

Finally, list the current role's functions and procedures:

```plpgsql
select schema, name as subprogram_name, kind
from s0.my_subprograms();
```

This is the result:

```output
 schema |   subprogram_name   |   kind    
--------+---------------------+-----------
 s0     | my_subprogram_names | function
 s0     | my_subprograms      | function
 s1     | f                   | function
 s2     | p                   | procedure
```

### Alternative syntax: using "returns table(...)" rather than "returns setof record"

Try this:

```plpgsql
create function s0.my_subprograms_2(kinds_in text[] = null)
  returns table(schema name, name name, kind text)
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  with s(schema, name, kind) as (
    select n.nspname, p.proname, p.prokind::text
    from
      pg_proc as p
      inner join
      pg_namespace as n
      on p.pronamespace = n.oid
      inner join pg_roles as r
      on p.proowner = r.oid
    where r.rolname = current_role
    and   p.prokind = any (array['f'::"char", 'p'::"char"]))
  select schema, name, case kind
                          when 'f' then 'function'
                          when 'p' then 'procedure'
                        end
  from s
  where kind = any(
                    case kinds_in
                      when array['f'] then array['f']
                      when array['p'] then array['p']
                      else                       array['f', 'p']
                    end
                  )
  order by schema, name;    
$body$;
```

Notice that the defining _select_ statement enquoted by _$body$ ... $body$_ is identical to the one that implemented the function _s0.my_subprograms()_. The critical differences are in the function's header:

- The _out_ formal arguments are simply omitted.
- The _setof record_ operand of the _returns_ keyword is replaced by _table(...)_.
- The information about the names and data types of the fields of the returned records is conveyed by the list within the parentheses that follow the keyword _table_ rather than by the list of _out_ formal arguments.

This syntax feels more intuitive than the syntax that _returns setof record_ uses. (It is newer, in the history of PostgreSQL, than the _returns setof record_ syntax.) The function _s0.my_subprograms_2()_ is invoked using the identical syntax that is used to invoke _s0.my_subprograms()_:

```plpgsql
select schema, name as func_name
from s0.my_subprograms_2(array['f']);

select schema, name as proc_name
from s0.my_subprograms_2(array['p']);

select schema, name as subprogram_name, kind
from s0.my_subprograms_2();
```

And the results are identical too, with the obvious trivial difference that _my_subprograms_2()_ itself now shows up in the output. A further benefit of the _returns table()_ syntax is that you use it to return either a set of scalar values or a set of record values.

### The result is delivered by the last "select" (or "values") statement

A _language sql_ function is allowed to encapsulate several SQL statements—but there are few cases where this might be useful. Try this:

```plpgsql
drop function if exists s0.f();
create function s0.f()
  returns table(k int, v text)
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  set local search_path = pg_catalog, s0, pg_temp;
  values (1, 'dog'), (2, 'cat'), (3, 'frog');
$body$;

select k, v from s0.f();
```

It runs without error and produces this result:

```output
 k |  v   
---+------
 1 | dog
 2 | cat
 3 | frog
```

However, the effect of _set local search_path_ in the function's statement list is the same as making the same setting in the function's header. It's therefore poor practice to write the function as presented here.

Now try this counter-example:

```plpgsql
drop function if exists s0.f();
create function s0.f()
  returns table(k int, v text)
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  values ('apple'), ('orange'), ('pear');
  values (1, 'dog'), (2, 'cat'), (3, 'frog');
$body$;

select k, v from s0.f();
```

It, too, runs without error and produces the same result as did the first version of _s0.f()_. The first _values_ statement, though not illegal, has no effect. Notice that its result set has the wrong shape. But this doesn't cause an error because a _language sql_ function's result is delivered by its _final_ statement. This final statement must, therefore, produce results with the shape that the function's header specifies. Deliberately break this rule to see what error this causes:

```plpgsql
drop function if exists s0.f();
create function s0.f()
  returns table(k int, v text)
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  values (1, 'dog'), (2, 'cat'), (3, 'frog');
  values ('apple'), ('orange'), ('pear');
$body$;
```

It fails at _create_ time thus:

```output
ERROR:  42P13: return type mismatch in function declared to return record
DETAIL: Final statement returns text instead of integer at column 1.
```

You'd see the same error if you made _set local search_path = pg_catalog, s0, pg_temp_ the final statement.

### Annotating the rows produced by a table function using "with ordinality"

The _with ordinality_ clause can be used by _any_ function that returns a set: a built-in function; a _language sql_ user-defined function; or a _language plpgsql_ user-defined function. Try this first:

```
select s.k, s.v
from generate_series(13, 40, 7) with ordinality as s(v, k);
```

This is the result:

```output
 k | v  
---+----
 1 | 13
 2 | 20
 3 | 27
 4 | 34
```

Now try this:

```plpgsql
drop function if exists s0.f();
create function s0.f()
  returns table(k int, v text)
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  with c(k, v) as (
    values (13, 'dog'), (20, 'cat'), (27, 'frog'), (34, 'mouse'))
  select k, v from c order by k;
$body$;

select a.r, a.k, a.v
from s0.f() with ordinality as a(v, k, r);
```

This is the result:

```output
 r |   k   | v  
---+-------+----
 1 | dog   | 13
 2 | cat   | 20
 3 | frog  | 27
 4 | mouse | 34
```

The _with ordinality_ clause can have a well-defined meaning only because a set-returning function (a.k.a. table function) is guaranteed to deliver its results in the exact order in which it computes them. This fact is most useful when a _language plpgsql_ table function is used to format a report. Here's a stylized example:

```plpgsql
drop function if exists s0.f(numeric, numeric, numeric);
create function s0.f(len in numeric, wid in numeric, hght in numeric)
  returns table(line text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  line := 'Computed Dimensions';                 return next;
  line := '———————————————————';                 return next;
  line := '';                                    return next;
  line := '  length: '||to_char(len,  '99D9');   return next;
  line := '  width:  '||to_char(wid,  '99D9');   return next;
  line := '  height: '||to_char(hght, '99D9');   return next;
end;
$body$;

\t on
select s0.f(5.7, 3.3, 1.9);
\t off
```

The _select_ statement has no _order by_ clause—and nor is this needed. (Notice that it's legal to elide the _select_ list here.) This is the result:

```plpgsql
 Computed Dimensions
 ———————————————————
 
   length:   5.7
   width:    3.3
   height:   1.9
```

This example gives you a hint about how you might choose between _language sql_ and _language plpgsql_. It's simple to rewrite it as _language sql_ by using this implementation:

```plpgsql
$body$
  values
    ('COMPUTED DIMENSIONS'),
    ('———————————————————'),
    (''),
    ('  length: '||to_char(len,  '99D9')),
    ('  width:  '||to_char(wid,  '99D9')),
    ('  height: '||to_char(hght, '99D9'));
$body$;
```

In this case, then, its terseness makes _language sql_ the winner. But, in general, a report interleaves headings and other annotations with the results from several set-returning _select_ statements. Depending on the ambition level, the implementation of such a report becomes at best unwieldy or, in the limit, impossible with _language sql_ but straightforward with _language plpgsql_.

## Using "language sql" procedures

The following examples demonstrate that there are very few use cases that can be best implemented using a _language sql_ procedure rather than a _language plpgsql_ procedure.

{{< note title="Procedures cannot have 'out' formal arguments." >}}
Try this:

```plpgsql
create schema s;

create table s.t(k serial primary key, v int not null);

create procedure s.p(n inout bigint)
  set search_path = pg_catalog, s, pg_class
  language sql
as $body$
  insert into t(v) select g.v from generate_series(-50, 50, 17) as g(v);
  select count(*) from t;
$body$;

call s.p(null::bigint);
```

This runs without error and produces the expected result:

```output
 n 
---
 6
```

Now rewrite it thus:

```plpgsql
drop procedure s.p(int) cascade;

create procedure s.p(n out int)
  set search_path = pg_catalog, s, pg_class
  language sql
as $body$
  insert into t(v) select g.v from generate_series(-50, 50, 17) as g(v);
  select count(*)::int from t;
$body$;
```

The _create_ fails with the _0A000_ error:

```output
procedures cannot have OUT arguments
```

and the hint says _INOUT arguments are permitted._

This restriction is inherited from PostgreSQL Version 11. Current PostgreSQL does not suffer from this limitation. Therefore, it will be lifted in a later version of YugabyteDB that uses a PostgreSQL version where the limitation has been lifted. GitHub [Issue #12348](https://github.com/yugabyte/yugabyte-db/issues/12348) tracks this.
{{< /note >}}

### Deleting from a table and reporting the outcome

You might find this example convincing. It certainly runs without error and produces the expected outcome:

```plpgsql
create schema s;

create table s.t(k serial primary key, v int not null);
insert into s.t(v) select g.v from generate_series(17, 50, 11) as g(v);

create procedure s.p(lb in int, ub in int, outcome inout int[])
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  with c(v) as (delete from s.t where v between lb and ub returning v)
  select array_agg(v) from c;
$body$;

call s.p(20, 45, null);
```

This is the result:

```output
 outcome 
---------
 {28,39}
```

### Inserting, updating, or deleting in two or several tables in a single transaction

This example, too, runs without error and produces the expected outcome:

```plpgsql
create table s.t1(k serial primary key, v int not null);
create table s.t2(k serial primary key, v int not null);

create procedure s.p1(t1_v in int, t2_v in int)
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  insert into t1(v) values(t1_v);
  insert into t2(v) values(t2_v);
$body$;

call s.p1(17, 42);

select 't1' as "table", k, v from s.t1
union all
select 't2' as "table", k, v from s.t2;
```

Here is the result of the final status query:

```output
 table | k | v  
-------+---+----
 t1    | 1 | 17
 t2    | 1 | 42
```

However, it's very rare that making two or several mutually independent inserts into different tables in a single transaction meets a useful business purpose. Realistic examples of inserting into two tables, or reading from one table and inserting into another table, occur, for example, when one table has a _foreign key_ constraint to the other, when each table has  an autogenerated surrogate primary key, and when the masters table has a unique business key.

- You want to insert a new master row and a few details rows for it. You need first to insert the master row and discover what primary key value it was given. Then you insert the details rows setting their foreign key column values to the value that you discovered for the new master row's primary key.
- You want to insert, update, or delete details rows for an existing master row identified by its unique business key. You need, therefore, first to query the masters table to discover the primary key value and then use that to do the intended inserts, updates, or deletes to the details table.

This is straightforward if you use _language plpgsql_ procedures. Try this:

<a name="insert-master-and-details"></a>

```plpgsql
create table s.masters(
  mk serial primary key,
  mv text not null unique);

create table s.details(
  dk serial primary key,
  mk int not null references s.masters(mk),
  dv text not null);

create procedure s.insert_new_master_and_details(new_mv in text, dvs in text[])
  set search_path = pg_catalog, s, pg_temp
  language plpgsql
as $body$
declare
  new_mk int not null := 0;
begin
  insert into s.masters(mv) values(new_mv) returning mk into new_mk;

  insert into s.details(mk, dv)
  select new_mk, u.v
  from unnest(dvs) as u(v);
end;
$body$;

call s.insert_new_master_and_details('mary', array['skiing', 'running', 'cycling']);

create procedure s.add_details_to_existing_master(old_mv in text, dvs in text[])
  set search_path = pg_catalog, s, pg_temp
  language plpgsql
as $body$
declare
  old_mk int not null := 0;
begin
  select mk into old_mk
  from masters
  where mv = old_mv;

  insert into details(mk, dv)
  select old_mk, u.v
  from unnest(dvs) as u(v);
end;
$body$;

call s.add_details_to_existing_master('mary', array['tennis', 'swimming']);

select mv, dv
from s.masters inner join s.details using(mk)
order by mk, dk;
```

This is the result of the final query, just as is expected:

```output
  mv  |    dv    
------+----------
 mary | skiing
 mary | running
 mary | cycling
 mary | tennis
 mary | swimming
```

Each procedure depends, critically, on holding a value that the first SQL statement returns in a local variable so that the second SQL statement can use it. But a _language sql_ subprogram as no notion that corresponds to a local variable to hold state for the duration of the subprogram call. A temporary table (created using _on commit delete rows_ seems at first to be promising—except, of course, that it would distribute the functionality across too many moving parts. (See, too, the section [The execution model for 'language sql' subprograms](./#the-execution-model-for-language-sql-subprograms) at the start of this page.) However, it turns out anyway to be impractical for another reason. This construct produces the required value:

```plpgsql
with c(v) as (
  insert into s.masters(mv) values('dick') returning mk)
select v from c;
```

And this analogous construct to consume it works fine:

```plpgsql
create table pg_temp.t(v int);
insert into pg_temp.t(v)
with c(v) as (
  values(17))
select v from c;
```

But combining the two ideas fails:

```plpgsql
insert into pg_temp.t(v)
with c(v) as (
  insert into s.masters(mv) values('dick') returning mk)
select v from c;
```

It causes the _0A000_ error:

```output
WITH clause containing a data-modifying statement must be at the top level
```

This example demonstrates vividly that a _language sql_ procedure has severely limited capability. You might like, therefore, to adopt a simple practice rule: always implement user-defined procedures using _language plpgsql_.

## Executing a 'language sql' subprogram versus executing a 'prepared' SQL statement

Each of these approaches lets you set up a _parameterized_ SQL statement for reuse so that each successive invocation can use different actual arguments. Connect as an ordinary user that has _all_ privileges on a sandbox database and do this set-up:

```plpgsql
create schema s;
create table s.t(k serial primary key, v int not null);

prepare stmt_1(int, int) as
insert into s.t(v) select g.v*2 from generate_series($1, $2) as g(v);

create procedure s.p(lb in int, ub in int)
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  insert into t(v) select g.v*3 from generate_series(lb, ub) as g(v);
$body$;

prepare stmt_2(int[]) as
select k, v from s.t where k = any($1) order by k;

create function s.f(in_list in int[])
  returns table(k int, v int)
  set search_path = pg_catalog, s, pg_temp
  security definer
  language sql
as $body$
  select k, v from t where k = any(in_list) order by k;
$body$;
```

Now populate the table:

```plpgsql
execute stmt_1(1, 5);
call s.p(lb=>6, ub=>10);
```

Query it first using the prepared statement:

```plpgsql
execute stmt_2(array[2, 4, 6]);
```

This is the result:

```output
 k | v  
---+----
 2 |  4
 4 |  8
 6 | 18
```

And now query it using the function:

```plpgsql
select k, v from s.f(in_list=>array[3, 5, 7]);
```

This is the result:

```output
 k | v  
---+----
 3 |  6
 5 | 10
 7 | 21
```

The two approaches are very similar in what they achieve:

- Each allows the use of _"placeholders"_ rather than explicit values at definition time.
- Each allows explicit values to be bound to the placeholders at execution time.
- Each allows the work of the syntactic and semantic analysis to be done just once at definition time and then re-used many times at execution time.
- Each allows the reuse of the same execution plan (and the corresponding performance benefit by avoiding recomputing the plan) when sufficient re-executions are detected as using the same plan.

The _prepare-execute_ approach uniquely supports a useful _explain_ thus:

```plpgsql
explain execute stmt_2(array[2, 4, 6]);
```

It produces this result:

```output
 Sort  (cost=4.12..4.13 rows=1 width=8)
   Sort Key: k
   ->  Index Scan using t_pkey on t  (cost=0.00..4.11 rows=1 width=8)
         Index Cond: (k = ANY ('{2,4,6}'::integer[]))
```

But the _explain_ for the _create-function-select-from-function_ approach provides no useful information:

```plpgsql
explain select k, v from s.f(in_list=>array[3, 5, 7]);
```

It produces this result:

```output
 Function Scan on f  (cost=0.25..10.25 rows=1000 width=8)
```

In all other ways, the _create-subprogram-select-from-function_ approach is better than the _prepare-execute_ approach.

- The placeholders in a prepared statement can take only the non-mnemonic form _$1_, _$2_,... but the placeholders in a _language sql_ function can use identifiers to denote  usefully mnemonic names.
- You don't need to arrange for session-pool initialization code to do the _prepare_ step whenever a new connection is added to the pool. Rather, the moral equivalent of that step happens implicitly in any newly-started session when _call the_procedure()_ or _select... from the_function()_ is first executed—so no dedicated initialization code is needed.
- Because a function (unless you choose to create it as a temporary object) is a persistent schema-object, it's available for use by any role to which _usage_ on the schema and _execute_ on the function are granted. In contrast, only the role that executes the _prepare_ statement can execute the statement that it prepared—and only for the lifetime of the session.
- You can use the _security definer_ mode to hide the schema-objects that the procedure(s) and functions(s) manipulate from the code that connects as the client role.
- You can invoke a function in any expression where the value is needed.
- You can add _"set search_path = pg_catalog, pg_temp"_ as a subprogram attribute—in other words, you can enforce the use of qualified identifiers and ensure that the objects that a procedure or function relies upon cannot be captured by a malicious client that manages to create temporary objects and manipulate the value of the _search_path_ at the session level and thereby capture the application's schema-objects. (See the section [Demonstrating how a temporary table, created on-the-fly, can capture an application's intended functionality](../name-resolution-in-subprograms/#demonstrating-how-a-temporary-table-created-on-the-fly-can-capture-an-application-s-intended-functionality).)

## A table function always immediately produces its entire result set

This fact is of no consequence when the table function is used "bare" like in the example that writes the _"Computed Dimensions"_ report in the section [Annotating the rows produced by a table function using "with ordinality"](./#annotating-the-rows-produced-by-a-table-function-using-with-ordinality). But it can have a noticeable negative performance effect if you use a table function in a surrounding SQL statement that imposes a restriction (or even a projection) or especially that uses _limit N_. In other words, there is no scheme analogous to the _"pipelined"_ feature for table functions in Oracle Database; nor is there a mechanism for pushing down predicates into the function.


## Selecting from a 'language sql' subprogram versus selecting from a view

Consider a _security definer_ _language sql_ subprogram with no formal parameters whose _returns_ argument is _setof record_. It's tempting to see it as the functional equivalent of a view. (A view non-negotiably behaves as if it were defined with _security definer_.) Each is a schema-object and, provided that appropriate privileges are granted, can be used by any role.

There are, however, critical mutually exclusive differences. A view wins in these ways:

- When a view is used in a _select_ statement that itself expresses a restriction, the combined effect of the using statement and the view's defining _select_ statement can be flattened into a single statement _before_ the execution plan is determined. This cannot happen with a function.
- When a view's definition meets certain criteria, the view can be the target of an _update_ or _delete_ statement. This cannot happen with a function.

And a function wins in this way:

- A function can, of course, be parameterized. But this cannot be done with a view.
