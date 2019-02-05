---
title: Simple Value
summary: Columns, constants, and null.
description: Simple Expressions
menu:
  latest:
    parent: api-cassandra
    weight: 1331
aliases:
  - /latest/api/cassandra/expr_simple
  - /latest/api/ycql/expr_simple
isTocNested: true
showAsideToc: true
---

Simple expression can be either a column, a constant, or NULL.

## Column Expression
A column expression refers to a column in a table by using its name, which can be either a fully qualified name or a simple name.  
```
column_expression ::= [keyspace_name.][table_name.][column_name]
```

## Constant Expression

A constant expression represents a simple value by using literals.  
```
constant_expression ::= string | number
```

## NULL

When an expression, typically a column, does not have a value, it is represented as NULL.  
```
null_expression ::= NULL
```

## See Also
[All Expressions](..#expressions)