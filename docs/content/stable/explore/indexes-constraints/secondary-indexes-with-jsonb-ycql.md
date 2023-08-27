---
title: Secondary indexes with JSONB in YugabyteDB YCQL
headerTitle: Secondary indexes with JSONB
linkTitle: Secondary indexes with JSONB
description: Secondary indexes with JSONB in YugabyteDB YCQL
headContent: Explore secondary indexes with JSONB in YugabyteDB using YCQL
image: /images/section_icons/secure/create-roles.png
menu:
  stable:
    identifier: secondary-indexes-with-jsonb-ycql
    parent: explore-indexes-constraints
    weight: 260
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li >
    <a href="../secondary-indexes-with-jsonb-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>
  <li >
    <a href="../secondary-indexes-with-jsonb-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>
</ul>

Secondary indexes can be created with a JSONB datatype column in YCQL. Secondary Indexes in YCQL are global and distributed and similar to tables.  So the use of indexes can enhance database performance by enabling the database server to find rows faster.  You can create covering indexes as well as partial indexes with JSONB columns.

The following section describes secondary indexes with JSONB column in YCQL using examples.

{{% explore-setup-single %}}

## Create table

Secondary indexes in YCQL can be created only on tables with `transaction = {'enabled':'true'}`. This is due to the use of distributed ACID transactions under the hood to maintain the consistency of secondary indexes in YCQL.

Any attempt to create a secondary index on a table without `transactions = {'enabled':'true'}` results in an error.

Create a `users` table that has a JSONB column as follows:

```cql
CREATE TABLE users(
   id int PRIMARY KEY,
   first_name TEXT,
   last_name TEXT,
   email TEXT,
   address JSONB
)
WITH transactions = {'enabled':'true'};
```

Insert a few records into `users` table as follows:

```cql
INSERT INTO users(id, first_name, last_name, email, address) VALUES(1, 'Luke', 'Skywalker','lskywalker@yb.com','{"lane":"551 Starwars way","city":"Skyriver","zip":"327"}');
INSERT INTO users(id, first_name, last_name, email, address ) VALUES(2, 'Obi-Wan', 'Kenobi','owkenobi@yb.com','{"lane":"552 Starwars way","city":"Skyriver","zip":"327"}');
INSERT INTO users(id, first_name, last_name, email, address) VALUES(3, 'Yoda',null,'yoda@yb.com','{"lane":"553 Starwars way","city":"Skyriver","zip":"327"}');
INSERT INTO users(id, first_name, last_name, email, address) VALUES(4, 'Din','Djarin','ddjarin@yb.com','{"lane":"554 Starwars way","city":"Skyriver","zip":"327"}');
INSERT INTO users(id, first_name, last_name, email) VALUES(5, 'R2','D2','r2d2@yb.com');
INSERT INTO users(id, first_name, last_name, email) VALUES(6, 'Leia','Princess','lprincess@yb.com');

```

## Create indexes

### Syntax

You can create indexes with JSON column in YCQL using the `CREATE INDEX` statement that has the following syntax:

```cql
CREATE INDEX index_name ON table_name(column->>'attribute');
```

*column* represents a JSONB datatype column of the table. *'attribute'* refers to an attribute of the JSON document stored in the JSONB column.

Create an index on the *'zip'* attribute of the JSON document stored in the address column of the users table as follows:

```cql
CREATE INDEX idx_users_jsonb ON users(address->>'zip');
```

## List the index and verify the query plan

You can use the `DESCRIBE INDEX` command to check the index as follows:

```cql
DESCRIBE INDEX idx_users_jsonb;
```

For additional information regarding the DESCRIBE INDEX command, see [DESCRIBE INDEX](../../../admin/ycqlsh/#describe).

You can also use the `EXPLAIN` statement to check if a query uses an index and determine the query plan before execution.

```cql
EXPLAIN SELECT * FROM users WHERE address->>'zip' = '327';
```

For additional information, see the [EXPLAIN](../../../api/ycql/explain/) statement.

## Covering index and Partial index with JSONB column

You can also create covering and partial indexes with a JSONB column.

### Covering index

A covering index includes all columns used in the query in the index definition. You do this using the `INCLUDE` keyword in the `CREATE INDEX` syntax as follows:

```cql
CREATE INDEX idx_users_jsonb_cov ON users((address->>'zip'))
     INCLUDE (first_name,last_name);
```

### Partial index

A partial index is created on a subset of data when you want to restrict the index to a specific condition. You do this using the `WHERE` clause in the `CREATE INDEX` syntax as follows:

``` cql
CREATE INDEX idx_users_jsonb_part ON users (address->>'zip')
       WHERE email = 'lskywalker@yb.com';
```

For additional information on the CREATE INDEX statement, see [CREATE INDEX](../../../api/ycql/ddl_create_index/).

## Remove indexes

You can remove an index created with the JSONB datatype column using the `DROP INDEX` statement in YCQL with the following syntax:

```sql
DROP INDEX idx_users_jsonb;
```

For additional information, see [DROP INDEX](../../../api/ycql/ddl_drop_index/).
