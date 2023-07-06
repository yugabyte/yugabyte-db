---
title: Text Search in YSQL
headerTitle: Text Search
linkTitle: Text Search
headcontent: Learn how to search text data types in YSQL
description: Learn how to search text data types in YSQL
menu:
  preview:
    identifier: text-search
    parent: learn
    weight: 100
type: indexpage
---

Text search has existed in databases since the inception of databases with the `=`, `LIKE` and `ILIKE` operators. These typically work by checking if the given pattern is present in the document. Although this is useful in many scenarios, it has some serious fallbacks when compared to modern search expectations like:

- It does not have any sort of ordering of the results
- There is no index support, so have to scan all rows and hence are slow
- It is cumbersome to search for multiple patterns and needs strict ordering of search terms

The full-text search brings in a whole new paradigm of pre-processing of data, enabling parsing the data into tokens, converting tokens to lexemes(c.f. stemming) and storing the processed data in a manner that is optimal for search(eg: inverted indexes). It also adds new types of pattern matching like prefix match, suffix match, fuzzy match etc.

Let's look into the different methods and features that YugabyteDB provides for searching text.

## Pattern Matching

Pattern matching is accomplished by specifying a pattern using the `%, _, *, .` characters and using the `LIKE`, `ILIKE` and `~` operators. The patterns can range from simple prefix/suffix expressions to complex regular expressions. For example:

```sql{class=nocopy}
'abc' LIKE 'a%'     true
'abc' LIKE '_bc'    true
'abc' LIKE 'c'      false
'abc' SIMILAR TO '%(b|d)%'      true
'abc' ~ 'a.*c'   true
```

{{<tip>}}
To learn more about pattern matching, see [Pattern Matching](./pattern-matching)
{{</tip>}}

## Full-text search

Almost all of today's search engines use [Inverted Indexes](https://en.wikipedia.org/wiki/Inverted_index) extensively. Simply put, an inverted index parses a document and stores the individual words(a.k.a tokens) and their corresponding position in the document. For example,

```sql{class=nocopy}
'The quick brown fox jumps jumps over the lazy dog'
```

would be parsed as,

```sql{class=nocopy}
 'brown':3 'dog':10 'fox':4 'jump':5,6 'lazi':9 'quick':2
```

This enables a user to search for the document which has `fox` and `quick` or `jumping dog`.

{{<tip>}}
To learn more about pattern matching, see [Full-Text Search](./full-text-search)
{{</tip>}}

## Phonetic search

In the case where we do not know the exact search term and want to find similar items or documents that sound similar to a specific term, _fuzzy_ or _phonetic_ search would come in handy. YugabyteDB supports fuzzy search like Soundex, Metaphone via Postgres extensions. For example, to find words that sound like `anapistagafi`, you could execute,

```sql
select word from words where dmetaphone(word) = dmetaphone('anapistagafi') limit 5;
```

{{<tip>}}
To learn more about pattern matching, see [Phonetic Search](./phonetic-matching)
{{</tip>}}

## Learn more

{{<index/block>}}

{{<index/item
    title="Pattern Match"
    body="Search text based on a text pattern"
    href="./pattern-matching"
    icon="fa-solid fa-equals">}}
{{<index/item
    title="Phonetic Match"
    body="Search text based on sound and similarity"
    icon="fa-solid fa-music"
    href="phonetic-matching">}}
{{<index/item
    title="Full-Text Search"
    body="Search engine like search"
    icon="fa-brands fa-searchengin"
    href="full-text-search">}}

{{</index/block>}}
