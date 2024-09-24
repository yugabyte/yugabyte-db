---
title: Money data types [YSQL]
headerTitle: Money data types
linkTitle: Money
description: The MONEY data type represents currency with a fixed precision for fraction.
menu:
  preview:
    identifier: api-ysql-datatypes-money
    parent: api-ysql-datatypes
aliases:
  - /preview/api/ysql/datatypes/type_money
type: docs
---

## Synopsis

The `MONEY` data type represents currency with a fixed precision for fraction.

Data type | Description | Min | Max |
----------|-------------|-----|-----|
MONEY | 8 bytes | -92233720368547758.08 | +92233720368547758.07 |

## Description

```ebnf
type_specification ::= MONEY
```

To avoid precision loss, `MONEY` value can be cast to `NUMERIC` type before applying calculations.
