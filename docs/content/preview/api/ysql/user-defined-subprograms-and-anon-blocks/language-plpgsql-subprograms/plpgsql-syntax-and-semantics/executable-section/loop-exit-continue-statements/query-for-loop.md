---
title: Query for loop [YSQL]
headerTitle: The "query for loop"
linkTitle: Query for loop
description: Describes the syntax and semantics of the "query for loop" [YSQL]
menu:
  preview:
    identifier: query-for-loop
    parent: loop-exit-continue-statements
    weight: 40
type: docs
showRightNav: true
---

The _query for loop_ has three variants according to how the query is defined:

- with a subquery
- with a so-called _bound ref cursor variable_
- with a dynamic _SQL_ statement

## "Query for loop" with a subquery

Do this:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create table s.t(k serial primary key, v int not null);
insert into s.t(v) select generate_series(0, 24, 5);

create function s.f(k_lo in int, k_hi in int)
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  rec record;
begin
  for rec in (
    select k, v
    from s.t
    order by k)
  loop
    z := rpad(rec.k::text, 10)||rec.v::text;      return next;
  end loop;
  z := '';                                        return next;
  for rec in (
    select v
    from s.t
    where k between k_lo and k_hi
    order by k)
  loop
    z := rec.v::text;                             return next;
  end loop;
end;
$body$;
```

Test it thus:

```plpgsql
select s.f(2, 4);
```

This is the result:

```plpgsql
 1         0         
 2         5         
 3         10        
 4         15        
 5         20        
 
 5
 10
 15
```

Look at the first _query for loop_:

```plpgsql
for rec in (
  select k, v
  from s.t
  order by k)
loop
  z := rpad(rec.k::text, 10)||rec.v::text;        return next;
end loop;
```

Notice these points:

- The _loop variable_ for a _query for loop_ must be explicitly declared.
- The _loop variable_, _rec_, has the data type _record_. This is a so-called pseudo-type. While you _can_ use it as the data type of a subprogram's formal argument or of a PL/pgSQL variable, you cannot use it as the data type of, for example, a table's column. The attempt _"create table s.x(k serial primary key, v record)"_ causes the _42P16_ error:
  ```output
  column "v" has pseudo-type record
  ```
- The _loop variable_, _rec_, can therefore accommodate, at runtime, whatever shape the loop's defining query happens to establish—i.e. it is polymorphic.

The polymorphism is emphasized by the second _query for loop_:

```plpgsql
for rec in (
  select v
  from s.t
  where k between k_lo and k_hi
  order by k)
loop
  z := rec.v::text;                               return next;
