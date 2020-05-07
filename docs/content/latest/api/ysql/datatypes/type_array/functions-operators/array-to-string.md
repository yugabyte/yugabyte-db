---
title: array_to_string()
linkTitle: array_to_string()
headerTitle: array_to_string()
description: array_to_string()
menu:
  latest:
    identifier: array-to-string
    parent: array-functions-operators
isTocNested: false
showAsideToc: false
---

**Purpose:** Return a `text` value computed by representing each array value, traversing these in row-major order, by its `::text` typecast, using the supplied delimiter between each such representation. (The result, therefore, loses all information about the arrays geometric properties.) Optionally, represent `null` by the supplied `text` value. The term _"row-major order"_ is explained in the [Joint semantics](./functions-operators/properties/#joint-semantics)) section within the _"Functions for reporting the geometric properties of an array"_ section.

**Signature:**
```
input value:       anyarray, text [, text]
return value:      text
```

**Example:**
```postgresql
create type rt as (f1 int, f2 text);
create table t(k int primary key, arr rt[]);
insert into t(k, arr) values(1,
  array[
    array[
      array[(1, 'a')::rt, (2, null)::rt, null, (3, 'c')::rt]
    ]
  ]::rt[]
);

select arr::text from t where k = 1;
```
It shows this:
```
                arr                
-----------------------------------
 {{{"(1,a)","(2,)",NULL,"(3,c)"}}}
```
To understand the syntax of the text of this literal, especially when a field is `null`, see  [The literal for a _"row"_ type value](../../literals/row/).

Now do this:
```postgresql
select
  array_to_string(
    arr,     -- the input array
    ' | ')   -- the delimiter
from t
where k = 1;
```
It shows this:
```
   array_to_string    
----------------------
 (1,a) | (2,) | (3,c)
```
Notice that the third, `null`, array value is simply not represented.

Now do this;
```postgresql
select
  array_to_string(
    arr,     -- the input array
    ' | ',   -- the delimiter
    '?')     -- the null indicator
from t
where k = 1;
```
It shows this:
```
     array_to_string      
--------------------------
 (1,a) | (2,) | ? | (3,c)
```

The third array value is now represented by `?`. But the fact that `f2 is null` within the second array value is _not_ represented by `?`. In other words, this technique for visualizing `null` is applied only at the granularity of top-level array values and not within such values when they are composite.
