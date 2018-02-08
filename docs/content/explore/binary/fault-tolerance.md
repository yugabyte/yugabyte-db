## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.

```sh
$ ./bin/yb-ctl destroy
```

Start a new local universe with replication factor 5.

```sh
$ ./bin/yb-ctl --replication_factor 5 create
```

Connect to cqlsh on node 1.

```sh
$ ./bin/cqlsh 127.0.0.1
```
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a CQL keyspace and a table.

```sql
$ cqlsh> CREATE KEYSPACE users;
```
```sql
$ cqlsh> CREATE TABLE users.profile (id bigint PRIMARY KEY,
	                               email text,
	                               password text,
	                               profile frozen<map<text, text>>);
```


## 2. Insert data through node 1

Now insert some data by typing the following into cqlsh shell we joined above.

```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (1000, 'james.bond@yugabyte.com', 'licensed2Kill',
   {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
  );
```
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (2000, 'sherlock.holmes@yugabyte.com', 'itsElementary',
   {'firstname': 'Sherlock', 'lastname': 'Holmes'}
  );
```

Query all the rows.

```sql
cqlsh> SELECT email, profile FROM users.profile;
```
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}

(2 rows)
```


## Step 3. Read data through another node

Let us now query the data from node 5.

```sh
./bin/cqlsh 127.0.0.5
```
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}

(2 rows)
```

## 4. Verify that one node failure has no impact

We have 5 nodes in this universe. You can verify this by running the following.

```sh
$ ./bin/yb-ctl status
```
```sh
...
2017-11-19 23:20:35,029 INFO: Server is running: type=tserver, node_id=1, ...
2017-11-19 23:20:35,061 INFO: Server is running: type=tserver, node_id=2, ...
2017-11-19 23:20:35,094 INFO: Server is running: type=tserver, node_id=3, ...
2017-11-19 23:20:35,128 INFO: Server is running: type=tserver, node_id=4, ...
2017-11-19 23:20:35,155 INFO: Server is running: type=tserver, node_id=5, ...
```

Let us simulate a node failure by removing node 5.

```sh
./bin/yb-ctl remove_node 5
```

Now running the status command should show only 4 nodes:

```sh
$ ./bin/yb-ctl status
```
```sh
...
2017-11-19 23:20:35,029 INFO: Server is running: type=tserver, node_id=1, ...
2017-11-19 23:20:35,061 INFO: Server is running: type=tserver, node_id=2, ...
2017-11-19 23:20:35,094 INFO: Server is running: type=tserver, node_id=3, ...
2017-11-19 23:20:35,128 INFO: Server is running: type=tserver, node_id=4, ...
2017-11-19 23:22:12,997 INFO: Server type=tserver node_id=5 is not running
```

Now connect to node 4.

```sh
./bin/cqlsh 127.0.0.4
```

Let us insert some data.

```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES 
  (3000, 'austin.powers@yugabyte.com', 'imGroovy',
   {'firstname': 'Austin', 'lastname': 'Powers'});
```

Now query the data.

```sql
cqlsh> SELECT email, profile FROM users.profile;
```
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}
   austin.powers@yugabyte.com |                 {'firstname': 'Austin', 'lastname': 'Powers'}

(3 rows)
```


## 5. Verify that second node failure has no impact

This cluster was created with replication factor 5 and hence needs only 3 replicas to make consensus. Therefore, it is resilient to 2 failures without any data loss. Let us simulate another node failure.

```sh
./bin/yb-ctl remove_node 1
```

We can check the status to verify:

```sh
$ ./bin/yb-ctl status
```
```sh
...
2017-11-19 23:31:02,183 INFO: Server type=tserver node_id=1 is not running
2017-11-19 23:31:02,217 INFO: Server is running: type=tserver, node_id=2, ...
2017-11-19 23:31:02,245 INFO: Server is running: type=tserver, node_id=3, ...
2017-11-19 23:31:02,278 INFO: Server is running: type=tserver, node_id=4, ...
2017-11-19 23:31:02,308 INFO: Server type=tserver node_id=5 is not running
```

Now let us connect to node 2.

```sh
./bin/cqlsh 127.0.0.2
```

Insert some data.

```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (4000, 'superman@yugabyte.com', 'iCanFly',
   {'firstname': 'Clark', 'lastname': 'Kent'});
```

Run the query.

```sql
cqlsh> SELECT email, profile FROM users.profile;
```
```sh
 email                        | profile
------------------------------+---------------------------------------------------------------
        superman@yugabyte.com |                    {'firstname': 'Clark', 'lastname': 'Kent'}
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}
   austin.powers@yugabyte.com |                 {'firstname': 'Austin', 'lastname': 'Powers'}

(4 rows)
```


## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```
