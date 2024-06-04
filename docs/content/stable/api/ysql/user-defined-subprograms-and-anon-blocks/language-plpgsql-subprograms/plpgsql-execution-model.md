---
title: PL/pgSQL's "create" time and execution model [YSQL]
headerTitle: PL/pgSQL's "create" time and execution model
linkTitle: Create-time and execution model
description: Describes the model for creating and executing PL/pgSQL subprograms. [YSQL].
menu:
  stable:
    identifier: plpgsql-execution-model
    parent: language-plpgsql-subprograms
    weight: 10
type: docs
showRightNav: true
---

## What does creating or replacing a subprogram do?

The SQL statements _[create [or replace] function](../../../the-sql-language/statements/ddl_create_function/)_ and _[create [or replace] procedure](../../../the-sql-language/statements/ddl_create_procedure/)_ do a full syntax check, but just a partial semantics check. Try the following examples. Connect as an ordinarily privileged user (say, _d0$u1_) to a database (say, _d0_) where it can create objects. Prepare for the tests by creating a function to show the PL/pgSQL source code that defines a subprogram owner by the _current_role_,  in schema _s_, and with the specified name:

```plpgsql
\c d0 d0$u0
create schema s;

create function s.prosrc(f in text)
  returns text
  language sql
  set search_path = pg_catalog, pg_temp
as $body$
  select prosrc
  from
    pg_proc p
    inner join
    pg_namespace n
    on p.pronamespace = n.oid
    inner join
    pg_roles r
    on p.proowner = r.oid
  where p.proname = f::name
  and   n.nspname = 's'::name
  and   r.rolname = current_role;
$body$;
```

Now create a function whose source text has an obvious semantic error:

```plpgsql
create function s.f1()
  returns text
  security definer
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a text not null := (select t.v from s.t where t.k = 1);
begin
  return a;
end;
$body$;
```

The table _s.t_ doesn't exist. But nevertheless the _create function_ succeeds. Confirm that its PL/pgSQL source code is recorded in _pg_proc_ thus:

```plpgsql
select s.prosrc('f1');
```

This is the result:

```output
                                                          +
 declare                                                  +
   a text not null := (select t.v from s.t where t.k = 1);+
 begin                                                    +
   return a;                                              +
 end;                                                     +
```

Now attempt to execute _s.f1()_. It's only now that the semantic error shows up, as error _42P01_, thus:

```output
relation "s.t" does not exist
```

In a test where the code allows many different code paths according to, say, the values of the actual arguments, an error like this might remain undetected over several different invocations of the subprogram only to show up when a path is taken that reaches the statement with the error.

Create and populate the missing table and try again:

```plgpgsq
create table s.t(k serial primary key, v text);
insert into s.t(v) values ('cat');
select s.f1();
```

This is the result:

```output
 f1  
-----
 cat
```

Drop the table _s.t_ and see if the function _s.f1()_ remains intact:

```plpgsql
drop table s.t cascade;
select s.prosrc('f1');
```

It does. The keyword _cascade_ doesn't have the effect that you might hope for because no metadata is recorded to say that the function _s.f1()_ depends upon the table _s.t_.

Next, create a second function whose source text, again, has a semantic error:

```plpgsql
create function s.f2()
  returns text
  security definer
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  a s.t.v%type not null := 'dog';
begin
  return a;
end;
$body$;
```

Here, the _42601_ error is reported:

```output
invalid type name "s.t.v%type"
```

(Notice that the error code, in fact, denotes a _syntax_ error.) But, in contrast with the behavior with the function _s.f1()_, the error occurs already on the attempt to _create_ the function rather than not until the attempt to _execute_ it. This query shows no record of _s.f2()_ in _pg_proc_:

```plpgsql
select s.prosrc('f2');
```

If _create [or replace]_ fails, then the statement has no effect. In particular, the content of _pg_proc_ remains unchanged. Only an error-free _create [or replace]_ will store the subprogram's definition in the catalog as its source text and other attributes spread among suitable dedicated columns in _[pg_proc](../../pg-proc-catalog-table/)_.

