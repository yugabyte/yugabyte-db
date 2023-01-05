---
title: Simple expressions [YCQL]
headerTitle: Simple expressions
linkTitle: Simple expressions
description: A simple expression can be a column, constant, or NULL.
menu:
  preview:
    parent: api-cassandra
    weight: 1331
aliases:
  - /preview/api/cassandra/expr_simple
  - /preview/api/ycql/expr_simple
type: docs
---

A simple expression can be a column, a constant, or NULL.

## Column expression

A column expression refers to a column in a table by using its name, which can be either a fully qualified name or a simple name.

```
column_expression ::= [keyspace_name.][table_name.][column_name]
```

## Constant expression

A constant expression represents a simple value by using literals.

```
constant_expression ::= string | number
```

## NULL

When an expression, typically a column, does not have a value, it is represented as NULL.

```
null_expression ::= NULL
```
