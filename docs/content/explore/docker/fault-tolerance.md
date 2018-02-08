## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.

```sh
$ ./yb-docker-ctl destroy
```

Start a new local universe with replication factor 5.

```sh
$ ./yb-docker-ctl create --rf 5 
```

Connect to cqlsh on node 1.

```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/cqlsh
```
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a Cassandra keyspace and a table.

```sql
cqlsh> CREATE KEYSPACE users;
```
```sql
cqlsh> CREATE TABLE users.profile (id bigint PRIMARY KEY,
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


## 3. Read data through another node

Let us now query the data from node 5.

```sh
docker exec -it yb-tserver-n5 /home/yugabyte/bin/cqlsh
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
./yb-docker-ctl status
```

Let us simulate node 5 failure by doing the following.

```sh
./yb-docker-ctl remove_node 5
```

Now running the status command should show only 4 nodes:

```sh
./yb-docker-ctl status
```

Now connect to node 4.

```sh
docker exec -it yb-tserver-n4 /home/yugabyte/bin/cqlsh
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
./yb-docker-ctl remove_node 1
```

We can check the status to verify:

```sh
./yb-docker-ctl status
```

Now let us connect to node 2.

```sh
docker exec -it yb-tserver-n2 /home/yugabyte/bin/cqlsh
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
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
        superman@yugabyte.com |                    {'firstname': 'Clark', 'lastname': 'Kent'}
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}
   austin.powers@yugabyte.com |                 {'firstname': 'Austin', 'lastname': 'Powers'}

(4 rows)
```


## Step 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./yb-docker-ctl destroy
```
