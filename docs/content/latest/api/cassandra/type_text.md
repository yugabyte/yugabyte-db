---
title: TEXT
summary: String of Unicode characters
description: TEXT Type
menu:
  latest:
    parent: api-cassandra
    weight: 1440
aliases:
  - /latest/api/cassandra/type_text
  - /latest/api/ycql/type_text
isTocNested: true
showAsideToc: true
---

## Synopsis
`TEXT` datatype is used to specify data of a string of unicode characters.

## Syntax
```
type_specification ::= TEXT | VARCHAR

text_literal ::= "'" [ letter ...] "'"
```

Where 

- `TEXT` and `VARCHAR` are aliases.
- `letter` is any character except for single quote (`[^']`)

## Semantics

- Columns of type `TEXT` or `VARCHAR` can be part of the `PRIMARY KEY`.
- Implicitly, value of type `TEXT` datatype are neither convertible nor comparable to non-text datatypes.
- The length of `TEXT` string is virtually unlimited.

## Examples

```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE users(user_name TEXT PRIMARY KEY, full_name VARCHAR);
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO users(user_name, full_name) VALUES ('jane', 'Jane Doe');
```
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO users(user_name, full_name) VALUES ('john', 'John Doe');
```
```{.sql .copy .separator-gt}
cqlsh:example> UPDATE users set full_name = 'Jane Poe' WHERE user_name = 'jane';
```
```{.sql .copy .separator-gt}
cqlsh:example> SELECT * FROM users;
```
```sh
 user_name | full_name
-----------+-----------
      jane |  Jane Poe
      john |  John Doe
```

## See Also

[Data Types](..#datatypes)
