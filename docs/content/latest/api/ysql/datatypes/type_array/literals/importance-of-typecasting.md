---
title: The critical importance of typecasting
linkTitle: Importance of typecasting
headerTitle: The critical importance of explicit typecasting
description: The critical importance of explicit typecasting
image: /images/section_icons/api/ysql.png
menu:
  latest:
    identifier: importance-of-typecasting
    parent: array-literals
    weight: 20
isTocNested: false
showAsideToc: false
---

The choice of an `int[]` target for some of the examples
in the section _"[The literal for an array of primitive values](../array-of-primitive-values)"_
was deliberate. It allows an important point to be made. Try this:

```postgresql
select '17' + '42';
````

It causes this error:

```
Could not choose a best candidate operator. You might need to add explicit type casts.
```

The bare literals are taken to be `text` values—and the `+` operator (unlike in other programming languages where it means "concatenate") has no `text` overload. Now try this—just as the error text proposes:

```postgresql
select '17'::int + '42'::int;
````

It runs without error and produces the result `59`. It's exactly this that allows the code in the section _"[The literal for an array of primitive values](../array-of-primitive-values)"_ to work. Because the `text` definition of the literals are typecast to `int[]`, then the values inside the `text` are taken to be `int` values. Therefor, if you replace, say, `8` with `a` in the _"Insert a 3-dimensional int[] value"_ example, then you get this error:
```
invalid input syntax for integer: "a"
```
Here's a more vivid example:

```postgresql
with
  v1 as (
    select '{{{1,2},{3,4}},{{5,6},{7,8}}}'::int[] as a),

  v2 as (
    select  a[1][1][1]::int as a111, a[2][2][2]::int as a222 from v1)
select
  a111, a222, (a111 + a222) as s
from v2;
```
It uses subscript notation to access chosen individual values in the three-dimensional array. It produces this result:
```
 a111 | a222 | s 
------+------+---
    1 |    8 | 9
```
Because the data types of `a111` and `a222` were established as `int` in the `with` clause definition of `v2` , it adds nothing to repeat those typecasts each side of the `+` operator. Now try this:

```postgresql
with
  v1 as (
    select '{{{1,2},{3,4}},{{5,6},{7,8}}}'::text[] as a),

  v2 as (
    select  a[1][1][1]::text as a111, a[2][2][2]::text as a222 from v1)
select
  a111, a222, (a111 || a222) as s
from v2;
```
Notice that the `text` value that defines the array literal is typecast here to `text[]`, and correspondingly `a111` and `a222` are typecast to `text`. This is the result:

```
 a111 | a222 | s  
------+------+----
 1    | 8    | 18
```
This fact explains what otherwise might seem to be confusing. The literal value here, because—as an example of poor practice—it isn't typecast, is taken to be a `text` value, and so the `select	` displays it as such.

```postgresql
select '
    {
      {
        {1,  2}, {3,  4}
      },
      {
        {5,  6}, {7,  8}
      }
    }
  ';
```

Here is the result:

```
     {                   +
       {                 +
         {1,  2}, {3,  4}+
       },                +
       {                 +
         {5,  6}, {7,  8}+
       }                 +
     }                   +
```

The `+` characters are a device that _ysqlsh_ inherits from _psql_ to visualise newlines. They are not present in the to-be-displayed value itself.

Here is an example of proper practice where the typecasting is explicit. First an opaque internal representation of the specified `int[]` array is established. Of course, whitespace has no meaning here. And then the conventional `text` representation of that value is derived for display by _ysqlsh_.

```postgresql
select ('
    {
      {
        {1,  2}, {3,  4}
      },
      {
        {5,  6}, {7,  8}
      }
    }
  '::int[])::text;
```

Here is the result:

```
 {{{1,2},{3,4}},{{5,6},{7,8}}}
```

For completeness, here's a version for `boolean[]`:

```postgresql
with
  v1 as (
    select '{t, false}'::boolean[] as a),

  v2 as (
    select  a[1]::boolean as a1, a[2]::boolean as a2 from v1)
select
  a1, a2, (a1 and a2) as and_result, (a1 or a2) as or_result
from v2;
```
Here, the first array value is specified by the literal `t` and the second array value is specified by the literal `false` as a device to emphasis that both forms are legal.

It produces this result:

```
 a1 | a2 | and_result | or_result 
----+----+------------+-----------
 t  | f  | f          | t
```
The displayed `t` and `f` characters are, as explained in the previous section, the `::text` typecasts of the primitive `boolean` values `true` and `false`. In other words, this shows us that the canonical forms of the literal are `t` and `f`.

Yugabyte recommends that you always use the canonical forms when you construct a literal programmatically.

