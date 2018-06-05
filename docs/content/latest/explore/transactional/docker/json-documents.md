## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create
```


## 2. Create a table with a JSON column

Connect to cqlsh on node 1.

```{.sh .copy .separator-dollar}
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/cqlsh
```
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a keyspace for the online bookstore.

```{.sql .copy .separator-gt}
cqlsh> CREATE KEYSPACE store;
```

Create a table with the book `id` as the primary key and a `jsonb` column to store the book `details`.

```{.sql .copy .separator-gt}
cqlsh> CREATE TABLE store.books ( id int PRIMARY KEY, details jsonb );
```

You can verify the schema of the table by describing it. 

```{.sql .copy .separator-gt}
cqlsh> DESCRIBE TABLE store.books;
```
```sql
CREATE TABLE store.books (
    id int PRIMARY KEY,
    details jsonb
) WITH default_time_to_live = 0;
```

## 3. Insert sample data

Let us seed the `books` table with some sample data. Note the following about the sample data:

- all book details have the book's `name`, its `author` and the publication `year`
- some books have a `genre` attribute specified, while others do not

Paste the following into the `cqlsh` shell.

```{.sql .copy}
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

## 4. Execute queries against the documents

Let us execute some queries against the `books` table.
To view .

### Query all the data

We can view all the data inserted by running the following query.


```{.sql .copy .separator-gt}
cqlsh> SELECT * FROM store.books;
```
```sql
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}
  1 |                            {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  4 |      {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
  2 |                                {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
  3 |             {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}

(5 rows)
```


### Query a particular book by id

If we want the details of a book whose id is known (in this example query, we are querying the details of the book with `id` 5).

```{.sql .copy .separator-gt}
cqlsh> SELECT * FROM store.books WHERE id=5;
```
```sql
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}

(1 rows)
```

### Query based on json attributes

We currently support two operators for json. The `->` operator returns a result which is a json
document and further json operations can be applied to the result. The `->>` operator returns a
string result and as a result no further json operations can be performed after this.

If we want to query all the books whose author is `William Shakespeare`

```sql
cqlsh> SELECT * FROM store.books where details->'author'->>'first_name' = 'William' AND details->'author'->>'last_name' = 'Shakespeare';

 id | details
----+----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  2 |     {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}

(2 rows)
```

If we want to retrieve the author for all entries:

```sql
cqlsh> SELECT id, details->>'author' as author from store.books;

 id | author
----+----------------------------------------------------
  5 |     {"first_name":"Stephen","last_name":"Hawking"}
  1 | {"first_name":"William","last_name":"Shakespeare"}
  4 |     {"first_name":"Charles","last_name":"Dickens"}
  2 | {"first_name":"William","last_name":"Shakespeare"}
  3 |     {"first_name":"Charles","last_name":"Dickens"}

(5 rows)
```

### Query based on array elements

```sql
cqlsh> SELECT * FROM store.books WHERE details->'editors'->>0 = 'Mark';

 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  3 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}

(1 rows)

cqlsh> SELECT * FROM store.books WHERE details->'editors'->>2 = 'Jeff';

 id | details
----+----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}

(1 rows)
```

### Working with integers within json documents

The operators `->` and `->>` introduced in the previous sections return a jsonb and string type
respectively, but sometimes its useful to consider json attributes as numeric types so that we can
apply the appropriate logical/arithmetic operators.

For this purpose, we can use the `CAST` function to handle integers within the json document:

```sql
cqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) > 1700;

 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}
  4 |      {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
  3 |             {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}

(3 rows)

cqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) = 1950;

 id | details
----+--------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}

(1 rows)


cqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) > 1600 AND CAST(details->>'year' AS integer) <= 1900;

 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  1 |                {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  2 |                    {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
  3 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}

(3 rows)
```

## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```
