---
title: Explain Statement
description: Explain Statement
summary: EXPLAIN
menu:
  latest:
    identifier: api-ysql-commands-explain
    parent: api-ysql-commands
    weight: 3700
aliases:
  - /latest/api/ysql/commands/explain
isTocNested: true
showAsideToc: true
---

## Synopsis

The `EXPLAIN` command shows the execution plan for an statement. If the `ANALYZE` option is used, the statement will be executed, rather than just planned. In that case, execution information (rather than just the planner's estimates) is added to the `EXPLAIN` result.

## Syntax

#### explain

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="466" height="110" viewbox="0 0 466 110"><path class="connector" d="M0 22h5m72 0h50m75 0h20m-110 0q5 0 5 5v8q0 5 5 5h85q5 0 5-5v-8q0-5 5-5m5 0h30m77 0h20m-112 0q5 0 5 5v8q0 5 5 5h87q5 0 5-5v-8q0-5 5-5m5 0h20m-277 0q5 0 5 5v65q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h21m24 0h22q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h80q5 0 5-5v-65q0-5 5-5m5 0h10m82 0h5"/><rect class="literal" x="5" y="5" width="72" height="25" rx="7"/><text class="text" x="15" y="22">EXPLAIN</text><rect class="literal" x="127" y="5" width="75" height="25" rx="7"/><text class="text" x="137" y="22">ANALYZE</text><rect class="literal" x="252" y="5" width="77" height="25" rx="7"/><text class="text" x="262" y="22">VERBOSE</text><rect class="literal" x="107" y="80" width="25" height="25" rx="7"/><text class="text" x="117" y="97">(</text><rect class="literal" x="178" y="50" width="24" height="25" rx="7"/><text class="text" x="188" y="67">,</text><a xlink:href="#option"><rect class="rule" x="162" y="80" width="57" height="25"/><text class="text" x="172" y="97">option</text></a><rect class="literal" x="249" y="80" width="25" height="25" rx="7"/><text class="text" x="259" y="97">)</text><a xlink:href="../grammar_diagrams#statement"><rect class="rule" x="379" y="5" width="82" height="25"/><text class="text" x="389" y="22">statement</text></a></svg>

#### option

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="301" height="425" viewbox="0 0 301 425"><path class="connector" d="M0 22h25m75 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h80m-281 40q0 5 5 5h5m77 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h63q5 0 5-5m-271 45q0 5 5 5h5m60 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h80q5 0 5-5m-271 45q0 5 5 5h5m75 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h65q5 0 5-5m-271 45q0 5 5 5h5m65 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h75q5 0 5-5m-271 45q0 5 5 5h5m80 0h30m66 0h20m-101 0q5 0 5 5v8q0 5 5 5h76q5 0 5-5v-8q0-5 5-5m5 0h60q5 0 5-5m-276-220q5 0 5 5v275q0 5 5 5h5m69 0h50m-5 0q-5 0-5-5v-17q0-5 5-5h102q5 0 5 5v17q0 5-5 5m-97 0h20m50 0h22m-82 25q0 5 5 5h5m45 0h12q5 0 5-5m-72 30q0 5 5 5h5m51 0h6q5 0 5-5m-77-55q5 0 5 5v80q0 5 5 5h5m52 0h5q5 0 5-5v-80q0-5 5-5m5 0h40m-167 0q5 0 5 5v98q0 5 5 5h142q5 0 5-5v-98q0-5 5-5m5 0h5q5 0 5-5v-275q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="75" height="25" rx="7"/><text class="text" x="35" y="22">ANALYZE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="130" y="5" width="66" height="25"/><text class="text" x="140" y="22">boolean</text></a><rect class="literal" x="25" y="50" width="77" height="25" rx="7"/><text class="text" x="35" y="67">VERBOSE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="132" y="50" width="66" height="25"/><text class="text" x="142" y="67">boolean</text></a><rect class="literal" x="25" y="95" width="60" height="25" rx="7"/><text class="text" x="35" y="112">COSTS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="115" y="95" width="66" height="25"/><text class="text" x="125" y="112">boolean</text></a><rect class="literal" x="25" y="140" width="75" height="25" rx="7"/><text class="text" x="35" y="157">BUFFERS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="130" y="140" width="66" height="25"/><text class="text" x="140" y="157">boolean</text></a><rect class="literal" x="25" y="185" width="65" height="25" rx="7"/><text class="text" x="35" y="202">TIMING</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="120" y="185" width="66" height="25"/><text class="text" x="130" y="202">boolean</text></a><rect class="literal" x="25" y="230" width="80" height="25" rx="7"/><text class="text" x="35" y="247">SUMMARY</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="135" y="230" width="66" height="25"/><text class="text" x="145" y="247">boolean</text></a><rect class="literal" x="25" y="290" width="69" height="25" rx="7"/><text class="text" x="35" y="307">FORMAT</text><rect class="literal" x="164" y="290" width="50" height="25" rx="7"/><text class="text" x="174" y="307">TEXT</text><rect class="literal" x="164" y="320" width="45" height="25" rx="7"/><text class="text" x="174" y="337">XML</text><rect class="literal" x="164" y="350" width="51" height="25" rx="7"/><text class="text" x="174" y="367">JSON</text><rect class="literal" x="164" y="380" width="52" height="25" rx="7"/><text class="text" x="174" y="397">YAML</text></svg>

### Grammar

```
explain ::= EXPLAIN [ ANALYZE] [ VERBOSE ] | ( option [, ...] ) ] statement
option ::= ANALYZE [ boolean ] | 
           VERBOSE [ boolean ] | 
           COSTS [ boolean ] | 
           BUFFERS [ boolean ] | 
           TIMING [ boolean ] | 
           SUMMARY [ boolean ] | 
           FORMAT { TEXT | XML | JSON | YAML }

boolean = TRUE | FALSE;
```

Where statement is the target statement (see more [here](../dml)).

## Examples

- Create a sample table.

```sql
postgres=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

- Insert some rows.

```sql
postgres=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

- Check the execution plan for simple select (condition will get pushed down).

```sql
postgres=# EXPLAIN SELECT * FROM sample WHERE k1 = 1;
```
```
                           QUERY PLAN
----------------------------------------------------------------
 Foreign Scan on sample  (cost=0.00..112.50 rows=1000 width=44)
(1 row)
```

- Check the execution plan for select with complex condition (second condition requires filtering).

```sql
postgres=# EXPLAIN SELECT * FROM sample WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```
```
                           QUERY PLAN
----------------------------------------------------------------
 Foreign Scan on sample  (cost=0.00..125.00 rows=1000 width=44)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
(2 rows)
```

- Check execution with `ANALYZE` option.

```sql
postgres=# EXPLAIN ANALYZE SELECT * FROM sample WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```
```
----------------------------------------------------------------------------------------------------------
 Foreign Scan on sample  (cost=0.00..125.00 rows=1000 width=44) (actual time=6.483..6.487 rows=1 loops=1)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
 Planning time: 2.390 ms
 Execution time: 5.146 ms
(4 rows)
```

## See Also

[`SELECT`](../dml_select)
[Other PostgreSQL Statements](..)