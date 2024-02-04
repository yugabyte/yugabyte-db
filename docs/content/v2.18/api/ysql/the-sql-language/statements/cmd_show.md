---
title: SHOW statement [YSQL]
headerTitle: SHOW
linkTitle: SHOW
description: Use the SHOW statement to display the value of a run-time parameter.
menu:
  v2.18:
    identifier: cmd_show
    parent: statements
type: docs
---

## Synopsis

Use the `SHOW` statement to display the value of a run-time parameter.

## Syntax

{{%ebnf%}}
  show_stmt
{{%/ebnf%}}

## Semantics

The parameter values in YSQL can be set and typically take effect the same way as in PostgreSQL. However, because YugabyteDB uses a different storage engine ([DocDB](../../../../../architecture/layered-architecture/#docdb)), many configurations related to the storage layer will not have the same effect in YugabyteDB as in PostgreSQL. For example, configurations related to connection and authentication, query planning, error reporting and logging, run-time statistics, client connection defaults, and so on, should work as in PostgreSQL.

However, configurations related to write ahead log, vacuuming, or replication, may not apply to Yugabyte. Instead related configuration can be set using yb-tserver (or yb-master) [configuration flags](../../../../../reference/configuration/yb-tserver/#configuration-flags).

### *run_time_parameter*

Specify the name of the parameter to be displayed.

### ALL

Show the values of all configuration parameters, with descriptions.

## See also

- [`SET`](../cmd_set)
- [`RESET`](../cmd_reset)
