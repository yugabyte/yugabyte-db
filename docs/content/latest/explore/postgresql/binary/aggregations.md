## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl destroy
```
</div>

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. 
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl create --enable_postgres
```
</div>

## 2. Run psql to connect to the service

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ ./bin/psql -p 5433 -U postgres
```
</div>
```sh
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

## 3. Create tables and insert data

You can do this as shown below.
<div class='copy separator-gt'>
```sql
postgres=> CREATE TABLE multi_types(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r));
```
</div>
<div class='copy separator-gt'>
```sql
postgres=> INSERT INTO multi_types(h, r, vi, vs) VALUES (1, 1.5, 3, 'YugaByte');
INSERT INTO multi_types(h, r, vi, vs) VALUES (2, 3.5, 4, 'YugaByte-1.1');
```
</div>

## 4. Query data with aggregates

You can do this as shown below.
<div class='copy separator-gt'>
```sql
postgres=> SELECT * FROM multi_types;
```
</div>
```sh
h |  r  | vi |      vs      
---+-----+----+--------------
 2 | 3.5 |  4 | YugaByte-1.1
 1 | 1.5 |  3 | YugaByte
(2 rows)
```
<div class='copy separator-gt'>
```sql
postgres=> SELECT avg(vi), sum(h + r) FROM multi_types;
```
</div>
```sh
avg         | sum 
--------------------+-----
 3.5000000000000000 |   8
(1 row)
```

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.
<div class='copy separator-dollar'>
```sh
$ ./bin/yb-ctl destroy
```
</div>
