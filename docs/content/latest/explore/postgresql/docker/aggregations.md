## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. 

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl create --enable_postgres
```

## 2. Run psql to connect to the service

```{.sh .copy .separator-dollar}
$ docker exec -it yb-postgres-n1 /home/yugabyte/postgres/bin/psql -p 5433 -U postgres
```
```sh
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

## 3. Create tables and insert data

```{.sql .copy .separator-gt}
postgres=> CREATE TABLE multi_types(h bigint, r float, vi int, vs text, PRIMARY KEY (h, r));
```

```{.sql .copy .separator-gt}
postgres=> INSERT INTO multi_types(h, r, vi, vs) VALUES (1, 1.5, 3, 'YugaByte');
INSERT INTO multi_types(h, r, vi, vs) VALUES (2, 3.5, 4, 'YugaByte-1.1');
```

## 4. Query data with aggregates

```{.sql .copy .separator-gt}
postgres=> SELECT * FROM multi_types;
```
```sh
h |  r  | vi |      vs      
---+-----+----+--------------
 2 | 3.5 |  4 | YugaByte-1.1
 1 | 1.5 |  3 | YugaByte
(2 rows)
```


```{.sql .copy .separator-gt}
postgres=> SELECT avg(vi), sum(h + r) FROM multi_types;
```
```sh
avg         | sum 
--------------------+-----
 3.5000000000000000 |   8
(1 row)
```

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ ./yb-docker-ctl destroy
```
