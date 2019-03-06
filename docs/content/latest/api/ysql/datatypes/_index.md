---
title: Datatypes
description: List of YSQL Builtin Datatypes
summary: Datatype overview and specification.
image: /images/section_icons/api/pgsql.png
menu:
  latest:
    identifier: api-ysql-datatypes
    parent: api-ysql
    weight: 4200
aliases:
  - /latest/api/ysql/datatypes/
isTocNested: true
showAsideToc: true
---

The following table lists all primitive types in YSQL.

| Datatype | Alias | Description | Literals |
|----------|-------|-------------|----------|
| `bigint` | `int8` | signed eight-byte integer | 9,223,372,036,854,775,807 |
| `bigserial` | `serial8` | autoincrementing eight-byte integer |  |
| `bit [(n)]` <sup>1<sup> | | fixed-length bit string |  |
| `bit varying [(n)]` <sup>1<sup> | `varbit [(n)]` | variable-length bit string |  |
| `boolean` | `bool` | logical Boolean (true/false) |  |
| `box` <sup>1<sup> | | rectangular box on a plane |  |
| `bytea` | | binary data (“byte array”) |  |
| `character [(n)]` | `char [(n)]` | fixed-length character string |  |
| `character varying [(n)]` | `varchar [(n)]` | variable-length character string |  |
| `cidr` <sup>1<sup> | | IPv4 or IPv6 network address |  |
| `circle` <sup>1<sup> | | circle on a plane |  |
| `date` | | calendar date (year, month, day) |  |
| `double precision` | `float8` | double precision floating-point number (8 bytes) |  |
| `inet` | | IPv4 or IPv6 host address |  |
| `integer` | `int`, `int4` | signed four-byte integer | 2,147,483,647 |
| `interval [fields] [(p)]` | | time span |  |
| `json` <sup>1<sup> | | textual JSON data |  |
| `jsonb` | | binary JSON data, decomposed |  |
| `line` <sup>1<sup> | | infinite line on a plane |  |
| `lseg` <sup>1<sup> | | line segment on a plane |  |
| `macaddr` <sup>1<sup> | | MAC (Media Access Control) address |  |
| `macaddr8` <sup>1<sup> | | MAC (Media Access Control) address (EUI-64 format) |  |
| `money <sup>1<sup>` | | currency amount |  |
| `numeric [(p, s)]` | `decimal [(p, s)]` | exact numeric of selectable precision |  |
| `path` <sup>1<sup> | | geometric path on a plane |  |
| `pg_lsn` <sup>1<sup> | | PostgreSQL Log Sequence Number |  |
| `point` <sup>1<sup> | | geometric point on a plane |  |
| `polygon` <sup>1<sup> | | closed geometric path on a plane |  |
| `real` | `float4` | single precision floating-point number (4 bytes) |  |
| `smallint` | `int2` | signed two-byte integer | 32,767 |
| `smallserial` | `serial2` | autoincrementing two-byte integer |  |
| `serial` | `serial4` | autoincrementing four-byte integer |  |
| `text` | | variable-length character string |  |
| `time [(p)] [without time zone]` | | time of day (no time zone) |  |
| `time [(p)] with time zone` | `timetz` | time of day, including time zone |  |
| `timestamp [(p)] [without time zone]` | | date and time (no time zone) |  |
| `timestamp [(p)] with time zone` | `timestamptz` | date and time, including time zone |  |
| `tsquery` <sup>1<sup> | | text search query |  |
| `tsvector` <sup>1<sup> | | text search document |  |
| `txid_snapshot` <sup>1<sup> | | user-level transaction ID snapshot |  |
| `uuid` | | universally unique identifier |  |
| `xml` <sup>1<sup> | | XML data |  |

<sup>1<sup>: Under development

<!--
Primitive Type | Allowed in Key | Type Parameters | Description |
---------------|----------------|-----------------|-------------|
[`BIGINT`](type_int) | Yes | - | 64-bit signed integer |
[`BOOLEAN`](type_bool) | Yes | - | Boolean |
[`DECIMAL`](type_number) | Yes | - | Exact, fixed-point number |
[`DOUBLE PRECISION`](type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`FLOAT`](type_number) | Yes | - | 64-bit, inexact, floating-point number |
[`REAL`](type_number) | Yes | - | 32-bit, inexact, floating-point number |
[`INT`](type_int) | Yes | - | 32-bit signed integer |
[`INTEGER`](type_int) | Yes | - | 32-bit signed integer |
[`SMALLINT`](type_int) | Yes | - | 16-bit signed integer |
[`TEXT`](type_text) | Yes | - | Variable-size string of Unicode characters |
[`VARCHAR`](type_text) | Yes | - | Variable-size string of Unicode characters |
[`BYTEA`](type_binary) | Yes | - | Variable-size string of 8-bit integer |
--->