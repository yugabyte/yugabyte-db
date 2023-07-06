---
title: Phonetic Matching in YSQL
headerTitle: Phonetic Search
linkTitle: Phonetic Search
headcontent: Learn how to do phonetic Matching in YSQL
description: Learn how to do phonetic Matching in YSQL
menu:
  preview:
    identifier: phonetic-matching-ysql
    parent: text-search
    weight: 300
rightNav:
  hideH3: true
type: docs
---

Although looking up exact matches would suffice in most scenarios, there are some cases where approximate matching would be beneficial. You may not know the exact term you are searching for but may remember the phonetics of the term or parts of the term. 

For such matching, YugabyteDB natively supports the PostgreSQL extension, [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html) that provides multiple functions - Soundex, Levenshtein, Metaphone and Double Metaphone - which can be used to determine similarities between text. 

Let us explore the different methodologies available in YugabyteBD via some examples.

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

For sample data, let's use the [list of English words](https://github.com/dwyl/english-words/blob/master/words.txt). Load the data into the table using the following command.

```sql
-- make sure to give the full path of the file
\COPY words(word) FROM '/tmp/words.txt';
```

To enable fuzzy matching, activate the `fuzzystrmatch` extension.

```sql
CREATE extension IF NOT EXISTS fuzzystrmatch;
```

## Soundex

The [Soundex](https://en.wikipedia.org/wiki/Soundex) system is a method of matching similar-sounding names by converting them to the same four letter code which can then be used to determine similarities between text.

For eg, you heard a word that sounded like `anapistagafi`. To find the actual word that sounds similar, you could use `soundex` method to find the closest-sounding word like,

```sql
SELECT word, soundex(word), difference(word,'anapistagafi') FROM words WHERE soundex(word) = soundex('anapistagafi') limit 5;
```

```output
       word       | soundex | difference
------------------+---------+------------
 anopisthographic | A512    |          4
 ambassadorship   | A512    |          4
 ambuscader       | A512    |          4
 ambiguity        | A512    |          4
 ampycides        | A512    |          4
```

The `difference` method calculates how different is one Soundex code from another. The value ranges from 0-4 and can be used to rank the results. But this is not advisable as the value is very naive.

## Metaphone

The [metaphone](https://en.wikipedia.org/wiki/Metaphone) algorithm improves upon the Soundex by taking into consideration the inconsistencies in English spelling and pronunciation to produce a more accurate encoding.

```sql
SELECT word, metaphone(word,4) FROM words WHERE metaphone(word,4) = metaphone('anapistagafi',4) limit 5;
```

```output
       word       | metaphone
------------------+-----------
 anopisthographic | ANPS
 anapsid          | ANPS
 anapaestically   | ANPS
 anapests         | ANPS
 anapsidan        | ANPS
```

The `metaphone` function takes in an additional parameter of code output length that can be used for slightly modifying the matching errors.

## Double Metaphone

The [Double Metaphone](https://en.wikipedia.org/wiki/Metaphone#Double_Metaphone) makes a number of fundamental design improvements over the original Metaphone algorithm. It calculates two different codes which enables it compare a wider range of pronunciations.

```sql
SELECT word, dmetaphone(word) FROM words WHERE dmetaphone(word) = dmetaphone('anapistagafi') limit 5;
```

```output
       word       | dmetaphone
------------------+------------
 anopisthographic | ANPS
 unpsychic        | ANPS
 unpessimistic    | ANPS
 unepistolary     | ANPS
 inabstracted     | ANPS
 ```

## Levenshtein

The [levenshtein distance](https://en.wikipedia.org/wiki/Levenshtein_distance) is a measure of the difference between 2 strings. It calculates the difference by considering the number of edits - insertions/deletions/substitutions needed for one string to be transformed into another. This is very useful for spell-checks. For example, to identify how the different mis-spellings of the `warehouse` that do not start with `w`, we could do:

```sql
SELECT word FROM words WHERE levenshtein('warehouse', word) < 3 AND word NOT LIKE 'w%' ORDER BY levenshtein('warehouse', word) ASC;
```

```output
    word
-------------
 gatehouse
 carhouse
 alehouse
 tirehouse
 morehouse
 firehouse
 rehouse
 cakehouse
 bargehouse
 cardhouse
 farmhouse
 bakehouse
 rewarehouse
 ```

## Similarity matching

The [pg_trgm](https://www.postgresql.org/docs/15/pgtrgm.html) extension provides the `similarity` function that provides a score of how similar two strings are. This can be useful when you do not know the exact spelling of your query term. For example, you are looking for words with spelling close to `geodymamist`. To get the actual word, you could do this,

```sql
select word, similarity(word, 'geodymamist') as score from words order by score desc limit 5;
```

```output
     word      |  score
---------------+----------
 geodynamicist | 0.444444
 geodist       | 0.428571
 geodesist     |    0.375
 geochemist    | 0.352941
 geodynamic    | 0.352941
(5 rows)
```

You can combine one or more of the above schemes to provide intuitive experiences to your users.