---
title: CREATE INDEX
summary: Create a new index on a table
description: CREATE INDEX
beta: /faq/product/#what-is-the-definition-of-the-beta-feature-tag
menu:
  latest:
    parent: api-cassandra
    weight: 1225
aliases:
  - api/cassandra/ddl_create_index
  - api/cql/ddl_create_index
  - api/ycql/ddl_create_index
---

## Synopsis
The `CREATE INDEX` statement is used to create a new index on a table. It defines the index name, index columns, and additional columns to include.

## Syntax

### Diagram 

#### create_index

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="919" height="100" viewbox="0 0 919 100"><path class="connector" d="M0 22h5m67 0h30m69 0h20m-104 0q5 0 5 5v8q0 5 5 5h79q5 0 5-5v-8q0-5 5-5m5 0h10m59 0h30m32 0h10m45 0h10m64 0h20m-196 0q5 0 5 5v8q0 5 5 5h171q5 0 5-5v-8q0-5 5-5m5 0h10m94 0h10m38 0h10m91 0h10m25 0h10m110 0h10m25 0h5m-919 50h25m127 0h20m-162 0q5 0 5 5v8q0 5 5 5h137q5 0 5-5v-8q0-5 5-5m5 0h30m216 0h20m-251 0q5 0 5 5v8q0 5 5 5h226q5 0 5-5v-8q0-5 5-5m5 0h5"/><rect class="literal" x="5" y="5" width="67" height="25" rx="7"/><text class="text" x="15" y="22">CREATE</text><rect class="literal" x="102" y="5" width="69" height="25" rx="7"/><text class="text" x="112" y="22">UNIQUE</text><rect class="literal" x="201" y="5" width="59" height="25" rx="7"/><text class="text" x="211" y="22">INDEX</text><rect class="literal" x="290" y="5" width="32" height="25" rx="7"/><text class="text" x="300" y="22">IF</text><rect class="literal" x="332" y="5" width="45" height="25" rx="7"/><text class="text" x="342" y="22">NOT</text><rect class="literal" x="387" y="5" width="64" height="25" rx="7"/><text class="text" x="397" y="22">EXISTS</text><a xlink:href="../grammar_diagrams#index-name"><rect class="rule" x="481" y="5" width="94" height="25"/><text class="text" x="491" y="22">index_name</text></a><rect class="literal" x="585" y="5" width="38" height="25" rx="7"/><text class="text" x="595" y="22">ON</text><a xlink:href="../grammar_diagrams#table-name"><rect class="rule" x="633" y="5" width="91" height="25"/><text class="text" x="643" y="22">table_name</text></a><rect class="literal" x="734" y="5" width="25" height="25" rx="7"/><text class="text" x="744" y="22">(</text><a xlink:href="../grammar_diagrams#index-columns"><rect class="rule" x="769" y="5" width="110" height="25"/><text class="text" x="779" y="22">index_columns</text></a><rect class="literal" x="889" y="5" width="25" height="25" rx="7"/><text class="text" x="899" y="22">)</text><a xlink:href="../grammar_diagrams#included-columns"><rect class="rule" x="25" y="55" width="127" height="25"/><text class="text" x="35" y="72">included_columns</text></a><a xlink:href="../grammar_diagrams#clustering-key-column-ordering"><rect class="rule" x="202" y="55" width="216" height="25"/><text class="text" x="212" y="72">clustering_key_column_ordering</text></a></svg>

#### index_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="382" height="50" viewbox="0 0 382 50"><path class="connector" d="M0 22h5m157 0h30m165 0h20m-200 0q5 0 5 5v8q0 5 5 5h175q5 0 5-5v-8q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#partition-key-columns"><rect class="rule" x="5" y="5" width="157" height="25"/><text class="text" x="15" y="22">partition_key_columns</text></a><a xlink:href="../grammar_diagrams#clustering-key-columns"><rect class="rule" x="192" y="5" width="165" height="25"/><text class="text" x="202" y="22">clustering_key_columns</text></a></svg>

#### partition_key_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="266" height="95" viewbox="0 0 266 95"><path class="connector" d="M0 22h25m106 0h130m-251 0q5 0 5 5v50q0 5 5 5h5m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5q5 0 5-5v-50q0-5 5-5m5 0h5"/><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="5" width="106" height="25"/><text class="text" x="35" y="22">column_name</text></a><rect class="literal" x="25" y="65" width="25" height="25" rx="7"/><text class="text" x="35" y="82">(</text><rect class="literal" x="121" y="35" width="24" height="25" rx="7"/><text class="text" x="131" y="52">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="80" y="65" width="106" height="25"/><text class="text" x="90" y="82">column_name</text></a><rect class="literal" x="216" y="65" width="25" height="25" rx="7"/><text class="text" x="226" y="82">)</text></svg>

#### clustering_key_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="156" height="65" viewbox="0 0 156 65"><path class="connector" d="M0 52h25m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h25"/><rect class="literal" x="66" y="5" width="24" height="25" rx="7"/><text class="text" x="76" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="25" y="35" width="106" height="25"/><text class="text" x="35" y="52">column_name</text></a></svg>

#### included_columns

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="311" height="65" viewbox="0 0 311 65"><path class="connector" d="M0 52h5m75 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h46m24 0h46q5 0 5 5v20q0 5-5 5m-5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="75" height="25" rx="7"/><text class="text" x="15" y="52">INCLUDE</text><rect class="literal" x="90" y="35" width="25" height="25" rx="7"/><text class="text" x="100" y="52">(</text><rect class="literal" x="186" y="5" width="24" height="25" rx="7"/><text class="text" x="196" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="145" y="35" width="106" height="25"/><text class="text" x="155" y="52">column_name</text></a><rect class="literal" x="281" y="35" width="25" height="25" rx="7"/><text class="text" x="291" y="52">)</text></svg>

