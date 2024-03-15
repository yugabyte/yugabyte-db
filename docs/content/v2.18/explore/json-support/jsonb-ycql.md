---
title: JSON Support in YCQL
headerTitle: JSON Support
linkTitle: JSON support
description: YCQL JSON Support in YugabyteDB.
headcontent: JSON Support in YugabyteDB
menu:
  v2.18:
    name: JSON support
    identifier: explore-json-support-2-ycql
    parent: explore
    weight: 260
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="../jsonb-ysql/" class="nav-link">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="../jsonb-ycql/" class="nav-link active">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>

JSON data types are for storing JSON (JavaScript Object Notation) data, as specified in [RFC 7159](https://tools.ietf.org/html/rfc7159). Such data can also be stored as `text`, but the JSON data types have the advantage of enforcing that each stored value is valid according to the JSON rules. Assorted JSON-specific functions and operators are also available for data stored in these data types.

{{% explore-setup-single %}}

JSON functionality in YCQL is **a subset** of the [JSON functionality in PostgreSQL](https://www.postgresql.org/docs/11/datatype-json.html).

YCQL supports the JSONB data type.

## Create a table

Create a table with a JSONB column as follows:

```sql
ycqlsh> CREATE KEYSPACE store;
```

```sql
ycqlsh> CREATE TABLE store.books ( id int PRIMARY KEY, details jsonb );
```

Insert JSONB documents:

```sql
INSERT INTO store.books (id, details) VALUES
  (1, '{ "name": "Macbeth", "author": { "first_name": "William", "last_name": "Shakespeare" }, "year": 1623, "editors": ["John", "Elizabeth", "Jeff"] }');
INSERT INTO store.books (id, details) VALUES
  (2, '{ "name": "Hamlet", "author": { "first_name": "William", "last_name": "Shakespeare" }, "year": 1603, "editors": ["Lysa", "Mark", "Robert"] }');
INSERT INTO store.books (id, details) VALUES
  (3, '{ "name": "Oliver Twist", "author": { "first_name": "Charles", "last_name": "Dickens" }, "year": 1838, "genre": "novel", "editors": ["Mark", "Tony", "Britney"] }');
INSERT INTO store.books (id, details) VALUES
  (4, '{ "name": "Great Expectations", "author": { "first_name": "Charles", "last_name": "Dickens" }, "year": 1950, "genre": "novel", "editors": ["Robert", "John", "Melisa"] }');
INSERT INTO store.books (id, details) VALUES
  (5, '{ "name": "A Brief History of Time", "author": { "first_name": "Stephen", "last_name": "Hawking" }, "year": 1988, "genre": "science", "editors": ["Melisa", "Mark", "John"] }');
```

## Query JSON documents

You can list all the row inserted using the command below.

```sql
ycqlsh> SELECT * FROM store.books;
```

```output
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}
  1 |                            {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  4 |      {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
  2 |                                {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
  3 |             {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}
```

### Using `->` and `->>`

Select with condition on JSONB object value:

```cql
ycqlsh> SELECT * FROM store.books WHERE details->'author'->>'first_name' = 'William' AND details->'author'->>'last_name' = 'Shakespeare';
```

```output
 id | details
----+----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  2 |     {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
```

Select with condition on JSONB array element:

```cql
ycqlsh> SELECT * FROM store.books WHERE details->'editors'->>0 = 'Mark';
```

```output
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  3 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}
```

Select with condition using JSONB element:

```sql
ycqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) = 1950;
```

```output
 id | details
----+--------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```

## Update JSON documents

You can update a JSON document in a number of ways.

### Update an entire document

To update an entire document, do the following:

```cql
ycqlsh> UPDATE store.books SET details = '{"author":{"first_name":"Carl","last_name":"Sagan"},"editors":["Ann","Rob","Neil"],"genre":"science","name":"Cosmos","year":1980}' WHERE id = 1;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 1;
```

```output
 id | details
----+-----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"Carl","last_name":"Sagan"},"editors":["Ann","Rob","Neil"],"genre":"science","name":"Cosmos","year":1980}
```

### Update an attribute

To update an attribute, do the following:

```cql
ycqlsh> UPDATE store.books SET details->'author'->>'first_name' = '"Steve"' WHERE id = 4;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 4;
```

```output
 id | details
----+------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Steve","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```

### Update an array element

To update an array element, do the following:

```cql
ycqlsh> UPDATE store.books SET details->'editors'->>1 = '"Jack"' WHERE id = 4;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 4;
```

```output
 id | details
----+------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Steve","last_name":"Dickens"},"editors":["Robert","Jack","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```

To update a subdocument:

```cql
ycqlsh> UPDATE store.books SET details->'author' = '{"first_name":"John", "last_name":"Doe"}' WHERE id = 4;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 4;
```

```output
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"John","last_name":"Doe"},"editors":["Robert","Jack","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```

## Upserts

### Add attributes

Update a missing JSONB document resulting in an insert as follows:

```cql
INSERT INTO store.books (id, details) VALUES
  (6, '{}');
ycqlsh> UPDATE store.books SET details->'editors' = '["Adam", "Bryan", "Charles"]' WHERE id = 6;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 6;
```

```output
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  6 | {"editors":["Adam","Bryan","Charles"]}
```

### Add subdocuments

Update a missing JSONB document resulting in an insert of a subdocument as follows:

```cql
ycqlsh> UPDATE store.books SET details->'author' = '{"first_name":"Jack", "last_name":"Kerouac"}' WHERE id = 6;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 6;
```

```output
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  6 | {"author":{"first_name":"Jack","last_name":"Kerouac"},"editors":["Adam","Bryan","Charles"]}
```

{{< note title="Note" >}}
JSONB upsert only works for JSON objects and not for other data types like arrays, integers, strings, and so on. Additionally, only the leaf property of an object is inserted if it is missing. Upsert on non-leaf properties is not currently supported.
{{< /note >}}
