---
title: CREATE INDEX
summary: Create a new index on a table
description: CREATE INDEX
menu:
  latest:
    parent: api-cassandra
    weight: 1225
aliases:
  - api/cassandra/ddl_create_index
  - api/cql/ddl_create_index
  - api/ycql/ddl_create_index
---

[ **_The CREATE INDEX statement is currently in beta_** ]

## Synopsis
The `CREATE INDEX` statement is used to create a new index on a table. It defines the index name, index columns, and covering columns.

## Syntax

### Diagram 

#### create_index

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="800" height="100" viewbox="0 0 800 100"><path class="connector" d="M0 22h5m67 0h10m59 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m94 0h10m38 0h10m91 0h10m25 0h10m110 0h10m25 0h5m-800 50h25m216 0h20m-251 0q5 0 5 5v8q0 5 5 5h226q5 0 5-5v-8q0-5 5-5m5 0h30m128 0h20m-163 0q5 0 5 5v8q0 5 5 5h138q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="82" y="5" width="59" height="25" rx="7"/><text class="text" x="92" y="22">INDEX</text><rect class="literal" x="171" y="5" width="32" height="25" rx="7"/><text class="text" x="181" y="22">IF</text><rect class="literal" x="213" y="5" width="45" height="25" rx="7"/><text class="text" x="223" y="22">NOT</text><rect class="literal" x="268" y="5" width="64" height="25" rx="7"/><text class="text" x="278" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#index-name"><rect class="rule" x="362" y="5" width="94" height="25"/><text class="text" x="372" y="22">index_name</text></a><rect class="literal" x="466" y="5" width="38" height="25" rx="7"/><text class="text" x="476" y="22">ON</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="514" y="5" width="91" height="25"/><text class="text" x="524" y="22">table_name</text></a><rect class="literal" x="615" y="5" width="25" height="25" rx="7"/><text class="text" x="625" y="22">(</text><a xlink:href="../grammar_diagrams#index-columns"><rect class="rule" x="650" y="5" width="110" height="25"/><text class="text" x="660" y="22">index_columns</text></a><rect class="literal" x="770" y="5" width="25" height="25" rx="7"/><text class="text" x="780" y="22">)</text><a xlink:href="../grammar_diagrams#clustering-key-column-ordering"><rect class="rule" x="25" y="55" width="216" height="25"/><text class="text" x="35" y="72">clustering_key_column_ordering</text></a><a xlink:href="../grammar_diagrams#covering-columns"><rect class="rule" x="291" y="55" width="128" height="25"/><text class="text" x="301" y="72">covering_columns</text></a></svg>

#### index_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="382" height="50" viewbox="0 0 382 50"><path class="connector" d="M0 22h5m157 0h30m165 0h20m-200 0q5 0 5 5v8q0 5 5 5h175q5 0 5-5v-8q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#partition-key-columns"><rect class="rule" x="5" y="5" width="157" height="25"/><text class="text" x="15" y="22">partition_key_columns</text></a><a xlink:href="../grammar_diagrams#clustering-key-columns"><rect class="rule" x="192" y="5" width="165" height="25"/><text class="text" x="202" y="22">clustering_key_columns</text></a></svg>

#### partition_key_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="266" height="95" viewbox="0 0 266 95"><path class="connector" d="M0 22h25m106 0h130m-251 0q5 0 5 5v50q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="25"/><text class="text" x="35" y="22">column_name</text></a><rect class="literal" x="25" y="65" width="25" height="25" rx="7"/><text class="text" x="35" y="82">(</text><rect class="literal" x="121" y="35" width="24" height="25" rx="7"/><text class="text" x="131" y="52">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="80" y="65" width="106" height="25"/><text class="text" x="90" y="82">column_name</text></a><rect class="literal" x="216" y="65" width="25" height="25" rx="7"/><text class="text" x="226" y="82">)</text></svg>

#### clustering_key_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="65" viewbox="0 0 156 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="25" rx="7"/><text class="text" x="76" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a></svg>

#### clustering_key_column_ordering

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="617" height="100" viewbox="0 0 617 100"><path class="connector" d="M0 52h5m53 0h10m98 0h10m62 0h10m35 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h97m24 0h98q5 0 5 5v20q0 5-5 5m-108 0h30m44 0h29m-83 25q0 5 5 5h5m53 0h5q5 0 5-5m-78-25q5 0 5 5v33q0 5 5 5h63q5 0 5-5v-33q0-5 5-5m5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="53" height="25" rx="7"/><text class="text" x="15" y="52">WITH</text><rect class="literal" x="68" y="35" width="98" height="25" rx="7"/><text class="text" x="78" y="52">CLUSTERING</text><rect class="literal" x="176" y="35" width="62" height="25" rx="7"/><text class="text" x="186" y="52">ORDER</text><rect class="literal" x="248" y="35" width="35" height="25" rx="7"/><text class="text" x="258" y="52">BY</text><rect class="literal" x="293" y="35" width="25" height="25" rx="7"/><text class="text" x="303" y="52">(</text><rect class="literal" x="440" y="5" width="24" height="25" rx="7"/><text class="text" x="450" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="348" y="35" width="106" height="25"/><text class="text" x="358" y="52">column_name</text></a><rect class="literal" x="484" y="35" width="44" height="25" rx="7"/><text class="text" x="494" y="52">ASC</text><rect class="literal" x="484" y="65" width="53" height="25" rx="7"/><text class="text" x="494" y="82">DESC</text><rect class="literal" x="587" y="35" width="25" height="25" rx="7"/><text class="text" x="597" y="52">)</text></svg>

