---
title: Alterable function-only attributes [YSQL]
headerTitle: Alterable function-only attributes
linkTitle: Alterable function-only attributes
description: Describes and categorizes the various attributes that characterize just user-defined functions [YSQL].
image: /images/section_icons/api/subsection.png
menu:
  preview:
    identifier: alterable-function-only-attributes
    parent: subprogram-attributes
    weight: 30
aliases:
type: indexpage
showRightNav: true
---

## Volatility

The _[volatility](../../../syntax_resources/grammar_diagrams/#volatility)_ attribute has these allowed values:

- _volatile_
- _stable_
- _immutable_

The default is _volatile_.

This attribute allows the function's author to state a promise about the timespan over which a given (set of) actual arguments uniquely determines the function's return value. According to what is promised, PostgreSQL (and therefore YSQL) is allowed to cache some number of _"return-value-for-actual-arguments"_ pairs—in pursuit of improving performance. If caching is done, the scope is a single session and the duration is limited to the session's lifetime.

Notice that _"is allowed to cache"_ does not mean _"will cache"_. The mere possibility is critical to the definition of the semantics of volatility. The conditions that make caching more, or less, likely are a separable concern.

Tautologically, PostgreSQL (and therefore YSQL) is unable to detect if the promise is good—so an author who gives a false promise is asking for wrong results.

<a name="pg-doc-on-volatility"></a>
{{< tip title="See the PostgreSQL documentation for more detail." >}}
The section [38.7. Function Volatility Categories](https://www.postgresql.org/docs/11/xfunc-volatility.html) explains some of the more subtle aspects of function _volatility_. In particular, it makes this recommendation:

> For best optimization results, you should label your functions with the strictest volatility category that is valid for them.

The section [43.11.2. Plan Caching](https://www.postgresql.org/docs/11/plpgsql-implementation.html#PLPGSQL-PLAN-CACHING) explains the caching mechanism and how it is tied to the notion of _prepare_ and the possibility that a prepared statement might (or might not) cache its execution plan.
{{< /tip >}}

### volatile

This denotes no promise at all. Here's a compelling demonstration of a function where _volatile_ is the only possible honest choice:

```plpgsql
drop function if exists volatile_result() cascade;

create function volatile_result()
  returns text
  volatile
  language sql
as $body$
  select gen_random_uuid()::text;
$body$;

select volatile_result() as v1, volatile_result() as v2;
```

This is a typical result:

```output
                  v1                  |                  v2
--------------------------------------+--------------------------------------
 0869edfa-af63-4308-8b80-d9d5a58491b8 | 55589cf4-5441-4582-9cf1-688481b37f55
```

The function _volatile_result()_ returns different results upon successive evaluations even during the execution of a single SQL statement.

### stable

This denotes a promise that holds good just for the duration of a SQL statement's execution. The human analyst can readily see that this following function can honestly be marked as _stable_:

```plpgsql
drop function if exists stable_result(text) cascade;

create function stable_result(which in text)
  returns text
  stable
  language sql
as $body$
  select
    case
      when which = 'a' then current_setting('x.a')
      when which = 'b' then current_setting('x.b')
    end;
$body$;
```

The scope of a user-defined session parameter like _"x.a"_ is just the single session that sets it. And, in the example, the human can readily see that the code of _stable_result()_ doesn't set _"a.x"_ or _"x.b"_ — and that no other route exists to setting it from elsewhere while _stable_result()_ is executing. Test it like this:

```plpgsql
set x.a = 'dog';
set x.b = 'cat';

select stable_result('a'), stable_result('b');
```

Clearly, the return values from _stable_result()_ can be different when it's invoked in a new SQL statement thus:

```plpgsql
set x.a = 'frog';
set x.b = 'bird';

select stable_result('a'), stable_result('b');
```

### immutable

When you mark a function as _immutable_, you give permission for PostgreSQL (and therefore YSQL) to build a session-duration cache where the key to a cache-entry is the vector of actual arguments with which the function is invoked and the key's payload is the function's return value.

(The caching mechanism is the prepared statement and the possibility to cache an execution plan with it. But you needn't understand the mechanism in order to understand the semantic proposition.)

Further, if you want to create an expression-based index that references a user-defined function, then it _must_ be marked _immutable_. Without this volatility setting, you get this error:

```output
42P17: functions in index expression must be marked IMMUTABLE
```

Marking a function as _immutable_ expresses a promise that must hold good for the lifetime of the function's existence (in other words, from the moment it's created to the moment that it's dropped) thus:

- The function has no side effects.
- The function is mathematically deterministic—that is, the vector of actual arguments uniquely determines the function's return value.

Nothing prevents you from lying. But doing so will, sooner or later, bring wrong results.

See the section [Immutable function examples](immutable-function-examples/).

## On_null_input

The _[on_null_input](../../../syntax_resources/grammar_diagrams/#on-null-input)_ attribute has these allowed values:

- _called on null input_
- _strict_
- _returns null on null input_

The default is _called on null input_. Notice that _strict_ is simply a synonym for _returns null on null input_.

### called on null input

This allows the function to be executed just as its source code specifies when at least one of its actual arguments is _null_. Function authors must then take the responsibility for handling the case that any actual is _null_ appropriately.

### strict

This instructs YSQL simply to skip executing the function's source code when at least one of its actual arguments is _null_ and simply to return _null_ immediately.

Try this:

```plpgsql
\pset null '<NULL>'
deallocate all;
drop function if exists f(text, int, boolean) cascade;

create function f(t in text, i in int, b in boolean)
  returns text
  called on null input
  language plpgsql
as $body$
declare
  status text not null := '???';
begin
  if t is null then
    status := 'Bad: t is null';

  elsif i is null then
    status :=  'Bad: i is null';

  elsif b is null then
    status :=  'Bad: b is null';

  else
    status :=  'OK.';
  end if;

  return status;
end;
$body$;

prepare q as
select
  (select f('dog', 42,   true)) as test_0,
  (select f(null,  42,   true)) as test_1,
  (select f('dog', null, true)) as test_2,
  (select f('dog', 42,   null)) as test_3;

execute q;
```

This is the result:

```output
 test_0 |     test_1     |     test_2     |     test_3
--------+----------------+----------------+----------------
 OK.    | Bad: t is null | Bad: i is null | Bad: b is null
```

Now try this:

```plpgsql
alter function f(text, int, boolean) strict;
execute q;
```

This is the new result:

```output
 test_0 | test_1 | test_2 | test_3
--------+--------+--------+--------
 OK.    | <NULL> | <NULL> | <NULL>
```

{{< tip title="Always explain your reasoning carefully in the design documentation when you decide to mark a function as 'strict'." >}}
It's quite hard to imagine a plausible use case where you want silently to bypass a function's execution—especially given that a function is not supposed to have side effects.

Yugabyte recommends that when you come across such a use case and decide to mark a function as _strict_, you explain your reasoning very carefully in the design documentation.
{{< /tip >}}

## Parallel_mode

The _[parallel_mode](../../../syntax_resources/grammar_diagrams/#parallel-mode)_ attribute has these allowed values:

- _unsafe_
- _restricted_
- _safe_

The default is _unsafe_. You risk wrong results in a parallel query:

- If you mark a function as parallel _safe_ when it should be marked _restricted_ or _unsafe_,.
- If you mark a function as parallel _restricted_ when it should be marked _unsafe_.

In this way, the _parallel_ mode attribute is like the _volatility_ and _leakproof_ attributes. You must make the marking honestly; and YSQL will not detect if you lie.

#### unsafe

This tells YSQL that the function can't be executed in parallel mode. The presence of such a function in a SQL statement therefore forces a serial execution plan.

You must mark a function as parallel unsafe:

- If it modifies any database state.
- If it makes any changes to the transaction such as using sub-transactions.
- If it accesses sequences or attempts to make persistent changes to settings.

Notice that, because a function ought not to have side effects, you should consider using a procedure instead. If it needs to return a value, then use an _inout_ argument.

#### restricted

You must mark a function as parallel restricted:

- if it accesses temporary tables.
- If it accesses client connection state (\(for examples by using the _current_value()_ built-in function).
- If it accesses a cursor or any miscellaneous backend-local state which the system cannot be synchronized in parallel mode.

For example, the _setseed()_ built-in function sets the seed for subsequent invocations of the _random()_ built-in function; but _setseed()_ cannot be executed other than by the parallelization group leader because a change made by another process would not be reflected in the leader.

#### safe

This tells YSQL that the function is safe to run in parallel mode without restriction.

## Leakproof

The default for this attribute is _not leakproof_. Only a _superuser_ may mark a function as _leakproof_.

Functions and operators marked as _leakproof_ are assumed to be trustworthy, and may be executed before conditions from security policies and security barrier views. This is a component of the [Rules and Privileges](https://www.postgresql.org/docs/11/rules-privileges.html) functionality. See the account of _[create view](https://www.postgresql.org/docs/11/sql-createview.html)_ in the PostgreSQL documentation for the syntax for the _security_barrier_ attribute.

The _leakproof_ attribute indicates whether or not the function has any side effects. A function is considered to be _leakproof_ only if:

- It makes no changes to the state of the database.
- It doesn't change the value of a session parameter.
- It reveals no information about its arguments other than by its return value.

For example, a function is _not leakproof_ if:

- It might raise an error for any particular value for at least one of the actual arguments with which it is invoked.

- It might report the value of least one of the actual arguments with which it is invoked in an error message.

Just as is the case with the _volatility_ attribute, the decision to mark a function as _leakproof_ or _not leakproof_ requires, and depends entirely upon, human judgment. YSQL cannot police the programmer's honesty.

The following demonstration assumes that you can connect to some database as a regular role and as a _superuser_. This code uses the database _demo_ and connects as the regular role _u1_ and the _superuser_ role _postgres_. Change the names to suit your environment.

```plpgsql
\c demo u1

drop schema if exists s1 cascade;
create schema s1;

create function s1.f(i in int)
  returns int
  language plpgsql
  not leakproof
as $body$
begin
  return i*2;
end;
$body$;

create view s1.f_leakproof_status(leakproof) as
select
  proleakproof::text
from pg_proc
where
  pronamespace::regnamespace::text = 's1' and
  proname::text = 'f' and
  prokind = 'f';

select leakproof from s1.f_leakproof_status;
```

This is the result:

```output
 leakproof
-----------
 false
```

Now create a _security invoker_ procedure to mark _s1.f(int)_ as _leakproof_ and execute it:

```plpgsql
create procedure s1.mark_f_leakproof(result in out text)
  security invoker
  language plpgsql
as $body$
begin
  alter function s1.f(int) leakproof;
  result := '"s1.f(int)" is now marked as leakproof';
exception when insufficient_privilege then
  result := 'Only superuser can define a leakproof function.';
end;
$body$;

call s1.mark_f_leakproof('');
```

This is the result, as expected:

```output
                     result
-------------------------------------------------
 Only superuser can define a leakproof function.
```

Notice how procedure _s1.mark_f_leakproof(text)_ is designed:

- It is set to be _security invoker_ so that it will act with the privileges of the invoking role—in this demonstration either the regular role _u1_ or the _superuser_ role _postgres_. This means that its power depends upon knowing the password for the _postgres_ role.
- It is created as a procedure, and not as a function, even though it needs to return a success message, because procedures _do_ something—but functions simply name a computed value that will be used in an expression and ought not to have (regular) side effects.

Now connect as _postgres_ and execute _mark_f_leakproof()_ again:

```plpgsql
\c demo postgres
call s1.mark_f_leakproof('');
```

This is the new result, again as expected:

```output
                 result
----------------------------------------
 "s1.f(int)" is now marked as leakproof
```

Re-connect as _u1_ and check the _leakproof_ status:

```plpgsql
\c demo u1
select leakproof from s1.f_leakproof_status;
```

This is the new result:

```output
 leakproof
-----------
 true
```

## Cost and rows

Each of these attributes takes a positive integer argument. They provide information for the planner to use.

- The _cost_ attribute provides an estimate for execution cost for the function, in units of _cpu_operator_cost_. If the function returns a set, this is the cost per returned row. If the _cost_ is not specified, then _1 unit_ is assumed for C-language and internal functions, and _100 units_ is assumed for functions in all other languages. Larger values cause the planner to try to avoid evaluating the function more often than necessary. (This suggests that the function should be marked with _stable_ or _immutable_ volatility.)

- The _rows_ attribute provides an estimate of the number of rows that the planner should expect the function to return. This is allowed only when the function is declared to return a set. The default assumption is _1000 rows_.
