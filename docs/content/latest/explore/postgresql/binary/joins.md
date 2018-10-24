## 1. Setup - create universe

If you have a previously running local universe, destroy it using the following.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```

Start a new local cluster - by default, this will create a 3-node universe with a replication factor of 3. 

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl create --enable_postgres
```

## 2. Run psql to connect to the service

```{.sh .copy .separator-dollar}
$ ./bin/psql -p 5433 -U postgres
```
```sh
psql (10.3, server 10.4)
Type "help" for help.

postgres=#
```

## 3. Create tables and insert data

```{.sql .copy .separator-gt}
postgres=> CREATE TABLE t1(h bigint, r float, v text, PRIMARY KEY (h, r));
```
```{.sql .copy .separator-gt}
postgres=> CREATE TABLE t2(h bigint, r float, v text, PRIMARY KEY (h, r));
```

```{.sql .copy .separator-gt}
postgres=> INSERT INTO t1(h, r, v) VALUES (1, 2.5, 'abc');
INSERT INTO t1(h, r, v) VALUES (1, 3.5, 'def');
INSERT INTO t1(h, r, v) VALUES (1, 4.5, 'xyz');
INSERT INTO t2(h, r, v) VALUES (1, 2.5, 'foo');
INSERT INTO t2(h, r, v) VALUES (1, 4.5, 'bar');
```

## 4. Query data with JOINs

```{.sql .copy .separator-gt}
postgres=> SELECT a.h, a.r, a.v as av, b.v as bv
               FROM t1 a LEFT JOIN t2 b
               ON (a.h = b.h and a.r = b.r)
               WHERE a.h = 1 AND a.r IN (2.5, 3.5);
```
```sh
 h |  r  | av  | bv  
---+-----+-----+-----
 1 | 2.5 | abc | foo
 1 | 3.5 | def | 
(2 rows)
```

## 6. Clean up (optional)

Optionally, you can shutdown the local cluster created in Step 1.

```{.sh .copy .separator-dollar}
$ ./bin/yb-ctl destroy
```
