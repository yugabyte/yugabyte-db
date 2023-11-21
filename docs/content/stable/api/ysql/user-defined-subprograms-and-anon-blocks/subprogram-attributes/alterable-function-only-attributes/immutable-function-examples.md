---
title: Immutable function examples [YSQL]
headerTitle: Immutable function examples
linkTitle: Immutable function examples
description: Shows four code examples of immutable function to illustrate the semantics if this volatility setting [YSQL].
menu:
  stable:
    identifier: immutable-function-examples
    parent: alterable-function-only-attributes
    weight: 10
type: docs
---

## Expression-based index referencing immutable function

Try this simple example to shows how the dishonesty might catch out out. First, the basic setup is straightforward:

```plpgsql
deallocate all;
drop table if exists t cascade;
drop function if exists f2(int) cascade;
drop function if exists f1(int) cascade;

create function f1(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return i*2;
end;
$body$;

create table t(k int primary key, v int);
insert into t(k, v)
select g.v, g.v*10
from generate_series(1, 1000) as g(v);

create index i on t(f1(v));

set pg_hint_plan.enable_hint = on;
prepare qry as
select /*+ IndexScan(t) */
  k, v
from t where f1(v) = 3000;

execute qry;
explain execute qry;
```

The query produces this result:

```output
  k  |  v
-----+------
 150 | 1500
```

And the _explain_ produces output:

```output
 Index Scan using i on t  (cost=0.00..7.72 rows=10 width=8)
   Index Cond: (f1(v) = 3000)
```

