---
title: Variadic and polymorphic subprograms [YSQL]
headerTitle: Variadic and polymorphic subprograms
linkTitle: Variadic and polymorphic subprograms
description: Defines Variadic and polymorphic subprograms and explains their purpose [YSQL].
menu:
  preview:
    identifier: variadic-and-polymorphic-subprograms
    parent: user-defined-subprograms-and-anon-blocks
    weight: 50
type: docs
---

A _variadic_ subprogram allows one of its formal arguments to have any number of actual values, provided as a comma-separated list, just like the built-in functions _least()_ and _greatest()_ allow.

A _polymorphic_ subprogram allows both its formal arguments and, for a function, its return data type to be declared using so-called polymorphic data types (for example,  _anyelement_ or _anyarry_) so that actual arguments with various different data types can be used, in different invocations of the subprogram, for the same formal arguments. Correspondingly, the data type(s) of the actual arguments will determine the data type of a function's return value.

You can declare _variadic_ or _polymorphic_ arguments, or _polymorphic_ return values, for both _language sql_ and _language plpgsql_ subprograms.

## Variadic subprograms

You mark a subprogram's formal argument with the keyword _variadic_ as an alternative to marking it with _in_, _out_, or _inout_. See the _[arg_mode](../../../ysql/syntax_resources/grammar_diagrams/#arg-mode)_ syntax rule. A subprogram can have at most just one _variadic_ formal argument; and it must be last in the list of arguments. Breaking this rule causes this syntax error:

```outout
42P13: VARIADIC parameter must be the last input parameter
```

### User-defined _mean()_ variadic function

Though there are built-in _least()_ and _greatest()_ _variadic_ functions (corresponding to the built-in _min()_ and _max()_ aggregate functions), there is no built-in _mean()_ _variadic_ function to correspond to the built-in _avg()_ aggregate function. It therefore provides a nice example of how to implement your own. Try this:

```plpgsql
create function mean(
  arr variadic numeric[] = array[1.1, 2.1, 3.1])
  returns numeric
  immutable
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  with c(v) as (
    select unnest(arr))
  select avg(v) from c;
$body$;

select to_char(mean(17.1, 6.5, 3.4), '99.99') as "mean";
```

The result is _9.00_, as is expected. You can see immediately that each actual value that you provide for the _variadic_ formal argument must have (or be typecastable to) the data type of the array with which this argument is declared.

You can transform the _language sql_ version trivially into a _language plpgsql_ function simply by re-writing the body thus:

```output
  ...
  language plpgsql
as $body$
begin
  return (
    with c(v) as (
      select unnest(arr))
    select avg(v) from c);
end;
$body$;
```

The example defines a default value for the _variadic_ formal argument simply to make the point that this is legal. It's probably unlikely that you'll have a use case that benefits from this.

### Choosing between the _variadic_ form and the non-_variadic_ equivalent form

The example makes the point that declaring a formal argument as _variadic_ is simply syntax sugar for what you could achieve without this device by declaring the argument ordinarily as an array. Try this:

```plpgsql
create function mean(arr numeric[])
  returns numeric
  immutable
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  with c(v) as (
    select unnest(arr))
  select avg(v) from c;
$body$;

select to_char(mean(array[17.1, 6.5, 3.4]), '99.99') as "mean";
```

Its body is identical to that of the _variadic_ _language sql_ version—and it produces the same result. The choice between the two approaches is determined entirely by usability: it's less cluttered to invoke the function with a bare list of values than to surround them with the _array[]_ constructor. On the other hand, if the common case is that the values are already available as an array, then you may just as well not choose the _variadic_ implementation. Notice, though, that syntax is defined that lets you invoke a _variadic_ function using an array of values as the actual argument.

