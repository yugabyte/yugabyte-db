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

| Datatype | Alias | Description |
|----------|-------|-------------|
| [`BIGINT`](type_numeric) | [`INT8`](type_numeric) | Signed 8-byte integer |
| [`BIGSERIAL`](type_serial) | [`SERIAL8`](type_serial) | Autoincrement 8-byte integer |
| `BIT [(n)]` <sup>1<sup> | | Fixed-length bit string |
| `BIT VARYING [(n)]` <sup>1<sup> | `VARBIT [(n)]` | Variable-length bit string |
| [`BOOLEAN`](type_bool) | [`BOOL`](type_bool) | Logical boolean (true/false) |
| `BOX` <sup>1<sup> | | Rectangular box |
| [`BYTEA`](type_binary) | | Binary data |
| [`CHARACTER [(n)]`](type_character) | [`CHAR [(n)]`](type_character) | Fixed-length character string |
| [`CHARACTER VARYING [(n)]`](type_character) | [`VARCHAR [(n)]`](type_character) | Variable-length character string |
| `CIDR` <sup>1<sup> | | IPv4 or IPv6 network address |
| `CIRCLE` <sup>1<sup> | | Circle |
| [`DATE`](type_datetime) | | Date (year, month, day) |
| [`DOUBLE PRECISION`](type_numeric) | [`FLOAT8`](type_numeric) | Floating-point number (8 bytes) |
| `INET` <sup>1<sup> | | IPv4 or IPv6 host address |
| [`INTEGER`](type_numeric) | [`INT`, `INT4`](type_numeric) | Signed 4-byte integer |
| [`INTERVAL [fields] [(p)]`](type_datetime) | | Time span |
| `JSON` <sup>1<sup> | | Textual JSON data |
| `JSONB` | | Binary JSON data |
| `LINE` <sup>1<sup> | | Infinite line |
| `LSEG` <sup>1<sup> | | Line segment |
| `MACADDR` <sup>1<sup> | | MAC address |
| `MACADDR8` <sup>1<sup> | | MAC address (EUI-64 format) |
| [`MONEY`](type_money) | | Currency amount |
| [`NUMERIC [(p, s)]`](type_numeric) | [`DECIMAL [(p, s)]`](type_numeric) | Exact fixed-point numeric |
| `PATH` <sup>1<sup> | | Geometric path |
| `PG_LSN` <sup>1<sup> | | Log Sequence Number |
| `POINT` <sup>1<sup> | | Geometric point |
| `POLYGON` <sup>1<sup> | | Closed geometric path |
| [`REAL`](type_numeric) | [`FLOAT4`](type_numeric) | Floating-point number (4 bytes) |
| [`SMALLINT`](type_numeric) | [`INT2`](type_numeric) | Signed 2-byte integer |
| [`SMALLSERIAL`](type_serial) | [`SERIAL2`](type_serial) | Autoincrement 2-byte integer |
| [`SERIAL`](type_serial) | [`SERIAL4`](type_serial) | Autoincrement 4-byte integer |
| [`TEXT`](type_character) | | Variable-length character string |
| [`TIME [(p)] [WITHOUT TIME ZONE]](type_datetime)` | | Time of day |
| [`TIME [(p)] WITH TIME ZONE](type_datetime)` | [`TIMETZ`](type_datetime) | Time of day |
| [`TIMESTAMP [(p)] [WITHOUT TIME ZONE]`](type_datetime) | | Date and time |
| [`TIMESTAMP [(p)] WITH TIME ZONE`](type_datetime) | [`TIMESTAMPTZ`](type_datetime) | Date and time |
| `TSQUERY` <sup>1<sup> | | Text search query |
| `TSVECTOR` <sup>1<sup> | | Text search document |
| `TXID_SNAPSHOT` <sup>1<sup> | | Transaction ID snapshot |
| [`UUID`](type_uuid) | | Universally unique identifier |
| `XML` <sup>1<sup> | | XML data |

<sup>1<sup>: Under development