#### covering_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="320" height="65" viewbox="0 0 320 65"><path class="connector" d="M0 52h5m84 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="84" height="25" rx="7"/><text class="text" x="15" y="52">COVERING</text><rect class="literal" x="99" y="35" width="25" height="25" rx="7"/><text class="text" x="109" y="52">(</text><rect class="literal" x="195" y="5" width="24" height="25" rx="7"/><text class="text" x="205" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="154" y="35" width="106" height="25"/><text class="text" x="164" y="52">column_name</text></a><rect class="literal" x="290" y="35" width="25" height="25" rx="7"/><text class="text" x="300" y="52">)</text></svg>

### Grammar
```
create_index ::= CREATE INDEX [ IF NOT EXISTS ] index_name ON table_name '(' index_columns ')'
                     [ clustering_key_column_ordering ] [ covering_columns ];

index_columns ::= partition_key_columns [ clustering_key_columns ]

partition_key_columns ::= column_name | '(' column_name [ ',' column_name ...] ')'

clustering_key_columns ::= column_name [ ',' column_name ...]

clustering_key_column_ordering ::= WITH CLUSTERING ORDER BY '(' column_name [ ASC | DESC ] [ ',' column_name [ ASC | DESC ] ...] ')'

covering_columns ::= COVERING '(' column_name { ',' column_name } ')'
```

Where

- `index_name`, `table_name`, and `column_name` are identifiers. `table_name` may be qualified with a keyspace name but `index_name` may not because an index must be created in the table's keyspace.

## Semantics
- An error is raised if transactions have not be enabled using the `WITH transactions = { 'enabled' : true }` clause on the table to be indexed.
- An error is raised if `index_name` already exists in the associated keyspace unless the `IF NOT EXISTS` option is used.
- Indexes do not support TTL. An error is raised if data is inserted with TTL into a table with indexes.
- Currently, when an index is created on a table, the existing data in the table is not indexed. Therefore, the index should be created before any data is inserted into the table.

### PARTITION KEY
 - Partition key is required and defines a split of the index into _partitions_.

### CLUSTERING KEY
 - Clustering key is optional and defines an ordering for index rows within a partition.
 - Default ordering is ascending (`ASC`) but can be set for each clustering column as ascending or descending using the `CLUSTERING ORDER BY` property.
 - Any primary key column of the table not indexed explicitly in `index_columns` is added as a clustering column to the index implicitly. This is necessary so that the whole primary key of the table is indexed.

### COVERING COLUMNS
 - Covering columns are optional table columns whose values are additionally replicated by the index in order to respond to queries directly from the index without querying the table.
 - Currently, an index is used to execute a query only when the index covers all columns selected by the query.

## Examples
### Create a table to be indexed
```{.sql .copy .separator-gt}
cqlsh:example> -- 'customer_id' is the partitioning column and 'order_date' is the clustering column.
cqlsh:example> CREATE TABLE orders (customer_id INT,
                                    order_date TIMESTAMP,
                                    warehouse_id INT,
                                    amount DOUBLE,
                                    PRIMARY KEY ((customer_id), order_date))
               WITH transactions = { 'enabled' : true };
```

### Create an index for query by the `order_date` column

```{.sql .copy .separator-gt}
cqlsh:example> CREATE INDEX orders_by_date ON orders (order_date) COVERING (amount);
```

### Create an index for query by the `warehouse_id` column

```{.sql .copy .separator-gt}
cqlsh:example> CREATE INDEX orders_by_warehouse ON orders (warehouse_id, order_date) COVERING (amount);
```

### Insert some data
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO orders (customer_id, order_date, warehouse_id, amount)
               VALUES (1001, '2018-01-10', 107, 100.30);
cqlsh:example> INSERT INTO orders (customer_id, order_date, warehouse_id, amount)
               VALUES (1002, '2018-01-11', 102, 50.45);
cqlsh:example> INSERT INTO orders (customer_id, order_date, warehouse_id, amount)
               VALUES (1001, '2018-04-09', 102, 20.25);
cqlsh:example> INSERT INTO orders (customer_id, order_date, warehouse_id, amount)
               VALUES (1003, '2018-04-09', 108, 200.80);
```

### Query by the partition column `customer_id` in the table

```{.sql .copy .separator-gt}
cqlsh:example> SELECT SUM(amount) FROM orders WHERE customer_id = 1001 AND order_date >= '2018-01-01';
```
```
  sum(amount)
-------------
      120.55
```

### Query by the partition column `order_date` in the index `orders_by_date`

```{.sql .copy .separator-gt}
cqlsh:example> SELECT SUM(amount) FROM orders WHERE order_date = '2018-04-09';
```
```
 sum(amount)
-------------
      221.05
```

### Query by the partition column `warehouse_id` column in the index `orders_by_warehouse`

```{.sql .copy .separator-gt}
cqlsh:example> SELECT SUM(amount) FROM orders WHERE warehouse_id = 102 AND order_date >= '2018-01-01';
```
```
 sum(amount)
-------------
        70.7
```

## See Also

[`CREATE TABLE`](../ddl_create_table)
[Other CQL Statements](..)
