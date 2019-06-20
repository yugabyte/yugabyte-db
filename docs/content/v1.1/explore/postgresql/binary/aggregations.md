## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
$ ./bin/yb-ctl destroy
```

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. 

```sh
$ ./bin/yb-ctl create --enable_postgres
```

## 2. Run psql to connect to the service

```sh
$ ./bin/psql -h 127.0.0.1 -p 5433 -U postgres
```

```
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

## 3. Create tables and insert data

```sql
postgres=> CREATE TABLE multi_types(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r));
```

```sql
postgres=> INSERT INTO multi_types(h, r, vi, vs) VALUES (1, 1.5, 3, 'YugaByte');
INSERT INTO multi_types(h, r, vi, vs) VALUES (2, 3.5, 4, 'YugaByte-1.1');
```

## 4. Query data with aggregates

```sql
postgres=> SELECT * FROM multi_types;
```

```
h |  r  | vi |      vs      
---+-----+----+--------------
 2 | 3.5 |  4 | YugaByte-1.1
 1 | 1.5 |  3 | YugaByte
(2 rows)
```
Run another query.

```sql
postgres=> SELECT avg(vi), sum(h + r) FROM multi_types;
```

```
avg         | sum 
--------------------+-----
 3.5000000000000000 |   8
(1 row)
```

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./bin/yb-ctl destroy
```
