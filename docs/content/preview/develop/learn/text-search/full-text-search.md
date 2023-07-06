---
title: Full-Text Search in YSQL
headerTitle: Full-Text Search
linkTitle: Full-Text Search
headcontent: Learn how to do full-text search in YSQL
description: Learn how to do full-text search in YSQL
menu:
  preview:
    identifier: full-text-search-ysql
    parent: text-search
    weight: 200
rightNav:
  hideH3: true
type: docs
---

The `LIKE` and `ILIKE` operators match patterns and are useful in many scenarios. But they also fail miserably to find a set of words that could be present in any order and slightly different form as they match the entire text. For example, it is not optimal to retrieve text with specific criteria like `'quick' and 'brown' not 'fox'` or match `wait` when searching for `waiting`. For this, YugabyteDB supports advanced searching mechanisms via `tsvector`, `tsquery` and inverted indexes. These are the same basic concepts that search engines use to build massive search systems at web scale.

Let us look into how to use full-text search via some examples.

### Setup

{{<warning>}}
TODO: Will add cluster setup tabs here.
{{</warning>}}

For examples and illustrations, let's use the following `movies` table.

```sql
CREATE TABLE movies (
    name TEXT NOT NULL,
    summary TEXT NOT NULL,
    PRIMARY KEY(name)
);
```

Let us add some sample data to the movies table.

```sql
INSERT INTO movies(name, summary) VALUES('The Shawshank Redemption', 'Two convicts become friends and one convict escapes.');
INSERT INTO movies(name, summary) VALUES('The Godfather','A don hands over his empire to one of his sons.');
INSERT INTO movies(name, summary) VALUES('Inception','A thief is given the task of planting an idea onto a mind');
```

## Text processing

Text can be represented as a vector of words, which is effectively the list of words and the positions that the words occur in the text. The data type that represents this is `tsvector`. For example, let's consider the phrase, `'Two convicts become friends and one convict escapes.'`. When we convert this to `tsvector` using the `to_tsvector` helper function, we would get

```sql
SELECT to_tsvector('Two convicts become friends and one convict escapes.');
```

```output
                         to_tsvector
--------------------------------------------------------------
 'becom':3 'convict':2,7 'escap':8 'friend':4 'one':6 'two':1
(1 row)
```

