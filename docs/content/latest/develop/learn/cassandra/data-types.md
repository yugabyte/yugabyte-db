This page lists the various data types available in YugaByte DB’s [Cassandra compatible YCQL API](../../../api/ycql).

## JSONB

There are a number of different serialization formats for JSON data, one of the popular formats being JSONB to efficiently model document data. And just in case you were wondering, JSONB stands for JSON Better.

The YCQL API supports the [JSONB data type](../../../api/ycql/type_jsonb/) to parse, store and query JSON documents natively. This data type is similar in query language syntax and functionality to the one supported by PostgreSQL. JSONB serialization allows for easy search and retrieval of attributes inside the document. This is achieved by storing all the JSON attributes in a sorted order, which allows for efficient binary search of keys. Similarly arrays are stored such that random access for a particular array index into the serialized json document is possible. [DocDB](../../../architecture/concepts/persistence/), YugaByte DB’s underlying storage engine, is document-oriented in itself which makes storing the data of the JSON data type lot more simple than otherwise possible.

Let us take the example of an ecommerce app of an online bookstore. The database for such a bookstore needs to store details of various books, some of which may have custom attributes. Below is an example of a JSON document that captures the details of a particular book, Macbeth written by William Shakespeare.

```sh
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

### Create a Table

The books table for this bookstore can be modelled as shown below. We assume that the id of each book is an int, but this could be a string or a uuid.

```{.sh .copy .separator-gt}
cqlsh> CREATE KEYSPACE store;
```
```{.sh .copy .separator-gt}
cqlsh> CREATE TABLE store.books ( id int PRIMARY KEY, details jsonb );
```

### Insert Data
Next we insert some sample data for a few books into this store. You can copy and paste the following commands into the cqlsh shell for YugaByte DB to insert the data. Note that you would need a cqlsh that has the enhancement to work with YugaByte DB JSON documents, you can download it using the documentation here.

```{.sh .copy}
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

Note the following interesting points about the book details above:
- The year attribute for each of the books is interpreted as an integer.
- The first two books do not have a genre attribute, which the others do.
- The author attribute is a map.
- The editors attribute is an array.


### Retrieve a Subset of Attributes

Running the following default select query will return all attributes of each book.

```{.sh .copy .separator-gt}
cqlsh> SELECT * FROM store.books;

```
But a number of times we may want to query just a subset of attributes from YugaByte DB database. Below is an example of a query that retrieves just the id and name for all the books.

```{.sh .copy .separator-gt}

cqlsh> SELECT id, details->>'name' as book_title FROM store.books;
```
```sh

 id | book_title
----+-------------------------
  5 | A Brief History of Time
  1 |                 Macbeth
  4 |      Great Expectations
  2 |                  Hamlet
  3 |            Oliver Twist
```

### Query by Attribute Values - String

The name attribute is a string in the book details JSON document. Let us query all the details of book named Hamlet.

```{.sh .copy .separator-gt}
cqlsh> SELECT * FROM store.books WHERE details->>'name'='Hamlet';
```
```sh
 id | details
----+---------------------------------------------------------------
  2 | {"author":{"first_name":"William","last_name":"Shakespeare"},
       "editors":["Lysa","Mark","Robert"],
       "name":"Hamlet","year":1603}
```

Note that we can query by attributes that exist only in some of the documents. For example, we can query for all books that have a genre of novel. Recall from before that all books do not have a genre attribute defined.

```{.sh .copy .separator-gt}
cqlsh> SELECT id, details->>'name' as title,
              details->>'genre' as genre
         FROM store.books
         WHERE details->>'genre'='novel';
```
```sh

 id | title              | genre
----+--------------------+-------
  4 | Great Expectations | novel
  3 |       Oliver Twist | novel
```

### Query by Attribute Values - Integer

The year attribute is an integer in the book details JSON document. Let us query the id and name of books written after 1900.

```{.sh .copy .separator-gt}
cqlsh> SELECT id, details->>'name' as title, details->>'year'
         FROM store.books
         WHERE CAST(details->>'year' AS integer) > 1900;
```
```sh
 id | title                   | expr
----+-------------------------+------
  5 | A Brief History of Time | 1988
  4 |      Great Expectations | 1950
```

### Query by Attribute Values - Map

The author attribute is a map, which in turn consists of the attributes first_name and last_name. Let us fetch the ids and titles of all books written by the author William Shakespeare.

```{.sh .copy .separator-gt}
cqlsh> SELECT id, details->>'name' as title,
              details->>'author' as author
         FROM store.books
         WHERE details->'author'->>'first_name' = 'William' AND
               details->'author'->>'last_name' = 'Shakespeare';
```
```sh

 id | title   | author
----+---------+----------------------------------------------------
  1 | Macbeth | {"first_name":"William","last_name":"Shakespeare"}
  2 |  Hamlet | {"first_name":"William","last_name":"Shakespeare"}
```


### Query by Attribute Values - Array

The editors attribute is an array consisting of the first names of the editors of each of the books. We can query for the book titles where Mark is the first entry in the editors list as follows.

```{.sh .copy .separator-gt}
cqlsh> SELECT id, details->>'name' as title,
              details->>'editors' as editors FROM store.books
         WHERE details->'editors'->>0 = 'Mark';
```
```sh
 id | title        | editors
----+--------------+---------------------------
  3 | Oliver Twist | ["Mark","Tony","Britney"]
```