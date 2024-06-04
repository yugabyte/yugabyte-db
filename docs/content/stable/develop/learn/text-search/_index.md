---
title: Text Search in YSQL
headerTitle: Text search
linkTitle: Text search
headcontent: Learn how to search text data types in YSQL
description: Learn how to search text data types in YSQL
menu:
  stable:
    identifier: text-search
    parent: learn
    weight: 150
type: indexpage
---

Basic text search uses pattern matching, using the `=`, `LIKE`, and `ILIKE` operators to check if a given pattern is present in the document. While this is adequate for many scenarios, it has the following shortcomings:

- Results aren't ordered.
- No index support, so all rows must be scanned, resulting in slower performance.
- Searching for multiple patterns is cumbersome and needs strict ordering of search terms.

Full-text search brings in a whole new paradigm of pre-processing of data, enabling parsing the data into tokens, converting tokens to lexemes (c.f. stemming) and storing the processed data in a manner that is optimal for search (for example, inverted indexes). It also adds new types of pattern matching, like prefix match, suffix match, fuzzy match, and so on.

YugabyteDB provides the following methods and features for searching text.

## Pattern matching

Pattern matching is accomplished by specifying a pattern using the `%, _, *, .` characters and using the `LIKE`, `ILIKE`, and `~` operators. The patterns can range from basic prefix/suffix expressions to complex regular expressions. For example:

```sql{.nocopy}
'abc' LIKE 'a%'             --> true
'abc' LIKE '_bc'            --> true
'abc' LIKE 'c'              --> false
'abc' SIMILAR TO '%(b|d)%'  --> true
'abc' ~ 'a.*c'              --> true
```

{{<lead link="./pattern-matching">}}
To learn more about pattern matching, see [Pattern matching](./pattern-matching).
{{</lead>}}

## Similarity search

Similarity matching works by determining how similar two strings are by taking into account how many letters are different and how many occur together. Use similarity search when you don't know the exact spelling of your query term. Similarity search can be used to design spell checkers.

For example, the distance between `warehoused` and `warehouse` is `1`, as it has one additional character (`d`) than `warehouse`.

{{<lead link="./similarity-matching">}}
To learn more about similarity search, see [Similarity search](./similarity-matching).
{{</lead>}}

## Full-text search

Almost all of today's search engines use [Inverted indexes](https://en.wikipedia.org/wiki/Inverted_index) extensively. An inverted index parses a document and stores the individual words (that is, tokens) and their corresponding position in the document. For example:

```sql{.nocopy}
'The quick brown fox jumps jumps over the lazy dog'
```

would be parsed as follows:

```sql{.nocopy}
 'brown':3 'dog':10 'fox':4 'jump':5,6 'lazi':9 'quick':2
```

This enables you to search for the document which has `fox` and `quick` or `jumping dog`.

{{<lead link="./full-text-search">}}
To learn more about full-text search, see [Full-text search](./full-text-search).
{{</lead>}}

## Phonetic search

In the case where you do not know the exact search term and want to find similar items or documents that sound similar to a specific term, _fuzzy_ or _phonetic_ search would come in handy. YugabyteDB supports fuzzy search like Soundex, Metaphone via PostgreSQL extensions. For example, to find words that sound like `anapistagafi`, you could execute,

```sql
select word from words where dmetaphone(word) = dmetaphone('anapistagafi') limit 5;
```

{{<lead link="./phonetic-matching">}}
To learn more about pattern matching, see [Phonetic Search](./phonetic-matching)
{{</lead>}}

## Learn more

{{<index/block>}}

{{<index/item
    title="Pattern matching"
    body="Search text based on a text pattern."
    href="./pattern-matching"
    icon="fa-solid fa-equals">}}

{{<index/item
    title="Similarity search"
    body="Find similar words."
    icon="fa-solid fa-equals"
    href="similarity-matching">}}

{{<index/item
    title="Full-Text Search"
    body="Search engine like search"
    icon="fa-brands fa-searchengin"
    href="full-text-search">}}

{{<index/item
    title="Phonetic Search"
    body="Search text based on sound and similarity"
    icon="fa-solid fa-music"
    href="phonetic-matching">}}

{{</index/block>}}
