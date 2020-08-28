---
title: BOOLEAN Datatypes
linktitle: Boolean
summary: BOOLEAN Datatypes
description: BOOLEAN Datatypes
block_indexing: true
menu:
  v1.2:
    identifier: api-ysql-datatypes-bool
    parent: api-ysql-datatypes
isTocNested: true
showAsideToc: true
---

## Synopsis
BOOLEAN datatype represents three different states: TRUE, FALSE, or NULL.

## Description

```
type_specification ::= { BOOLEAN | BOOL }
literal ::= { TRUE | true | 't' | 'y' | 'yes' | 'on' | 1 |
              FALSE | false | 'f' | 'n' | 'no' | 'off' | 0 }
```

