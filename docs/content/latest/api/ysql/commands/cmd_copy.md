---
title: COPY
linkTitle: COPY
summary: COPY
description: COPY
menu:
  latest:
    identifier: api-ysql-commands-copy
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_copy
isTocNested: true
showAsideToc: true
---

## Synopsis

`COPY` command transfers data between tables and files. `COPY TO` copies from tables to files. `COPY FROM` copies from files to tables. `COPY` outputs the number of rows that were copied.

## Syntax

### Diagram 

### Grammar
```
copy_from ::= COPY table_name [ ( column_name [, ...] ) ]
              FROM { 'filename' | PROGRAM 'command' | STDIN }
              [ [ WITH ] ( option [, ...] ) ]

copy_to ::= COPY { table_name [ ( column_name [, ...] ) ] | ( query ) }
            TO { 'filename' | PROGRAM 'command' | STDOUT }
            [ [ WITH ] ( option [, ...] ) ]

copy_option ::=
  { FORMAT format_name |
    OIDS [ boolean ] |
    FREEZE [ boolean ] |
    DELIMITER 'delimiter_character' |
    NULL 'null_string' |
    HEADER [ boolean ] |
    QUOTE 'quote_character' |
    ESCAPE 'escape_character' |
    FORCE_QUOTE { ( column_name [, ...] ) | * } |
    FORCE_NOT_NULL ( column_name [, ...] ) |
    FORCE_NULL ( column_name [, ...] ) |
    ENCODING 'encoding_name' }
```

Where
- `table_name` specifies the table to be copied.

- `column_name` speciies list of columns to be copied.

- query can be either SELECT, VALUES, INSERT, UPDATE or DELETE whose results will be copied to files. RETURNING clause must be provided for INSERT, UPDATE and DELETE commands.

- filename specifies an absolute or relative path of a file to be copied.

## Examples

- Errors are raised if table does not exist.
- `COPY TO` can only be used with regular tables.
- `COPY FROM` can be used with either tables, foreign tables, or views.

## See Also
[Other PostgreSQL Statements](..)
