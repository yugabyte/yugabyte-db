---
title: TEXT
linktitle: Character
description: Character Types
summary: String of Unicode characters
menu:
  latest:
    identifier: api-ysql-datatypes-character
    parent: api-ysql-datatypes
aliases:
  - /latest/api/ysql/datatypes/type_character
isTocNested: true
showAsideToc: true
---

## Synopsis
Character-based datatypes are used to specify data of a string of Unicode characters.

DataType | Description |
---------|-------------|
`CHAR` | Character string of 1 byte |
`CHAR` (n) | Character string of fixed-length (n) and blank padded |
`CHARACTER` (n) | Character string of fixed-length (n) and blank padded |
`CHARACTER` `VARYING` (n) | Character string of variable-length with maximum limit (n) |
`VARCHAR` (n) | Character string of variable-length with maximum limit (n) |
`VARCHAR` | Character string of variable and unlimited length |
`TEXT` | Character string of variable and unlimited length |

## Description
```
text_literal ::= "'" [ '' | letter ...] "'"
```

Where 

- Single quote must be escaped as ('').
- `letter` is any character except for single quote (`[^']`).
- Character-based datatypes can be part of the `PRIMARY KEY`.
- Value of character datatype are convertible and comparable to non-text datatypes.

