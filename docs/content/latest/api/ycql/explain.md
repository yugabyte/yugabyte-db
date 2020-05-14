---
title: EXPLAIN statement [YCQL]
headerTitle: EXPLAIN
linkTitle: EXPLAIN
description: Use the EXPLAIN statement to show the execution plan for a YCQL statement.
summary: EXPLAIN
menu:
  latest:
    parent: api-cassandra
    weight: 1320
aliases:
  - /latest/api/ycql/explain/
isTocNested: true
showAsideToc: true
---

## Synopsis

Use the `EXPLAIN` statement to show the execution plan for an statement.

## Syntax

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#grammar" class="nav-link active" id="grammar-tab" data-toggle="tab" role="tab" aria-controls="grammar" aria-selected="true">
      <i class="fas fa-file-alt" aria-hidden="true"></i>
      Grammar
    </a>
  </li>
  <li>
    <a href="#diagram" class="nav-link" id="diagram-tab" data-toggle="tab" role="tab" aria-controls="diagram" aria-selected="false">
      <i class="fas fa-project-diagram" aria-hidden="true"></i>
      Diagram
    </a>
  </li>
</ul>


## Semantics

Where statement is the target statement (see more [here](../dml_select)).

## Examples
Create the keyspace, tables and indexes.

### Setup Table and indexes
```CQL
cqlsh> CREATE KEYSPACE IF NOT EXISTS imdb;
cqlsh> CREATE TABLE IF NOT EXISTS imdb.movie_stats (
   ...        movie_name text,
   ...        movie_genre text,
   ...        user_name text,
   ...        user_rank int,
   ...        last_watched timestamp,
   ...        PRIMARY KEY (movie_genre, movie_name, user_name)
   ... ) WITH transactions = { 'enabled' : true };
cqlsh> CREATE INDEX IF NOT EXISTS most_watched_by_year
   ...   ON imdb.movie_stats((movie_genre, last_watched), movie_name, user_name)
   ...   INCLUDE(user_rank);
cqlsh> CREATE INDEX IF NOT EXISTS best_rated
   ...   ON imdb.movie_stats((user_rank, movie_genre), movie_name, user_name)
   ...   INCLUDE(last_watched);
```

Insert some rows.
```CQL
cqlsh> USE imdb;
cqlsh:imdb> INSERT INTO movie_stats(movie_name, movie_genre, user_name, user_rank, last_watched)
        ...     VALUES ('m1', 'g1', 'u1', 5, '2019-01-18');
cqlsh:imdb> INSERT INTO movie_stats(movie_name, movie_genre, user_name, user_rank, last_watched)
        ...     VALUES ('m2', 'g2', 'u1', 4, '2019-01-17');
cqlsh:imdb> INSERT INTO movie_stats(movie_name, movie_genre, user_name, user_rank, last_watched)
        ...     VALUES ('m3', 'g1', 'u2', 5, '2019-01-18');
cqlsh:imdb> INSERT INTO movie_stats(movie_name, movie_genre, user_name, user_rank, last_watched)
        ...     VALUES ('m4', 'g1', 'u1', 2, '2019-02-27');
```
If movie_genre, or movie_genre & movie_name, or movie_genre & movie_name & user_name are specified, the query should be served efficiently from the primary table.

```CQL
cqlsh:imdb> EXPLAIN SELECT *
        ...   FROM movie_stats
        ...  WHERE movie_genre = 'g1';

QUERY PLAN
----------------------------------------
 Range Scan on imdb.movie_stats
   Key Conditions: (movie_genre = 'g1')
```
If movie_genre & last_watched are specified, then the query should be served efficiently from the `most_watched_by_year` index.

```CQL
cqlsh:imdb> EXPLAIN SELECT *
        ...   FROM movie_stats
        ...  WHERE movie_genre = 'g1' and last_watched='2019-02-27';

QUERY PLAN
--------------------------------------------------------------------------
 Index Only Scan using imdb.most_watched_by_year on imdb.movie_stats
   Key Conditions: (movie_genre = 'g1') AND (last_watched = '2019-02-27')

```

