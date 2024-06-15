---
title: Full-Text Search in YSQL
headerTitle: Full-text search
linkTitle: Full-text search
headcontent: Learn how to do full-text search in YSQL
description: Learn how to do full-text search in YSQL
menu:
  stable:
    identifier: full-text-search-ysql
    parent: text-search
    weight: 300
rightNav:
  hideH3: true
type: docs
---

While the `LIKE` and `ILIKE` operators match patterns and are helpful in many scenarios, they can't be used to find a set of words that could be present in any order or in a slightly different form. For example, it is not optimal for retrieving text with specific criteria like `'quick' and 'brown' not 'fox'` or match `wait` when searching for `waiting`. For this, YugabyteDB supports advanced searching mechanisms via `tsvector`, `tsquery`, and inverted indexes. These are the same basic concepts that search engines use to build massive search systems at web scale.

Let us look into how to use full-text search via some examples.

## Setup

{{<cluster-setup-tabs>}}

Create the following `movies` table:

```sql
CREATE TABLE movies (
    name TEXT NOT NULL,
    summary TEXT NOT NULL,
    PRIMARY KEY(name)
);
```

Add some sample data to the movies table as follows:

```sql
INSERT INTO movies(name, summary) VALUES('The Shawshank Redemption', 'Two convicts become friends and one convict escapes.');
INSERT INTO movies(name, summary) VALUES('The Godfather','A don hands over his empire to one of his sons.');
INSERT INTO movies(name, summary) VALUES('Inception','A thief is given the task of planting an idea onto a mind');
```

## Parsing documents

Text can be represented as a vector of words, which is effectively the list of words and the positions that the words occur in the text. The data type that represents this is `tsvector`. For example, consider the phrase `'Two convicts become friends and one convict escapes.'`. When you convert this to `tsvector` using the `to_tsvector` helper function, you get the following:

```sql
SELECT to_tsvector('Two convicts become friends and one convict escapes.');
```

```sql{.nocopy}
                         to_tsvector
--------------------------------------------------------------
 'becom':3 'convict':2,7 'escap':8 'friend':4 'one':6 'two':1
(1 row)
```

The word `one` occurs at position `6` in the text and the word `friend` occurs at position `4`. Also as the word `convict` occurs twice, both positions `2` and `7` are listed.

### Stemming

