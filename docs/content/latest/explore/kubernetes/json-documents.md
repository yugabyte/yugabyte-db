## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```

Start a new local cluster - by default, this will create a 3 node universe with a replication factor of 3.

```{.sh .copy .separator-dollar}
$ kubectl apply -f yugabyte-statefulset.yaml
```

Check the Kubernetes dashboard to see the 3 yb-tserver and 3 yb-master pods representing the 3 nodes of the cluster.

```{.sh .copy .separator-dollar}
$ minikube dashboard
```

![Kubernetes Dashboard](/images/ce/kubernetes-dashboard.png)

## 2. Create a table for transactions

Connect to cqlsh on node 1.

```{.sh .copy .separator-dollar}
$ kubectl exec -it yb-tserver-0 /home/yugabyte/bin/cqlsh
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
  (1, '{ "name": "Macbeth", "author": "William Shakespeare", "year": 1623 }');
INSERT INTO store.books (id, details) VALUES 
  (2, '{ "name": "Hamlet", "author": "William Shakespeare", "year": 1603 }');
INSERT INTO store.books (id, details) VALUES 
  (3, '{ "name": "Oliver Twist", "author": "Charles Dickens", "year": 1838, "genre": "novel" }');
INSERT INTO store.books (id, details) VALUES 
  (4, '{ "name": "Great Expectations", "author": "Charles Dickens", "year": 1950, "genre": "novel" }');
INSERT INTO store.books (id, details) VALUES 
  (5, '{ "name": "A Brief History of Time", "author": "Stephen Hawking", "year": 1988, "genre": "science" }');
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
----+---------------------------------------------------------------------------------------------
  5 | {"author":"Stephen Hawking","genre":"science","name":"A Brief History of Time","year":1988}
  1 |                                {"author":"William Shakespear","name":"Macbeth","year":1623}
  4 |        {"author":"Charles Dickens","genre":"novel","name":"Great Expectations","year":1950}
  2 |                                 {"author":"William Shakespear","name":"Hamlet","year":1603}
  3 |              {"author":"Charles Dickens","genre":"novel","name":"Oliver Twist","year":1838}

(5 rows)
```


### Query a particular book by id

If we want the details of a book whose id is known (in this example query, we are querying the details of the book with `id` 5).

```{.sql .copy .separator-gt}
cqlsh> SELECT * FROM store.books WHERE id=5;
```
```sql
 id | details
----+---------------------------------------------------------------------------------------------
  5 | {"author":"Stephen Hawking","genre":"science","name":"A Brief History of Time","year":1988}

(1 rows)
```



## 5. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ kubectl delete -f yugabyte-statefulset.yaml
```
