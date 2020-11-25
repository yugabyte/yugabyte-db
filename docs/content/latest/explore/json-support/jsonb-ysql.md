---
title: JSON Support
headerTitle: JSON Support
linkTitle: JSON Support
description: JSON Support in YugabyteDB.
headcontent: JSON Support in YugabyteDB.
image: <div class="icon"><i class="fas fa-file-invoice"></i></div>
menu:
  latest:
    name: JSON Support
    identifier: explore-json-support-1-ysql
    parent: explore
    weight: 234
isTocNested: true
showAsideToc: true
---

JSON data types are for storing JSON (JavaScript Object Notation) data, as specified in [RFC 7159](https://tools.ietf.org/html/rfc7159). Such data can also be stored as `text`, but the JSON data types have the advantage of enforcing that each stored value is valid according to the JSON rules. There are also assorted JSON-specific functions and operators available for data stored in these data types.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/explore/json-support/jsonb-ysql/" class="nav-link active">
      <i class="icon-postgres" aria-hidden="true"></i>
      YSQL
    </a>
  </li>

  <li >
    <a href="/latest/explore/json-support/jsonb-ycql/" class="nav-link">
      <i class="icon-cassandra" aria-hidden="true"></i>
      YCQL
    </a>
  </li>

</ul>


There are two JSON data types supported in YSQL: `json` and `jsonb`. 

{{< note title="Note" >}}
The JSON functionality in YSQL is nearly identical to the [JSON functionality in PostgreSQL](https://www.postgresql.org/docs/11/datatype-json.html). 
{{< /note >}}

* **The `jsonb` type** does not preserve white space, does not preserve the order of object keys, and does not keep duplicate object keys. If duplicate keys are specified in the input, only the last value is kept.

* **The `json` type** stores an exact copy of the input text, and therefore preserves semantically-insignificant white space between tokens, as well as the order of keys within JSON objects. Also, if a JSON object within the value contains the same key more than once, all the key/value pairs are kept. The processing functions consider the last value as the operative one.


{{< tip title="Tip" >}}
**When to use `jbonb` vs `json`?** In general, most applications should prefer to store JSON data as `jsonb`, unless there are quite specialized needs, such as legacy assumptions about ordering of object keys.

They accept *almost* identical sets of values as input. The major practical difference is one of efficiency:
* The `json` data type stores an exact copy of the input text, which processing functions must reparse on each execution
* THe `jsonb` data is stored in a decomposed binary format that makes it slightly slower to input due to added conversion overhead, but significantly faster to process, since no reparsing is needed. `jsonb` also supports indexing, which can be a significant advantage.
{{< /tip >}}

In this article, we will focus only on the `jsonb` type.


## 1. Prerequisites

You need a YugabyteDB cluster to run through the steps below. If do not have a YugabyteDB cluster, you can create one on your local machine as shown below.

```sh
$ ./bin/yb-ctl create
```

Connect to the cluster using `ysqlsh` to run through the examples below.

```sh
$ ./bin/ysqlsh
```

Next, let us create a simple table `books` which has a primary key and one `jsonb` column `doc` which contains various details about that book.

```sql
create table books(k int primary key, doc jsonb not null);
```

Next, let us insert some rows which contain details about various books. These details are represented as JSON documents, as shown below.

```sql
insert into books(k, doc) values
  (1,
  '{ "ISBN"    : 4582546494267,
     "title"   : "Macbeth", 
     "author"  : {"given_name": "William", "family_name": "Shakespeare"},
     "year"    : 1623}'),

  (2,
  '{ "ISBN"    : 8760835734528,
     "title"   : "Hamlet",
     "author"  : {"given_name": "William", "family_name": "Shakespeare"},
     "year"    : 1603,
     "editors" : ["Lysa", "Elizabeth"] }'),

  (3,
  '{ "ISBN"    : 7658956876542,
     "title"   : "Oliver Twist",
     "author"  : {"given_name": "Charles", "family_name": "Dickens"},
     "year"    : 1838,
     "genre"   : "novel",
     "editors" : ["Mark", "Tony", "Britney"] }'),
  (4,
  '{ "ISBN"    : 9874563896457,
     "title"   : "Great Expectations",
     "author"  : {"family_name": "Dickens"},
     "year"    : 1950,
     "genre"   : "novel",
     "editors" : ["Robert", "John", "Melisa", "Elizabeth"] }'),

  (5,
  '{ "ISBN"    : 8647295405123,
     "title"   : "A Brief History of Time",
     "author"  : {"given_name": "Stephen", "family_name": "Hawking"},
     "year"    : 1988,
     "genre"   : "science",
     "editors" : ["Melisa", "Mark", "John", "Fred", "Jane"] }'),

  (6,
  '{
    "ISBN"     : 6563973589123,
    "year"     : 1989,
    "genre"    : "novel",
    "title"    : "Joy Luck Club",
    "author"   : {"given_name": "Amy", "family_name": "Tan"},
    "editors"  : ["Ruilin", "Aiping"]}');

```

{{< note title="Note" >}}
Some of the rows in the example have some of the keys missing (intentional). But the row with "k=6" has every key. 
{{< /note >}}

## 2. Query JSON documents

Let us list all the rows we inserted using the command below.

```sql
select * from books;
```

You should see the following output.

```
yugabyte=# select * from books;
 k |                                                                                                         doc
---+---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
 5 | {"ISBN": 8647295405123, "year": 1988, "genre": "science", "title": "A Brief History of Time", "author": {"given_name": "Stephen", "family_name": "Hawking"}, "editors": ["Melisa", "Mark", "John", "Fred", "Jane"]}
 1 | {"ISBN": 4582546494267, "year": 1623, "title": "Macbeth", "author": {"given_name": "William", "family_name": "Shakespeare"}}
 6 | {"ISBN": 6563973589123, "year": 1989, "genre": "novel", "title": "Joy Luck Club", "author": {"given_name": "Amy", "family_name": "Tan"}, "editors": ["Ruilin", "Aiping"]}
 4 | {"ISBN": 9874563896457, "year": 1950, "genre": "novel", "title": "Great Expectations", "author": {"family_name": "Dickens"}, "editors": ["Robert", "John", "Melisa", "Elizabeth"]}
 2 | {"ISBN": 8760835734528, "year": 1603, "title": "Hamlet", "author": {"given_name": "William", "family_name": "Shakespeare"}, "editors": ["Lysa", "Elizabeth"]}
 3 | {"ISBN": 7658956876542, "year": 1838, "genre": "novel", "title": "Oliver Twist", "author": {"given_name": "Charles", "family_name": "Dickens"}, "editors": ["Mark", "Tony", "Britney"]}
(6 rows)
```

### Using `->` and `->>`

YSQL has two native operators `->` and `->>` to query JSON documents. The first operator `->` returns a JSON object, while the operator `->>` returns text. These operators work on both `JSON` as well as `JSONB` columns to select a subset of attributes as well as to inspect the JSON document.


The example below shows how to select a few attributes from each document.

```sql
SELECT doc->'title' AS book_title, 
       CONCAT(doc->'author'->'family_name', 
              ', ', doc->'author'->'given_name') AS author
    FROM books;
```
You should see the following output:
```
yugabyte=# SELECT doc->'title' AS book_title,
yugabyte-#        CONCAT(doc->'author'->'family_name',
yugabyte(#               ', ', doc->'author'->'given_name') AS author
yugabyte-#     FROM books;
        book_title         |          author
---------------------------+--------------------------
 "A Brief History of Time" | "Hawking", "Stephen"
 "Macbeth"                 | "Shakespeare", "William"
 "Joy Luck Club"           | "Tan", "Amy"
 "Great Expectations"      | "Dickens",
 "Hamlet"                  | "Shakespeare", "William"
 "Oliver Twist"            | "Dickens", "Charles"
(6 rows)
```

Because the -> operator returns an object, you can chain it to inspect deep into a JSON document, as shown below.

```sql
select '{"title": "Macbeth", "author": {"given_name": "William"}}'::jsonb 
  -> 'author' -> 'given_name' as first_name;
```

This should produce the following output.

```
yugabyte=# select '{"title": "Macbeth", "author": {"given_name": "William"}}'::jsonb
             -> 'author' -> 'given_name' as first_name;
 first_name
------------
 "William"
(1 row)
```

### Existence with `?`

The `?` operator can be used to check if a JSON document contains a certain attribute. For example, let us say we want to find a count of the records where the `doc` column contains a property named *genre*. This can be accomplished by running the following statement.

```sql
SELECT doc->'title' AS book_title, 
       doc->'genre' as genre 
    FROM books WHERE doc ? 'genre';
```

This should produce the following output.

```
yugabyte=# SELECT doc->'title' AS book_title,
yugabyte-#        doc->'genre' as genre
yugabyte-#     FROM books WHERE doc ? 'genre';
        book_title         |   genre
---------------------------+-----------
 "A Brief History of Time" | "science"
 "Joy Luck Club"           | "novel"
 "Great Expectations"      | "novel"
 "Oliver Twist"            | "novel"
(4 rows)
```

### Containment with `@>`

The containment operator `@>` tests whether one document contains another. For example, let us say we want to find all books that contain the document `{"author": {"given_name": "William"}}` in them (or in other words, the author of the book has the first name *William*). This can be done as shown below.

```sql
SELECT doc->'title' AS book_title, 
       CONCAT(doc->'author'->'family_name', 
              ', ', doc->'author'->'given_name') AS author
    FROM books 
    WHERE doc @> '{"author": {"given_name": "William"}}'::jsonb;
```

The output is shown below.

```
yugabyte=# SELECT doc->'title' AS book_title,
yugabyte-#        CONCAT(doc->'author'->'family_name',
yugabyte(#               ', ', doc->'author'->'given_name') AS author
yugabyte-#     FROM books
yugabyte-#     WHERE doc @> '{"author": {"given_name": "William"}}'::jsonb;
 book_title |          author
------------+--------------------------
 "Macbeth"  | "Shakespeare", "William"
 "Hamlet"   | "Shakespeare", "William"
(2 rows)
```


## 3. Update JSON documents

There are a number of ways to update a JSON document, as shown below.

### Add an attribute

Use the `||` operator to either update or insert the attribute into the existing JSON document. For example, if we wanted to add a `stock` attribute to all the books:

```sql
UPDATE books SET doc = doc || '{"stock": "true"}';
```

This would update all documents as shown below:

```
yugabyte=# SELECT doc->'title' AS title, doc->'stock' as stock FROM books;
           title           | stock
---------------------------+--------
 "A Brief History of Time" | "true"
 "Macbeth"                 | "true"
 "Joy Luck Club"           | "true"
 "Great Expectations"      | "true"
 "Hamlet"                  | "true"
 "Oliver Twist"            | "true"
(6 rows)

Time: 1.283 ms
```

### Remove an attribute

To remove an attribute, the following SQL statement can be used:

```sql
UPDATE books SET doc = doc - 'stock';
```

This would remove the field from all the documents, as shown below.

```
yugabyte=# SELECT doc->'title' AS title, doc->'stock' as stock FROM books;
           title           | stock
---------------------------+-------
 "A Brief History of Time" |
 "Macbeth"                 |
 "Joy Luck Club"           |
 "Great Expectations"      |
 "Hamlet"                  |
 "Oliver Twist"            |
(6 rows)
```

### Replace a document

To replace an entire document, run the following SQL statement.

```sql
UPDATE books 
    SET doc = '{"ISBN": 4582546494267, "year": 1623, "title": "Macbeth", "author": {"given_name": "William", "family_name": "Shakespeare"}}' 
    WHERE k=1;
```

## 4. Built-in functions

YSQL supports a large number of operators and built-in functions that operate on JSON documents. This section highlights a few of these built-in functions.

{{< note title="Note" >}}
Most of the built-in functions supported by PostgreSQL are supported in YSQL.

Check the documentation page for a complete [list of JSON functions and operators](../../../api/ysql/datatypes/type_json/functions-operators/).
{{< /note >}}


### Expand JSON - `jsonb_each`

Expands the top-level JSON document into a set of key-value pairs, as shown below.


```sql
SELECT jsonb_each(doc) FROM books WHERE k=1;
```

The output is shown below.

```
yugabyte=# SELECT jsonb_each(doc) FROM books WHERE k=1;
                                 jsonb_each
----------------------------------------------------------------------------
 (ISBN,4582546494267)
 (year,1623)
 (title,"""Macbeth""")
 (author,"{""given_name"": ""William"", ""family_name"": ""Shakespeare""}")
(4 rows)
```

### Retrieve keys - `jsonb_object_keys`

To retrieve the keys of the top-level JSON document, run the statement shown below.

```sql
SELECT jsonb_object_keys(doc) FROM books WHERE k=1;
```

The output is shown below.

```
yugabyte=# SELECT jsonb_object_keys(doc) FROM books WHERE k=1;
 jsonb_object_keys
-------------------
 ISBN
 year
 title
 author
(4 rows)
```

### Format JSON - `jsonb_pretty`

By default, a compact representation of the JSON is returned, which is ideal for programmatic purposes. However, output the JSON documents in a more human-readable format, use this pretty print function:

```sql
SELECT jsonb_pretty(doc) FROM books WHERE k=1;
```

This should produce the following output.

```
yugabyte=# SELECT jsonb_pretty(doc) FROM books WHERE k=1;
             jsonb_pretty
--------------------------------------
 {                                   +
     "ISBN": 4582546494267,          +
     "year": 1623,                   +
     "title": "Macbeth",             +
     "author": {                     +
         "given_name": "William",    +
         "family_name": "Shakespeare"+
     }                               +
 }
(1 row)
```






## 5. Constraints

You can create constraint on `jsonb` data types. Here are a couple of examples.

### Check JSON documents are objects

Here's how to insist that each JSON document is an object:

```sql
alter table books 
add constraint books_doc_is_object
check (jsonb_typeof(doc) = 'object');
```

### Check ISBN is a 13-digit number

Here's how to insist that the ISBN is always defined and is a positive 13-digit number:

```sql
alter table books 
add constraint books_isbn_is_positive_13_digit_number 
check (
  (doc->'ISBN') is not null
    and
  jsonb_typeof(doc->'ISBN') = 'number'
     and
  (doc->>'ISBN')::bigint > 0
    and
  length(((doc->>'ISBN')::bigint)::text) = 13
);
```


## 6. Indexes on JSON attributes

Indexes are essential to perform efficient lookups by document attributes. Without indexes, queries on document attributes end up performing a full table scan and process each JSON document. This section outlines some of the indexes supported.


### Secondary index

Let us say you want to support range queries that reference the value for the *year* attribute. This can be accomplished as shown below:

```sql
CREATE INDEX books_year 
    ON books ((doc->>'year') ASC)
    WHERE doc->>'year' is not null;
```

This would make the following query efficient:

```sql
select
  (doc->>'ISBN')::bigint as year,
  doc->>'title'          as title,
  (doc->>'year')::int    as year
from books
where (doc->>'year')::int > 1850
order by 3;
```

### Partial and expression indexes

In many cases, we would want to index only those documents that contain the attribute (as opposed to indexing the rows that have a `NULL` value for that attribute). This is a common scenario because not all the documents would have all the attributes defined. This can be achieved using a *partial index*.

In the previous section where we created a secondary index, not all the books may have the `year` attribute defined. Let us say we want to only index those documents that have a `year` attribute defined, or in other words, the `year` attribute is not `NULL`. This can be achieved using the following partial index.

```sql
CREATE INDEX books_year 
    ON books ((doc->>'year') ASC)
    WHERE doc->>'year' IS NOT NULL;
```

### Unique index

You can create a unique index on the "ISBN" key for the books table as shown below.

```sql
create unique index books_isbn_unq on books((doc->>'ISBN'));
```

Inserting a row with a duplicate value would fail as shown below, where we insert a book with a new primary key `k` but an existing ISBN number `4582546494267`.

```
yugabyte=# insert into books values 
           (7, '{  "ISBN"    : 4582546494267, 
                   "title"   : "Fake Book with duplicate ISBN" }');
ERROR:  23505: duplicate key value violates unique constraint "books_isbn_unq"
```


## 7. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```