Notice that the words `become` and `escape` are stored as `becom` and `escap`. This is the result of a process called [Stemming](https://en.wikipedia.org/wiki/Stemming), which converts different forms of a word to their root form. For example, the words `escape escaper escaping escaped` all stem to `escap`. This enables fast retrieval of all the different forms of `escap` when searching for `escaping` or `escaped`.

### Stop words

Note how the word `and` is missing from the vector. This is because common words like `a, an, and, the ...` are known as [Stop Words](https://en.wikipedia.org/wiki/Stop_word) and are typically dropped during document and query processing.

## Parsing search queries

Just as the text has to be processed for faster search, the query has to go through the same stemming and stop word removal process. The data type representing the query is `tsquery`. You convert simple text to `tsquery` using one of the many helper functions like `to_tsquery, plainto_tsquery, phraseto_tsquery, websearch_to_tsquery`, and so on. If you want to search for `escaping` or `empire`, do the following:

```sql
SELECT to_tsquery('escaping | empire');
```

```sql{.nocopy}
   to_tsquery
-----------------
 'escap' | 'empir'
(1 row)
```

This transforms the query in a similar fashion to how the text was transformed to `tsvector`.

## Searching

After processing both the text and the query, you use the query to match the text. To do this, use the `@@` operator, which connects the vector to the query.

### OR

```sql
-- either `one` or `son`
SELECT * FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one | son');
```

```output
           name           |                       summary
--------------------------+------------------------------------------------------
 The Godfather            | A don hands over his empire to one of his sons.
 The Shawshank Redemption | Two convicts become friends and one convict escapes.
```

### AND

```sql
-- both `one` and `son`
SELECT * FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one & son');
```

```output
     name      |                     summary
---------------+-------------------------------------------------
 The Godfather | A don hands over his empire to one of his sons.
 ```

### NOT

```sql
-- both `one` but NOT `son`
SELECT * FROM movies WHERE to_tsvector(summary) @@ to_tsquery('one & !son');
```

```output
           name           |                       summary
--------------------------+------------------------------------------------------
 The Shawshank Redemption | Two convicts become friends and one convict escapes.
```

### Stemming

Search for `conviction` in the movies table as follows:

```sql
SELECT * FROM movies WHERE to_tsvector(summary) @@ to_tsquery('conviction');
```

```output
           name           |                       summary
--------------------------+------------------------------------------------------
 The Shawshank Redemption | Two convicts become friends and one convict escapes.
```

Even though the word `conviction` was not present in the table, it returned `The Shawshank Redemption`. That is because the term `conviction` stemmed to `convict` and matched the right movie. This is the power of the full-text search.

## Rank results

Retrieved results can be ranked using a matching score generated using the `ts_rank` function, which measures the relevance of the text to the query. This can be used to identify text that is more relevant to the query. For example, when you search for `one` or `son` as follows:

```sql
SELECT ts_rank(to_tsvector(summary), to_tsquery('one | son')) as score,* FROM movies;
```

You get the following output:

```output
   score   |           name           |                          summary
-----------+--------------------------+-----------------------------------------------------------
 0.0607927 | The Godfather            | A don hands over his empire to one of his sons.
         0 | Inception                | A thief is given the task of planting an idea onto a mind
 0.0303964 | The Shawshank Redemption | Two convicts become friends and one convict escapes.
```

Notice that the score for `The Godfather` is twice the score for `The Shawshank Redemption`. This is because both `one` and `son` is present in the former but only `one` is present in the latter. This score can be used to sort results by relevance.

## Highlight matches

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

The matching terms are surrounded by `<b>..</b>`. This can be very beneficial when displaying search results.

## Search multiple columns

All the preceding searches have been made on the `summary` column. If you want to search both the `name` and `summary`, you can concatenate both columns as follows:

```sql
SELECT * FROM movies WHERE to_tsvector(name || ' ' || summary) @@ to_tsquery('godfather | thief');
```

```output
     name      |                          summary
---------------+-----------------------------------------------------------
 The Godfather | A don hands over his empire to one of his sons.
 Inception     | A thief is given the task of planting an idea onto a mind
```

The query term `godfather` matched the title of one movie while the term `thief` matched the summary of another movie.

## Store processed documents

For every preceding search, the summary in all the rows was parsed again and again. You can avoid this by storing the `tsvector` in a separate column and storing the calculated `tsvector` on every insert. Do this by adding a new column and adding a trigger to update that column on row updates as follows:

```sql
ALTER TABLE movies ADD COLUMN tsv tsvector;
```

Update the `tsv` column as follows:

```sql
UPDATE movies SET tsv = to_tsvector(name || ' ' || summary);
```

Now you can query the table just on the `tsv` column as follows:

```sql
SELECT * FROM movies WHERE tsv @@ to_tsquery('godfather | thief');
```

```sql{.nocopy}
     name      |                          summary                          |                                       tsv
---------------+-----------------------------------------------------------+---------------------------------------------------------------------------------
 The Godfather | A don hands over his empire to one of his sons.           | 'empir':8 'godfath':2 'hand':5 'one':10 'son':13
 Inception     | A thief is given the task of planting an idea onto a mind | 'given':5 'idea':11 'incept':1 'mind':14 'onto':12 'plant':9 'task':7 'thief':3
```

You can set the column to be automatically updated on future inserts and updates with a trigger using the `tsvector_update_trigger` function.

```sql
CREATE TRIGGER tsvectorupdate BEFORE INSERT OR UPDATE
    ON movies FOR EACH ROW EXECUTE FUNCTION
    tsvector_update_trigger (tsv, 'pg_catalog.english', name, summary);
```

## Optimize queries using a GIN Index

Even though the processed `tsvector` is now stored in a separate column, all the rows have to be scanned for every search.

Show the query plan as follows:

```sql
EXPLAIN ANALYZE SELECT name FROM movies WHERE tsv @@ to_tsquery('godfather');
```

```sql{.nocopy}
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

This is a sequential scan. To avoid this, create a `GIN` index on the `tsv` column as follows:

```sql
CREATE INDEX idx_movie ON movies USING ybgin(tsv);
```

Get the query plan again:

```sql
EXPLAIN ANALYZE SELECT name FROM movies WHERE tsv @@ to_tsquery('godfather');
```

```sql{.nocopy}
                                      QUERY PLAN
---------------------------------------------------------------------------------------
 Index Scan using idx_movie on public.movies (actual time=2.580..2.584 rows=1 loops=1)
   Output: name
   Index Cond: (movies.tsv @@ to_tsquery('godfather'::text))
 Planning Time: 0.207 ms
 Execution Time: 2.684 ms
 Peak Memory Usage: 18 kB
```

Notice that it now does an index scan and takes much less time.

{{<warning>}}
In the current implementation of `ybgin`, only single query term lookups are allowed. In other cases, you will get the error message, `DETAIL:  ybgin index method cannot use more than one required scan entry: got 2`.
{{</warning>}}

## Learn more

- [Understand GIN indexes](../../../../explore/ysql-language-features/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)