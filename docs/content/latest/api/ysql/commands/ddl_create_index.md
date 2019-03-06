---
title: CREATE INDEX
linkTitle: CREATE INDEX
summary: Create index on a table in a database
description: CREATE INDEX
menu:
  latest:
    identifier: api-ysql-commands-create-index
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/ddl_create_index
isTocNested: true
showAsideToc: true
---

## Synopsis

## Syntax

### Diagram 

### Grammar
```
create_index ::= CREATE [ UNIQUE ] INDEX [ [ IF NOT EXISTS ] name ] ON [ ONLY ] table_name
    ( { column_name | ( expression ) } [ opclass ] [ ASC | DESC ] [, ...] )
    [ INCLUDE ( column_name [, ...] ) ]
    [ WITH ( storage_parameter = value [, ... ] ) ]
    [ WHERE predicate ]
```

Where
- `UNIQUE` enforced that duplicate values in a table in not allowed.

- `INCLUDE clause` specifies a list of columns which will be included in the index as non-key columns.

- `name` specifies the index to be created.

- `table_name` specifies the name of the table to be indexed.

- `column_name` specifies the name of a column of the table.

- expression specifies one or more columns of the table and must be surrounded by parentheses.

- `ASC` indicates ascending sort order.

- DESC indicates descending sort order.

## Semantics

- `CONCURRENTLY`, `USING method`, `COLLATE`, `NULL order`, and `tablespace` options are not yet supported.

## See Also
[Other PostgreSQL Statements](..)
