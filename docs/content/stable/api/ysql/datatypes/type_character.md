---
title: Character data types [YSQL]
headerTitle: Character data types
linkTitle: Character
description: Use character-based data types to specify data of a string of Unicode characters.
menu:
  stable:
    identifier: api-ysql-datatypes-character
    parent: api-ysql-datatypes
type: docs
---

## Synopsis

Character-based data types are used to specify data of a string of Unicode characters.

Data type | Description |
----------|-------------|
`CHAR` | Character string of 1 byte |
`CHAR` (n) | Character string of fixed-length (n) and blank padded |
`CHARACTER` (n) | Character string of fixed-length (n) and blank padded |
`CHARACTER` `VARYING` (n) | Character string of variable-length with maximum limit (n) |
`VARCHAR` (n) | Character string of variable-length with maximum limit (n) |
`VARCHAR` | Character string of variable and unlimited length |
`TEXT` | Character string of variable and unlimited length |

## Description

```ebnf
text_literal ::= "'" [ '' | letter ...] "'"
```

Where

- Single quote must be escaped as ('').
- `letter` is any character except for single quote (`[^']`).
- Character-based data types can be part of the `PRIMARY KEY`.
- Value of character data type are convertible and comparable to non-text data types.