#### clustering_key_column_ordering

<svg class="rrdiagram" version="1.1" xmlns:xlink="http://www.w3.org/1999/xlink" xmlns="http://www.w3.org/2000/svg" width="617" height="100" viewbox="0 0 617 100"><path class="connector" d="M0 52h5m53 0h10m98 0h10m62 0h10m35 0h10m25 0h30m-5 0q-5 0-5-5v-20q0-5 5-5h97m24 0h98q5 0 5 5v20q0 5-5 5m-108 0h30m44 0h29m-83 25q0 5 5 5h5m53 0h5q5 0 5-5m-78-25q5 0 5 5v33q0 5 5 5h63q5 0 5-5v-33q0-5 5-5m5 0h30m25 0h5"/><rect class="literal" x="5" y="35" width="53" height="25" rx="7"/><text class="text" x="15" y="52">WITH</text><rect class="literal" x="68" y="35" width="98" height="25" rx="7"/><text class="text" x="78" y="52">CLUSTERING</text><rect class="literal" x="176" y="35" width="62" height="25" rx="7"/><text class="text" x="186" y="52">ORDER</text><rect class="literal" x="248" y="35" width="35" height="25" rx="7"/><text class="text" x="258" y="52">BY</text><rect class="literal" x="293" y="35" width="25" height="25" rx="7"/><text class="text" x="303" y="52">(</text><rect class="literal" x="440" y="5" width="24" height="25" rx="7"/><text class="text" x="450" y="22">,</text><a xlink:href="../grammar_diagrams#column-name"><rect class="rule" x="348" y="35" width="106" height="25"/><text class="text" x="358" y="52">column_name</text></a><rect class="literal" x="484" y="35" width="44" height="25" rx="7"/><text class="text" x="494" y="52">ASC</text><rect class="literal" x="484" y="65" width="53" height="25" rx="7"/><text class="text" x="494" y="82">DESC</text><rect class="literal" x="587" y="35" width="25" height="25" rx="7"/><text class="text" x="597" y="52">)</text></svg>

### Grammar
```
create_index ::= CREATE [ UNIQUE ] INDEX [ IF NOT EXISTS ] index_name ON table_name '(' index_columns ')'
                     [ included_columns ] [ clustering_key_column_ordering ];

index_columns ::= partition_key_columns [ clustering_key_columns ]

partition_key_columns ::= column_name | '(' column_name [ ',' column_name ...] ')'

clustering_key_columns ::= column_name [ ',' column_name ...]

included_columns ::= INCLUDE '(' column_name { ',' column_name } ')'

clustering_key_column_ordering ::= WITH CLUSTERING ORDER BY '(' column_name [ ASC | DESC ] [ ',' column_name [ ASC | DESC ] ...] ')'
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

### INCLUDED COLUMNS
 - Included columns are optional table columns whose values are copied into the index in addition to storing them in the table. This is done in order to respond to queries directly from the index without querying the table.
 - Currently, an index is used to execute a query only when all columns selected by the query are included by the index, either as index columns or additional included columns. This limitation will be removed in future versions.

### UNIQUE INDEX
 - A unique index disallows duplicate values from being inserted into the indexed columns. It can be used to ensure uniqueness of index column values.

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
cqlsh:example> CREATE INDEX orders_by_date ON orders (order_date) INCLUDE (amount);
```

### Create an index for query by the `warehouse_id` column

```{.sql .copy .separator-gt}
cqlsh:example> CREATE INDEX orders_by_warehouse ON orders (warehouse_id, order_date) INCLUDE (amount);
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

### Create a table with a unique index
```{.sql .copy .separator-gt}
cqlsh:example> CREATE TABLE emp (enum INT primary key,
                                 lastname VARCHAR,
                                 firstname VARCHAR,
                                 userid VARCHAR)
               WITH transactions = { 'enabled' : true };
cqlsh:example> CREATE UNIQUE INDEX emp_by_userid ON emp (userid);
```

### Insert values into the table and verify no duplicate `userid` is inserted
```{.sql .copy .separator-gt}
cqlsh:example> INSERT INTO emp (enum, lastname, firstname, userid)
               VALUES (1001, 'Smith', 'John', 'jsmith');
cqlsh:example> INSERT INTO emp (enum, lastname, firstname, userid)
               VALUES (1002, 'Smith', 'Jason', 'jsmith');
InvalidRequest: Error from server: code=2200 [Invalid query] message="SQL error: Execution Error. Duplicate value disallowed by unique index emp_by_userid
INSERT INTO emp (enum, lastname, firstname, userid)
       ^^^^
VALUES (1002, 'Smith', 'Jason', 'jsmith');
 (error -300)"
cqlsh:example> INSERT INTO emp (enum, lastname, firstname, userid)
               VALUES (1002, 'Smith', 'Jason', 'jasmith');
```
```
cqlsh:example> SELECT * FROM emp;
```
```
 enum | lastname | firstname | userid
------+----------+-----------+---------
 1002 |    Smith |     Jason | jasmith
 1001 |    Smith |      John |  jsmith
```



## See Also

[`CREATE TABLE`](../ddl_create_table)
[Other CQL Statements](..)