Re-create the _variadic_ form of _mean()_. (It doesn't matter whether you use the _language sql_ or the _language plpgsql_ version.) Now invoke it thus:

```plpgsql
select to_char(mean(variadic array[17.1, 6.5, 3.4]), '99.99') as "mean";
```

This runs without error and produces the expected _9.00_ result.

## Polymorphic subprograms

These are the polymorphic data types:

- _anyelement_
- _anyarray_
- _anynonarray_
- _anyenum_
- _anyrange_

See the PostgreSQL documentation section [38.2.5. Polymorphic Types](https://www.postgresql.org/docs/11/extend-type-system.html#EXTEND-TYPES-POLYMORPHIC). These data types are a subset of the so-called pseudo-Types. See the PostgreSQL documentation section [8.21. Pseudo-Types](https://www.postgresql.org/docs/11/datatype-pseudo.html). Notice this from the Polymorphic Types account:

> Note that _anynonarray_ and _anyenum_ do not represent separate type variables; they are the same type as _anyelement_, just with an additional constraint. For example, declaring a function as _f(anyelement, anyenum)_ is equivalent to declaring it as _f(anyenum, anyenum)_: both actual arguments have to be the same _enum_ type.

This section illustrates the use of just _anyelement_ and _anyarray_. See the PostgreSQL documentation section [38.5.10. Polymorphic SQL Functions](https://www.postgresql.org/docs/11/xfunc-sql.html#id-1.8.3.8.18) for more information on this topic.

### Proof-of-concept example #1

Try this. It simply shows an example of how to use the feature.

```plpgsql
create function my_typeof(v in text)
  returns text
  immutable
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select 'non-polymorphic text: '||pg_typeof(v)::text;
$body$;

create function my_typeof(v in int)
  returns text
  immutable
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select 'non-polymorphic int: '||pg_typeof(v)::text;
$body$;

create function my_typeof(v in anyelement)
  returns text
  immutable
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select 'polymorphic: '||pg_typeof(v)::text;
$body$;

create type s.ct as (b boolean, i int);

\x on
\t on
select
  (select my_typeof('dog'))             as "dog",
  (select my_typeof(42))                as "42",
  (select my_typeof(true))              as "true",
  (select my_typeof((true, 99)::s.ct))  as "(true, 99)::s.ct";
\t off
\x off
```

It runs without error and produces this result:

```output
dog              | non-polymorphic text: text
42               | non-polymorphic int: integer
true             | polymorphic: boolean
(true, 99)::s.ct | polymorphic: s.ct
```

The function _my_typeof()_ adds no functionality beyond what the _pg_typeof()_ built-in function exposes. So the code example has no value beyond this pedagogy:

- The built-in function _pg_typeof()_ is itself polymorphic. The \\_df_ meta-command shows that its input formal argument has the data type _"any"_. (The double quotes are used because _any_ is a SQL reserved word.) Notice that the designer of this built-in could just as well have defined it with an input formal argument of data type _anyelement_.
- A user-defined subprogram with an input formal argument of data type _anyelement_ can be among a set of distinguishable overloads where others in the set have input formal arguments of data type, for example, _text_ or _int_.

### Proof-of-concept example #2

Try this. It simply shows another example of how to use the feature.

```plpgsql
create function array_from_two_elements(
  e1 in anyelement, e2 in anyelement)
  returns anyarray
  immutable
  set search_path = pg_catalog, pg_temp
  language sql
as $body$
  select array[e1, e2];
$body$;

create type s.ct as (k int, t text);

create view pg_temp.v(arr) as
select array_from_two_elements((17, 'dog')::ct, (42, 'cat')::ct);

with c(e) as (
  select unnest(arr) from pg_temp.v)
select (e).k, (e).t from c;
```

It produces this result:

```plpgsql
 k  |  t  
----+-----
 17 | dog
 42 | cat
```

Here, too, the function _array_from_two_elements()_ adds no functionality beyond what the native array constructor exposes. So the code example has no value beyond this pedagogy:

- It shows that when two elements (i.e. scalar values) of data type _ct_ are listed in an array constructor, the emergent data type of the result is _ct[]_.
- In other words, when the input formal arguments have the data type _anyelement_, the _returns anyarray_ clause automatically accommodates this correctly.

This makes the same point more explicitly:

```plpgsql
select pg_typeof(arr) from pg_temp.v;
```

This is the result:

```output
 pg_typeof 
-----------
 ct[]
```

## Combining variadic and polymorphic functionality in a single user-defined function

A _variadic_ function can be _polymorphic_ too. Simply declare its last formal argument as _variadic anyarray_. Argument matching, and the determination of the actual result type, behave as you'd expect.

### Example: _mean()_ for numeric values or single character values

Don't be concerned about the artificiality of this example. Its aim is simply to illustrate how the low-level functionality works. If the _variadic_ input is a list of _numeric_ values, then the body code detects this (using the _pg_typeof()_ built-in function) and branches to use the code that the non-_polymorphic_ implementation shown in the section [User-defined _mean()_ variadic function](#user-defined-mean-variadic-function) above uses. But if the _variadic_ input is a list of _s.one_character_ values (where _s.one_character_ is a user-defined domain based on the native _text_ data type whose constraint ensures that the length is exactly one character), then the implementation uses a different method:

- It converts each single character _text_ value to a _numeric_ value with the _ascii()_ built in function.
- It calculates the mean of these numeric values in the same way that the implementation for the mean of _numeric_ input values does.
- It uses the _round()_ built in function, together with the _::int_ typecast to convert this mean to the nearest _int_ value.
- It converts this _int_ value back to a single character _text_ value using the _chr()_  built in function.

Create the constraint function, the domain, and the _variadic polymorphic_ function thus:

```plpgsql
create schema s;

create function s.is_one_character(t in text)
  returns boolean
  set search_path = pg_catalog, pg_text
  language plpgsql
as $body$
declare
  msg constant text not null := '«%s» is not exactly one character character';
begin
  assert length(t) = 1, format(msg, t);
  return true;
end;
$body$;

create domain s.one_character as text
constraint is_one_character check(s.is_one_character(value));

create function s.mean(arr variadic anyarray)
  returns anyelement
  immutable
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  n  numeric         not null := 0.0;
  t  s.one_character not null := 'a';
begin
  assert cardinality(arr) > 0, 'cardinality of arr < 1';
  declare
    d_type constant regtype not null := pg_typeof(arr[1]);
  begin
    case d_type
      when pg_typeof(n) then
        with c(v) as (select unnest(arr))
          select avg(v) from c into n;
        return n;

      when pg_typeof(t) then
        with c(v) as (select unnest(arr))
          select chr(round(avg(ascii(v)))::int) from c into t;
        return t;
    end case;
  end;
end;
$body$;
```

Test it with a list of _numeric_ values.

```plpgsql
select to_char(s.mean(17.1, 6.5, 3.4), '99.99') as "mean";
```

This is the result:

```output
  mean  
--------
   9.00
```

Now test it with a list of _s.one_character_ values:

```plpgsql
select s.mean(
  'c'::s.one_character,
  'e'::s.one_character,
  'g'::s.one_character);
```

This is the result:

```output
 mean 
------
 e
```

Notice that the argument of the _returns_ keyword in the function header is _anyelement_. This has a strictly defined meaning. The internal implementation detects the data type of the input array's elements. And then it checks that, however the user-defined implementation manages this, the data type of the computed to-be-returned value matches the data type of the input array's elements. If this requirement isn't met, then you're likely to get a run-time error when the run-time interpretation of your code attempts to typecast the value that you attempt to return to the expected actual data type for _anyelement_ in the present execution. For example, if you simply use this hard-coded return:

```output
return true::boolean;
```

then you'll get this run-time error:

```output
P0004: «true» is not exactly one character character
```

because, of course, the _text_ typecast of the boolean value has four characters. You can experiment by adding a leg to the _case_ statement to handle a _variadic_ input list of some user-defined composite type values—and then hard-code a non-composite value for the _return_ argument. You'll see this run-time error:

```
42804: cannot return non-composite value from function returning composite type
```

### Example: _mean()_ for numeric values or rectangle area values

This example emphasizes the meaning of writing _returns anyelement_ by specifying that the returned mean should always be _numeric_—both when the _variadic_ input is a list of scalar _numeric_ values and when it's a list of _rectangle_ composite type values. Here, you simply write what you want: _returns numeric_. Create the function thus:

```plpgsql
create schema s;
create type s.rectangle as (len numeric, wid numeric);

create function s.mean(arr variadic anyarray)
  returns numeric
  immutable
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  c    constant int         not null :=  cardinality(arr);
  n             numeric     not null := 0.0;
  r             s.rectangle not null := (1.0, 1.0);
  tot           numeric     not null  := 0.0;
begin
  assert c > 0, 'cardinality of arr < 1';
  declare
    d_type constant regtype not null := pg_typeof(arr[1]);
  begin
    case d_type
      when pg_typeof(n) then
        with c(v) as (select unnest(arr))
          select avg(v) from c into n;
        return n;

      when pg_typeof(r) then
        foreach r in array arr loop
          tot := tot + r.len*r.wid;
        end loop;
        return tot/c::numeric;
    end case;
  end;
end;
$body$;
```

Test it with the same list of _numeric_ values that you used to test the previous version of _mean()_:

```plpgsql
select to_char(s.mean(17.1, 6.5, 3.4), '99.99') as "mean";
```

Of course, the result is the same for this version of _mean()_ as for the previous version:

```output
  mean  
--------
   9.00
```

Now test it with a list of _rectangle_ values:

```plpgsql
select to_char(s.mean(
    (2.0, 3.0)::s.rectangle,
    (3.0, 9.0)::s.rectangle,
    (5.0, 7.0)::s.rectangle),
  '99.99') as "mean";
```

This is the result:

```output
  mean  
--------
  22.67
```

## One polymorphic subprogram _versus_ an overload set of non-polymorphic subprograms

Recast the previous _polymorphic_ implementation of the _variadic_ _mean()_ as two separate non-_polymorphic_ implementations, thus:

```plpgsql
drop function if exists s.mean(anyarray) cascade;

create function s.mean(arr variadic numeric[])
  returns numeric
  immutable
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
begin
  assert cardinality(arr) > 0, 'cardinality of arr < 1';
  return (
    with c(v) as (select unnest(arr))
      select avg(v) from c);
end;
$body$;

create function s.mean(arr variadic s.rectangle[])
  returns numeric
  immutable
  set search_path = pg_catalog, pg_temp
  language plpgsql
as $body$
declare
  c    constant int         not null :=  cardinality(arr);
  r             s.rectangle not null := (1.0, 1.0);
  tot           numeric     not null  := 0.0;
begin
  assert cardinality(arr) > 0, 'cardinality of arr < 1';
  foreach r in array arr loop
    tot := tot + r.len*r.wid;
  end loop;
  return tot/c::numeric;
end;
$body$;
```

Test it with the same two _select_ statements that you used to test the _polymorphic_ version. The results are identical.

Choosing between the different approaches can only be a judgement call—informed by intuition born of experience.

- The total source code size of the two non-_polymorphic_ implementations, here, is greater than that of the single _polymorphic_ implementation. But this is largely explained by the repetition, with the non-_polymorphic_ implementations, of the boilerplate source text of the function headers.
- The implementation of each non-_polymorphic_ implementation is simpler than that of the _polymorphic_ version because no user-written code is needed to detect the data type of the _variadic_ argument list; rather, this is done behind the scenes by the run-time system when it picks the appropriate overload.
- The _polymorphic_ implementation encapsulates all the implementation variants in a single code unit. This is beneficial because (short of using dedicated schemas to group functionally related subprogram overloads) you'd have to rely on external documentation to explain that the different _name()_ variants  belonged together as a set.
