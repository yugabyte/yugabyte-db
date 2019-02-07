## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.
<div class='copy separator-dollar'>
```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```
</div>

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.
<div class='copy separator-dollar'>
```sh
$ kubectl apply -f yugabyte-statefulset.yaml
```
</div>

Check the Kubernetes dashboard to see the 3 yb-tserver and 3 yb-master pods representing the 3 nodes of the cluster.
<div class='copy separator-dollar'>
```sh
$ minikube dashboard
```
</div>

![Kubernetes Dashboard](/images/ce/kubernetes-dashboard.png)

## 2. Create a table with a JSON column

Connect to cqlsh on node 1.
<div class='copy separator-dollar'>
```sh
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/cqlsh
```
</div>
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a keyspace for the online bookstore.
<div class='copy separator-gt'>
```sql
cqlsh> CREATE KEYSPACE store;
```
</div>

Create a table with the book `id` as the primary key and a `jsonb` column to store the book `details`.
<div class='copy separator-gt'>
```sql
cqlsh> CREATE TABLE store.books ( id int PRIMARY KEY, details jsonb );
```
</div>

You can verify the schema of the table by describing it. 
<div class='copy separator-gt'>
```sql
cqlsh> DESCRIBE TABLE store.books;
```
</div>
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
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books;
```
</div>
```sql
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}
  1 |                            {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  4 |      {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
  2 |                                {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
  3 |             {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}
```


### Query a particular book by id

If we want the details of a book whose id is known (in this example query, we are querying the details of the book with `id` 5).
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE id=5;
```
</div>
```sql
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}
```

### Query based on json attributes

We currently support two operators for json. The `->` operator returns a result which is a json
document and further json operations can be applied to the result. The `->>` operator returns a
string result and as a result no further json operations can be performed after this.

If we want to query all the books whose author is `William Shakespeare`
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books where details->'author'->>'first_name' = 'William' AND details->'author'->>'last_name' = 'Shakespeare';
```
</div>
```
 id | details
----+----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  2 |     {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
```

If we want to retrieve the author for all entries:
<div class='copy separator-gt'>
```sql
cqlsh> SELECT id, details->>'author' as author from store.books;
```
</div>
```
 id | author
----+----------------------------------------------------
  5 |     {"first_name":"Stephen","last_name":"Hawking"}
  1 | {"first_name":"William","last_name":"Shakespeare"}
  4 |     {"first_name":"Charles","last_name":"Dickens"}
  2 | {"first_name":"William","last_name":"Shakespeare"}
  3 |     {"first_name":"Charles","last_name":"Dickens"}
```

### Query based on array elements

You can do this as shown below.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE details->'editors'->>0 = 'Mark';
```
</div>
```
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  3 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE details->'editors'->>2 = 'Jeff';
```
</div>
```
 id | details
----+----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
```

### Working with integers within json documents

The operators `->` and `->>` introduced in the previous sections return a jsonb and string type
respectively, but sometimes its useful to consider json attributes as numeric types so that we can
apply the appropriate logical/arithmetic operators.

For this purpose, we can use the `CAST` function to handle integers within the json document:
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) > 1700;
```
</div>
```
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------------------
  5 | {"author":{"first_name":"Stephen","last_name":"Hawking"},"editors":["Melisa","Mark","John"],"genre":"science","name":"A Brief History of Time","year":1988}
  4 |      {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
  3 |             {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) = 1950;
```
</div>
```
 id | details
----+--------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE CAST(details->>'year' AS integer) > 1600 AND CAST(details->>'year' AS integer) <= 1900;
```
</div>

```
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  1 |                {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["John","Elizabeth","Jeff"],"name":"Macbeth","year":1623}
  2 |                    {"author":{"first_name":"William","last_name":"Shakespeare"},"editors":["Lysa","Mark","Robert"],"name":"Hamlet","year":1603}
  3 | {"author":{"first_name":"Charles","last_name":"Dickens"},"editors":["Mark","Tony","Britney"],"genre":"novel","name":"Oliver Twist","year":1838}
```

### Updating json documents

- Update entire JSONB document

You can do this as shown below.
<div class='copy separator-gt'>
```sql
cqlsh> UPDATE store.books SET details = '{"author":{"first_name":"Carl","last_name":"Sagan"},"editors":["Ann","Rob","Neil"],"genre":"science","name":"Cosmos","year":1980}' WHERE id = 1;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE id = 1;
```
</div>
```
 id | details
----+-----------------------------------------------------------------------------------------------------------------------------------
  1 | {"author":{"first_name":"Carl","last_name":"Sagan"},"editors":["Ann","Rob","Neil"],"genre":"science","name":"Cosmos","year":1980}
```

- Update a JSONB object value.

You can do this as shown below.
<div class='copy separator-gt'>
```sql
cqlsh> UPDATE store.books SET details->'author'->>'first_name' = '"Steve"' WHERE id = 4;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE id = 4;
```
</div>
```
 id | details
----+------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Steve","last_name":"Dickens"},"editors":["Robert","John","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```

- Update a JSONB array element.

You can do this as shown below.
<div class='copy separator-gt'>
```sql
cqlsh> UPDATE store.books SET details->'editors'->>1 = '"Jack"' WHERE id = 4;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE id = 4;
```
</div>
```
 id | details
----+------------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"Steve","last_name":"Dickens"},"editors":["Robert","Jack","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```

- Update a JSONB subdocument.

You can do this as shown below.
<div class='copy separator-gt'>
```sql
cqlsh> UPDATE store.books SET details->'author' = '{"first_name":"John", "last_name":"Doe"}' WHERE id = 4;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> SELECT * FROM store.books WHERE id = 4;
```
</div>
```
 id | details
----+-------------------------------------------------------------------------------------------------------------------------------------------------
  4 | {"author":{"first_name":"John","last_name":"Doe"},"editors":["Robert","Jack","Melisa"],"genre":"novel","name":"Great Expectations","year":1950}
```
## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.
<div class='copy separator-dollar'>
```sh
$ kubectl delete -f yugabyte-statefulset.yaml
```
</div>
