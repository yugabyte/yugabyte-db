---
title: Text Search in YSQL
headerTitle: Text Search
linkTitle: Text Search
headcontent: Learn how to search text data types in YSQL
description: Learn how to search text data types in YSQL
menu:
  preview:
    identifier: text-search-ysql
    parent: learn
    weight: 580
rightNav:
  hideH3: true
type: docs
---

Traditional matching in databases has been an exact match with the `=` operator. The full-text search brings in a whole new class of pattern matching like prefix match, suffix match, fuzzy match etc. Let's look into the different methods and features that YugabyteDB provides for searching text.

## Setup

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
\copy words(word) from '/tmp/words.txt';
```

## Pattern matching

The `LIKE` operator is a simple pattern-matching operator that emulates wildcard-like matching similar to many *nix shells. Pattern matching can be done either using `%` (percent) that matches any sequence of characters or `_`(underscore) that matches any single character. 

### Suffix matching

The `%` could be added to the end of a pattern to match any string that completes the given pattern. For example, to get all the words starting with `ca`, you would execute:

```sql
select * from words where word like 'ca%' limit 5;
```

```output
  id   |     word
-------+--------------
 57757 | can.
 56710 | calyciferous
 60726 | carpology
 57735 | campward
 57239 | camachile
(5 rows)
```

### Prefix matching

The `%` could be added to the beginning of a pattern to match any string that ends in the given pattern. For example, to get words ending with `box`, you would execute:

```sql
select * from words where word like '%ca' limit 5;
```

```output
   id   |  word
--------+---------
  33798 | baraca
 464587 | xinca
  92076 | crusca
 343128 | ronica
 302782 | polacca
(5 rows)
```

### Infix matching

The `%` could also be used to match any sequence of text between a given pattern. For example, to get all words starting with `ca` and ending in `ly`, you could execute,

```sql
select * from words where word like 'ca%ly' limit 5;
```

```output
  id   |      word
-------+----------------
 62287 | cathodically
 62300 | catholically
 56535 | calculably
 58913 | capitally
 58520 | cantankerously
(5 rows)
```

### Single character matching

You can use `_`(underscore) to match any single character. To get all the 3 letter words that start with `c` and end in `t`, you would execute,

```sql
select * from words where word ilike 'c_t' limit 5;
```

```output
  id   | word
-------+------
 88853 | cpt
 86923 | cot
 63037 | cct
 91859 | crt
 72942 | cit
(5 rows)
```

### Case insensitive matching

The `LIKE` operator performs case-sensitive matching. For example, if we change our pattern to uppercase, we may not get the same results.

```sql
select * from words where word like 'C_T' limit 5;
```

```output
 id | word
----+------
(0 rows)
```

To support case-insensitive matching, you would need to use the `ILIKE` operator.

```sql
select * from words where word ilike 'C_T' limit 5;
```

```output
  id   | word
-------+------
 88853 | cpt
 86923 | cot
 63037 | cct
 91859 | crt
 72942 | cit
(5 rows)
```

## Regex matching

The `SIMILAR TO` operator can be used to match patterns using the SQL standard's definition of a regular expression. SQL regular expressions are a curious cross between `LIKE` notation and common (`POSIX`) regular expression notation. To find all words that 
have `e` occurring three or more times consecutively,

```sql
select word from words where word SIMILAR TO '%e{3,}%' ;
```

```output
   word
-----------
 andeee
 ieee
 aieee
 whenceeer
 eee
(5 rows)
```

`SIMILAR TO` supports these pattern-matching metacharacters:

- `|` denotes alternation (either of two alternatives).
- `*` denotes repetition of the previous item zero or more times.
- `+` denotes repetition of the previous item one or more times.
- `?` denotes repetition of the previous item zero or one time.
- `{m}` denotes repetition of the previous item exactly m times.
- `{m,}` denotes repetition of the previous item m or more times.
- `{m,n}` denotes repetition of the previous item at least m and not more than n times.

Parentheses `()` can be used to group items into a single logical item. A bracket expression `[...]` specifies a character class, just as in POSIX regular expressions.

## Inverted index search

The `LIKE` and `ILIKE` operators match patterns and are useful in many scenarios. But they also fail miserably to find a set of words that could be present in any order and slightly different form as they match the entire text. For example, it is not optimal to retrieve text with specific criteria like `'quick' and 'brown' not 'fox'` or match `wait` when searching for `waiting`. For this, YugabyteDB supports advanced searching mechanisms via `tsvector`, `tsquery` and inverted indexes. These are the same basic concepts that search engines use to build massive search systems at web scale.

{{<note>}}
Even though adding indexes would greatly improve pattern matching with `LIKE` operator, the speed of retrieval with inverted indexes would be orders of magnitude better because of the inherent point lookups that inverted index end up doing.
{{</note>}}

### Text processing

Text can be represented as a vector of words, which is effectively the list of words and the positions that the words occur in the text. The data type that represents this is `tsvector`. For example, let's consider the phrase, `'The quick brown fox jumps jumps over the lazy dog'`. When we convert this to `tsvector` using the `to_tsvector` helper function, we would get,

```sql
select to_tsvector('The quick brown fox jumps jumps over the lazy dog');
```

```output
                       to_tsvector