If user_rank and movie_genre are specified then the query should be served efficiently from the `best_rated` index.

```CQL
cqlsh:imdb> EXPLAIN SELECT *
        ...   FROM movie_stats
        ...  WHERE movie_genre = 'g2' and user_rank=5;

QUERY PLAN
--------------------------------------------------------------
 Index Only Scan using imdb.best_rated on imdb.movie_stats
   Key Conditions: (user_rank = '5') AND (movie_genre = 'g2')

```
Create non-covering index.
```CQL
cqlsh:imdb> DROP INDEX best_rated;
cqlsh:imdb> CREATE INDEX IF NOT EXISTS best_rated
        ...   ON imdb.movie_stats((user_rank, movie_genre), movie_name, user_name);
```
2-Step select. Using Index Scan as opposed to Index Only Scan.
```CQL
cqlsh:imdb> EXPLAIN SELECT *
        ...   FROM movie_stats
        ...  WHERE movie_genre = 'g2' and user_rank=5;

 QUERY PLAN
--------------------------------------------------------------
 Index Scan using imdb.best_rated on imdb.movie_stats
   Key Conditions: (user_rank = '5') AND (movie_genre = 'g2')
```
### Other explain select types
We don’t support QLName() for these expressions yet
```CQL
cqlsh:imdb> EXPLAIN SELECT * FROM movie_stats where movie_genre in ('g1', 'g2');

 QUERY PLAN
-------------------------------------------
 Range Scan on imdb.movie_stats
   Key Conditions: (movie_genre IN 'expr')
```


```CQL
cqlsh:imdb> EXPLAIN SELECT COUNT(*) FROM movie_stats  WHERE movie_genre = 'g2' and user_rank=5;

 QUERY PLAN
--------------------------------------------------------------------
 Aggregate
   ->  Index Only Scan using imdb.best_rated on imdb.movie_stats
         Key Conditions: (user_rank = '5') AND (movie_genre = 'g2')
```


```CQL
cqlsh:imdb> EXPLAIN SELECT * FROM movie_stats  WHERE movie_genre = 'g2' and user_rank = 5 LIMIT 5;

 QUERY PLAN
--------------------------------------------------------------------
 Limit
   ->  Index Only Scan using imdb.best_rated on imdb.movie_stats
         Key Conditions: (user_rank = '5') AND (movie_genre = 'g2')
```
### Insert Example

```CQL
cqlsh:imdb> EXPLAIN INSERT INTO movie_stats(movie_name, movie_genre, user_name, user_rank, last_watched)
        ...     VALUES ('m4', 'g1', 'u1', 2, '2019-02-27');

 QUERY PLAN
----------------------------
 Insert on imdb.movie_stats
```
### Delete Examples

```CQL
cqlsh:imdb> explain delete from movie_stats  where movie_genre = 'g1' and movie_name = 'm1';

 QUERY PLAN
----------------------------------------------
 Delete on imdb.movie_stats
   ->  Range Scan on imdb.movie_stats
         Key Conditions: (movie_genre = 'g1')
         Filter: (movie_name = 'm1')
```
```CQL
cqlsh:imdb> explain delete from movie_stats  where movie_genre = 'g1';

 QUERY PLAN
----------------------------------------------
 Delete on imdb.movie_stats
   ->  Range Scan on imdb.movie_stats
         Key Conditions: (movie_genre = 'g1')
```
### Update Example

```CQL
cqlsh:imdb> EXPLAIN UPDATE movie_stats SET user_rank = 1 WHERE movie_name = 'm1' and movie_genre = 'g1' and user_name = 'u1';

 QUERY PLAN
---------------------------------------------------------------------------------------------
 Update on imdb.movie_stats
   ->  Primary Key Lookup on imdb.movie_stats
         Key Conditions: (movie_genre = 'g1') AND (movie_name = 'm1') AND (user_name = 'u1')
```

## See also

- [`INSERT`](../dml_insert)
- [`SELECT`](../dml_select)
