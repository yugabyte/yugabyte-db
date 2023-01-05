---
title: Operators [YCQL]
headerTitle: YCQL operators
linkTitle: Operators
description: Combine multiple expressions using YCQL operators supported in YugabyteDB.
menu:
  preview:
    parent: api-cassandra
    weight: 1360
aliases:
  - /preview/api/cassandra/expr_ocall
  - /preview/api/ycql/expr_ocall
type: docs
---

An expression with operators is a compound expression that combines multiple expressions using built-in operators. The following sections discuss the YCQL operators in YugabyteDB.

## Null operators

| Operator | Description |
|----------|-------------|
| `EXISTS`, `NOT EXISTS` | predicate for existence of a row |

`EXISTS` and `NOT EXISTS` can only be used in the `IF` clause.

## Unary operators

| Operator | Description |
|----------|-------------|
| `-` | numeric negation |
| `+` | no-op |
| `NOT` | Logical (boolean) negation |

Unary `-` and `+` can only be used with constant expressions such as `-77`.

## Binary operators

| Operator | Description |
|----------|-------------|
| `OR`, `AND`| Logical (boolean) expression |
| `=`, `!=`, `<`, `<=`, `>`, `>=` | Comparison expression |
| `+` | Addition, append, or prepend |
| `-` | Subtraction or removal |
| `*` | Multiplication. Not yet supported |
| `/` | Division. Not yet supported |
| `ISNULL`, `IS NOT NULL` | Not yet supported comparison expression. |

- The first argument of comparison operators must be a column. For example, `column_name = 7`.
- Comparing `NULL` with others always yields a `false` value. Operator `ISNULL` or `IS NULL` must be used when comparing with `NULL`.
- When `+` and `-` are applied to a NULL argument of `COUNTER` data type, the NULL expression is replaced with a zero value before the computation. When these operators are applied to a NULL expression of all other numeric data types, the computed value is always NULL.
- Operator `+` either prepends or appends a value to a LIST while operator `-` removes elements from LIST.
- Operator `+` inserts new distinct elements to a MAP or SET while operator `-` removes elements from them.
