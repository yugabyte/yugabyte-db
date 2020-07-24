---
title: ANY and ALL—test if an element is in an array
linkTitle: =ANY and =ALL
headerTitle: =ANY and =ALL — test if an element is in an array
description: The =ANY and =ALL operators test if an element is in an array
menu:
  latest:
    identifier: any-all
    parent: array-functions-operators
    weight: 10
isTocNested: true
showAsideToc: true
---

## Overview

**Purpose:** Return `TRUE`, `FALSE`, or `NULL` according to the outcome of the particular comparison, `=ANY` or `=ALL`, between the input [LHS](https://en.wikipedia.org/wiki/Sides_of_an_equation) _element_ and input [RHS](https://en.wikipedia.org/wiki/Sides_of_an_equation) array. Each of the LHS and the RHS is, in general, an expression. Further, the RHS expression must be parenthesized.

The value to which the LHS evaluates is compared, using the appropriate data type overload of the `=` operator, to each of the RHS expression's evaluated _elements_ to yield a `boolean` result (`TRUE`, `FALSE`, or `NULL`) according to the normal rules for _element_ comparison. (See the [Semantics demonstration](./#semantics-demonstration) section.)

**Signature**

Each of the `=ANY` and `=ALL` operators has the same signature, thus:

```
input value:       anyelement, anyarray
return value:      boolean
```
**Note:** The term of art _element_ is used in this section for what the pseudo-type name `anyelement` represents. Each of these operators requires that the data type of the _element_ value to which the LHS expression evaluates corresponds to the data type of the array value to which the RHS expression evaluates. For example, when the data type of the LHS _element_ is, say _"t"_, then the RHS array's data type must be _"t[]"_. The RHS array can have any dimensionality. The operators are sensitive only to the actual _elements_ in the array.

Think of `=ANY` and `=ALL` as the spellings of single [binary operators](https://en.wikipedia.org/wiki/Binary_operation). Notice that, `<> ANY`, `<> ALL`, `< ANY`, `>= ALL`, and so on are syntactically legal, and produce outcomes without error. But these outcomes are semantically meaningless.

`=ANY` is functionally equivalent to `IN` (but `IN` is illegal syntax when the RHS is an array).

`=ALL` has no functional equivalent in the way that `=ANY` is functionally equivalent to `IN`. Briefly (and leaving the possibility of `NULL` to the [semantics of the `=ALL` operator](./#the-semantics-of-the-all-operator) section, `=ALL` produces `TRUE` when every _element_ in the RHS array is equal to every other of its _elements_ and when the LHS _element_ is also equal to that value. 

## The semantics of the =ANY operator

These rules govern the result of `=ANY`:

- If either the LHS _element_ expression or the RHS array expression evaluates to `NULL`, then the `=ANY` result is `NULL`.
- If the LHS _element_ is `NOT NULL` and the RHS array has zero _elements_, then the the `=ANY` result is `FALSE`.
- If the succession of comparisons of the LHS _element_ to the RHS array's _elements_ yields at least one `TRUE` result, then the `=ANY` result is `TRUE`. (This holds even if other comparisons yield `NULL`.)
- If every comparison yields`FALSE`, then the `=ANY` result is `FALSE`.
- If no comparison yields`TRUE`, and at least one comparison yields `NULL`, then the `=ANY` result is `NULL`.

`SOME` is a synonym for `ANY`.

The following small test emphasizes the symmetry between `=ANY` and `IN`.

```plpgsql
select (
    42 =any (array[17, 42, 53])
    and
    42 in (17, 42, 53)  
  )::text as b;
```
See the section [The `array[]` value constructor](../../array-constructor/). This is the result:
```
  b   
------
 true
```
Notice that here, _"(17, 42, 53)"_ is _not_ a constructed record value because of the semantic effect of preceding it by `IN`. In contrast, in the following example, _"(17, 42, 53)"_ _is_ a constructed record:
```plpgsql
with v as (select (17, 42, 53) as r)
select pg_typeof(r)::text from v;
```
This outcome is due to the semantic effect of using _"(17, 42, 53)"_ directly as a `SELECT` list item. This, therefore, causes a syntax error:

```
with v as (select (1, 2, 3, 4) as r)
select 1 in r from v;
```

## The semantics of the =ALL operator

The following rules, governing the result of `=ALL`, can be re-written from the rules that govern the result of `=ANY` by applying your understanding of the difference, in ordinary prose, between the words _any_ and _all_:

- If either the LHS _element_ expression or the RHS array expression evaluates to `NULL`, then the `=ALL` result is `NULL`.
- If the LHS _element_ is `NOT NULL` and the RHS array has zero _elements_, then the `=ALL` result is `TRUE`. You might think that this is a counter-intuitive rule definition. But the [PostgreSQL documentation for Version 11.2](https://www.postgresql.org/docs/11/functions-comparisons.html#id-1.5.8.28.17) states this clearly. And the first example in the [Semantics demonstration](./#semantics-demonstration) section is consistent with this definition. Moreover, and as is required to be the case, the behavior of the example is identical using YugabyteDB and PostgreSQL Version 11.2.
- If the succession of comparisons of the LHS _element_ to the RHS array's _elements_ yields no `NULL` results and yields at least one `FALSE` result, then the `=ALL` result is `FALSE`.
- If the succession of comparisons of the LHS _element_ to the RHS array's _elements_ yields at least one `NULL` result, then the `=ALL` result is `NULL`.
- In summary, _only_ if every one of the RHS array's _elements_ is `NOT NULL`, all of these _elements_ are equal to each other, and the LHS _element_ is also equal to that value, does the `=ALL` comparison yield `TRUE`.

Here is an example. It shows that you can test whether an array's _elements_ are all the same as each other without needing to know their common value or anything about the array's dimensionality.

```plpgsql
do $body$
declare
  arr constant int[] not null := '
    [2:3][4:6][7:10]={
      {
        {42,42,42,42},{42,42,42,42},{42,42,42,42}
      },
      {
        {42,42,42,42},{42,42,42,42},{42,42,42,42}
      }
    }'::int[];

   val constant int not null := (
    select unnest(arr) limit 1);

  b99 boolean not null := val =ALL (arr);
begin
  assert b99, 'assert failed';
end;
$body$;
```
See the section [Multidimensional array of `int` values](../../literals/array-of-primitive-values/#multidimensional-array-of-int-values) for the explanation of the general syntax for the literal for a multidimensional array that specifies the lower and upper index bounds along each dimension.

See the section [Multidimensional array_agg() and unnest() — first overloads](..//array-agg-unnest/#multidimensional-array-agg-and-unnest-first-overloads) for the specification of the behavior `unnest()` when its actual argument is a multidimensional array.

You might want to include this code directly in the code example presented in the section [Using an array of atomic elements](./#using-an-array-of-atomic-elements) below. It is presented stand-alone here because its design needs a specific explanation.

## Semantics demonstration

The first demonstration uses an array of _atomic elements_. The second demonstration uses an array of _composite elements_. And the third demonstration uses an array of _elements_ that are values of a `DOMAIN`.

### Using an array of atomic elements

```plpgsql
do $body$
declare
  v1 constant int := 1;
  v2 constant int := 2;
  v3 constant int := 3;
  v4 constant int := 4;
  v5 constant int := 5;
  v6 constant int := null;

  arr1 constant int[] := array[v1, v1, v1, v1];
  arr2 constant int[] := array[v1, v1, v1, v2];
  arr3 constant int[] := array[v1, v2, v3, v4];
  arr4 constant int[] := array[v1, v1, v1, null];
  arr5 constant int[] := null;
  
  -- Notice that an array with zero elements is nevertheless NOT NULL.
  arr6 constant int[] not null := '{}';

  b01 constant boolean not null :=      v1 =all (arr1);
  b02 constant boolean not null := not  v1 =all (arr3);
  b03 constant boolean not null := not  v1 =all (arr2);
  b04 constant boolean not null :=      v1 =any (arr3);
  b05 constant boolean not null := not  v5 =any (arr3);

  b06 constant boolean not null :=      v1 =any (arr4);
  b07 constant boolean not null :=     (v5 =any (arr4)) is null;
  b08 constant boolean not null :=     (v1 =all (arr4)) is null;

  b09 constant boolean not null :=     (v1 =any (arr5)) is null;
  b10 constant boolean not null :=     (v6 =any (arr1)) is null;
  b11 constant boolean not null :=     (v1 =all (arr5)) is null;
  b12 constant boolean not null :=     (v6 =all (arr1)) is null;

  b13 constant boolean not null := not (v1 =any (arr6));
  b14 constant boolean not null :=     (v1 =all (arr6));
begin
  assert
    (b01 and b02 and b03 and b04 and b05 and b06 and b07 and b08
         and b09 and b10 and b11 and b12 and b13 and b14),
  'assert failed';
end;
$body$;
```
Here is a mechanically derived example from the code above that uses two-dimensional arrays in place of one-dimensional arrays. But some of the tests were removed to help readability. The outcomes of the remaining tests are unchanged because these depend only upon the array's actual elements and not upon its geometry.

```plpgsql
do $body$
declare
  v1 constant int := 1;
  v2 constant int := 2;
  v3 constant int := 3;
  v4 constant int := 4;
  v5 constant int := 5;

  arr1 constant int[] := array[array[v1, v1], array[v1, v1]];
  arr2 constant int[] := array[array[v1, v2], array[v3, v4]];
  arr3 constant int[] := array[array[v1, v1], array[v1, null]];

  b1 constant boolean not null :=     v1 =all (arr1);
  b2 constant boolean not null := not v1 =all (arr2);
  b3 constant boolean not null :=     v1 =any (arr2);
  b4 constant boolean not null := not v5 =any (arr2);

  b5 constant boolean not null :=     v1 =any (arr3);
  b6 constant boolean not null :=    (v5 =any (arr3)) is null;
  b7 constant boolean not null :=    (v1 =all (arr3)) is null;
begin
  assert
    (b1 and b2 and b3 and b4 and b5 and b6 and b7),
  'assert failed';
end;
$body$;
```

### Using an array of composite "row" type value elements

This code was produced by mechanically replacing `int` with _"rt"_ and by changing the spelling of the values that are assigned to  _"v1"_ through _"v5_" accordingly. But, again, some of the tests were removed to help readability. The rest of the remaining code is identical to its counterpart in the first example.

```plpgsql
drop type if exists rt;
create type rt as (a int, b text);

do $body$
declare
  v1 constant rt := (0, 1);
  v2 constant rt := (2, 3);
  v3 constant rt := (4, 5);
  v4 constant rt := (6, 7);
  v5 constant rt := (8, 9);

  arr1 constant rt[] := array[v1, v1, v1, v1];
  arr2 constant rt[] := array[v1, v2, v3, v4];
  arr3 constant rt[] := array[v1, v1, v1, null::rt];

  b1 constant boolean not null :=     v1 =all (arr1);
  b2 constant boolean not null := not v1 =all (arr2);
  b3 constant boolean not null :=     v1 =any (arr2);
  b4 constant boolean not null := not v5 =any (arr2);

  b5 constant boolean not null :=     v1 =any (arr3);
  b6 constant boolean not null :=    (v5 =any (arr3)) is null;
  b7 constant boolean not null :=    (v1 =all (arr3)) is null;
begin
  assert
    (b1 and b2 and b3 and b4 and b5 and b6 and b7),
  'assert failed';
end;
$body$;
```

### Using an array of elements that are values of a DOMAIN

First, consider these two examples:

```plpgsql
drop domain if exists d1_t;
create domain d1_t as int
default 42 constraint d1_t_chk check(value >= 17);

drop domain if exists d2_t;
create domain d2_t as int[];
```

It's clear that the values of _"d1_t"_, as a specialized kind of `int`, are _elements_. But what about the values of _"d2_t"_ which is a specialized kind of _array_? Critically, but somewhat counter-intuitively,  _"d2_t"_ does _not_ qualify as `anyarray`. Rather, it qualifies as `anyelement`. This code example underlines the point:

```plpgsql
create or replace procedure p(i in anyelement)
  language plpgsql
as $body$
begin
  raise info '%', pg_typeof(i);
end;
$body$;

call p(53::d1_t);

call p('{1, 2}'::d2_t);
```

The first `CALL` produces this result:

```
INFO:  d1_t
```

and the second `CALL` produces this result:

```
INFO:  d2_t
```

Once again, the following code was produced by mechanically replacing `int` — this time with _"int_arr_t"_ and by changing the spelling of the values that are assigned to  _"v1"_ through _"v5"_ accordingly. But, again, some of the tests were removed to help readability. The rest of the remaining code is identical to its counterpart in the first example.

```plpgsql
drop domain if exists int_arr_t;
create domain int_arr_t as int[];

do $body$
declare
  v1 constant int_arr_t := array[1, 1];
  v2 constant int_arr_t := array[2, 3];
  v3 constant int_arr_t := array[4, 5];
  v4 constant int_arr_t := array[5, 7];
  v5 constant int_arr_t := array[8, 9];

  arr1 constant int_arr_t[] := array[v1, v1, v1, v1];
  arr2 constant int_arr_t[] := array[v1, v2, v3, v4];
  arr3 constant int_arr_t[] := array[v1, v1, v1, null::int_arr_t];

  b1 constant boolean not null :=     v1 =all (arr1);
  b2 constant boolean not null := not v1 =all (arr2);
  b3 constant boolean not null :=     v1 =any (arr2);
  b4 constant boolean not null := not v5 =any (arr2);

  b5 constant boolean not null :=     v1 =any (arr3);
  b6 constant boolean not null :=    (v5 =any (arr3)) is null;
  b7 constant boolean not null :=    (v1 =all (arr3)) is null;
begin
  assert
    (b1 and b2 and b3 and b4 and b5 and b6 and b7),
  'assert failed';
end;
$body$;
```
