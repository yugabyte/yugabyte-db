---
title: EXPLAIN statement [YSQL]
headerTitle: EXPLAIN
linkTitle: EXPLAIN
description: Use the EXPLAIN statement to show the execution plan for an statement. If the ANALYZE option is used, the statement will be executed, rather than just planned.
menu:
  v2.20:
    identifier: perf_explain
    parent: statements
type: docs
---

## Synopsis

Use the `EXPLAIN` statement to show the execution plan for a statement. If the `ANALYZE` option is used, the statement will be executed, rather than just planned. In that case, execution information (rather than just the planner's estimates) is added to the `EXPLAIN` result.

{{< warning title="DML vs DDL" >}}
The `EXPLAIN` statement is designed to work primarily for DML statements (for example, `SELECT`, `INSERT`, and so on). DDL statements are _not_ explainable and in cases where DDL and DML are combined, the EXPLAIN statement shows only an approximation. For example, `EXPLAIN` on `SELECT * FROM <TABLE-1> INTO <TABLE-2>` provides only an approximation as `INTO` is a DDL statement.
{{</ warning >}}

## Syntax

{{%ebnf%}}
  explain,
  option
{{%/ebnf%}}

## Semantics

Where statement is the target statement (see [SELECT](../dml_select/)).

### ANALYZE

Execute the statement and show actual run times and other statistics (default: `FALSE`).

### VERBOSE

Present more details about the plan, such as the output column list for each node in the plan tree, schema-qualify table and function names, ensure labeling variables in expressions with their range table alias, and consistently indicate the name of each trigger for which statistics are shown (default: `FALSE`).

### BUFFERS

Include information on buffer usage. Specifically, include the number of shared blocks hit, read, dirtied, and written; the number of local blocks hit, read, dirtied, and written; and the number of temporary blocks read and written (default: `FALSE`).

### COSTS

Incorporate details about the anticipated startup and total expenses for each plan node, along with the estimated number of rows and the projected width of each row (default: `ON`).

### DIST

Display additional runtime statistics related to the distributed storage layer as seen at the query layer. The flag also provides more implementation-specific insights into the distributed nature of query execution (default: `FALSE`).

### DEBUG

Display low-level runtime statistics related to the distributed storage layer (default: `FALSE`).

### FORMAT

Define the desired output format, choosing from TEXT, XML, JSON, or YAML. Non-text output retains the same information as the text format, but is more programmatically accessible (default: `TEXT`).

## Examples

Create a sample table.

```sql
yugabyte=# CREATE TABLE sample(k1 int, k2 int, v1 int, v2 text, PRIMARY KEY (k1, k2));
```

Insert some rows.

```sql
yugabyte=# INSERT INTO sample(k1, k2, v1, v2) VALUES (1, 2.0, 3, 'a'), (2, 3.0, 4, 'b'), (3, 4.0, 5, 'c');
```

Check the execution plan for simple select (condition will get pushed down).

```sql
yugabyte=# EXPLAIN SELECT * FROM sample WHERE k1 = 1;
```

```output
                                  QUERY PLAN
------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample  (cost=0.00..15.25 rows=100 width=44)
   Index Cond: (k1 = 1)
(2 rows)
```

- Check the execution plan for select with complex condition (second condition requires filtering).

```sql
yugabyte=# EXPLAIN SELECT * FROM sample WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```

```output
                                  QUERY PLAN
------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample  (cost=0.00..17.75 rows=100 width=44)
   Index Cond: (k1 = 2)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
(3 rows)
```

Check execution with `ANALYZE` option.

```sql
yugabyte=# EXPLAIN ANALYZE SELECT * FROM sample WHERE k1 = 2 and floor(k2 + 1.5) = v1;
```

```output
                                                       QUERY PLAN
------------------------------------------------------------------------------------------------------------------------
 Index Scan using sample_pkey on sample  (cost=0.00..17.75 rows=100 width=44) (actual time=3.123..3.126 rows=1 loops=1)
   Index Cond: (k1 = 2)
   Filter: (floor(((k2)::numeric + 1.5)) = (v1)::numeric)
 Planning Time: 0.149 ms
 Execution Time: 3.198 ms
 Peak Memory Usage: 8 kB
(6 rows)
```

## See also

- [INSERT](../dml_insert/)
- [SELECT](../dml_select/)
