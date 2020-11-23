---
title: Recursive WITH substatement—SQL syntax and semantics
linkTitle: recursive WITH
headerTitle: The recursive WITH substatement
description: This section specifies the syntax and semantics of the recursive WITH substatement
menu:
  latest:
    identifier: recursive-with
    parent: with-clause
    weight: 30
isTocNested: true
showAsideToc: true
---

The optional `RECURSIVE` keyword fundamentally changes the meaning of a `WITH` clause substatement. The recursive variant lets you implement SQL solutions that, without it, at best require verbose formulations involving, for example, self-joins. In the limit, the recursive `WITH` clause lets you implement SQL solutions that otherwise would require procedural programming.

## Syntax

When the optional `RECURSIVE` keyword is used, the [`with_clause_substatement_defn`](../../../syntax_resources/grammar_diagrams/#with-clause-substatement-defn) must be a `SELECT` statement—and this must have a specific form as the `UNION` or `UNION ALL` of the so-called _non-recursive term_ and the _recursive term_, thus:

```
with
  recursive <name>(c1, c2, ...) as (

    -- Non recursive term.
    (
      select ...
    )

    union [all]

    -- Recursive term (notice the recursive self-reference to <name>.
    (
      select ... from <name> ...
    )
  )
select ... from <name> ...;
```
The following minimal example is found in very many articles and in the documentation for several SQL databases.

```plpgsql
with
  recursive r(n) as (

-- Non-recursive term.
    (
      values(1)
    )

    union all

    -- Recursive term.
    (
      select n + 1
      from r
      where n < 5
    )
  )
select n from r order by n;
```
Notice that the parentheses that surround the non-recursive term and the recursive term are not required. They are used for clarity. See the section [The recursive term must be parenthesised to allow this to use a WITH clause](#the-recursive-term-must-be-parenthesised-to-allow-this-to-use-a-with-clause) for a scenario where the parentheses _are_ essential.

This is the result:

```
 n 
---
 1
 2
 3
 4
 5
```

The [Semantics](#semantics) section explains how a recursive `WITH` substatement is evaluated. When you understand this, you can predict the result of this minimal example and, by induction, the result of any arbitrarily complex example.

## Restrictions

### Maximum one recursive WITH clause substatement

The attempt to define more than one recursive substatement within a particular `WITH` clause causes a generic _42601_ syntax error. You can work around this restriction by pushing it down by one level of nesting, thus:

```plpgsql
with
  a1(n) as (
    select 42),

  a2(n) as (
    with
      recursive r(n) as (
        values(1)

        union all

        select n + 1
        from r
        where n < 5
        )
    select n from r),

  a3(n) as (
    select 99)

(
  select n from a1
  union all
  select n from a2
  union all
  select n from a3
)
order by n desc;
```

This is the result:

```
 n  
----
 99
 42
  5
  4
  3
  2
  1
```

### The recursive WITH clause substatement must be first in the clause

This code:

```plpgsql
with
  a1(n) as (
    select 42),

  recursive r(n) as (
    values(1)

    union all

    select n + 1
    from r
    where n < 5
    ),

  a2(n) as (
    select 99)

(
  select n from r
  union all
  select n from a2
)
order by n desc;
```

causes a _42601_ syntax error to be reported for the line `recursive r(n) as...` If you remove this:

```
a1(n) as (
    select 42),
```

then the statement executes without error to produce this result:

```
 n  
----
 99
  5
  4
  3
  2
  1
```

Alternatively, you can push down the recursive `WITH` clause substatement one level as shown above.

### The recursive term must be parenthesised to allow this to use a WITH clause

First try this positive example:

```plpgsql
with
  recursive r(n) as (
    (
      with a1(n) as (
        values(1))
      select n from a1
    )

    union all

    (
      with a2(n) as (
        select n + 1
        from r
        where n < 5)
      select n from a2
    )
  )
select n from r order by n;
```

Notice that this is simply a _syntax_ example. Using `WITH` clauses within the recursive and non-recursive terms brings no value. The statement executes without error and produces this result:

```
 n 
---
 1
 2
 3
 4
 5
```

Next, first, remove the parenthesis pair that surrounds the non-recursive term. The statement runs without error to produce the same result. Now, re-instate this pair and remove the parenthesis pair that surrounds the recursive term. You get the generic _42601_ syntax error, reported for this line:

```
with a2(n) as (
```

### Aggregate functions are not allowed in a recursive term

Try this:

```plpgsql
with
  recursive r(n) as (
    (
      values(1)
    )

    union all

    (
      select max(n) + 1
      from r
      where n < 5
    )
  )
select n from r order by n;
```

It causes this specific error:

```
42P19: aggregate functions are not allowed in a recursive query's recursive term
```
The restriction, more carefully stated, disallows aggregate functions when the `FROM` list item is the recursive self-reference. By extension, the keywords `GROUP BY` and `HAVING` are also disallowed when the `FROM` list item is the recursive self-reference.

Here is an example that executes without error that uses an aggregate function on a `FROM` list item that is _not_ the recursive self-reference:

```plpgsql
set client_min_messages = warning;
drop table if exists t cascade;
create table t(n int primary key);
insert into t(n) values (1), (2), (3);

with
  recursive r(n) as (
    (
      values(1)
    )

    union all

    (
      select n + (select min(n) from t)
      from r
      where n < 5
    )
  )
select n from r order by n;
```

This is the results:

```
 n 
---
 1
 2
 3
 4
 5
```

## Semantics

You can find various formulations of the following explanation by Internet search. In particular, here is a version in the [PostgreSQL Version 11 documentation](https://www.postgresql.org/docs/11/queries-with.html).

In informal prose:

- The non-recursive term is invoked just once and establishes a starting relation.
- The recursive term is invoked time and again. On its first invocation, it acts on the relation produced by the evaluation of the non-recursive term. On subsequent invocations, it acts on the relation produced by its previous invocation.
- Each successive term evaluation appends its output to the growing result of the recursive substatement.
- The repeating invocation of the recursive term stops when it produces an empty relation.

A compact, and exact, formulation is given by using pseudocode. The `RECURSIVE_WITH_RESULTS` table and the `WORKING_RESULTS` table are transient, statement-duration, structures.

```
Evaluate the non-recursive term.

Initialize the RECURSIVE_WITH_RESULTS table with the result rows.

Initialize the WORKING_RESULTS table with the result rows.

while WORKING_RESULTS table is not empty loop

  Evaluate the recursive term, substituting the current contents of the WORKING_RESULTS
  table for the recursive self-reference.

  Append the result rows into the RECURSIVE_WITH_RESULTS table.

  Truncate the WORKING_RESULTS table and insert the result rows into this.

end loop
```

### Pseudocode implementation: example 1

This pseudocode can be easily implemented, as a PL/pgSQL procedure, for the minimal example shown in the [Syntax](#syntax) section above. First, do this set-up:

```plpgsql
set client_min_messages = warning;

drop procedure if exists recursive_with_semantics_1 cascade;
drop table if exists recursive_with_results cascade;
drop table if exists working_results cascade;

create table recursive_with_results(n int primary key);
create table working_results(n int primary key);
```
Now create the procedure:

```plggsql
create procedure recursive_with_semantics_1(max_n in int)
  language plpgsql
as $body$
<<b>>declare
  n    int not null := 0;
  lvl  int not null := 0;
begin
  -- Emulate the non-recursive term.
  truncate table recursive_with_results;
  truncate table working_results;
  insert into recursive_with_results(n) values (1);
  insert into working_results       (n) values (1);

  -- Emulate the recursive term.
  while ((select count(*) from working_results) > 0) loop
    lvl := lvl + 1;
    -- This "select into" works only for the present special case
    -- where just one row is accumulated with each iteration.
    -- Notice the use of "strict". An error will be drawn if
    -- the "one row per trip" assumption doesn't hold.
    select a.n + 1 into strict b.n from working_results a;

    truncate table working_results;

    if n <= max_n then
      insert into recursive_with_results(n) values(b.n);
      insert into working_results       (n) values(b.n);
    end if;
  end loop;
end b;
$body$;
```

Notice that the [PostgreSQL Version 11 documentation](https://www.postgresql.org/docs/11/queries-with.html) says this:

> Strictly speaking, this process is iteration not recursion, but RECURSIVE is the terminology chosen by the SQL standards committee.

This is a somewhat dubious claim because, in any language, recursion is implemented at a lower level in the hierarchy of abstractions, as iteration. The code of the PL/pgSQL procedure, because it uses a `WHILE` loop, makes this point explicitly.

Now invoke the procedure and observe the contents of the `RECURSIVE_WITH_RESULTS` table:

```plpgsql
call recursive_with_semantics_1(5);
select n from recursive_with_results order by 1;
```

The result is identical to that produced by the SQL implementation that it emulates (shown in the [Syntax](#syntax) section above).

### Pseudo code implementation: example 2

Try this extended version of the minimal example:

```plpgsql
with
  recursive r(lvl, n) as (

    -- Non-recursive term.
    (
      values (0, 1), (0, 11), (0, 21)
    )

    union all

    -- Recursive term.
    (
      select lvl + 1, n + 1
      from r
      where n < 25
    )
  )
select lvl, n from r order by lvl, n;
```
This is the result:

```
 lvl | n  
-----+----
   0 |  1
   0 | 11
   0 | 21
   1 |  2
   1 | 12
   1 | 22
   2 |  3
   2 | 13
   2 | 23
   3 |  4
   3 | 14
   3 | 24
   4 |  5
   4 | 15
   4 | 25
   5 |  6
   5 | 16
   6 |  7
   6 | 17
   7 |  8
   7 | 18
   8 |  9
   8 | 19
   9 | 10
   9 | 20
  10 | 11
  10 | 21
  11 | 12
  11 | 22
  12 | 13
  12 | 23
  13 | 14
  13 | 24
  14 | 15
  14 | 25
  15 | 16
  16 | 17
  17 | 18
  18 | 19
  19 | 20
  20 | 21
  21 | 22
  22 | 23
  23 | 24
  24 | 25
```

The procedural implementation that emulates the pseudocode is a natural extension of the [first example](#pseudo-code-implementation-example-1). First, do this set-up:

```plpgsql
set client_min_messages = warning;

drop procedure if exists recursive_with_semantics_2 cascade;
drop table if exists recursive_with_results cascade;
drop table if exists working_results cascade;

create table recursive_with_results(
  lvl int not null,
  n int not null,
  constraint recursive_with_results_pk primary key(lvl, n));

create table working_results(
  lvl int not null,
  n int not null,
  constraint working_results_pk primary key(lvl, n));
```
Now create the procedure:

```plggsql
create procedure recursive_with_semantics_2(max_n in int)
  language plpgsql
as $body$
<<b>>declare
  r   working_results   not null := (0, 0);
  arr working_results[] not null := '{}';
begin
  -- Emulate the non-recursive term.
  truncate table recursive_with_results;
  truncate table working_results;
  insert into recursive_with_results(lvl, n) values (0, 1), (0, 11), (0, 21);
  insert into working_results       (lvl, n) values (0, 1), (0, 11), (0, 21);

  -- Emulate the recursive term.
  while ((select count(*) from working_results) > 0) loop
    select array_agg((a.lvl + 1, a.n + 1)) into strict b.arr from working_results a;

    truncate table working_results;

    foreach r in array arr loop
      if r.n <= max_n then
        insert into recursive_with_results(lvl, n) values(r.lvl, r.n);
        insert into working_results       (lvl, n) values(r.lvl, r.n);
      end if;
    end loop;
  end loop;
end b;
$body$;
```

Notice that, here, each iteration accumulates _three_ rows. So the logic needs to be correspondingly more elaborate.

Now invoke the procedure and observe the contents of the `RECURSIVE_WITH_RESULTS` table:

```plpgsql
call recursive_with_semantics_2(25);
select lvl, n from recursive_with_results order by lvl, n;
```

The result is identical to that produced by the SQL implementation that it emulates.