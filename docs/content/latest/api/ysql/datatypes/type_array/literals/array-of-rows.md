---
title: The literal for an array of rows
linkTitle: Array of rows
headerTitle: The literal for an array of "row" type values
description: Bla bla
menu:
  latest:
    identifier: array-of-rows
    parent: array-literals
    weight: 40
isTocNested: false
showAsideToc: false
---

> **Monday 27-Apr-2009. This section is still work-in-progress. The text and example needed to complete it are ready in draft. The work to incorporate this is relatively small.**

We now combine the understanding of how to write the literal for an array of primitive values with that of how to write the literal for a _"row"_ type value. First, recall this example from the section To_Do:

```postgresql
create table t(k serial primary key, v int[]);

insert into t(v) values('
    {1,  2}
  '::int[]);

select k, v from t order by k;
```
It produces this result:
```
 k |   v   
---+-------
 1 | {1,2}
```
Now try this:
```postgresql
insert into t(v) values('
    {
      "1",
      "2"
    }
'::int[]);

select k, v from t order by k;
```
This `insert` statement was derived from the previous one simply by adding whitespace and by enclosing the `int` literals for the first and second array value in double quotes.

It produces this result:

```
 k |   v   
---+-------
 1 | {1,2}
```

So the values within the literal for an array may optionally be surrounded by double quotes. At least, they my be so surrounded when the values are of a primitive non-stringy data type.

Create the same _"row"_ type the was used in the section  _"[The literal for a _"row"_ type value](../row/)"_:

```postgresql
create type rt as (a int, b int);
```
and this table:
```postgresql
create table t(k serial primary key, rs rt[]);
```
The syntax for the literal for an array of values of a user-defined _"row"_ type of course uses, for the _"row"_ type value itself, the same syntax between the opening parenthesis and the closing parenthesis that was explained in the section [The literal for a _"row"_ type value](../row/).





Now insert a _"row"_ type value using the literal for the `rt[]`type:

```postgresql
insert into t(rs) values
  ('{"(1, 2)", "(3, 4)"}'::rt[]);
```
Now insert a second _"row"_ type value, making more dramatic use of whitespace:
```postgresql
insert into t(rs) values
  ('{
      "(5, 6)",
      "(7, 8)"
    }'::rt[]);
```

Check the result in the obvious way:

```postgresql
select k, rs::text from t order by k;
```
It produces this result:
```
 k |        rs         
---+-------------------
 1 | {"(1,2)","(3,4)"}
 2 | {"(5,6)","(7,8)"}
```
This looks a lot like the literal values used for the input. But notice that the whitespace in the input was insignificant and the convention for the `::text` type cast of a _"row"_ type value is to remove the whitespace between values.

The best way to confirm that the `insert` really did produce the intended result is to specify the _"row"_ type value fields individually for the first _"row"_ type value, thus:

```postgresql
select
  k,
  (rs[1]).a as r1a,
  (rs[1]).b as r1b,
  (rs[2]).a as r2a,
  (rs[2]).b as r2b
from t order by k;
```
(The parentheses around `rs[n]` are essential.) This is the result:
```
 k | r1a | r1b | r2a | r2b 
---+-----+-----+-----+-----
 1 |   1 |   2 |   3 |   4
 2 |   5 |   6 |   7 |   8
```

