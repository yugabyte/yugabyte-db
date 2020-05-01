---
title: BOOLEAN data types
linktitle: Boolean
summary: BOOLEAN data types
description: BOOLEAN data types
block_indexing: true
menu:
  v2.0:
    identifier: api-ysql-datatypes-bool
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

## Synopsis

The `BOOLEAN` data type represents three different states: `TRUE`, `FALSE`, or `NULL`.

## Description

```
type_specification ::= { BOOLEAN | BOOL }
literal ::= { TRUE | true | 't' | 'y' | 'yes' | 'on' | 1 |
              FALSE | false | 'f' | 'n' | 'no' | 'off' | 0 }
```
