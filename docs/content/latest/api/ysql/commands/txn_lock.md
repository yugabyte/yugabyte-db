---
title: LOCK
linkTitle: LOCK
summary: Lock a table
description: LOCK
menu:
  latest:
    identifier: api-ysql-commands-lock
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/txn_lock
isTocNested: true
showAsideToc: true
---

## Synopsis

`LOCK` command locks a table.

## Syntax

### Diagram 

### Grammar
```
lock_stmt ::= LOCK [ TABLE ] [ ONLY ] name [ * ] [, ...] [ IN lockmode MODE ] [ NOWAIT ]

lockmode ::= { ACCESS SHARE |
               ROW SHARE |
               ROW EXCLUSIVE |
               SHARE UPDATE EXCLUSIVE |
               SHARE |
               SHARE ROW EXCLUSIVE |
               EXCLUSIVE |
               ACCESS EXCLUSIVE }
```

Where
- name specifies an existing table to be locked.

## Semantics

- Only `ACCESS SHARE` lock mode is supported at this time.
- All other modes listed in `lockmode` are under development.

## See Also
[`CREATE TABLE`](../ddl_create_table)
[Other PostgreSQL Statements](..)
