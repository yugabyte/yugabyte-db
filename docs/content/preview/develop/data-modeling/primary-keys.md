---
title: Designing optimal primary keys in YugabyteDB
headerTitle: Designing optimal primary keys
linkTitle: Primary keys
badges: ysql
menu:
  preview:
    identifier: data-modeling-pk
    parent: data-modeling
    weight: 100
type: docs
---

The Primary key is a column or a set of columns that uniquely identifies a row, such as a user ID or order number. The primary key should chosen based on the most common access pattern. Columns of data type [string](../../../explore/ysql-language-features/data-types/#strings), [number](../../../explore/ysql-language-features/data-types/#numeric-types), [serial](../../../explore/ysql-language-features/data-types/#serial-pseudotype), or [UUID](../../../api/ysql/datatypes/type_uuid/) make good choices for primary keys.

## Automatically generating the primary key

For uniquely identifying a record, it is typically advantageous to allow the database to assign a unique identifier to the row. YugabyteDB supports multiple identifier generation schemes that can be chosen depending on the needs of your application.

### UUID

The UUID is a 128-bit number represented as a string of 36 characters including hyphens For e.g, `7b4245cb-1be4-4c0b-b64a-e92ca3750715`. YugabyteDB natively supports [UUID](https://en.wikipedia.org/wiki/Universally_unique_identifier) generation as per [RFC 4122](https://datatracker.ietf.org/doc/html/rfc4122) via the uuid-ossp extension. UUIDs have several advantages.

- The random nature of UUID ensures that the likelihood of generating duplicate UUIDs is extremely low.
- The decentralized generation of UUID allows for different nodes in the cluster to generate unique numbers independently without any coordination with other systems.
- The randomness in UUID makes it hard to predict the next ID, providing an additional layer of security.

You can easily add a UUID to your schema as,

```sql
CREATE TABLE users (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    name TEXT
);
```

The [DEFAULT](../../../api/ysql/the-sql-language/statements/ddl_create_table/#default) clause ensures that for every row inserted, a UUID is automatically generated and inserted along with the row.

### Serial

[Serial](../../../api/ysql/datatypes/type_serial/) is a special data type in YugabyteDB that creates an auto-incrementing integer column starting with `1`. It is essentially a shorthand for creating a sequence and using it as a default value for a column. You can choose between three types of serial data types depending on the needs of your application.

- **SMALLSERIAL**:  An integer column in the range of 1 to 32,767
- **SERIAL**: An integer column in the range of 1 to 2,147,483,647
- **BIGSERIAL**:  An integer column in the range of 1 to 9,223,372,036,854,775,807

It can be used directly within table definitions to simplify the creation of auto-incrementing columns.

```sql
DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id serial,
    name TEXT,
    PRIMARY KEY(id)
);
```

For each row inserted into the table, an auto-incremented `id` value will be automatically inserted along with the row.

### Sequence

A [sequence](../../../api/ysql/the-sql-language/statements/ddl_create_sequence/) is a database object that generates a sequence of unique numbers. Sequences are independent objects that can be associated with one or more tables or columns. Sequences offer more flexibility and control over auto-incrementing behavior. They can be created, managed, and used separately from table definitions. Sequences can be customized with different increment values, start values, minimum and maximum values, and cycle behavior.

```sql
CREATE SEQUENCE user_id_seq START 100 INCREMENT BY 100 CACHE 10000;

DROP TABLE IF EXISTS users;
CREATE TABLE users (
    id INTEGER DEFAULT nextval('user_id_seq'),
    name TEXT,
    PRIMARY KEY(id)
);
```

Now, for every row inserted, user-ids will be automatically generated as 100,200,300 and so on.

{{<tip>}}
Use serial for simple use cases and opt for sequences when you need more control over the sequence behavior, need to share a sequence between multiple tables or columns, or require custom incrementing logic.
{{</tip>}}

## Existing columns as primary keys

Let us see how to choose existing columns as primary keys. Let us understand this with a sample census schema.

<!-- begin: nav tabs -->
{{<nav/tabs list="local,anywhere" active="local" repeatedTabs="true"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- local cluster setup instructions -->
{{<setup/local>}}
{{</nav/panel>}}

{{<nav/panel name="anywhere">}} {{<setup/anywhere>}} {{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

For illustration, create a census table as follows.

```sql
CREATE TABLE census(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id ASC)
)
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

### ID as the primary key

In the `census` table, the most likely way to look up a person is by their `id`, so the primary key has been set to `id ASC`. This means that the data is stored in ascending order of ID, ensuring contiguous IDs are mostly located in the same tablet. This works well for point lookups on ID and range scans on IDs. For example, to look up ID 9, you can do the following:

```sql
select * from census where id=9;
```

You will see an output similar to the following:

```yaml{.nocopy}
 id | name  | age | zipcode | employed
----+-------+-----+---------+----------
  9 | Nancy |  59 |   94084 | f
```

One row matching ID 9 was quickly fetched with just one request. You can also do a quick range scan.

```sql
select * from census where id>=5 and id<=15;
```

You will see an output similar to:

```tablegen{.nocopy}
 id |    name    | age | zipcode | employed
----+------------+-----+---------+----------
  5 | Barry      |  56 |   94084 | f
  6 | Tyler      |  45 |   94084 | f
  7 | Nancy      |  47 |   94085 | f
  8 | Sarah      |  52 |   94084 | t
  9 | Nancy      |  59 |   94084 | f
 10 | Diane      |  51 |   94083 | f
 11 | Ashley     |  42 |   94083 | f
 12 | Jacqueline |  58 |   94085 | f
 13 | Benjamin   |  49 |   94084 | f
 14 | James      |  48 |   94083 | f
 15 | Ann        |  43 |   94083 | f
(11 rows)
```

11 rows were quickly retrieved as the data is stored sorted on the `id` column. So range scans are also fast.

### Name as the primary key

Suppose your most common lookup is based on the name. In this case you would make the name column part of the primary key. Because the name alone may not be unique enough to be the primary key (the primary key has to be unique), you can choose a primary key with both name and ID as follows:

```sql
CREATE TABLE census2(
   id int,
   name varchar(255),
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(name ASC, id ASC)
);

-- copy the same data into census2
INSERT INTO census2 SELECT * FROM census;
```

Note how the `name` column is specified first, and `id` second. This ensures that the data is stored sorted based on `name` first, and for all matching names, the `id` will be stored sorted in ascending order, and all the people with the same name will be in the same tablet. This allows you to do a fast lookup on `name` even though `(name, id)` is the primary key. Retrieve all the people with the name James as follows:

```sql
select * from census2 where name = 'James';
```

You will see an output similar to the following:

```tablegen{.nocopy}
 id | name  | age | zipcode | employed
----+-------+-----+---------+----------
  2 | James |  56 |   94085 | f
 14 | James |  48 |   94083 | f
 20 | James |  45 |   94084 | f
 24 | James |  42 |   94083 | f
 26 | James |  49 |   94085 | t
(5 rows)
```

There are 5 people named James, and all of them can be quickly looked up as the data has been sorted on name.

{{<note title="Ordering">}}
The primary key was specified with `ASC` order. However, if the queries are going to retrieve data in descending order with `ORDER BY name DESC`, then it is better to match the same ordering in the primary key definition.
{{</note>}}
