---
title: Partial indexes in YugabyteDB YCQL
headerTitle: Partial indexes
linkTitle: Partial indexes
description: Using Partial indexes in YCQL
headContent: Explore partial indexes in YugabyteDB using YCQL
menu:
  preview:
    identifier: partial-index-ycql
    parent: explore-indexes-constraints-ycql
    weight: 241
type: docs
---

Partial indexes allow you to improve query performance by reducing the index size. A smaller index is faster to scan, easier to maintain, and requires less storage.

Partial indexing works by specifying the rows defined by a conditional expression (called the predicate of the partial index), typically in the `WHERE` clause of the table.

Partial indexes can be `UNIQUE`. A UNIQUE partial index enforces the constraint that for each possible tuple of indexed columns, only one row that satisfies the `index_predicate` is allowed in the table.

A partial index might not be chosen even if the implication holds in cases where there are better query plans.

The logical implication holds if all sub-expressions of the `index_predicate` are present as is in the `where_expression`. For example, assume `where_expression = A AND B AND C`, `index_predicate_1 = A AND B`, `index_predicate_2 = A AND B AND D`, `index_predicate_3 = A AND B AND C AND D`. Then `where_expression` only implies `index_predicate_1`.

Currently, valid mathematical implications are not taken into account when checking for logical implication. For example, even if `where_expression = x > 5` and `index_predicate = x > 4`, the `SELECT` query will not use the index for scanning. This is because the two sub-expressions `x > 5` and `x > 4` differ.

## Syntax

```sql
CREATE INDEX index_name ON table_name(column_list) WHERE condition;
```

## Example

{{% explore-setup-single %}}

Create a keyspace and a table as follows:

```cql
ycqlsh> CREATE KEYSPACE example;
ycqlsh> USE example;
ycqlsh:example> CREATE TABLE orders (customer_id INT,
                                    order_date TIMESTAMP,
                                    product JSONB,
                                    warehouse_id INT,
                                    amount DOUBLE,
                                    PRIMARY KEY ((customer_id), order_date))
                WITH transactions = { 'enabled' : true };
```

Create a partial index for the `warehouse_id` column with the expression `WHERE warehouse_id < 100` to be able to enable a faster scanning of rows on queries which will benefit from such a search criteria.

```cql
ycqlsh:example> CREATE INDEX idx ON orders (warehouse_id)
                WHERE warehouse_id < 100;
```

When using a prepared statement, the logical implication check (to decide if a partial index is usable) will only consider those sub-expressions of `where_expression` that don't have dynamic parameters. This is because the query plan is decided before execution (when a statement is prepared).

```cql
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id < 100 AND order_date >= ?; // Idx can be used
```

```output
 QUERY PLAN
------------------------------------------
 Index Scan using temp.idx on temp.orders
   Filter: (order_date >= :order_date)

```

```sql
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id < ? and order_date >= ?; // Idx cannot be used
```

```output
 QUERY PLAN
--------------------------------------------------------------------------
 Seq Scan on temp.orders
   Filter: (warehouse_id < :warehouse_id) AND (order_date >= :order_date)
```

### Partial indexes with combinations of operators

Without partial indexes, many combinations of operators together on the same column in a `SELECT`'s where expression (for example, `WHERE v1 != NULL and v1 = 5`) are not allowed.

```sql
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id != NULL AND warehouse_id = ?;
```

```output
SyntaxException: Invalid CQL Statement. Illogical condition for where clause
EXPLAIN SELECT product from orders where warehouse_id != NULL and warehouse_id = ?;
                                                                  ^^^^^^^^^^^^
 (ql error -12)
```

With a partial index that subsumes some clauses of the `SELECT`'s where expression, then two or more operators that would not otherwise be supported together are supported.

```sql
ycqlsh:example> CREATE INDEX warehouse_idx ON orders (warehouse_id)
                WHERE warehouse_id != NULL;
ycqlsh:example> EXPLAIN SELECT product FROM orders
                WHERE warehouse_id != NULL AND warehouse_id = ?; // warehouse_idx can be used
```

```output
 QUERY PLAN
----------------------------------------------------
 Index Scan using temp.warehouse_idx on temp.orders
   Key Conditions: (warehouse_id = :warehouse_id)
```

## Learn more

- [PARTIAL INDEX](../../../../api/ycql/ddl_create_index/#partial-index) in the YCQL API documentation

- [Partial index with JSONB column](../secondary-indexes-with-jsonb-ycql/#partial-index)
