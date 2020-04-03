---
title: SERIAL data types
linktitle: Serial
summary: SERIAL data types
description: SERIAL data types
block_indexing: true
menu:
  v1.3:
    identifier: api-ysql-datatypes-serial
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

## Synopsis

SMALLSERIAL, SERIAL, and BIGSERIAL are short notation for sequences of `SMALLINT`, `INTEGER`, and `BIGINT` respectively.

## Description

```
type_specification ::= SMALLSERIAL | SERIAL | BIGSERIAL
```

- Columns of serial types are auto-incremented.
- `SERIAL` does not imply that an index is created on the column.
