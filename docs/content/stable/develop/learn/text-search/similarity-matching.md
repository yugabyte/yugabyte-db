---
title: Similarity Search in YSQL
headerTitle: Similarity search
linkTitle: Similarity search
headcontent: Learn how to do similarity search in YSQL
description: Learn how to do similarity search in YSQL
menu:
  stable:
    identifier: similarity-search
    parent: text-search
    weight: 200
rightNav:
  hideH3: true
type: docs
---

Similarity matching works by determining how similar two different strings are. This can be helpful when you don't know the exact spelling of your query term and can be used to design spell checkers.

## Setup

{{<cluster-setup-tabs>}}

Create the following table:

```sql
CREATE TABLE words (
    id SERIAL,
    word TEXT NOT NULL,
    PRIMARY KEY(id)
);
```

 Load some sample words into the table as follows:

```sql
INSERT INTO words(word) VALUES 
  ('anopisthographic'),('ambassadorship'),('ambuscader'),('ambiguity'),('ampycides'),
  ('anapaestically'),('anapests'),('anapsidan'),('unpsychic'),('anapsid'),
  ('unpessimistic'),('unepistolary'),('inabstracted'),('anapaest'),('unobstinate'),
  ('amphigoric'),('amebic'),('amebous'),('ambassage'),('unpacified'),('unposing');
```

## Levenshtein

The [Levenshtein distance](https://en.wikipedia.org/wiki/Levenshtein_distance) is a measure of the difference between 2 strings. It calculates the difference by considering the number of edits (insertions, deletions, and substitutions) needed for one string to be transformed into another. This is particularly useful for spell-checks. This function is provided by the PostgreSQL extension [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html).

To enable the Levenshtein function, activate the `fuzzystrmatch` extension as follows:

```sql
CREATE extension IF NOT EXISTS fuzzystrmatch;
```

For example, to identify how the different mis-spellings of the `warehouse`, do the following:

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

The Levenshtein scoring for `warehoused` is `1` because it has one more character (`d`) than `warehouse`. The scoring is `2` for `wayhouse` because it needs two edits (`r->y` and `del e`).

## Trigrams

A trigram is a group of three consecutive characters taken from a string. You can measure the similarity of two strings by counting the number of trigrams they share. The [pg_trgm](https://www.postgresql.org/docs/15/pgtrgm.html) extension provides multiple functions like `show_trgm` and `similarity`, which provide a score of how similar two strings are.

For example, the trigrams for `warehouse` would be as follows:

```sql{class=output}
{"  w"," wa",are,eho,hou,ous,reh,"se ",use,war}
```

To enable the trigrams functionality, activate the `pg_trgm` extension:

```sql
CREATE extension IF NOT EXISTS pg_trgm;
```

For example, suppose you are looking for words with spelling close to `geodymamist`. To get the actual word, you could do the following:

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

To match word boundaries by avoiding cross-word trigrams, you can use the `strict_word_similarity` function. For example:

```sql
SELECT strict_word_similarity('word', 'two words'), similarity('word', 'two words');
```

```output
 strict_word_similarity | similarity
------------------------+------------
               0.571429 |   0.363636
```

The `strict_word_similarity` is higher than the `similarity` as it gave higher importance to the presence of the exact term `word` in both strings.

## Learn more

- [Understand GIN indexes](../../../../explore/ysql-language-features/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)
