---
title: Similarity Search in YSQL
headerTitle: Similarity Search
linkTitle: Similarity Search
headcontent: Learn how to do similarity search in YSQL
description: Learn how to do similarity search in YSQL
menu:
  preview:
    identifier: similarity-search
    parent: text-search
    weight: 300
rightNav:
  hideH3: true
type: docs
---

Similarity matching works by determining how similar two different strings are. This can be useful when you do not know the exact spelling of your query term and can be to design spell checkers. Let us go into some of the methods you can use to achieve this.

### Setup

{{<warning>}}
TODO: Will add cluster setup tabs here.
{{</warning>}}

For examples and illustrations, let's use the following table and data.

```sql
CREATE TABLE words (
    id SERIAL,
    word TEXT NOT NULL,
    PRIMARY KEY(id)
);
```

Let us load some sample words into the table.

```sql
INSERT INTO words(word) VALUES 
  ('anopisthographic'),('ambassadorship'),('ambuscader'),('ambiguity'),('ampycides'),
  ('anapaestically'),('anapests'),('anapsidan'),('unpsychic'),('anapsid'),
  ('unpessimistic'),('unepistolary'),('inabstracted'),('anapaest'),('unobstinate'),
  ('amphigoric'),('amebic'),('amebous'),('ambassage'),('unpacified'),('unposing');
```

## Levenshtein

The [Levenshtein distance](https://en.wikipedia.org/wiki/Levenshtein_distance) is a measure of the difference between 2 strings. It calculates the difference by considering the number of edits - insertions/deletions/substitutions needed for one string to be transformed into another. This is very useful for spell-checks. This function is provided by the PostgreSQL extension, [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html).

To enable the levenshtein function, activate the `fuzzystrmatch` extension.

```sql
CREATE extension IF NOT EXISTS fuzzystrmatch;
```

For example, to identify how the different mis-spellings of the `warehouse`, we could do:

```sql
SELECT word, levenshtein('warehouse', word) FROM words WHERE levenshtein('warehouse', word) < 3  ORDER BY levenshtein('warehouse', word) ASC LIMIT 10;
```

```output
    word    | levenshtein
------------+-------------
 warehouse  |           0
 warmhouse  |           1
 warehoused |           1
 warehouser |           1
 warhorse   |           2
 tirehouse  |           2
 washhouse  |           2
 carhouse   |           2
 firehouse  |           2
 gatehouse  |           2
```

Note the levenshtein scoring for `warehoused` is `1`. This is because it has `1` additional character `d` than `warehouse`. Also it is `2` for `wayhouse` because it needs 2 edits `r->y` and `del e`.

## Trigrams

A trigram is a group of three consecutive characters taken from a string. We can measure the similarity of two strings by counting the number of trigrams they share. The [pg_trgm](https://www.postgresql.org/docs/15/pgtrgm.html) extension provides multiple functions like `show_trgm` and `similarity`,   function that provides a score of how similar two strings are. 

For example, the trigrams for `warehouse` would be,

```sql{class=output}
{"  w"," wa",are,eho,hou,ous,reh,"se ",use,war}
```

To enable the trigrams functionality, activate the `pg_trgm` extension.

```sql
CREATE extension IF NOT EXISTS pg_trgm;
```

For example, you are looking for words with spelling close to `geodymamist`. To get the actual word, you could do this,

```sql
SELECT word, similarity(word, 'geodymamist') as score FROM words ORDER BY score DESC LIMIT 5;
```

```output
     word      |  score
---------------+----------
 geodynamicist | 0.444444
 geodesist     |    0.375
 geochemist    | 0.352941
 geodynamics   | 0.333333
 geologist     | 0.294118
```

To match word boundaries by avoiding cross-word trigrams, you can use the `strict_word_similarity` function. For example,

```sql
SELECT strict_word_similarity('word', 'two words'), similarity('word', 'two words');
```

```output
 strict_word_similarity | similarity
------------------------+------------
               0.571429 |   0.363636
```

Note that the `strict_word_similarity` is higher than the `similarity` as it gave higher importance to the presence of the exact term `word` in both strings.

## Learn more

- [Understand GIN indexes](../../../../explore/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)