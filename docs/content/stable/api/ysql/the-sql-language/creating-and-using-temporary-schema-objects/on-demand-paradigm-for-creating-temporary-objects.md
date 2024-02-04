---
title: Recommended on-demand paradigm for creating temporary objects [YSQL]
headerTitle: Recommended on-demand paradigm for creating temporary objects
linkTitle: Paradigm for creating temporary objects
description: Describes the recommended on-demand paradigm for creating temporary objects—given that PostgreSQL has no native feature to perform actions when a session is created. [YSQL]
menu:
  stable:
    identifier: on-demand-paradigm-for-creating-temporary-objects
    parent: creating-and-using-temporary-schema-objects
    weight: 400
type: docs
---

Here are two rather different scenarios to consider.

## Representing statement-duration state during trigger execution

A temporary table is sometimes used to communicate statement-duration state between DML triggers at the different timing points (before/after with statement/row) on a particular table. A temporary table (created with _on commit delete rows_) meets this requirement nicely. But it prompts a question: _"How and when should the temporary table be created?"_ The motivation for a trigger is that it implements the designed response to table changes no matter how, and by whom, these are made. This implies that the design and implementation of the trigger code must not depend on a separate set-up step, needing to be invoked explicitly in a session, in order that the trigger logic can be relied upon.

The natural choice, therefore, is to implement the code to create the required temporary table in the function that implements the _"before... for each statement"_ trigger. This function would also, of course, set the designed initial state for the temporary table. This thinking implies that once the temporary table has been created, it can remain in place for the remainder of the session's duration. Because the _"before... for each statement"_ trigger fires before the triggers at the other timing points, the state that's represented in the temporary table can be safely read and set in the functions that implement these other triggers.

Here are two alternative approaches:

```plpgsql
-- Approach #1.
---------------
begin
  < Do the initialization >
exception
  when undefined_table then
    create temporary table t(...);
    < Do the initialization >
end;
```

and:

```plpgsql
-- Approach #2.
---------------
create temporary table if not exists t(...);
< Do the initialization >
```

At first glance, _Approach #1_ seems preferable because, except for the first time that the block is encountered, it simply does what's needed immediately. However, it has two disadvantages:

- Adding an exception section to a block brings a performance cost because, under the covers, a savepoint is established on entry to the block and is released when the block completes. This is not done when the block has no exception section.
- The code is harder to read and maintain because: _either_, you have to repeat the statements that implement the required initialization, textually, in two places; _or_, you need to encapsulate them in a separate procedure.

In contrast, _Approach #2_ suffers from neither of these disadvantages (the _if not exists_ test is quick and doesn't imply savepoint management). The acknowledged expert subscribers to the _pgsql-general_ email list recommend using _Approach #2_ on readability and self-evident correctness grounds—and doubt if any performance experiment would measure a noticeable difference between the two approaches.

## Representing session-duration state for a set of subprograms that model what a package does in PL/SQL

A PL/SQL package famously supports package global variables and has a specific mechanism (the optional executable section at the end of the package body) where you can write initialization code—especially to set starting values for such global variables. This package initialization code is executed exactly once during a session's lifetime when an element in the package is first used. (You don't need to write code to make sure that this happens. The runtime system looks after this.) There is no such mechanism in PostgreSQL (and nor, therefore, in YSQL). You have, therefore, to design and implement your own scheme when you need this functionality—and to do this on a case by case basis.

Here is a sketch that might give you some ideas for your use case(s). Here are the hypothetical requirements for this example use case:

- Must represent "package state" as five scalars, called _a_, _b_, _c_, _d_, and _e_, of various data types.
- Each of these "package globals" has a specified initial value.
- Must be possible to read, and to write, this state at any time and from any subprogram that is grouped within the "package".

This is exactly what PL/SQL packages provide declaratively.

Here's the solution:

