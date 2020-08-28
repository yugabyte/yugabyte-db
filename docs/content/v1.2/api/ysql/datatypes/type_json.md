---
title: JSON Datatypes
linktitle: JSON
summary: JSON Datatypes
description: JSON Datatypes
block_indexing: true
menu:
  v1.2:
    identifier: api-ysql-datatypes-json
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

## Synopsis
JSON datatypes are introduced to support JavaScript Object Notation data. `JSON` type represents the exact text format of JSON while `JSONB` type represents its binary format in YSQL database.

- `JSONB` is supported in YSQL
- `JSON` is not yet supported.

## Description

```
type_specification ::= { `JSON` | `JSONB` }
```

- `JSON` and `JSONB` literals can be any text strings that follow the specifications for JavaScript Object Notation.
- When data is inserted into `JSONB` column, the text string will be parsed and converted to binary form before storing.
- When selected, data of `JSONB` type will be converted and returned in the text format.

