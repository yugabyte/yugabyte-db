---
title: BOOLEAN data types
headerTitle: BOOLEAN data types
linkTitle: Boolean
description: Use the BOOLEAN data type to represent three different states - TRUE, FALSE, or NULL.
menu:
  v2024.2_api:
    identifier: api-ysql-datatypes-bool
    parent: api-ysql-datatypes
type: docs
---

## Synopsis

The `BOOLEAN` data type represents three different states: `TRUE`, `FALSE`, or `NULL`.

## Description

```ebnf
type_specification ::= { BOOLEAN | BOOL }
literal ::= { TRUE | true | 't' | 'y' | 'yes' | 'on' | 1 |
              FALSE | false | 'f' | 'n' | 'no' | 'off' | 0 }
```
