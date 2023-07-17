---
title: Pattern Matching in YSQL
headerTitle: Pattern matching
linkTitle: Pattern matching
headcontent: Learn how to search text using pattern matching
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

The `LIKE` operator is a basic pattern-matching operator that emulates wildcard-like matching similar to many *nix shells. Pattern matching can be done either using `%` (percent) to match any sequence of characters, or `_`(underscore) to match any single character.

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
  ('camp'),('carousel'),('cartel'),('carpet'),('carnivore'),('cartoon'),('carry'),('capsule'),
  ('corsica'),('medica'),('azteca'),('republica'),('chronica'),('orca'),('cathodically'),('capably'),
  ('cot'),('cat'),('cut'),('cwt'),('cit'),('cit'),('captainly'),('callously'),('career'),('calculate'),
  ('lychees'),('deer'),('peer'),('seer'),('breeze'),('green'),('teen'),('casually');
```

## Suffix matching

Add `%` to the end of a pattern to match any string that completes the given pattern. For example, to get all the words starting with `ca`, execute the following:

```sql
SELECT word FROM words WHERE word LIKE 'ca%' limit 5;
```

```output
      word
--------------
 carnivore
 camp
 capably
 cathodically
 cartoon
```

## Prefix matching

Add `%` to the beginning of a pattern to match any string that ends in the given pattern. For example, to get words ending with `ca`, execute the following:

```sql
SELECT word FROM words WHERE word LIKE '%ca' limit 5;
```

```output
   word
-----------
 azteca
 chronica
 republica
 corsica
 medica
```

## Infix matching

You can also use `%` to match any sequence of text between a given pattern. For example, to get all words starting with `ca` and ending in `ly`, execute the following:

```sql
SELECT word FROM words WHERE word LIKE 'ca%ly' limit 5;
```

```output
     word
--------------
 capably
 cathodically
 casually
 captainly
 callously
```

## Case insensitive matching

The `LIKE` operator performs case-sensitive matching. For example, if you change the pattern to uppercase, you may not get the same results.

```sql
SELECT word FROM words WHERE word LIKE 'C_T' limit 5;
```

```output
 word
------
(0 rows)
```

To support case-insensitive matching, use the `ILIKE` operator.

```sql
SELECT word FROM words WHERE word ILIKE 'C_T' limit 5;
```

```output
 word
------
 cit
 cot
 cut
 cat
 cit
```

## Regex matching

Use the `SIMILAR TO` operator to match patterns using the SQL standard's definition of a regular expression. SQL regular expressions are a cross between `LIKE` notation and common (`POSIX`) regular expression notation.

For example, to find all words that have `e` occurring three or more times consecutively, do the following:

```sql
SELECT word FROM words WHERE word SIMILAR TO '%e{2,}%' ;
```

```output
  word
---------
 peer
 green
 seer
 lychees
 deer
 teen
 breeze
 career
```

`SIMILAR TO` supports the following pattern-matching meta-characters:

- `|` denotes alternation (either of two alternatives).
- `*` denotes repetition of the previous item zero or more times.
- `+` denotes repetition of the previous item one or more times.
- `?` denotes repetition of the previous item zero or one time.
- `{m}` denotes repetition of the previous item exactly m times.
- `{m,}` denotes repetition of the previous item m or more times.
- `{m,n}` denotes repetition of the previous item at least m and not more than n times.

Use parentheses `()` to group items into a single logical item. A bracket expression `[...]` specifies a character class, just as in POSIX regular expressions.

## Single character matching

Use `_`(underscore) to match any single character. To get all the 3 letter words that start with `c` and end in `t`, execute the following:

```sql
SELECT word FROM words WHERE word LIKE 'c_t' limit 5;
```

```output
 word
------
 cit
 cot
 cut
 cat
 cit
```

## Learn more

- [Understand GIN indexes](../../../../explore/indexes-constraints/gin/)
- [Advanced fuzzy matching in YugabyteDB](https://www.yugabyte.com/blog/fuzzy-matching-in-yugabytedb/)
- [Optimizing LIKE/ILIKE with indexes](https://www.yugabyte.com/blog/postgresql-like-query-performance-variations/)