We can see that the word `one` occurs at position `6` in the text and the word `friend` occurs at position `4`. Also as the word `convict` occurs twice, both positions `2` and `7` are listed. Notice that the words `become` and `escape` are stored are `becom` and `escap`. This is the result of a process called [Stemming](https://en.wikipedia.org/wiki/Stemming), which converts different forms of a word to their root form. For example, the words `escape escaper escaping escaped` all stem to `escap`. This would enable fast retrieval of all the different forms of `escap` when searching for `escaping` or `escaped`.

The other interesting to note is that the word `and` is missing from the vector. This is because common words like `a, an, and, the ...` are known as [Stop Words](https://en.wikipedia.org/wiki/Stop_word) and are typically dropped during document and query processing.

## Query processing

Just as the text has to be processed for faster search, the query has to be processed too and it has to go through the same stemming and stop word removal process. The data type representing the query is `tsquery`. We can convert simple text to `tsquery` using one of the many helper functions like `to_tsquery, plainto_tsquery, phraseto_tsquery, websearch_to_tsquery` etc. If you want to search for `escaping` or `empire`, then

```sql
SELECT to_tsquery('escaping | empire');
```

```output
   to_tsquery
-----------------
 'escap' | 'empir'
(1 row)
```

Notice that the query has been transformed like how we transformed the text to `tsvector`.

## Searching

Now that we have processed both the text and the query, we have to use the query to match the text. For this, we need to use the `@@` operator which connects the vector to the query.

### OR

```sql
-- either `one` or `son`
SELECT name FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one | son');
```

```output
           name
--------------------------
 The Godfather
 The Shawshank Redemption
(2 rows)
```

### AND

```sql
-- both `one` and `son`
SELECT name FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one & son');
```

```output
     name
---------------
 The Godfather
 ```

### NOT

```sql
-- both `one` but NOT `son`
SELECT name FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one & !son');
```

```output
           name
--------------------------
 The Shawshank Redemption
```

Let's search for `conviction` in our movies table.

### Stemming

```sql
SELECT name FROM movies WHERE to_tsvector(summary) @@ to_tsquery('conviction');
```

```output
           name
--------------------------
 The Shawshank Redemption
```

Even though the word `conviction` was not present in the table, it returned `The Shawshank Redemption`. That is because the term `conviction` stemmed to `convict` and matched the right movie. This is the power of the full-text search!

## Ranking results

Retrieved results can be ranked using a matching score generated with the `ts_rank` function that measures the relevance of the text to the query. This can be used for identifying text that is more relevant to the query. For example, when we search for `one` or `son`,

```sql
SELECT name, ts_rank(to_tsvector(summary), to_tsquery('one | son')) as score FROM movies;
```

we get

```output
           name           |  score
--------------------------+-----------
 The Godfather            | 0.0607927
 Inception                |         0
 The Shawshank Redemption | 0.0303964
```

Notice that the score for `The Godfather` is twice the score for `The Shawshank Redemption`. This is because both `one` and `son` were present in the former but only `one` was present in the latter. This score can be used to sort the results by relevance.

## Highlighting matches

You can use the `ts_headline` function to highlight the query matches inside the text. 

```sql
SELECT name, ts_headline(summary,to_tsquery('one | son'))  FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one | son');
```

```output
           name           |                          ts_headline
--------------------------+---------------------------------------------------------------
 The Godfather            | A don hands over his empire to <b>one</b> of his <b>sons</b>.
 The Shawshank Redemption | Two convicts become friends and <b>one</b> convict escapes.
```

The matching terms are surrounded by `<b>..</b>`. This can be very useful when displaying search results.

## Searching multiple columns

All the above searches have been done on the `summary` column. If you want to search both the `name` and `summary`, you could concat both columns as below.

```sql
SELECT name FROM movies WHERE to_tsvector(name || ' ' || summary) @@ to_tsquery('godfather | thief');
```

```output
     name
---------------
 The Godfather
 Inception
```

The query term `godfather` matched the title of one movie but the term `thief` matched the summary of another movie.

## Storing tsvector

For every search above, the summary in all the rows was parsed again and again. This could be avoided by storing the `tsvector` in a separate column and storing the calculated `tsvector` on every insert. This can be accomplished by adding a new column and adding a trigger to update that column on row updates as below.

```sql
ALTER TABLE movies ADD COLUMN tsv tsvector;
```

Now update the `tsv` column.

```sql
UPDATE movies SET tsv = to_tsvector(name || ' ' || summary);
```

Now, you can query the table just on the `tsv` column as,

```sql
SELECT name FROM movies WHERE tsv @@ to_tsquery('godfather | thief');
```

```output
     name
---------------
 The Godfather
 Inception
```

You can also set the column to be automatically updated on future inserts and updates with a trigger using the `tsvector_update_trigger` function.

```sql
CREATE TRIGGER tsvectorupdate BEFORE INSERT OR UPDATE
    ON movies FOR EACH ROW EXECUTE FUNCTION
    tsvector_update_trigger (tsv, 'pg_catalog.english', name, summary);
```

## GIN Index

Even though we have stored the processed `tsvector` in a separate column, all the rows have to be scanned for every search. Let us look at the query plan.

```sql
EXPLAIN ANALYZE SELECT name FROM movies WHERE tsv @@ to_tsquery('godfather');
```

```output
                             QUERY PLAN
---------------------------------------------------------------------
 Seq Scan on public.movies (actual time=2.987..6.378 rows=1 loops=1)
   Output: name
   Filter: (movies.tsv @@ to_tsquery('godfather'::text))
   Rows Removed by Filter: 2
 Planning Time: 0.248 ms
 Execution Time: 7.067 ms
 Peak Memory Usage: 14 kB
(7 rows)
```

This is a sequential scan. This can be avoided by creating a `GIN` index on the `tsv` column as below.

```sql
CREATE INDEX idx_movie ON movies USING ybgin(tsv);
```

Now get the query plan again.

```sql
EXPLAIN ANALYZE SELECT name FROM movies WHERE tsv @@ to_tsquery('godfather');
```

```output
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Index Scan using idx_movie on public.movies (actual time=2.580..2.584 rows=1 loops=1)
   Output: name
   Index Cond: (movies.tsv @@ to_tsquery('godfather'::text))
 Planning Time: 0.207 ms
 Execution Time: 2.684 ms
 Peak Memory Usage: 18 kB
```

Notice that it is now an index scan and takes much lesser time.

{{<warning>}}
In the current implementation of `ybgin`, only single query term lookups are allowed. In other cases, you will get the error message, `DETAIL:  ybgin index method cannot use more than one required scan entry: got 2`
{{</warning>}}

## Learn more

- [Understand GIN indexes](../../../../explore/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)