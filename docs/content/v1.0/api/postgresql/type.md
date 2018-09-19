---
title: Datatypes
description: PostgreSQL Datatypes
summary: Datatype overview and specification.
menu:
  v1.0:
    identifier: api-postgresql-type
    parent: api-postgresql
    weight: 3300
aliases:
  - api/postgresql/type
  - api/pgsql/type
---

The following table lists all supported primitive types.

Primitive Type | Allowed in Key | Type Parameters | Description |
---------------|----------------|-----------------|-------------|
[`BIGINT`](../type_int) | Yes | - | 64-bit signed integer |
[`BOOLEAN`](../type_bool) | Yes | - | Boolean |
[`DECIMAL`](../type_number) | Yes | - | Exact, fixed-point number |
[`DOUBLE PRECISION`](../type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`FLOAT`](../type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`REAL`](../type_number) | Yes | - | 32-bit, inexact, floating-point number |
[`INT` &#124; `INTEGER`](../type_int) | Yes | - | 32-bit signed integer |
[`SMALLINT`](../type_int) | Yes | - | 16-bit signed integer |
[`TEXT` &#124; `VARCHAR`](../type_text) | Yes | - | Variable-size string of Unicode characters |