Suppose that you realize that you designed the function body wrong and that you now want to implement it differently. You might naïvely (but _wrongly_ as you'll soon see) do this:

```plpgsql
create or replace function f1(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return i*3;
end;
$body$;

execute qry;
explain execute qry;
```

The query result is unchanged—in other words, you have a _wrong result_. (And still, as you wanted, the index is used to produce the result.) The problem is that you used _create or replace_, and this is designed to avoid cascade-dropping dependent objects. The index _i_ is just such a dependent object—and so it remained in place. But is was built using the  _old_ definition of the function's behavior. The index is, of course, an explicitly requested cache of the function's results—and so the only choice that you have is to drop it and recreate it when the newly-defined function is in place. You should, therefore, use _drop_ and then a fresh bare _create_ in this scenario. Try this first, to dramatize the outcome:

```plpgsql
drop function f1(int);
```

It cause this error:

```output
2BP01: cannot drop function f1(integer) because other objects depend on it
```

This, therefore, is the proper approach:

```plpgpsql
drop function f1(int) cascade;
create function f1(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return i*3;
end;
$body$;

-- Now re-create the index.
create index i on t(f1(v));
```

Now do this again:

```plpgsql
execute qry;
explain execute qry;
```

Now you get the correct result:

```output
  k  |  v
-----+------
 100 | 1000
```

and the plan remains unchanged:

```output
 Index Scan using i on t  (cost=0.00..7.72 rows=10 width=8)
   Index Cond: (f1(v) = 3000)
```

<a name="always-drop-re-create"></a>
{{< tip title="Always use 'drop' followed by a fresh bare 'create' to change an existing 'immutable' function." >}}
The only safe way to change the definition of a function that's referenced in the definition of an expression-based index is to _drop_ it and then to use a fresh bare _create_ to make it what you want. The same reasoning holds when an _immutable_ function defines a constraint. Yugabyte recommends that, in other cases where you want to change the definition of an _immutable_ function, rather than try to reason about when _create or replace_ might be safe, you instead simply _always_ follow this practice.
{{< /tip >}}

## Constraint referencing immutable function

Here's a simple example:

```plpgsql
drop table if exists t cascade;
drop function if exists t_v_ok(text) cascade;

create table t(k serial primary key, v text not null);

create function t_v_ok(v in text)
  returns boolean
  immutable
  language plpgsql
as $body$
begin
  if not (v ~ '^[a-z]{5}$') then
    raise exception using
      errcode := '23514'::text,
      message := 'Bad "'||v||'" — Must be exactly five lower-case [a-z] characters';
  end if;
  return true;
end;
$body$;

alter table t add constraint t_v_ok
  check(t_v_ok(v));
```

In this case, the constraint could have been defined with far less code, this:

```plgsql
create table t(
  k serial primary key,
  v text not null constraint t_v_ok check(v ~ '^[a-z]{5}$'));
```

But sometimes it's useful, for example, to encapsulate several constraints whose expressions are fairly elaborate in the body of a single function. The function encapsulation also lets you raise an exception, on violation, using specific wording to express the problem. Suppose that you attempt this:

```plpgsql
insert into t(k, v) values(2, 'rabbit');
```

when the constraint's expression is written explicitly in the table's definition, you get this error:

```output
new row for relation "t" violates check constraint "t_v_ok"
```

But with the function encapsulation, as shown here, you get this error:

```output
Bad "rabbit" — Must be exactly five lower-case [a-z] characters
```

Surprisingly, and in contrast to the example of the expression-based index that references a user-defined function, you are _not_ required to mark the function that defines a constraint as _immutable_. But clearly a function that's used this way must be consistent with the rules that allow it to be honestly so marked.

Marking a function that's used to define a constraint _immutable_ reminds you to follow the same practice, should you need to change its definition, as for the expression-based index case:  [Always use 'drop' followed by a fresh bare 'create'](#always-drop-re-create). _Create or replace_ allows you to change the meaning of the constraint-defining function without dropping and re-creating the constraint. And this would leave you with table data in place that violated the new definition of the constraint.

## Immutable function that is dishonestly so marked

This example shows that wrong-results brought by the session-duration caching of a function that is dishonestly marked _immutable_. First, do the set-up. Notice that the result that _dishonestly_marked_immutable()_ returns is affected by the value of the user-defined session parameter _"x.a"_ and so, clearly, successive invocations with the same actual argument might not return the same value. The appropriate marking for this function (as was demonstrated above) is _stable_.

```plpgsql
deallocate all;
drop function if exists dishonestly_marked_immutable(int) cascade;

set x.a = '13';

create function dishonestly_marked_immutable(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return i*(current_setting('x.a')::int);
end;
$body$;

prepare q as
select
  dishonestly_marked_immutable(2) as "With actual '2'",
  dishonestly_marked_immutable(3) as "With actual '3'";

execute q;
```

This is the result:

```output
 With actual '2' | With actual '3'
-----------------+-----------------
              26 |              39
```

Now contrive a wrong result by changing the value of _"x.a"_:

```plpgsql
set x.a = '19';
execute q;
```

The result is unchanged—and so it's now _wrong_ with respect the function's intended behavior. The reason is that the results for the actual arguments _"2"_ and _"3"_ have been cached in the execution plan that hangs off the prepared statement _"q"_. You can demonstrate this by using _[discard plans](https://www.postgresql.org/docs/11/sql-discard.html)_, thus:

```plpgsql
discard plans;
execute q;
```

This is the new result:

```output
 With actual '2' | With actual '3'
-----------------+-----------------
              38 |              57
```

Don't consider this to be an acceptable workaround. The problem is that the function has been marked with the wrong volatility—and so the only way to fix this is to drop is and to re-create it with the correct volatility, _stable_.

{{< tip title="Using DISCARD PLANS, to clear the 'immutable' cache, is generally unsafe." >}}
The present example showed a deliberate error for the sake of pedagogy. And the pedagogy was served simply by clearing the stale cached results in the present session—and only here. But there will be ordinary situations where you don't want cached results for an _immutable_ function to persist after a change that makes these no longer correct.

_discard plans_ affects only the session that issues it. But many concurrent sessions each could have cached such to-be-purged stale results. To do this safely in a way that handles all situations, cluster-wide, you must drop and re-create the _immutable_ function in question. See the tip [Always use 'drop' followed by a fresh bare 'create'](#always-drop-re-create) above.
{{< /tip >}}

## Immutable function that depends, functionally, on another immutable function

Begin with this setup:

```plpgsql
deallocate all;
drop function if exists f1(int) cascade;
drop function if exists f2(int) cascade;

create function f1(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return i*2;
end;
$body$;

create function f2(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return f1(i);
end;
$body$;

prepare q as
select f2(13);

execute q;
```

This is the result:

```output
 f2
----
 26
```

This might seem to be a proper use of the _immutable_ marking—not just for _f1()_; but also for _f2()_, because _f2()_ does nothing but call the _immutable_ _f1()_. However, you must be very careful if you implement this pattern—as the following shows.

Now drop and re-create _f1()_ and then re-execute _"q"_:

```plpgsql
drop function f1(int) cascade;

create function f1(i in int)
  returns int
  immutable
  language plpgsql
as $body$
begin
  return i*3;
end;
$body$;

execute q;
```

You might have expected to see _"39"_—but the result is unchanged! The reason is that PostgreSQL (and therefore YSQL) do not track what the human sees as the dependency of _f2()_ upon _f1()_. This outcome is a consequence of how PL/pgSQL source text is interpreted and executed. This is explained in the section [PL/pgSQL's execution model](../../../language-plpgsql-subprograms/plpgsql-execution-model/).

You must therefore track functional dependencies like _f2()_ upon _f1()_ manually in external documentation. And you must understand that you must intervene manually after changing the definition of _f1()_ by dropping and re-creating _f2()_—even though you use the same source text and other attributes for the new _f2()_ as defined the old _f2()_.

The emphasizes the importance of the advice that the tip [Always use 'drop' followed by a fresh bare 'create'](#always-drop-re-create), above, gives.

A functional dependency like _f2()_ upon _f1()_ might plausibly arise in a scheme that you might adopt to implement a single point of definition for universal constants that are needed by several subprograms. You could create a composite type whose attributes represent the constants together with an _immutable_ function that sets the values of these attributes and returns the composite type instance. For example, such a constant might be a regular expression that represents an undesirable pattern. You might sometimes want just to detect occurrences of the pattern with a _boolean_ function; and you might sometimes want to remove them with a function that returns a stripped version of the input value. These two functions will depend _functionally_ upon the function that returns the constants. If you follow the advice _"label your functions with the strictest volatility category that is valid for them"_ (see the [tip](#pg-doc-on-volatility) above that quotes from the PostgreSQL documentation on function volatility categories), then you will mark both the detection function and the stripping function _immutable_.
