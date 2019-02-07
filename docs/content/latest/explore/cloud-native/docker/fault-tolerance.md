## 1. Setup - create universe and table

If you have a previously running local universe, destroy it using the following.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl destroy
```
</div>

Start a new local universe with replication factor 5.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl create --rf 5 
```
</div>

Connect to cqlsh on node 1.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n1 /home/yugabyte/bin/cqlsh
```
</div>
```sh
Connected to local cluster at 127.0.0.1:9042.
[cqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
cqlsh>
```

Create a Cassandra keyspace and a table.
<div class='copy separator-gt'>
```sql
cqlsh> CREATE KEYSPACE users;
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> CREATE TABLE users.profile (id bigint PRIMARY KEY,
	                               email text,
	                               password text,
	                               profile frozen<map<text, text>>);
```
</div>


## 2. Insert data through node 1

Now insert some data by typing the following into cqlsh shell we joined above.
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (1000, 'james.bond@yugabyte.com', 'licensed2Kill',
   {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
  );
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (2000, 'sherlock.holmes@yugabyte.com', 'itsElementary',
   {'firstname': 'Sherlock', 'lastname': 'Holmes'}
  );

```
</div>

Query all the rows.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}

(2 rows)
```


## 3. Read data through another node

Let us now query the data from node 5.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n5 /home/yugabyte/bin/cqlsh
```
</div>
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
<div class='copy separator-gt'>
```sql
 email                        | profile
------------------------------+---------------------------------------------------------------
      james.bond@yugabyte.com | {'firstname': 'James', 'lastname': 'Bond', 'nickname': '007'}
 sherlock.holmes@yugabyte.com |               {'firstname': 'Sherlock', 'lastname': 'Holmes'}

(2 rows)
```
</div>

## 4. Verify that one node failure has no impact

We have 5 nodes in this universe. You can verify this by running the following.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl status
```
</div>

Let us simulate node 5 failure by doing the following.
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl remove_node 5
```
</div>

Now running the status command should show only 4 nodes:
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl status
```
</div>

Now connect to node 4.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n4 /home/yugabyte/bin/cqlsh
```
</div>

Let us insert some data.
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES 
  (3000, 'austin.powers@yugabyte.com', 'imGroovy',
   {'firstname': 'Austin', 'lastname': 'Powers'});
```
</div>

Now query the data.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
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
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl remove_node 1
```
</div>

We can check the status to verify:
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl status
```
</div>

Now let us connect to node 2.
<div class='copy separator-dollar'>
```sh
$ docker exec -it yb-tserver-n2 /home/yugabyte/bin/cqlsh
```
</div>

Insert some data.
<div class='copy separator-gt'>
```sql
cqlsh> INSERT INTO users.profile (id, email, password, profile) VALUES
  (4000, 'superman@yugabyte.com', 'iCanFly',
   {'firstname': 'Clark', 'lastname': 'Kent'});
```
</div>

Run the query.
<div class='copy separator-gt'>
```sql
cqlsh> SELECT email, profile FROM users.profile;
```
</div>
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
<div class='copy separator-dollar'>
```sh
$ ./yb-docker-ctl destroy
```
</div>
