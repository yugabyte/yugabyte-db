---
title: Designing optimal primary keys in YugabyteDB
headerTitle: Designing optimal primary keys
linkTitle: Primary keys
badges: ycql
menu:
  stable:
    identifier: data-modeling-pk-ycql
    parent: data-modeling
    weight: 100
type: docs
---

{{<api-tabs>}}

The Primary key is a column or a set of columns that uniquely identifies a row, such as a user ID or order number. You should choose the primary key based on the most common access pattern. Columns of data type [string](../../../explore/ycql-language/data-types/#strings), [number](../../../explore/ycql-language/data-types/#numeric-types), [serial](../../../explore/ysql-language-features/data-types/#serial-pseudotype), or [UUID](../../../explore/ycql-language/data-types/#universally-unique-id-types) make good choices for primary keys.

## Automatically generate the primary key

The best way to uniquely identify a record is to allow the database to assign a unique identifier to the row. YugabyteDB supports multiple schemes for generating identifiers that you can choose based on the needs of your application.

### UUID

A UUID is a 128-bit number represented as a string of 36 characters, including hyphens. For example, `4b6aa2ff-53e6-44f5-8bd0-ef9de90a8095`. YugabyteDB natively supports [UUID](https://en.wikipedia.org/wiki/Universally_unique_identifier) generation as per [RFC 4122](https://datatracker.ietf.org/doc/html/rfc4122) via the uuid-ossp extension. UUIDs have several advantages:

- The likelihood of generating duplicate UUIDs is extremely low.
- UUIDs can be independently generated on different nodes in the cluster without any coordination with other systems.
- The randomness of UUIDs makes it hard to predict the next ID, providing an additional layer of security.

You can add a UUID to your schema as follows:

```sql
create keyspace if not exists yugabyte;
use yugabyte;

drop table if exists users;
create table users (
    id uuid,
    name text,
    primary key (id)
);
```

Insert some rows into the table:

```sql
insert into users (id, name) values (uuid(), 'John Wick');
insert into users (id, name) values (uuid(), 'Iron Man');
insert into users (id, name) values (uuid(), 'Harry Potter');
insert into users (id, name) values (uuid(), 'Kack Sparrow');
```

Select all the rows from the table:

```cql
select * from users;
```

Notice how the generated IDs are totally random.

```cql{.nocopy}
 id                                   | name
--------------------------------------+--------------
 85a17586-317f-4ef1-b5dd-582a13ccc832 | Harry Potter
 b431bb80-b20d-42fe-900d-fed295de507a | Kack Sparrow
 7abae478-532c-40aa-9f81-42e85750fe01 |    John Wick
 2a151214-272d-4448-af3e-a343f434fa68 |     Iron Man
```

### TimeUUID

[TimeUUID](../../../api/ycql/type_uuid/) is a special type of UUID that has a time factor integrated into it so the generated UUIDs have an order associated with them. They can be generated using the `now()` function. To do this, first create a table as follows:

```cql
create keyspace if not exists yugabyte;
use yugabyte;

drop table if exists users;
create table users (
    id timeuuid,
    name text,
    primary key(id)
);
```

Insert some rows into the table:

```sql
insert into users (id, name) values (now(), 'John Wick');
insert into users (id, name) values (now(), 'Iron Man');
insert into users (id, name) values (now(), 'Harry Potter');
insert into users (id, name) values (now(), 'Kack Sparrow');
```

Select all the rows from the table:

```cql
select * from users;
```

The generated ids are not very random:

```cql{.nocopy}
 id                                   | name
--------------------------------------+--------------
 19f24446-2904-11ef-917b-6bf61abbc06e |     Iron Man
 13dc1460-2904-11ef-917b-6bf61abbc06e |    John Wick
 1a3f8ac6-2904-11ef-917b-6bf61abbc06e | Kack Sparrow
 19f29720-2904-11ef-917b-6bf61abbc06e | Harry Potter
```

## Existing columns as primary keys

To illustrate how to choose existing columns as primary keys, first create a sample census schema.

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

Create a census table as follows:

```sql
CREATE TABLE census(
   id int,
   name varchar,
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(id)
);
```

<details> <summary>Add some data to the table as follows.</summary>

```cql
INSERT INTO census (id,name,age,zipcode,employed) VALUES (1,'Zachary',55,94085,True);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (2,'James',56,94085,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (3,'Kimberly',50,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (4,'Edward',56,94085,True);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (5,'Barry',56,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (6,'Tyler',45,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (7,'James',47,94085,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (8,'Sarah',52,94084,True);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (9,'James',59,94084,False);
INSERT INTO census (id,name,age,zipcode,employed) VALUES (10,'Diane',51,94083,False);
```

</details>

### ID as the primary key

In the `census` table, the most likely way to look up a person is by their `id`, so the primary key has been set to `id`. This means that the data is distributed based on ID. This works well for point lookups on ID. For example, to look up ID 9, you can do the following:

```sql
select * from census where id=9;
```

You will see output similar to the following:

```yaml{.nocopy}
 id | name  | age | zipcode | employed
----+-------+-----+---------+----------
  9 | Nancy |  59 |   94084 |    False
```

One row matching ID 9 was quickly fetched with just one request.

### Name as the primary key

Suppose your most common lookup is based on the name. In this case you would make the name column part of the primary key. Because the name alone may not be unique enough to be the primary key (the primary key has to be unique), you can choose a primary key with both name and ID as follows:

```sql
CREATE TABLE census2(
   id int,
   name varchar,
   age int,
   zipcode int,
   employed boolean,
   PRIMARY KEY(name, id)
) WITH CLUSTERING ORDER BY id ASC;
```

```cql
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (1,'Zachary',55,94085,True);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (2,'James',56,94085,False);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (3,'Kimberly',50,94084,False);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (4,'Edward',56,94085,True);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (5,'Barry',56,94084,False);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (6,'Tyler',45,94084,False);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (7,'James',47,94085,False);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (8,'Sarah',52,94084,True);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (9,'James',59,94084,False);
INSERT INTO census2 (id,name,age,zipcode,employed) VALUES (10,'Diane',51,94083,False);
```

When specifying the primary key, the `name` column is specified first, and `id` second. This ensures that the data is stored sorted based on `name` first, and for all matching names, the `id` is stored sorted in ascending order, ensuring all people with the same name will be stored in the same tablet. This allows you to do a fast lookup on `name` even though `(name, id)` is the primary key.

Retrieve all the people with the name James as follows:

```sql
select * from census2 where name = 'James';
```

You will see output similar to the following:

```tablegen{.nocopy}
 name  | id | age | zipcode | employed
-------+----+-----+---------+----------
 James |  2 |  56 |   94085 |    False
 James |  7 |  47 |   94085 |    False
 James |  9 |  59 |   94084 |    False
(5 rows)
```

There are 3 people named James, and all of them can be quickly looked up as the data has been distributed by name.

Notice that the rows are ordered by id. This is because you specified `CLUSTERING ORDER BY id ASC` to ensure that the rows with the same name will be stored ordered in the order of the `id` column.
