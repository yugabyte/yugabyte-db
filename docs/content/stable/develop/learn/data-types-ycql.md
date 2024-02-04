---
title: Data types in YCQL
headerTitle: Data types
linkTitle: Data types
description: Learn about the data types in YCQL.
menu:
  stable:
    identifier: data-types-1-ycql
    parent: learn
    weight: 120
type: docs
---

{{<api-tabs list="ycql">}}

This topic lists the various data types available in YugabyteDB's [Cassandra-compatible YCQL API](../../../api/ycql/).

## JSONB

There are a number of different serialization formats for JSON data, one of the popular formats being JSONB (JSON Better) to efficiently model document data.

The YCQL API supports the [JSONB data type](../../../api/ycql/type_jsonb/) to parse, store, and query JSON documents natively. This data type is similar in query language syntax and functionality to the one supported by PostgreSQL. JSONB serialization allows for easy search and retrieval of attributes inside the document. This is achieved by storing all the JSON attributes in a sorted order, which allows for efficient binary search of keys. Similarly, arrays are stored such that random access for a particular array index into the serialized JSON document is possible. In addition, [DocDB](../../../architecture/docdb/persistence/), YugabyteDB's underlying storage engine, is document-oriented, which makes storing JSON data simpler than would otherwise be possible.

Consider the example of an ecommerce application for an online bookstore. The database for such a bookstore needs to store details of various books, some of which may have custom attributes. The following example shows a JSON document that captures the details of a particular book, Macbeth by William Shakespeare.

```json
{
   "name":"Macbeth",
   "author":{
      "first_name":"William",
      "last_name":"Shakespeare"
   },
   "year":1623,
   "editors":[
      "John",
      "Elizabeth",
      "Jeff"
   ]
}
```

### Create a table

The books table for this bookstore can be modelled as follows. Assume that the ID of each book is an int, but this could be a string or a UUID.

```cql
ycqlsh> CREATE KEYSPACE store;
```

```cql
ycqlsh> CREATE TABLE store.books ( id int PRIMARY KEY, details jsonb );
```

### Insert data

Next, insert some sample data for a few books into this store as follows:

```cql
INSERT INTO store.books (id, details) VALUES (1,
  '{ "name"   : "Macbeth",
     "author" : {"first_name": "William", "last_name": "Shakespeare"},
     "year"   : 1623,
     "editors": ["John", "Elizabeth", "Jeff"] }'
);
INSERT INTO store.books (id, details) VALUES (2,
  '{ "name"   : "Hamlet",
     "author" : {"first_name": "William", "last_name": "Shakespeare"},
     "year"   : 1603,
     "editors": ["Lysa", "Mark", "Robert"] }'
);
INSERT INTO store.books (id, details) VALUES (3,
  '{ "name"   : "Oliver Twist",
     "author" : {"first_name": "Charles", "last_name": "Dickens"},
     "year"   : 1838,
     "genre"  : "novel",
     "editors": ["Mark", "Tony", "Britney"] }'
);
INSERT INTO store.books (id, details) VALUES (4,
  '{ "name"   : "Great Expectations",
     "author" : {"first_name": "Charles", "last_name": "Dickens"},
     "year"   : 1950,
     "genre"  : "novel",
     "editors": ["Robert", "John", "Melisa"] }'
);
INSERT INTO store.books (id, details) VALUES (5,
  '{ "name"   : "A Brief History of Time",
     "author" : {"first_name": "Stephen", "last_name": "Hawking"},
     "year"   : 1988,
     "genre"  : "science",
     "editors": ["Melisa", "Mark", "John"] }'
);
```

Note the following about the preceding book details:

- The year attribute for each of the books is interpreted as an integer.
- The first two books do not have a genre attribute, which the others do.
- The author attribute is a map.
- The editors attribute is an array.

### Retrieve a subset of attributes

Running the following `SELECT` query returns all attributes of each book:

```cql
ycqlsh> SELECT * FROM store.books;

```

The following query retrieves just the ID and name for all the books:

```cql
ycqlsh> SELECT id, details->>'name' as book_title FROM store.books;
```

```output
 id | book_title
----+-------------------------
  5 | A Brief History of Time
  1 |                 Macbeth
  4 |      Great Expectations
  2 |                  Hamlet
  3 |            Oliver Twist
```

### Query by attribute values - string

The name attribute is a string in the book details JSON document. Run the following to query the details of the book named *Hamlet*.

```cql
ycqlsh> SELECT * FROM store.books WHERE details->>'name'='Hamlet';
```

```output
 id | details
----+---------------------------------------------------------------
  2 | {"author":{"first_name":"William","last_name":"Shakespeare"},
       "editors":["Lysa","Mark","Robert"],
       "name":"Hamlet","year":1603}
```

Note that you can query by attributes that exist only in some of the documents. For example, you can query for all books that have a genre of novel. Recall that not all books have a genre attribute defined.

```cql
ycqlsh> SELECT id, details->>'name' as title,
              details->>'genre' as genre
         FROM store.books
         WHERE details->>'genre'='novel';
```

```output
 id | title              | genre
----+--------------------+-------
  4 | Great Expectations | novel
  3 |       Oliver Twist | novel
```

### Query by attribute values - integer

The year attribute is an integer in the book details JSON document. Run the following to query the ID and name of books written after 1900:

```cql
ycqlsh> SELECT id, details->>'name' as title, details->>'year'
         FROM store.books
         WHERE CAST(details->>'year' AS integer) > 1900;
```

```output
 id | title                   | expr
----+-------------------------+------
  5 | A Brief History of Time | 1988
  4 |      Great Expectations | 1950
```

### Query by attribute values - map

The author attribute is a map, which in turn consists of the attributes `first_name` and `last_name`. Fetch the IDs and titles of all books written by William Shakespeare as follows:

```cql
ycqlsh> SELECT id, details->>'name' as title,
              details->>'author' as author
         FROM store.books
         WHERE details->'author'->>'first_name' = 'William' AND
               details->'author'->>'last_name' = 'Shakespeare';
```

```output
 id | title   | author
----+---------+----------------------------------------------------
  1 | Macbeth | {"first_name":"William","last_name":"Shakespeare"}
  2 |  Hamlet | {"first_name":"William","last_name":"Shakespeare"}
```

### Query by attribute Values - array

The editors attribute is an array consisting of the first names of the editors of each of the books. You can query for the book titles where `Mark` is the first entry in the editors list as follows:

```cql
ycqlsh> SELECT id, details->>'name' as title,
              details->>'editors' as editors FROM store.books
         WHERE details->'editors'->>0 = 'Mark';
```

```output
 id | title        | editors
----+--------------+---------------------------
  3 | Oliver Twist | ["Mark","Tony","Britney"]
```
