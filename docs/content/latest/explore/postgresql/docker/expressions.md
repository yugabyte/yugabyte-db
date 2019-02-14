## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```sh
$ ./yb-docker-ctl destroy
```

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. 

```sh
$ ./yb-docker-ctl create --enable_postgres
```

## 2. Run psql to connect to the service

You can do this as shown below.

```sh
$ docker exec -it yb-postgres-n1 /home/yugabyte/postgres/bin/psql -p 5433 -U postgres
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

Regular inserts without expressions.

```sql
postgres=> INSERT INTO multi_types(h, r, vi, vs) VALUES (1, 1.5, 3, 'YugaByte');
INSERT INTO multi_types(h, r, vi, vs) VALUES (2, 3.5, 4, 'YugaByte-1.1');
```

Insert with expression.

```sql
postgres=> INSERT INTO multi_types(h, r, vi, vs) VALUES (floor(2 + 1.5), log(3, 27), ceil(pi()), 'ab' || 'c');
```

## 4. Query data with expressions

Query without expressions.

```sql
postgres=> SELECT * FROM multi_types WHERE h = 3;
```

```
 h | r | vi | vs  
---+---+----+-----
 3 | 3 |  4 | abc
(1 row)
```

Query with expressions as SELECT targets.

```sql
postgres=> SELECT h + 1.5, pow(r, 2), vi * h, 7 FROM multi_types WHERE h = 3;
```

```
?column? | pow | ?column? | ?column? 
----------+-----+----------+----------
      4.5 |   9 |       12 |        7
(1 row)
```

Query with expressions in SELECT WHERE clause.

```sql
postgres=> SELECT * FROM multi_types WHERE h + r >= 6 AND substring(vs from 2) = 'bc';
```

```
 h | r | vi | vs
---+---+----+-----
 3 | 3 |  4 | abc
(1 row)
```

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```sh
$ ./yb-docker-ctl destroy
```
