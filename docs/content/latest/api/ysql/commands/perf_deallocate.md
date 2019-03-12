---
title: DEALLOCATE
linkTitle: DEALLOCATE
summary: Deallocate a prepared statement
description: DEALLOCATE
menu:
  latest:
    identifier: api-ysql-commands-deallocate
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/perf_deallocate
isTocNested: true
showAsideToc: true
---

## Synopsis

`DEALLOCATE` command deallocates a previously-prepared statement.

## Syntax

### Diagrams

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="338" height="63" viewbox="0 0 338 63"><path class="connector" d="M0 21h5m97 0h30m76 0h20m-111 0q5 0 5 5v8q0 5 5 5h86q5 0 5-5v-8q0-5 5-5m5 0h30m55 0h20m-90 0q5 0 5 5v19q0 5 5 5h5m40 0h20q5 0 5-5v-19q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="97" height="24" rx="7"/><text class="text" x="15" y="21">DEALLOCATE</text><rect class="literal" x="132" y="5" width="76" height="24" rx="7"/><text class="text" x="142" y="21">PREPARE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="258" y="5" width="55" height="24"/><text class="text" x="268" y="21">name</text></a><rect class="literal" x="258" y="34" width="40" height="24" rx="7"/><text class="text" x="268" y="50">ALL</text></svg>

### Grammar
```
deallocate_stmt ::= DEALLOCATE [ PREPARE ] { name | ALL }
```

Where
- name specifies the prepared statement to deallocate.

## Semantics

- `ALL` option is to deallocate all prepared statements.

## Examples
Prepare and deallocate an insert statement.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

```sql
postgres=# PREPARE ins (bigint, double precision, int, text) AS 
               INSERT INTO sample(k1, k2, v1, v2) VALUES ($1, $2, $3, $4);
```

```sql
postgres=# DEALLOCATE ins;
```

## See Also
[Other YSQL Statements](..)