{{< tip title="Don't try to understand what errors are detected already at 'create' time and what errors are detected only at runtime." >}}
You must accept that an error-free _create [or replace]_ doesn't guarantee the correctness of the function or procedure. This is hardly a new realization. As ever, you must design your tests so that they exercise every possible code path.
{{< /tip >}}

## What is recorded in the catalog following an error-free "create [or replace]"?

Do this:

```plpgsql
create schema s;
create table s.t(k int primary key, c1 boolean, c2 numeric, dummy int);
insert into s.t(k, c1, c2) values (1, true, 17.1), (2, false, 42.3), (3, true, 59.7);

create function S.F(K_IN S.T.K%TYPE, dummy in s.t.dummy%type = 0)
  returns table(z text)
  security definer
  set timezone = 'UTC'
  language plpgsql
as $body$
declare
  v_c1 s.t.c1%type;
  c2_out s.t.c2%type;
begin
  for v_c1, c2_out in (select c1, c2 from s.t where k > k_in order by k) loop
    z := rpad(v_c1::text, 7)||c2_out::text;
    return next;
  end loop;
end;
$body$
  stable
  set search_path = pg_catalog, pg_temp;
```

The arbitrary (and unconventional) use of upper and lower case, and the whimsical placing of the _[language](../../subprogram-attributes/#unalterable-subprogram-attributes)_ and _[security](../../subprogram-attributes/#alterable-subprogram-attributes)_ attributes and one of the two _[set run_time_parameter](../../subprogram-attributes/#alterable-subprogram-attributes)_ attributes before the PL/pgSQL source text and the _[volatility](../../subprogram-attributes/#alterable-function-only-attributes)_ attribute and another _set run_time_parameter_ attribute after the source text is for pedagogic effect. Now use the _"_\\_sf s.f"_ meta-command to see what was recorded in the catalog. This is the output:

```output
CREATE OR REPLACE FUNCTION s.f(k_in integer, dummy integer DEFAULT 0)
 RETURNS TABLE(z text)
 LANGUAGE plpgsql
 STABLE SECURITY DEFINER
 SET "TimeZone" TO 'UTC'
 SET search_path TO 'pg_catalog', 'pg_temp'
AS $function$
declare
  v_c1 s.t.c1%type;
  c2_out s.t.c2%type;
begin
  for v_c1, c2_out in (select c1, c2 from s.t where k > k_in order by k) loop
    z := rpad(v_c1::text, 7)||c2_out::text;
    return next;
  end loop;
end;
$function$
```

This is most certainly _not_ the verbatim form of the _create function_ statement that was submitted. While the PL/pgSQL source text within the dollar quotes (stored in _pg_proc.prosrc_) has been preserved verbatim, there are many other differences:

- The bare _create_ as entered has been rendered by \\_sf_ as _create or replace_.
- The spelling of the dollar quoting has been changed from _$body$_ to _$function$_.
- The case of everything else has been canonicalized so that SQL keywords are rendered in upper case and all other text is rendered in lower case. (The names of schema-objects, formal arguments, and the like are actually rendered with proper respect to their actual case. But in this example, only non-quoted identifiers were used—meaning that the denoted names are all lower case. See the section [Names and identifiers](../../../names-and-identifiers/).)
- The syntax for specifying the default expression for the _dummy_ formal argument uses the keyword _default_ but the text that followed the user's _create_ used specified the default expression by using the alternative _=_ syntax.
- Double and single quotes have been added in  the _set run_time_parameter_ attributes.
- All the attributes have been placed before the PL/pgSQL source text, and the mutual ordering of these attributes has been changed.
- The _%type_ data type designations for the formal arguments have been translated to what they happen to mean _at the moment that the DDL was issued_.

This shows that everything but the PL/pgSQL source text undergoes full analysis at _create_ time. The parsed out information is individually stored in dedicated _pg_proc_ columns so that the original upper/lower case usage and ordering has been lost. For example, the name, the mode, and the data type of each formal argument are represented in the three parallel arrays, _proargnames text[]_, _proargmodes "char"[]_, and _proallargtypes oid[]_. Therefore, because the user-entered text that follows _create [or replace]_ is not recorded verbatim in the catalog, it's impossible to re-create that text. Rather, \\_sf_ generates a canonically represented _create or replace_ statement that has the same effect as what the user entered. Of course, then, the function works as expected. Test it thus:

```plpgsql
select z from s.f(1);
```
This is the result, as expected:

```output
      z      
-------------
 false  42.3
 true   59.7
```

The last bullet point, about the _create_-time translation of the _%type_ designations has significant consequences. Try this.

```pgpgsql
alter table s.t add column c2_new text;
alter table s.t add column dummy_new text;
update s.t set c2_new = c2::text;
alter table s.t rename column c2 to c2_old;
alter table s.t rename column dummy to dummy_old;
alter table s.t rename column c2_new to c2;
alter table s.t rename column dummy_new to dummy;
alter table s.t drop column c2_old;
alter table s.t drop column dummy_old;
```

{{< note title="YSQL doesn't support 'alter table... alter column... set data type'." >}}
The effect of the eight _alter table_ statements together with the one _update_ statement can be achieved with fewer statements in PostgreSQL by using _alter table... set data type_.
{{< /note >}}

These DDL statements, in PostgreSQL, have _almost_ the same effect:

```plpgsql
alter table s.t alter column c2 set data type text;
alter table s.t alter column dummy set data type text;
```

Notice that because _alter table... alter column... set data type_ leaves the data in place in the column whose data type is altered, this implies _implicit_ data conversion using the typecast from the pre-_alter_ data type to the post-_alter_ data type. The DDL has no option to allow you to specify your own conversion method. In contrast, the method that you must use in YSQL necessitates an explicit DML to copy the data from the pre-_alter_ data type in the starting column to the post-_alter_ data type in the replacement column. And this would allow you, if it suited your purpose, to use your own method to convert the data values. (For example, you might want to use _to_char_ and specify a fixed number of decimal digits.) In other words, YSQL forces you to use a more general approach.

Now repeat _"_\\_sf s.f"_. The output starts with this:

```output
CREATE OR REPLACE FUNCTION s.f(k_in integer, dummy integer DEFAULT 0)
```

You can see that the user's intention, that the data type of the _dummy_ formal argument should be whatever the column _s.t.dummy_ has, is no longer honored. It's clear how this happened: it flows from the PostgreSQL design concept, inherited by YSQL, that parses out the meaning of all the user text that follows _create [or replace]_, apart from the PL/pgSQL source text, at the time that the DDL is issued and stores this as atomic facts in _pg_proc_. The net effect is that using _%type_ to define the data type of a subprogram's formal argument has only documentation value.

Notice that _%type_ works differently in PL/pgSQL source text. See the section [How PL/pgSQL's "create" time and execution model informs the approach to patching a database application's artifacts](#how-pl-pgsql-s-create-time-and-execution-model-informs-the-approach-to-patching-a-database-application-s-artifacts).

{{< tip title="You must re-create a subroutine with a formal argument that uses %type when the data type of the column to which this refers is changed." >}}
You may decide to avoid using _%type_ for formal arguments and to specify the data type explicitly. However,  whichever choice you make, you face the same maintenance challenge if the data type of the column that you want the formal argument to match is changed. You must maintain your own dependency documentation that will inform you when subprogram(s) need changes to accommodate data type changes in table columns. And, when called for, you must explicitly _create or replace_ the affected subprograms. If you use _%type_, then you won't need to change the spelling of the _create or replace_ statement; and if you don't use _%type_, then you will need to change the spelling of the _create or replace_ statement. And this is the extent of the benefit that using _%type_ for a subprogram's formal argument brings.

See the section [Specifying a column's data type by using a domain](#specifying-a-column-s-data-type-by-using-a-domain). The approach it describes greatly reduces the likelihood that you will fail to notice where you need to re-create subprograms in response to changes in the structure of tables.
{{< /tip >}}

## Execution model

This wording is taken (but re-written slightly) from the section [PL/pgSQL under the Hood](https://www.postgresql.org/docs/11/plpgsql-implementation.html) in the PostgreSQL documentation:

> The first time that a subprogram is called _within each session_, the PL/pgSQL interpreter fetches the subprogram's definition from _pg_proc_, parses the source text, and produces an _abstract syntax tree_ (a.k.a. _AST_). The AST fully translates the PL/pgSQL statement structure and control flow, but individual expressions and complete SQL statements used in the subprogram are not translated immediately. (Every PL/pgSQL expression is evaluated, at runtime, as a SQL expression.)
>
> As each expression and SQL statement is first executed in the subprogram, the PL/pgSQL interpreter parses and analyzes it to create a prepared statement.
>
> Subsequent visits to that expression or SQL statement reuse the prepared statement. Thus, a subprogram with conditional code paths that are seldom visited will never incur the overhead of analyzing what is never executed within the current session. On the other hand, errors in a specific expression or SQL statement cannot be detected until that part of the subprogram is reached in execution. This brings the outcome that errors that, with a different model, would be detected at _create_ time can (without proper testing) remain undetected until the subprogram has been in production use for some time.

Successive subsequent executions of a particular subprogram in a particular session will, in general, prepare more and more of its SQL statements and expressions as each new execution takes a different control-flow path.

This model brings huge understandability and usability benefits to the application developer:

- The identical set of data types, with identical semantics, is available in both top-level SQL and in PL/pgSQL.
- Expression syntax and semantics are identical in both top-level SQL and in PL/pgSQL.

But the model also brings the application developer some drawbacks:

- Errors in a specific expression or SQL statement cannot be detected until runtime, and then not until (or unless) it is reached. Such an encounter depends on a current execution's control flow. And this is determined by run-time values like the actual arguments with which a subprogram is invoked or the results of executing SQL statements. Some particular programmer error might therefore remain undetected for a long time, even after a subprogram is deployed into the production system.

  Simply mis-spelling the name of a variable, at just one location in a subprogram's source code, can cause this kind of delayed error.

- You must track functional dependencies like subprogram-upon-table, subprogram-upon-subprogram, and so on manually in your own external documentation. And you must drive your plan for making the cascade of accommodating changes to the closure of _functionally_ dependent subprograms, when an object is changed, entirely from this documentation.

## How PL/pgSQL's "create" time and execution model informs the approach to patching a database application's artifacts.

Because neither PostgreSQL nor, correspondingly, YSQL tracks the dependencies that a PL/pgSQL subprogram has on other database objects that it uses, the representation of a subprogram that's already been executed in a particular session will, in general, remain in place and unchanged even when any of its dependency parents are changed.

Notice that various _ad hoc_ mechanisms are in place. For example, empirical tests show that when a variable is declared by defining its data type using _some_relation%rowtype_, a subprogram's in-memory representation _will_ track changes to the referenced table or view—both in PostgreSQL and in YSQL.

The test shown here uses a variable whose data type is defined using _some_relation.some_column%type_. Try it first using PostgreSQL. It behaves the same in any of Version 11 through the current version. Connect as an ordinary role to a suitable sandbox database where the role has the _create_ privilege on the database and do this:

```plpgsql
create schema s;
create table s.t(k serial primary key, v integer);
insert into s.t(k, v) values (1, 42);

create function s.f(k_in in s.t.v%type)
  returns text
  security definer
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  v_type text      not null := '';
  v_out s.t.v%type;
begin
  select pg_typeof(t.v)::text, t.v
  into strict v_type, v_out
  from s.t
  where t.k = k_in;
  return 'pg_typeof(t.v): '  ||v_type                 ||' / '||
         'pg_typeof(v_out): '||pg_typeof(v_out)::text ||' / '||
         'value: '           ||v_out;
end;
$body$;

select s.f(1);
```

This is the result:

```output
 pg_typeof(t.v): integer / pg_typeof(v_out): integer / value: 42
```

Imagine that the requirements  change so that the table _s.t_ must now store _text_ values rather than just _integer_ values. This can be accommodated, because _integer_ values can be typecast to _text values, thus:

```plpgsql
alter table s.t add column v_new varchar(10);
update s.t set v_new = v::text;
alter table s.t rename column v to v_old;
alter table s.t rename column v_new to v;
alter table s.t drop column v_old;

-- Exercise the new functionality
insert into s.t(k, v) values (2, 'dog');
```

In general, you'd expect that any subprogram that accesses the table _s.t_ would need code changes to reflect the change to the table. But in special cases, like this contrived example shows, no code changes are needed. You might think that it's sufficient just to quiesce all client-side activity while the table change is ongoing and then allow activity to start again without restarting each of the client sessions. But this is in general unsafe. You can demonstrate this by using PostgreSQL (but not, as it happens, by using YSQL) thus:

```plpgsql
select s.f(1);
```

This is the result:

```output
pg_typeof(t.v): character varying / pg_typeof(v_out): integer / value: 42
```

The reported data type of the table column, _t.v_, _does_ reflect the table change that was made because it's reported by an ordinary SQL statement—and the representation of a prepared SQL statement _does_ maintain dependency information so that it will be invalidated and recalculated in the present scenario. But the reported data type of the PL/pgSQL variable _v_out_ is _unchanged_ from the pre-patch result. This tells you that the data type for the local variable was determined when the function _s.f()_ was executed for the first time in the session; and that the result of that determination has survived the change to the table's replacement column, still called _v_, but now with data type _text_ and not the original _integer_. The unchanged in-memory representation of the function _s.f()_ means that this test is bound to cause a runtime error when the function selects a _text_ value:

```plpgsql
select s.f(2);
```

It causes the _22P02_ error, thus:

```outout
invalid input syntax for integer: "dog"
```

You can repeat _"select s.f(2)"_ as many times as you like. It always causes the same error. In other words, the failure doesn't cause the in-memory representation of the function _s.f()_ to be marked so that the next execution will cause it to be recomputed. Rather, you must force this by exiting the session and starting a new one. Only then, do you see the intended result for the row. This is what executing _s.f()_, for the two rows of interest, now shows:

```output
 pg_typeof(t.v): character varying / pg_typeof(v_out): character varying / value: 42
 pg_typeof(t.v): character varying / pg_typeof(v_out): character varying / value: dog
```

Now try the same test using YSQL. Its internal implementation, for this particular scenario, _does_ notice the change to the table _s.t_ and marks the in-memory representation of the function _s.f()_ so that it will be re-calculated on the immediate next execution attempt. In other words you don't see wrong results or runtime errors.

{{< note title="Neither PostgreSQL nor YSQL promises a reliable response to changes in a subprogram's dependency parents." >}}
Sometimes, you _do_ see a correct automatic response to a change in a dependency parent. And sometimes you _do not_ see this. Neither RDBMS documents when such responses happen and when they do not. And nor will this stance change. The developers of RDBMSs always retain the right to leave certain observable aspects of behavior undocumented so that they can, when there are good reasons for this, make changes to the internal implementation that bring changes in such undocumented aspects of behavior as unintended side-effects.
{{< /note >}}

This realization leads to a critical practice recommendation.

{{< tip title="Terminate all database clients before making any changes among the database artifacts that implement an application's backend." >}}
In the general case, no patching exercise among a database application's artifacts is done by changing just a single one of these. As mentioned, it's the development shop's responsibility to determine the set of artifacts that must be changed—without the help of a dependency graph that the RDBMS maintains. (This thinking extends to the determination of the set of client-side code changes that are needed.) Of course, from the moment that the first change is made through the moment when all needed changes have been made, the application as a whole cannot be in use because its components are mutually inconsistent.

Terminating all database clients before starting the changes, and re-starting them only when all have been made, guarantees that the in-memory representations of PL/pgSQL subprograms will be current when the application is opened up again for normal use.

This strict approach guarantees correctness. But no attempt that aims to work out, by human analysis, when normal database client sessions might be left connected during patching can do this. It's very unlikely that following the approach that this tip recommends will measurably affect the overall duration of the application's unavailability window that the patching exercise brings.
{{< /tip >}}

## Specifying a column's data type by using a domain

{{< note title="A current YSQL limitation defeats the approach that this section describes." >}}
These DDL statements in the section [What does the catalog record following an error-free "create [or replace]"](#what-does-the-catalog-record-following-an-error-free-create-or-replace) execute without error in YSQL:

```plpgsql
alter table s.t add column c2_new text;
alter table s.t add column dummy_new text;
```

But this DDL statement, critical for the approach that this section describes, currently fails:

```plpgsql
alter table s.t add column v_new s.num_new;
```

It causes this error:

```output
ERROR:  0A000: Rewriting of YB table is not yet implemented
```

The only difference is that the data type of the to-be-added column is given by a user-defined domain rather than by a base type.

The hint refers you to [GitHub issue #13278](https://github.com/yugabyte/yugabyte-db/issues/13278) and tells you to react with _thumbs up_ to raise its priority.

Notice that this error occurs only if the domain definition for the to-be-added column specifies a constraint. Notice, too, that when _%type_ is used in a declaration, only the _data type_ is inherited and constraints that might have been defined on the column to which _%type_ refers are ignored. It's therefore still better to use a domain where you might have thought of using _%type_.
{{< /note >}}

You can use _%type_ and _%rowtype_ for specifying the data type of a subprogram's formal argument  or return value for both _language plpgsql_ subprograms and _language sql_ subprograms. And you can use the same syntax for specifying the data type of a local variable in PL/pgSQL source text. The aim of the syntax is, of course, to give you a single point of definition fr the data type that can be referred to from many sites where it's needed. However, there are many use cases where these referring sites occur for the columns in two or several tables. For example:

- The data type(s) of the column(s) on which a foreign key constraint is defined must match the data type(s) of the column(s) on which the primary key constraint is defined in the referenced table.
- A numeric quantity like the route distance between two points on the Earth's surface will ideally be defined in the same way (for example, must be expressed in kilometers with three decimal digits of precision, must not be negative, must not be greater than, say, the Earth's circumference) and might appear in several different tables for different kinds of routes.

You can't use _%type_ or _%rowtype_ for specifying the data type of a table's column. But a _domain_ meets exactly this use case. And it does so better than _%type_ and _%rowtype_ can do when the referring site is in a subprogram because a domain can represent constraints as well as designating the data type.

Try this example in PostgreSQL using Version 11 or any later version:

```plpgsql
create schema s;
-- Deliberate poor definition of domain "s.num" (upper bound is too small).
create domain s.num as numeric constraint num_ok check(value > 0.0 and value <= 10.0);

create table s.t(k integer primary key, v s.num);
insert into s.t(k, v) values (1, 5);
```

Now create the file _"cr-function.sql"_ thus:

```plpgsql
create function s.f(v_in in s.num)
  returns text
  security definer
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare 
  r text not null := '';
begin
  select k::text into strict r from s.t where v = v_in;
  return r;
end;
$body$;
```

and continue thus:

```plpgsql
\ir cr-function.sql
select s.f(5.0);

-- Improved definition of domain "s.num".
create domain s.num_new as numeric constraint num_ok check(value > 0.0 and value <= 20.0);

-- Causes error 0A000 in YSQL. OK in PostgreSQL.
alter table s.t add column v_new s.num_new;

update s.t set v_new = v::s.num_new;
alter table s.t rename column v to v_old;
alter table s.t rename column v_new to v;
alter table s.t drop column v_old;

-- Without "cascade", this causes error 2BP01:
-- cannot drop type s.num because other objects depend on it.
drop domain s.num cascade; --> drop cascades to function s.f(s.num)

alter domain s.num_new rename to num;
insert into s.t(k, v) values (2, 14.5);

\ir cr-function.sql
select s.f(5.0);
```

If you issue _set client_min_messages = notice_ before doing this, then you'll be notified that _drop domain... cascade_ also drops the function _s.f()_. This means that you're forced to re-create it. But you don't have to change the text of the _create function_ statement. This guarantees that all references to the replaced domain _s.num_, will now use the new definition so that the single point of definition is properly honored.