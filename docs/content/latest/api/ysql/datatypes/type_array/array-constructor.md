---
title: The array[] value constructor
linkTitle: array[] constructor
headerTitle: The array[] value constructor
description: The array[] value constructor
menu:
  latest:
    identifier: array-constructor
    parent: api-ysql-datatypes-array
    weight: 10
isTocNested: false
showAsideToc: false
---

**Purpose:** The array[] value constructor is a special variadic function that creates an array value from scratch using an expression for each of the array's values.

**Signature** 

```
input value:       anyelement, [anyelement]*
return value:      anyarray
```

**Notes:** The alternative approach that meets the same goal of creating an array from directly specified values to use an _[array literal](../literals/)_.

These two functions also create an array value from scratch:

- The _[array_fill()](../functions-operators/array-fill/)_ builtin SQL function creates a "blank canvas" array of the specified shape with all values set the same to what you want.

- The _[array_agg()](../functions-operators/array-agg-unnest/#array-agg)_ builtin SQL function creates an array (of an implied _"row"_ type) from a SQL subquery.

## Example:

To_Do

## Using the array[] constructor in PL/pgSQL code

```postgresql
prepare stmt(int[]) as insert into t(arr) values($1);
```

To_Do

## Using the array[] constructor in a prepared statement

To_Do