- Follow the recommendation given in the PostgreSQL documentation _[Porting from Oracle PL/SQL](https://www.postgresql.org/docs/11/plpgsql-porting.html)_ to model the "package" as a single schema (call it _pkg_ here) and to manage session-duration private state in temporary table(s). This recommendation implies locating all the subprograms that belong in the "package" in its _pkg_ schema. Of course, the temporary table(s), and any temporary supporting schema-objects that these need, will be created on-demand in the current session's temporary schema and will be referenced using the alias _pg_temp_.

  The recommendation also implies having all of these schema-objects, both permanent and temporary, owned by the same role.

- Implement the five "package globals" as five rows in a single temporary table. Because other "packages", implemented in other schemas, might be in concurrent use in a single session (with its single temporary schema) the "package state" temporary tables for a particular "package" must have names that include the name of the schema that implements the "package" in question—for example, _pg_temp.pkg_globals_.
- Implement a (permanent) procedure _pkg.initialize_if_not_initialized()_ that starts by querying the _pg_class_ catalog table to find out if the table _pg_temp.pkg_globals_ exists yet. If it does, then the procedure takes no action; and if it does not, then the procedure creates the package state table and any supporting temporary schema-objects that it might need, and initializes that state table's rows.

Here is the set-up code:

```plpgsql
-- "d0" is the chosen database and the role "d0$u0" has "all" privileges on it.
\c d0 d0$u0
create schema pkg;

create procedure pkg.initialize_if_not_initialized()
  set search_path = pg_catalog, pg_temp
  security definer
  language plpgsql
as $proc$
declare
  -- The "package" must be initialized exactly once in a session.
  pkg_is_initialized constant boolean not null :=
    exists(
      select 1
      from
        pg_class as c
        inner join
        pg_roles as r
        on c.relowner = r.oid
      where c.relname = 'pkg_globals'
      and   r.rolname = current_role
      and   c.relnamespace = pg_my_temp_schema()
    );
begin
  if not pkg_is_initialized then
    create table pg_temp.pkg_globals(
      k varchar(1) primary key,
      v varchar(30) not null,
      constraint is_initialized_v check (k = any(array['a', 'b', 'c', 'd', 'e']))
      ) on commit preserve rows;

    insert into pg_temp.pkg_globals(k, v) values
      ('a', (42::int)::varchar),
      ('b', 'hot'),
      ('c', (true::boolean)::varchar),
      ('d', (false::boolean)::varchar),
      ('e', ('2023-01-31 21:35:42'::timestamptz)::varchar);

    create function pg_temp.assert_pkg_globals_has_exactly_five_rows()
      returns trigger
      security definer
      language plpgsql
    as $func$
    declare
      n constant int not null := (select count(*) from pg_temp.pkg_globals);
    begin
      assert n = 5, 'table pg_temp.pkg_globals must have exactly five rows';
    end;
    $func$;

    create trigger assert_pkg_globals_has_exactly_five_rows
      after insert or delete
      on pg_temp.pkg_globals
      for each row
    execute function pg_temp.assert_pkg_globals_has_exactly_five_rows();
  end if;
end;
$proc$;

create function pkg.value_of_a()
  returns int
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  call pkg.initialize_if_not_initialized();
  return (select v::int from pg_temp.pkg_globals where k = 'a');
end;
$body$;

create procedure pkg.set_a(new_v in int)
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  call pkg.initialize_if_not_initialized();
  update pg_temp.pkg_globals set v = new_v::varchar where k = 'a';
end;
$body$;
```

The setter procedures and getter functions for the remaining "package globals", _b_, _c_, _d_, and _e_ are not shown. They would follow the same pattern as those for _a_—but the typecasts would reflect the intended data type of each "global variable".

You might think that these devices are overkill:

- the unique temporary index on the column that holds the names of the "package globals"
- the constraint that ensures that only the expected names are used
- the trigger that ensures that the specified five "global variables" are always represented.

However, sensibly written application code will never attempt to update the names of the "global variables" or to insert or delete rows in the _pg_temp.pkg_globals_ table, and so the presence of these devices will never incur a run-time cost.

Start a new session to test it:

```plpgsql
\c d0 d0$u0
select pkg.value_of_a();
```

It shows the specified initial value for the "package global" _a_, i.e. _42_.

Set a new value and observe the outcome:

```plpgsql
call pkg.set_a(17);
select pkg.value_of_a();
```

This confirms that the value of _a_ has been successfully set to _17_.

Now start a second concurrent session to test the privacy of the "package" state:

```plpgsql
\c d0 d0$u0
call pkg.set_a(19);
select pkg.value_of_a();
```

This is the result:

```output
 value_of_a 
------------
         19
```

Go back to the first session and observe the value of _a_ there. It's still _17_. You can now continue with _ad hoc_ testing variously setting and getting the "package" state in each of the two concurrent sessions in arbitrarily interleaving order. You'll see that you always get the value that you most-recently set, in each session, irrespectively of what the other session sets—in other words, the (simulated) package global variables are indeed session-private.

{{< note title="Where to locate the calls to 'pkg.initialize_if_not_initialized()'?" >}}
It's certainly safe to implement every setter procedure and getter function by calling _pkg.initialize_if_not_initialized()_ at the start of each of these. And doing this makes the rest of the code uncluttered and therefore easy to understand. Writing this:

```plpgsql
pkg.set_a(17);
```

 instead of just this:

```plpgsql
a := 17;
```

as you would in PL/SQL brings only a tiny readability burden. And the same goes for this:

```plpgsql
r := pkg.value_of_a() + sqrt(pkg.value_of_b());
```

instead of this:

```plpgsql
r := a + sqrt(b);
```

You might argue that this approach tests, time-and-again, what has already been tested and is therefore less efficient than code that locates the call to _pkg.initialize_if_not_initialized()_ at the start of each subprogram that sets or gets a "package global". (You'd probably still want to implement _language sql_  setter procedures and getter functions for all of the "package globals" simply to avoid the cumbersome textual repetition of the SQL _select_ and _update_ statements that these would encapsulate.) You can do your own performance tests to see if you can detect a difference in the context of typical usage patterns for the application as a whole. It's unlikely that you'd notice a performance difference because YSQL supports catalog queries with various dedicated caching schemes.
{{< /note >}}