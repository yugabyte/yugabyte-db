---
title: Serial data types [YSQL]
headerTitle: Serial data types
linkTitle: Serial
description: YSQL serial data types include SMALLSERIAL (SMALLINT), SERIAL (INTEGER), and BIGSERIAL (BIGINT).
menu:
  v2.18:
    identifier: api-ysql-datatypes-serial
    parent: api-ysql-datatypes
type: docs
---

## Synopsis

SMALLSERIAL, SERIAL, and BIGSERIAL are short notation for sequences of `SMALLINT`, `INTEGER`, and `BIGINT`, respectively.

## Description

```ebnf
type_specification ::= SMALLSERIAL | SERIAL | BIGSERIAL
```

### Notes

- Columns of serial types are auto-incremented.
- `SERIAL` does not imply that an index is created on the column.
- Serial types are sequences under the hood, and its values can be cached on the YB-TServer using the `ysql_sequence_cache_method` flag. For more information, see [nextval](../../exprs/func_nextval/).
