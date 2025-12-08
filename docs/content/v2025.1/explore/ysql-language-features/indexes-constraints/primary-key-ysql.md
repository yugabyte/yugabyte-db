---
title: Primary keys in YugabyteDB YSQL
headerTitle: Primary keys
linkTitle: Primary keys
description: Defining Primary key constraint in YSQL
headContent: Explore primary keys in YugabyteDB using YSQL
menu:
  v2025.1:
    identifier: primary-key-ysql
    parent: explore-indexes-constraints-ysql
    weight: 200
type: docs
---

The Primary Key constraint is a means to uniquely identify a specific row in a table via one or more columns. To define a primary key, you create a constraint that is, functionally, a [unique index](../unique-index-ysql/) applied to the table columns.

When choosing and designing the primary key of a table, it's important to get the following characteristics right:

- Uniqueness.

    The primary key is a column or a set of columns that act as the unique identifier for the rows of the table. This is essential to identify a row uniquely across the different nodes in the cluster.

- Data distribution.

    In YugabyteDB, data is distributed based on the primary key. In [hash sharding](../../../../explore/going-beyond-sql/data-sharding#hash-sharding), the data is distributed based on the hash of the primary key. In [range sharding](../../../../explore/going-beyond-sql/data-sharding#range-sharding), it is based on the actual value of the primary key.

- Data ordering.

    The table data is internally ordered based on the primary key of the table. In hash sharding, the data is ordered based on the hash of the Primary key. In range sharding, it is ordered on the actual value of the primary key.

## Definition of the primary key

Primary keys can be defined when the table is defined using the `PRIMARY KEY (columns)` clause. They can also be added after table creation using the [ALTER TABLE](../../../../explore/ysql-language-features/indexes-constraints/primary-key-ysql/#alter-table) statement, but this is not recommended as adding a primary key after data has been loaded could be an expensive operation to re-order and re-distribute the data.

{{<warning>}}
In the absence of an explicit primary key, YugabyteDB automatically inserts an internal *row_id* to be used as the primary key. This *row_id* is not accessible by users.
{{</warning>}}

In hash sharding the primary key definition has the following format:

```sql{.nocopy}
PRIMARY KEY ((columns),    columns)
--           [SHARDING]    [CLUSTERING]
```

The first set of columns, typically referred to as sharding columns, is used for the distribution of the rows. The second set of columns, referred to as Clustering columns, defines the ordering of rows with the same sharding values.

In range sharding, the primary key has the following format:

```sql{.nocopy}
PRIMARY KEY (columns)
--          [CLUSTERING]
```

The order of the keys matters a lot in range sharding. The data is distributed and ordered based on the first column, and for rows with the same first column, the rows are ordered on the second column, and so on.

## Setup

{{% explore-setup-single-new %}}

Create a census table as follows:

```sql
CREATE TABLE census(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id ASC)
);
```

<details> <summary>Add some data to the table as follows.</summary>

```sql
INSERT INTO public.census ( id,name,age,zipcode,employed ) VALUES
  (1,'Zachary',55,94085,True),    (2,'James',56,94085,False),    (3,'Kimberly',50,94084,False),
  (4,'Edward',56,94085,True),     (5,'Barry',56,94084,False),    (6,'Tyler',45,94084,False),
  (7,'Nancy',47,94085,False),     (8,'Sarah',52,94084,True),     (9,'Nancy',59,94084,False),
  (10,'Diane',51,94083,False),    (11,'Ashley',42,94083,False),  (12,'Jacqueline',58,94085,False),
  (13,'Benjamin',49,94084,False), (14,'James',48,94083,False),   (15,'Ann',43,94083,False),
  (16,'Aimee',47,94085,True),     (17,'Michael',49,94085,False), (18,'Rebecca',40,94085,False),
  (19,'Kevin',45,94085,True),     (20,'James',45,94084,False),   (21,'Sandra',60,94085,False),
  (22,'Kathleen',40,94085,True),  (23,'William',42,94084,False), (24,'James',42,94083,False),
  (25,'Tyler',50,94085,False),    (26,'James',49,94085,True),    (27,'Kathleen',55,94083,True),
  (28,'Zachary',55,94083,True),   (29,'Rebecca',41,94085,True),  (30,'Jacqueline',49,94085,False),
  (31,'Diane',48,94083,False),    (32,'Sarah',53,94085,True),    (33,'Rebecca',55,94083,True),
  (34,'William',47,94085,False),  (35,'William',60,94085,True),  (36,'Sarah',53,94085,False),
  (37,'Ashley',47,94084,True),    (38,'Ashley',54,94084,False),  (39,'Benjamin',42,94083,False),
  (40,'Tyler',47,94085,True),     (41,'Michael',42,94084,False), (42,'Diane',50,94084,False),
  (43,'Nancy',51,94085,False),    (44,'Rebecca',56,94085,False), (45,'Tyler',41,94085,True);
```

</details>

## Single column

Most commonly, the primary key is added to the table when the table is created, as demonstrated by the following syntax:

```sql
CREATE TABLE census(
   ...
   id int,
   PRIMARY KEY(id ASC)
   ...
);
```

It can also be specified along the definition of the column like so:

```sql
CREATE TABLE census(
   ...
   id int PRIMARY KEY,
   ...
);
```

## Multi-column key

Multiple columns can be grouped to be defined as the primary key as follows:

```sql
CREATE TABLE census(
   id int,
   name varchar(255),
  ...
   PRIMARY KEY(id, name ASC)
);
```

## Adding via a CONSTRAINT

You can define a primary key using the constraint clause as follows:

```sql
CONSTRAINT constraint_name PRIMARY KEY(column1, column2, ...);
```

```sql
CONSTRAINT census_pkey PRIMARY KEY(id);
```

## Using ALTER TABLE

Although you should define the primary key along with the table definition, when needed you can use the ALTER TABLE statement to create a primary key on a table after the table is created or defined:

```sql
ALTER TABLE census ADD PRIMARY KEY (id);
```

## Learn more

- [Designing optimal Primary keys](/stable/develop/data-modeling/primary-keys-ysql)
- [Primary Key](../../../../api/ysql/the-sql-language/statements/ddl_create_table/#primary-key)
- [Table with Primary Key](../../../../api/ysql/the-sql-language/statements/ddl_create_table/#table-with-primary-key)
- [Natural versus Surrogate Primary Keys in a Distributed SQL Database](https://www.yugabyte.com/blog/natural-versus-surrogate-primary-keys-in-a-distributed-sql-database/)
- [Primary Keys in PostgreSQL documentation](https://www.postgresql.org/docs/12/ddl-constraints.html#DDL-CONSTRAINTS-PRIMARY-KEYS)