end loop;
```

The _select list_ in the first loop is _k, v_. But in the second loop, it's just _v_ alone.

In most cases, you'll simply use ordinary variables or formal arguments (_i.e._ not a _record_) for the _loop variable(s)_ like this:

```plpgsql
create function s.g(v_lo in int, v_hi in int)
  returns table(k_out int, v_out int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  for k_out, v_out in (
    with
      c as (update s.t set v = v + 3 where k between v_lo and v_hi returning k, v)
    select k, v from c
    order by k
    )
  loop
    return next;
  end loop;
end;
$body$;

select k_out, v_out from s.g(3, 7);
```

This is the result:

```output
 k_out | v_out 
-------+-------
     3 |    13
     4 |    18
     5 |    23
```

This example makes another point. The _query for loop_ cannot use an _insert... returning_, an _update... returning_, or a _delete... returning_ statement directly. (The attempt causes a syntax error.) However, the rules of top-level SQL _do_ allow a _select_ statement to use a _common table expression_ (_a.k.a. CTE_) that is defined with such a DML statement that uses a _returning_ clause. See the section [WITH clause—SQL syntax and semantics](../../../../../../the-sql-language/with-clause/with-clause-syntax-semantics/). You can therefore take advantage of this to use a _query for loop_ to iterate over the result set that the _returning_ clause of a DML statement defines.

## "Query for loop" with a bound refcursor variable

See the subsection [Semantics for the "plpgsql_bound_refcursor_declaration" rule](../../../declaration-section/#plpgsql-bound-refcursor-declaration) on the [declaration section](../../../declaration-section/) page.

Try this:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create table s.t(k serial primary key, v int not null);
insert into s.t(v) select generate_series(0, 49, 5);

create function s.f()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  cur cursor(k_lo int, k_hi int) for (
    select k, v
    from s.t
    where k between k_lo and k_hi
    order by k);
begin
  assert pg_typeof(cur)::text = 'refcursor';
  for rec in cur(2, 4) loop
    assert pg_typeof(rec)::text = 'record';
    z := rpad(rec.k::text, 10)||rec.v::text;      return next;
  end loop;
  z := '';                                        return next;
  for rec in cur(7, 9) loop
    z := rpad(rec.k::text, 10)||rec.v::text;      return next;
  end loop;
end;
$body$;

select s.f();
```

This is the result:

```output
 2         5
 3         10
 4         15
 
 7         30
 8         35
 9         40
```

Notice these points:

- With this form of the _query for loop_, the _loop variable_ is an implicitly declared record. (The first _assert_ confirms this.) You cannot use, say, two ordinarily declared _int_ variables, like _k_out_ and _v_out_, even if this would serve your purposes better.
- The cursor is implicitly opened just before the loop's first iteration. And it's implicitly closed when the loop exits.
- The parameterization of the bound cursor variable does not allow the use of, say, _(k_lo in int, k_hi in int)_. The _in/out_ mode can only be _in_—and this is simply implied.
- Even though the spelling of the declaration of the bound refcursor variable, _cur_, seems to declare it as plain _cursor_, the second _assert_ shows that the data type of _cur_ is in fact _refcursor_. The declaration syntax is simply a historical quirk.

Finally, try this:

```plpgsql
create function s.g()
  returns refcursor
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  cur cursor for (
    select k, v
    from s.t
    where k between 2 and 4
    order by k);
begin
  return cur;
end;
$body$;

create function s.h()
  returns table(z text)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  cur refcursor := s.g();
begin
  assert pg_typeof(cur)::text = 'refcursor';
  /*
  for rec in cur loop

  end loop;
  */
end;
$body$;

select s.h();
```

The functions are created without error. And _s.h()_, as written, executes without error. Of course, it's useless because it does nothing. Try to re-create _s.h()_ with the _query for loop_ uncommented. The attempt causes the _42601_ syntax error with this message:

```output
 cursor FOR loop must use a bound cursor variable
```

The status as _bound_ of a variable whose data type is _refcursor_ is established by the special syntax in the _declaration_ section that's used in the function _s.g()_:

```plpgsql
declare
  cur cursor for (
    select k, v
    from s.t
    where k between 2 and 4
    order by k);
```

But this status is retained only within the block statement that the _declaration_ section introduces. The data type of the return value of  _s.g()_ is simply an ordinary _unbound_ _refcursor_—and, as such, this declaration in _s.h()_:

```plpgsql
cur refcursor := s.g();
```

has the identical effect to just this:

```plpgsql
cur refcursor;
```

This means that a _query for loop_ with a bound refcursor variable brings very limited added value beyond what a _query for loop_ with a subquery brings. The function _s.f()_, above, shows what you might find to be useful added value: the same bound refcursor variable is used, but with different parameterizations, twice within the same block statement whose _declaration_ section defines the cursor.

## "Query for loop" with a dynamic _SQL_ statement

Try this:

```plpgsql
\c :db :u
drop schema if exists s cascade;
create schema s;
create table s.t(k serial primary key, v int not null);
insert into s.t(v) select generate_series(0, 49, 5);

create function s.f(k_lo in int, k_hi in int)
  returns table(k int, v int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  qry constant text not null := '
    select k, v 
    from s.t
    where k between $1 and $2
    order by k;';
begin
  for k, v in
    execute qry using k_lo, k_hi
  loop
    return next;
  end loop;
end;
$body$;

select k, v from s.f(5, 8);
```

This is the result:

```output
 k | v  
---+----
 5 | 20
 6 | 25
 7 | 30
 8 | 35
```

You might think that it could help readability by surrounding the dynamically defined query with parentheses like this:

```plpgsql
for k, v in (execute qry using k_lo, k_hi) loop
```

But the attempt causes the _42601_ syntax error and complains about the opening parenthesis.

Finally, try this

```plpgsql
create function s.g(k_lo in int, k_hi in int)
  returns table(k int, v int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  stmt constant text not null := '
    update s.t
    set v = v + 3
    where k between $1 and $2 returning k, v;';
begin
  for k, v in
    execute stmt using k_lo, k_hi
  loop
    return next;
  end loop;
end;
$body$;

select k, v from s.g(5, 8);
```

This is the result:

```output
 k | v  
---+----
 5 | 23
 6 | 28
 7 | 33
 8 | 38
```

In other words, when the loop's SQL statement is defined dynamically, you _can_ use an _insert... returning_ statement directly in the loop's definition. (The same holds for _update... returning_ and _delete... returning._) Notice, though, that the syntax of such a DML statement with a _returning_ clause does not allow an _order by_ clause. You might, therefore, want to use the same construct where the DML statement is defined within a _CTE_ for a _query for loop_ with a dynamic _SQL_ statement that was shown in the example for a _query for loop_ with a subquery, above.