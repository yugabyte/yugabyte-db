---
title: JSON Support in YCQL
headerTitle: JSON Support
linkTitle: JSON support
description: YCQL JSON Support in YugabyteDB.
headcontent: Explore YugabyteDB YCQL support for JSON data
menu:
  preview:
    name: JSON support
    identifier: explore-json-support-ycql
    parent: explore-ycql-language
    weight: 260
type: docs
---

JSON data types are for storing JSON (JavaScript Object Notation) data, as specified in [RFC 7159](https://tools.ietf.org/html/rfc7159). Such data can also be stored as `text`, but the JSON data types have the advantage of enforcing that each stored value is valid according to the JSON rules. Assorted JSON-specific functions and operators are also available for data stored in these data types.

{{% explore-setup-single %}}

YCQL supports the JSONB data type. JSONB stores JSON data in binary format. JSONB does not preserve white space, does not preserve the order of object keys, and does not keep duplicate object keys. If duplicate keys are specified in the input, only the last value is kept.

The following sections describe different operations on JSONB columns with some examples.

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

You can list all the rows inserted using the following command:

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

`->` and `->>` are supported JSONB operators that can be used to access attributes of a JSON object.

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

### Add a new attribute via Update

To add a new attribute via Update, do the following:

```cql
ycqlsh> UPDATE store.books SET details->'language' = '"English"' WHERE id = 4;
```

```cql
ycqlsh> SELECT * FROM store.books WHERE id = 4;
```

```output
 id | details
----+------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Steve","last_name":"Dickens"},"editors":["Robert","Jack","Melisa"],"genre":"novel","language":"English","name":"Great Expectations","year":1950}
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
