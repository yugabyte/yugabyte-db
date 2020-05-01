---
title: JSON data types
linktitle: JSON
summary: JSON data types
description: JSON data types
block_indexing: true
menu:
  v2.0:
    identifier: api-ysql-datatypes-json
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/datatypes/type_json
isTocNested: true
showAsideToc: true
---

## Synopsis

The JSON data types are introduced to support JavaScript Object Notation (JSON) data. The `JSON` data type represents the exact text format of JSON while the `JSONB` data type represents its binary format in YSQL database. Both `JSONB` and `JSON` are supported in YSQL.

## Description

```
type_specification ::= { `JSON` | `JSONB` }
```

- `JSON` and `JSONB` literals can be any text strings that follow the specifications for JavaScript Object Notation.
- When data is inserted into `JSONB` column, the text string will be parsed and converted to binary form before storing.
- When selected, data of `JSONB` type will be converted and returned in the text format.
