---
title: Prepared Statements
description: Prepared Statements.
summary: Prepare and execute statements.
menu:
  latest:
    identifier: api-postgresql-prepare
    parent: api-postgresql
    weight: 3600
aliases:
  - /latest/api/postgresql/prepare
  - /latest/api/ysql/prepare
---

## Synopsis

The `PREPARE` command creates a prepared statement by parsing, analyzing and rewriting (but not executing) the target statement. 
Then, the `EXECUTE` command can be issued against the prepared statement actually execute it. This separation is useful as an optimization
for the case when a statement would be executed many times with different parameters. Then the parsing, analysis and rewriting steps can only
be done once (during `PREPARE`). 


## Syntax

### Diagrams

#### prepare
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="526" height="80" viewbox="0 0 526 80"><path class="connector" d="M0 52h5m74 0h10m54 0h30m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h33m24 0h33q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-225 0q5 0 5 5v8q0 5 5 5h200q5 0 5-5v-8q0-5 5-5m5 0h10m36 0h10m82 0h5"/><rect class="literal" x="5" y="35" width="74" height="25" rx="7"/><text class="text" x="15" y="52">PREPARE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="89" y="35" width="54" height="25"/><text class="text" x="99" y="52">name</text></a><rect class="literal" x="173" y="35" width="25" height="25" rx="7"/><text class="text" x="183" y="52">(</text><rect class="literal" x="256" y="5" width="24" height="25" rx="7"/><text class="text" x="266" y="22">,</text><a xlink:href="../grammar_diagrams#data-type"><rect class="rule" x="228" y="35" width="80" height="25"/><text class="text" x="238" y="52">data_type</text></a><rect class="literal" x="338" y="35" width="25" height="25" rx="7"/><text class="text" x="348" y="52">)</text><rect class="literal" x="393" y="35" width="36" height="25" rx="7"/><text class="text" x="403" y="52">AS</text><a xlink:href="../grammar_diagrams#statement"><rect class="rule" x="439" y="35" width="82" height="25"/><text class="text" x="449" y="52">statement</text></a></svg>


#### execute
<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="393" height="80" viewbox="0 0 393 80"><path class="connector" d="M0 52h5m76 0h10m54 0h30m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h34m24 0h35q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h20m-228 0q5 0 5 5v8q0 5 5 5h203q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="35" width="76" height="25" rx="7"/><text class="text" x="15" y="52">EXECUTE</text><a xlink:href="../grammar_diagrams#name"><rect class="rule" x="91" y="35" width="54" height="25"/><text class="text" x="101" y="52">name</text></a><rect class="literal" x="175" y="35" width="25" height="25" rx="7"/><text class="text" x="185" y="52">(</text><rect class="literal" x="259" y="5" width="24" height="25" rx="7"/><text class="text" x="269" y="22">,</text><a xlink:href="../grammar_diagrams#expression"><rect class="rule" x="230" y="35" width="83" height="25"/><text class="text" x="240" y="52">expression</text></a><rect class="literal" x="343" y="35" width="25" height="25" rx="7"/><text class="text" x="353" y="52">)</text></svg>

### Grammar

```
PREPARE name [ ( data_type [, ...] ) ] AS statement

EXECUTE name [ ( expression [, ...] ) ]
```

## Semantics

- The statement in `PREPARE` may (should) contain parameters (e.g. `$1`) that will be provided by the expression list in `EXECUTE`.
- The data type list in `PREPARE` represent the types for the parameters used in the statement.
- Each expression in `EXECUTE` must match with the corresponding data type from `PREPARE`.

## Examples

- Create a sample table.

```{.sql .copy .separator-hash}
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

- Prepare a simple insert.

```{.sql .copy .separator-hash}
postgres=# PREPARE ins (bigint, double precision, int, text) AS 
               INSERT INTO sample(k1, k2, v1, v2) VALUES ($1, $2, $3, $4);
```

- Execute the insert twice (with different parameters).

```{.sql .copy .separator-hash}
postgres=# EXECUTE ins(1, 2.0, 3, 'a');
```
```{.sql .copy .separator-hash}
postgres=# EXECUTE ins(2, 3.0, 4, 'b');
```

- Check the results.

```{.sql .copy .separator-hash}
postgres=# SELECT * FROM sample ORDER BY k1;
```
```
 k1 | k2 | v1 | v2
----+----+----+----
  1 |  2 |  3 | a
  2 |  3 |  4 | b
(2 rows)
```

## See Also

[`INSERT`](../dml_insert)
[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)
