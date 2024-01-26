---
title: >
  Recursive CTE: SQL syntax and semantics
linkTitle: Recursive CTE
headerTitle: The recursive CTE
description: This section specifies the syntax and semantics of the recursive CTE
menu:
  preview:
    identifier: recursive-cte
    parent: with-clause
    weight: 30
type: docs
---

The optional `RECURSIVE` keyword fundamentally changes the meaning of a CTE. The recursive variant lets you implement SQL solutions that, without it, at best require verbose formulations involving, for example, self-joins. In the limit, the recursive CTE lets you implement SQL solutions that otherwise would require procedural programming.

## Syntax

When the optional `RECURSIVE` keyword is used, the [`common_table_expression`](../../../syntax_resources/grammar_diagrams/#common-table-expression) must be a `SELECT` statement—and this must have a specific form as the `UNION` or `UNION ALL` of the so-called _non-recursive term_ and the _recursive term_, thus:

```output.sql
with
  recursive r(c1, c2, ...) as (

    -- Non-recursive term.
    (
      select ...
    )

    union [all]

    -- Recursive term. Notice the so-called recursive self-reference to r.
    (
      select ... from r ...
    )
  )
select ... from r ...;
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

Notice that the parentheses that surround the _non-recursive term_ and the _recursive term_ are not required. They are used for clarity. See the section [The _recursive term_ must be parenthesised to allow this to use a WITH clause](#the-recursive-term-must-be-parenthesised-to-allow-this-to-use-a-with-clause) for a scenario where the parentheses _are_ essential.

This is the result:

```output
 n
---
 1
 2
 3
 4
 5
```

The [Semantics](#semantics) section explains how a recursive CTE is evaluated. When you understand this, you can predict the result of this minimal example and, by induction, the result of any arbitrarily complex example.

## Restrictions

The `WITH` clause syntax (see the section [WITH clause—SQL syntax and semantics](../with-clause-syntax-semantics/) implies a pair of restrictions.

{{%ebnf%}}
  with_clause
{{%/ebnf%}}

It shows that you can use the `RECURSIVE` keyword only immediately after the keyword `WITH` and that, therefore only the first CTE in a `WITH` clause can be a recursive CTE. These restrictions are illustrated in the immediately following sections [Maximum one recursive CTE](#maximum-one-recursive-cte) and [The recursive CTE must be first in the clause](#the-recursive-cte-must-be-first-in-the-clause).

### Maximum one recursive CTE

The attempt to define more than one recursive CTE within a particular `WITH` clause causes a generic _42601_ syntax error. You can work around this restriction by pushing it down by one level of nesting, thus:

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

```output
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

### The recursive CTE must be first in the clause

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

```output.sql
a1(n) as (
    select 42),
```

then the statement executes without error to produce this result:

```output
 n
----
 99
  5
  4
  3
  2
  1
```

Alternatively, you can push down the recursive CTE one level as shown above.

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

```output
 n
---
 1
 2
 3
 4
 5
```

Next, first, remove the parenthesis pair that surrounds the _non-recursive term_. The statement runs without error to produce the same result. Now, re-instate this pair and remove the parenthesis pair that surrounds the _recursive term_. You get the generic _42601_ syntax error, reported for this line:

```output.sql
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

```output
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

This is the result:

```output
 n
---
 1
 2
 3
 4
 5
```

## Semantics

You can find various formulations of the following explanation by Internet search. In particular, here is a version from the [PostgreSQL Version 11 documentation](https://www.postgresql.org/docs/11/queries-with.html#id-1.5.6.12.5.4).

In informal prose:

- The _non-recursive term_ is invoked just once and establishes a starting relation.
- The _recursive term_ is invoked time and again. On its first invocation, it acts on the relation produced by the evaluation of the _non-recursive term_. On subsequent invocations, it acts on the relation produced by its previous invocation.
- Each successive term evaluation appends its output to the growing result of the recursive CTE.
- The repeating invocation of the _recursive term_ stops when it produces an empty relation.

### Pseudocode definition of the semantics

A compact, and exact, formulation is given by using pseudocode. The _final_results_ table, the _previous_results_ table, and the _"TEMP_RESULTS"_ table are transient, statement-duration, structures.

> - **Purge the _final_results_ table and the _previous_results_ table.**
>
> - **Evaluate the non-recursive term, inserting the resulting rows into the _previous_results_ table.**
>
> - **Insert the contents of the _previous_results_ table into the _final_results_ table.**
>
> - **while the _previous_results_ table is not empty loop**
>
>   - **Purge the _temp_results_ table.**
>
>   - **Evaluate the recursive term using the current contents of the _previous_results_ table for the recursive self-reference, inserting the resulting rows into the _temp_results_ table.**
>
>   - **Purge the _previous_results_ table and insert the contents of the _temp_results_ table.**
>
>   - **Append the contents of the _temp_results_ table into the _final_results_ table.**
>
>- **end loop**
>
>- **Deliver the present contents of the _final_results_ table so that whatever follows the recursive CTE can use them.**

### PL/pgSQL procedure implementation of the pseudocode : example 1

This pseudocode can be easily implemented, as a PL/pgSQL procedure, for the minimal example shown in the [Syntax](#syntax) section above. First, do this set-up:

```plpgsql
set client_min_messages = warning;

drop procedure if exists recursive_with_semantics_1 cascade;
drop table if exists final_results cascade;
drop table if exists previous_results cascade;
drop table if exists temp_results cascade;

create table final_results(n int primary key);
create table previous_results(n int primary key);
create table temp_results(n int primary key);
```

Now create the procedure:

```plpgsql
create procedure recursive_with_semantics_1(max_n in int)
  language plpgsql
as $body$
begin
  -- Emulate the non-recursive term.
  delete from final_results;
  delete from previous_results;
  insert into previous_results(n) values(1);
  insert into final_results(n) select n from previous_results;

  -- Emulate the recursive term.
  while ((select count(*) from previous_results) > 0) loop
    delete from temp_results;
    insert into temp_results
    select n + 1 from previous_results
    where n < max_n;

    delete from previous_results;
    insert into previous_results(n) select n from temp_results;
    insert into final_results(n) select n from temp_results;
  end loop;
end;
$body$;
```

Notice that the [PostgreSQL Version 11 documentation](https://www.postgresql.org/docs/11/queries-with.html#id-1.5.6.12.5.4) says this:

> Strictly speaking, this process is iteration not recursion, but RECURSIVE is the terminology chosen by the SQL standards committee.

This is a somewhat dubious claim because, in any language, recursion is implemented at a lower level in the hierarchy of abstractions, as iteration. The code of the PL/pgSQL procedure, because it uses a `WHILE` loop, makes this point explicitly.

Now invoke the procedure and observe the contents of the _final_results_ table:

```plpgsql
call recursive_with_semantics_1(5);
select n from final_results order by 1;
```

The result is identical to that produced by the SQL implementation that it emulates (shown in the [Syntax](#syntax) section above).

### PL/pgSQL procedure implementation of the pseudocode : example 2

Try this extended version of the minimal example:

```plpgsql
with
  recursive r(c1, c2) as (

    -- Non-recursive term.
    (
      values (0, 1), (0, 2), (0, 3)
    )

    union all

    -- Recursive term.
    (
      select c1 + 1, c2 + 1
      from r
      where c1 < 4
    )
  )
select c1, c2 from r order by c1, c2;
```

This is the result:

```output
 c1 | c2
----+----
  0 |  1
  0 |  2
  0 |  3

  1 |  2
  1 |  3
  1 |  4

  2 |  3
  2 |  4
  2 |  5

  3 |  4
  3 |  5
  3 |  6

  4 |  5
  4 |  6
  4 |  7
```

The whitespace was added by hand to group the results into those produced first by evaluating the _non-recursive term_ and then those produced by each successive repeat evaluation of the _recursive term_.

The procedural implementation that emulates the pseudocode is a natural extension of the [first example](#pl-pgsql-procedure-implementation-of-the-pseudocode-example-1). First, do this set-up:

```plpgsql
set client_min_messages = warning;

drop procedure if exists recursive_with_semantics_2 cascade;
drop table if exists final_results cascade;
drop table if exists previous_results cascade;
drop table if exists temp_results cascade;

create table final_results(
  c1 int not null,
  c2 int not null,
  constraint recursive_cte_results_pk primary key(c1, c2));

create table previous_results(
  c1 int not null,
  c2 int not null,
  constraint previous_results_pk primary key(c1, c2));

create table temp_results(
  c1 int not null,
  c2 int not null,
  constraint temp_results_pk primary key(c1, c2));
```

Now create the procedure:

```plggsql
create procedure recursive_with_semantics_2(max_c1 in int)
  language plpgsql
as $body$
begin
  -- Emulate the non-recursive term.
  delete from final_results;
  delete from previous_results;
  insert into previous_results(c1, c2) values (0, 1), (0, 2), (0, 3);
  insert into final_results(c1, c2) select c1, c2 from previous_results;

  -- Emulate the recursive term.
  while ((select count(*) from previous_results) > 0) loop
    delete from temp_results;
    insert into temp_results
    select c1 + 1, c2 + 1 from previous_results
    where c1 < max_c1;

    delete from previous_results;
    insert into previous_results(c1, c2) select c1, c2 from temp_results;
    insert into final_results(c1, c2) select c1, c2 from temp_results;
  end loop;
end;
$body$;
```

Notice that, here, each iteration accumulates _three_ rows.

Now invoke the procedure and observe the contents of the _final_results_ table:

```plpgsql
call recursive_with_semantics_2(4);
select c1, c2 from final_results order by c1, c2;
```

The result is identical to that produced by the SQL implementation that it emulates.

The section [Using a recursive CTE to traverse graphs of all kinds](../traversing-general-graphs/) shows how to do graph traversal of undirected and directed graphs using application-agnostic examples. When the graph is cyclic, it shows how to detect and prevent endless repetition.

## Case studies

The remaining sections (in the overall main [WITH clause](../../with-clause/) parent section) describe how to use a recursive CTE to implement path finding for the special case of a hierarchy, for the general case of an undirected cyclic graph (and other more restricted kinds of graph), and for a specific application of the approach for the general graph.

- [Case study—Using a recursive CTE to traverse an employee hierarchy](../emps-hierarchy) describes the use case (traversing an employee hierarchy) that is most commonly used to illustrate a practical application of the recursive CTE. Different SQL databases with different variants of SQL use importantly different approaches. PostgreSQL, and therefore YSQL, have only standard SQL features here (and not, therefore, the `CONNECT BY PRIOR` feature that is typically used with Oracle Database). Neither do they have dedicated syntax to ask for breadth-first or depth-first traversal. Rather, these two kinds of traversal must be programmed explicitly. The explicit solutions use array functionality and are straightforward. Moreover, using this approach allows various second-order display choices easily to be implemented.

- [Using a recursive CTE to traverse graphs of all kinds](../traversing-general-graphs/) leading to [Using a recursive CTE to compute Bacon Numbers for actors listed in the IMDb](../bacon-numbers/). The approach to traversing graphs of all kinds is a natural extension of the approach shown for the employee hierarchy traversal. It adds logic to accommodate the fact that the edges are undirected and for cycle prevention. However, this straightforward approach collapses when, as is the case with the IMBd data, there are very many paths between most pairs of actors. This brings an exponential explosion in both time to completion and use of memory. The Bacon Numbers account shows how to avoid this collapse by implementing the algorithm that the recursive CTE implements using explicit SQL issued from a PL/pgSQL stored procedure. This allows early pruning to leave only the shortest paths with each repetition of the _recursive term_.
