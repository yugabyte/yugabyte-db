---
title: Pattern Matching in YSQL
headerTitle: Pattern Search
linkTitle: Pattern Search
headcontent: Learn how to search text with pattern matching
description: Learn how to search text with pattern matching
menu:
  preview:
    identifier: pattern-matching-ysql
    parent: text-search
    weight: 100
rightNav:
  hideH3: true
type: docs
---

The `LIKE` operator is a simple pattern-matching operator that emulates wildcard-like matching similar to many *nix shells. Pattern matching can be done either using `%` (percent) that matches any sequence of characters or `_`(underscore) that matches any single character. Let us explore the different types of matching that can be done in YugabyteDB with a few examples.

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
\copy words(word) from '/tmp/words.txt';
```

## Suffix matching

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

## Prefix matching

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

## Infix matching

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

## Single character matching

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

## Case insensitive matching

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