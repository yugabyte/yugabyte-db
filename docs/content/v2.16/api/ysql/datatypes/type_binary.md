---
title: Binary data types [YSQL]
headerTitle: Binary data types
linkTitle: Binary
summary: Binary data types
description: Use the BYTEA data type to represent binary string of bytes (octets).
menu:
  v2.16:
    identifier: api-ysql-datatypes-binary
    parent: api-ysql-datatypes
type: docs
---

## Synopsis

Use the `BYTEA` data type to represent binary string of bytes (octets). Binary strings allow zeros (`0`) and non-printable bytes.

Data type | Description |
----------|-------------|
BYTEA | Variable length binary string |

## Description

- `BYTEA` is used to declare a binary entity.

```
type_specification ::= BYTEA
```

- Escaped input can be used for input binary data.

```
SELECT E'\\001'::bytea
```
