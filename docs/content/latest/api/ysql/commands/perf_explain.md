---
title: EXPLAIN
description: EXPLAIN Command
summary: EXPLAIN
menu:
  latest:
    identifier: api-ysql-commands-explain
    parent: api-ysql-commands
aliases:
  - /latest/api/ysql/commands/perf_explain
isTocNested: true
showAsideToc: true
---

## Synopsis

The `EXPLAIN` command shows the execution plan for an statement. If the `ANALYZE` option is used, the statement will be executed, rather than just planned. In that case, execution information (rather than just the planner's estimates) is added to the `EXPLAIN` result.

## Syntax

#### explain

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="371" height="107" viewbox="0 0 371 107"><path class="connector" d="M0 21h25m69 0h30m75 0h20m-110 0q5 0 5 5v8q0 5 5 5h85q5 0 5-5v-8q0-5 5-5m5 0h30m77 0h20m-112 0q5 0 5 5v8q0 5 5 5h87q5 0 5-5v-8q0-5 5-5m5 0h20m-356 0q5 0 5 5v63q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-19q0-5 5-5h23m24 0h23q5 0 5 5v19q0 5-5 5m-5 0h30m25 0h10m25 0h10m85 0h26q5 0 5-5v-63q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="69" height="24" rx="7"/><text class="text" x="35" y="21">EXPLAIN</text><rect class="literal" x="124" y="5" width="75" height="24" rx="7"/><text class="text" x="134" y="21">ANALYZE</text><rect class="literal" x="249" y="5" width="77" height="24" rx="7"/><text class="text" x="259" y="21">VERBOSE</text><rect class="literal" x="25" y="78" width="25" height="24" rx="7"/><text class="text" x="35" y="94">(</text><rect class="literal" x="98" y="49" width="24" height="24" rx="7"/><text class="text" x="108" y="65">,</text><a xlink:href="../grammar_diagrams#option"><rect class="rule" x="80" y="78" width="60" height="24"/><text class="text" x="90" y="94">option</text></a><rect class="literal" x="170" y="78" width="25" height="24" rx="7"/><text class="text" x="180" y="94">)</text><a xlink:href="../grammar_diagrams#]"><rect class="rule" x="205" y="78" width="25" height="24"/><text class="text" x="215" y="94">]</text></a><a xlink:href="../grammar_diagrams#statement"><rect class="rule" x="240" y="78" width="85" height="24"/><text class="text" x="250" y="94">statement</text></a></svg>

#### option

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="251" height="385" viewbox="0 0 251 385"><path class="connector" d="M0 21h25m75 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h25m-231 39q0 5 5 5h5m77 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h8q5 0 5-5m-221 44q0 5 5 5h5m60 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h25q5 0 5-5m-221 44q0 5 5 5h5m75 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h10q5 0 5-5m-221 44q0 5 5 5h5m61 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h24q5 0 5-5m-221 44q0 5 5 5h5m80 0h30m71 0h20m-106 0q5 0 5 5v8q0 5 5 5h81q5 0 5-5v-8q0-5 5-5m5 0h5q5 0 5-5m-226-215q5 0 5 5v254q0 5 5 5h5m69 0h30m49 0h22m-81 24q0 5 5 5h5m43 0h13q5 0 5-5m-71 29q0 5 5 5h5m49 0h7q5 0 5-5m-76-53q5 0 5 5v77q0 5 5 5h5m51 0h5q5 0 5-5v-77q0-5 5-5m5 0h36q5 0 5-5v-254q0-5 5-5m5 0h5"/><rect class="literal" x="25" y="5" width="75" height="24" rx="7"/><text class="text" x="35" y="21">ANALYZE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="130" y="5" width="71" height="24"/><text class="text" x="140" y="21">boolean</text></a><rect class="literal" x="25" y="49" width="77" height="24" rx="7"/><text class="text" x="35" y="65">VERBOSE</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="132" y="49" width="71" height="24"/><text class="text" x="142" y="65">boolean</text></a><rect class="literal" x="25" y="93" width="60" height="24" rx="7"/><text class="text" x="35" y="109">COSTS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="115" y="93" width="71" height="24"/><text class="text" x="125" y="109">boolean</text></a><rect class="literal" x="25" y="137" width="75" height="24" rx="7"/><text class="text" x="35" y="153">BUFFERS</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="130" y="137" width="71" height="24"/><text class="text" x="140" y="153">boolean</text></a><rect class="literal" x="25" y="181" width="61" height="24" rx="7"/><text class="text" x="35" y="197">TIMING</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="116" y="181" width="71" height="24"/><text class="text" x="126" y="197">boolean</text></a><rect class="literal" x="25" y="225" width="80" height="24" rx="7"/><text class="text" x="35" y="241">SUMMARY</text><a xlink:href="../grammar_diagrams#boolean"><rect class="rule" x="135" y="225" width="71" height="24"/><text class="text" x="145" y="241">boolean</text></a><rect class="literal" x="25" y="269" width="69" height="24" rx="7"/><text class="text" x="35" y="285">FORMAT</text><rect class="literal" x="124" y="269" width="49" height="24" rx="7"/><text class="text" x="134" y="285">TEXT</text><rect class="literal" x="124" y="298" width="43" height="24" rx="7"/><text class="text" x="134" y="314">XML</text><rect class="literal" x="124" y="327" width="49" height="24" rx="7"/><text class="text" x="134" y="343">JSON</text><rect class="literal" x="124" y="356" width="51" height="24" rx="7"/><text class="text" x="134" y="372">YAML</text></svg>

### Grammar

```
explain ::= EXPLAIN [ ANALYZE] [ VERBOSE ] | ( option [, ...] ) ] statement
option ::= ANALYZE [ boolean ]
           | VERBOSE [ boolean ]
           | COSTS [ boolean ]
           | BUFFERS [ boolean ]
           | TIMING [ boolean ]
           | SUMMARY [ boolean ]
           | FORMAT { TEXT | XML | JSON | YAML }

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