----------------------------------------------------------
 'brown':3 'dog':10 'fox':4 'jump':5,6 'lazi':9 'quick':2
(1 row)
```

We can see that the word `brown` occurs at position 3 in the text and the word `dog` occurs at position 10. Also as the word `jumps` occurs twice, both positions `5` and `6` are listed. Notice that the words `jumps` and `lazy` are stored are `jump` and `lazi`. This is the result of a process called [Stemming](https://en.wikipedia.org/wiki/Stemming), which converts different forms of a word to their root form. For example, the words `jump, jumps, jumping, jumped` all stem to `jump`. This would enable fast retrieval of all the different forms of `jump` when searching for `jump` or `jumping`.

The other interesting to note is that the word `the` is missing from the vector. This is because common words like `a, an, and, the ...` are known as [Stop Words](https://en.wikipedia.org/wiki/Stop_word) and are typically dropped during document and query processing.

### Query processing

Just as the text has to be processed for faster search, the query has to be processed too and it has to go through the same stemming and stop word removal process. The data type representing the query is `tsquery`. We can convert simple text to `tsquery` using one of the many helper functions like `to_tsquery, plainto_tsquery, phraseto_tsquery, websearch_to_tsquery` etc. If you want to search for `lazy` or `jumping`, then

```sql
select to_tsquery('lazier | jumping');
```

```output
   to_tsquery
-----------------
 'lazi' | 'jump'
(1 row)
```

Notice that the query has been transformed like how we transformed the text to `tsvector`.

### The actual search

Now that we have processed both the text and the query, we have to use the query to match the text. For this, we need to use the `@@` operator which connects the vector to the query. For example,

```sql
select to_tsvector('The quick brown fox jumps jumps over the lazy dog') @@ to_tsquery('lazy | jumping');
```

```output
 ?column?
----------
 t
(1 row)
```

Let's search for `lazy` in our word list.

```sql
select word from words where to_tsvector(word) @@ to_tsquery('lazy');
```

```output
    word
------------
 lazied
 lazying
 lazies
 lazy
 laziness
 lazinesses
 bone-lazy
 lazys
(8 rows)
```

Notice that along with the word `lazy` it also retrieved `laziness` and `lazying`. This is the power of the full-text search!

### Ranking results

Retrieved results can be ranked using a matching score generated with the `ts_rank` function that measures the relevance of the text to the query. This can be used for identifying text that is more relevant to the query. For example,

```sql
-- dog OR fox
select ts_rank(to_tsvector('The quick brown fox jumps over the lazy dog'), to_tsquery('dog | fox')) as rank;

  rank
-----------
 0.0607927
(1 row)
```

```sql
-- dog AND fox
select ts_rank(to_tsvector('The quick brown fox jumps over the lazy dog'), to_tsquery('dog & fox')) as rank;

  rank
-----------
 0.09149
(1 row)
```

## Fuzzy search

Although looking up exact matches would suffice in most scenarios, there would be some cases where approximate matching would be beneficial. You may not know the exact term you are searching for but remember the phonetics of the term or parts of the term. For such matching, YugabyteDB natively supports [PostgreSQL extensions](https://www.postgresql.org/docs/15/contrib.html), [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html) and [pg_trgm](https://www.postgresql.org/docs/15/pgtrgm.html)

### Sound-based matching

The [fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html) extension provides multiple functions - Soundex, Levenshtein, Metaphone and Double Metaphone - which can be used to determine similarities between text. For eg, you heard a word that sounded like `anapistagafi`. To find the actual word that sounds similar, you could use `dmetaphone` to find the closest sounding word like,

```sql
select word from words where dmetaphone(word) = dmetaphone('anapistagafi') limit 5;
```

```output
       word
------------------
 anopisthographic
 unpsychic
 unpessimistic
 unepistolary
 inabstracted
```

You can easily recollect that the original word was probably `anopisthographic`.

### Similarity matching

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

## Learn more

- [Understand GIN indexes](../../../explore/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)