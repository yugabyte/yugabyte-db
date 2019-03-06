---
title: SET
linkTitle: SET 
summary: Set a session or system variable
description: SET
menu:
  latest:
    identifier: api-ysql-commands-set
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/cmd_set
isTocNested: true
showAsideToc: true
---

## Synopsis

`SET` command update a run-time control parameter.

## Syntax

### Diagram 

### Grammar
```
SET [ SESSION | LOCAL ] { configuration_parameter { TO | = } { value | 'value' | DEFAULT } |
                          TIME ZONE { timezone | LOCAL | DEFAULT } }
```

Where
- `SESSION` option specifies that the command affects only the current session.

- `LOCAL` option specifies that the command affect only the current transaction. After COMMIT or ROLLBACK, the session-level setting takes effect again.

- `configuration_parameter` specifies the name of a mutable run-time parameter.

- `value` specifies new value of parameter.

## Semantics

- Although the values of a parameter can be set, showed, and reset, the effect of these parameters are not yet supported in YugaByte. The factory-settings or default behaviors will be used for the moment.

## See Also
[`SHOW`](../cmd_show)
[`RESET`](../cmd_reset)
[Other PostgreSQL Statements](..)
