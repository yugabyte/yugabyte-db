---
title: Pattern Matching in YSQL
headerTitle: Pattern Matching
linkTitle: Pattern Matching
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
CREATE TABLE IF NOT EXISTS words (
    id SERIAL,
    word TEXT NOT NULL,
    PRIMARY KEY(id)
);
```

Let us load some sample words into the table.

```sql
INSERT INTO words(word) VALUES 
  ('camp'),('carousel'),('cartel'),('carpet'),('carnivore'),('cartoon'),('carry'),('capsule'),
  ('corsica'),('medica'),('azteca'),('republica'),('chronica'),('orca'),('cathodically'),('capably'),
  ('cot'),('cat'),('cut'),('cwt'),('cit'),('cit'),('captainly'),('callously'),('career'),('calculate'),
  ('lychees'),('deer'),('peer'),('seer'),('breeze'),('green'),('teen'),('casually');
```

## Suffix matching

The `%` could be added to the end of a pattern to match any string that completes the given pattern. For example, to get all the words starting with `ca`, you would execute:

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

The `%` could be added to the beginning of a pattern to match any string that ends in the given pattern. For example, to get words ending with `box`, you would execute:

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

The `%` could also be used to match any sequence of text between a given pattern. For example, to get all words starting with `ca` and ending in `ly`, you could execute,

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

The `LIKE` operator performs case-sensitive matching. For example, if we change our pattern to uppercase, we may not get the same results.

```sql
SELECT word FROM words WHERE word LIKE 'C_T' limit 5;
```

```output
 word
------
(0 rows)
```

To support case-insensitive matching, you would need to use the `ILIKE` operator.

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

The `SIMILAR TO` operator can be used to match patterns using the SQL standard's definition of a regular expression. SQL regular expressions are a curious cross between `LIKE` notation and common (`POSIX`) regular expression notation. To find all words that 
have `e` occurring three or more times consecutively,

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

`SIMILAR TO` supports these pattern-matching metacharacters:

- `|` denotes alternation (either of two alternatives).
- `*` denotes repetition of the previous item zero or more times.
- `+` denotes repetition of the previous item one or more times.
- `?` denotes repetition of the previous item zero or one time.
- `{m}` denotes repetition of the previous item exactly m times.
- `{m,}` denotes repetition of the previous item m or more times.
- `{m,n}` denotes repetition of the previous item at least m and not more than n times.

Parentheses `()` can be used to group items into a single logical item. A bracket expression `[...]` specifies a character class, just as in POSIX regular expressions.

## Single character matching

You can use `_`(underscore) to match any single character. To get all the 3 letter words that start with `c` and end in `t`, you would execute,

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