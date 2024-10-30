---
title: DEALLOCATE statement [YSQL]
headerTitle: DEALLOCATE
linkTitle: DEALLOCATE
description: Use the `DEALLOCATE` statement to deallocate a previously prepared SQL statement.
menu:
  v2.20:
    identifier: perf_deallocate
    parent: statements
type: docs
---

## Synopsis

Use the `DEALLOCATE` statement to deallocate a previously prepared SQL statement.

## Syntax

{{%ebnf%}}
  deallocate
{{%/ebnf%}}

## Semantics

### *name*

Specify the name of the prepared statement to deallocate.

### ALL

Deallocate all prepared statements.

## Examples

Prepare and deallocate an insert statement.

```plpgsql
yugabyte=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```plpgsql
yugabyte=# PREPARE ins (bigint, double precision, int, text) AS
               INSERT INTO sample(k1, k2, v1, v2) VALUES ($1, $2, $3, $4);
```

```plpgsql
yugabyte=# DEALLOCATE ins;
```

## See also

- [`EXECUTE`](../perf_execute)
- [`PREPARE`](../perf_prepare)
