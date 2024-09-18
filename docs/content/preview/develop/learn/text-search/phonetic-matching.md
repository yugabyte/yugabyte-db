---
title: Phonetic Matching in YSQL
headerTitle: Phonetic search
linkTitle: Phonetic search
headcontent: Learn how to do phonetic matching in YSQL
description: Learn how to do phonetic matching in YSQL
badges: ysql
menu:
  preview:
    identifier: phonetic-matching-ysql
    parent: text-search
    weight: 400
rightNav:
  hideH3: true
type: docs
---

While looking up exact matches suffices for most scenarios, you need approximate matching in situations where you don't know the exact term you are searching for but may remember the phonetics of the term or parts of the term.

For phonetic matching, YugabyteDB natively supports the PostgreSQL extension [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html), which provides multiple functions - Soundex, Metaphone, and Double Metaphone - which you can use to determine phonetic similarities between text.

## Setup

{{<cluster-setup-tabs>}}

Create the following table:

```sql
CREATE TABLE IF NOT EXISTS words (
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

To enable fuzzy matching, activate the `fuzzystrmatch` extension as follows:

```sql
CREATE extension IF NOT EXISTS fuzzystrmatch;
```

## Soundex

The [Soundex](https://en.wikipedia.org/wiki/Soundex) system is a method of matching similar-sounding names by converting them to the same four letter code which can then be used to determine similarities between text.

For example, suppose you heard a word that sounded like `anapistagafi`. To find the actual word that sounds similar, you could use `soundex` method to find the closest-sounding word as follows:

```sql
SELECT word, soundex(word), difference(word,'anapistagafi') FROM words WHERE soundex(word) = soundex('anapistagafi') limit 5;
```

```output
      word      | soundex | difference
----------------+---------+------------
 anapaestically | A512    |          4
 ampycides      | A512    |          4
 anapsid        | A512    |          4
 amebous        | A512    |          4
 anapaest       | A512    |          4
```

The `difference` method calculates how different one Soundex code is from another. The value ranges from 0-4 and can be used to rank the results. In this case, the difference is the same because the value is very naive.

## Metaphone

The [metaphone](https://en.wikipedia.org/wiki/Metaphone) algorithm improves upon Soundex by taking into consideration the inconsistencies in English spelling and pronunciation to produce a more accurate encoding. For example:

```sql
SELECT word, metaphone(word,4) FROM words WHERE metaphone(word,4) = metaphone('anapistagafi',4) limit 5;
```

```output
       word       | metaphone
------------------+-----------
 anapaestically   | ANPS
 anapsid          | ANPS
 anapaest         | ANPS
 anapsidan        | ANPS
 anopisthographic | ANPS
```

The `metaphone` function takes in an additional parameter of code output length that can be used for slightly modifying the matching errors.

## Double Metaphone

The [Double Metaphone](https://en.wikipedia.org/wiki/Metaphone#Double_Metaphone) makes a number of fundamental design improvements over the original Metaphone algorithm. It calculates two different codes that enable it to compare a wider range of pronunciations.

```sql
SELECT word, dmetaphone(word) FROM words WHERE dmetaphone(word) = dmetaphone('anapistagafi') limit 5;
```

```output
      word      | dmetaphone
----------------+------------
 unpsychic      | ANPS
 anapaestically | ANPS
 inabstracted   | ANPS
 unobstinate    | ANPS
 unposing       | ANPS
 ```

## Learn more

- [Understand GIN indexes](../../../../explore/ysql-language-features/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)