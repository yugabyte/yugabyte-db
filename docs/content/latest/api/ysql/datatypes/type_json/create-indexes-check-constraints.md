---
title: Indexes and check constraints
linkTitle: Indexes and check constraints
summary: Create indexes and check constraints on JSON columns
headerTitle: Create indexes and check constraints on JSON columns
description: Create indexes and check constraints on JSON columns
menu:
  latest:
    identifier: create-indexes-check-constraints
    parent: api-ysql-datatypes-json
    weight: 50
isTocNested: true
showAsideToc: true
---

Often, when  JSON documents are inserted into a table, the table will have just a self-populating surrogate primary key column and a value column, like `v`, of datatype `jsonb`. Choosing `jsonb` allows the use of a broader range of operators and functions, and allows these to execute more efficiently, than does choosing `json`.

It's most likely that each document will be a JSON _object_ and that all will conform to the same structure definition. In other words, each _object_ will have the same set of possible key names (but some might be missing) and the same JSON datatype for the value for each key. And when a datatype is compound, the same notion of common structure definition will apply, extending the notion recursively to arbitrary depth. Here is an example. To reduce clutter, the primary key is not defined to be self-populating. 

```postgresql
create table books(k int primary key, json_doc jsonb not null);

insert into books(k, json_doc) values
  (1,
  '{ "ISBN"    : 4582546494267,
     "title"   : "Macbeth", 
     "author"  : {"given_name": "William", "family_name": "Shakespeare"},
     "year"    : 1623}'),

  (2,
  '{ "ISBN"    : 8760835734528,
     "title"   : "Hamlet",
     "author"  : {"given_name": "William", "family_name": "Shakespeare"},
     "year"    : 1603,
     "editors" : ["Lysa", "Elizabeth"] }'),

  (3,
  '{ "ISBN"    : 7658956876542,
     "title"   : "Oliver Twist",
     "author"  : {"given_name": "Charles", "family_name": "Dickens"},
     "year"    : 1838,
     "genre"   : "novel",
     "editors" : ["Mark", "Tony", "Britney"] }'),
  (4,
  '{ "ISBN"    : 9874563896457,
     "title"   : "Great Expectations",
     "author"  : {"family_name": "Dickens"},
     "year"    : 1950,
     "genre"   : "novel",
     "editors" : ["Robert", "John", "Melisa", "Elizabeth"] }'),

  (5,
  '{ "ISBN"    : 8647295405123,
     "title"   : "A Brief History of Time",
     "author"  : {"given_name": "Stephen", "family_name": "Hawking"},
     "year"    : 1988,
     "genre"   : "science",
     "editors" : ["Melisa", "Mark", "John", "Fred", "Jane"] }'),

  (6,
  '{
    "ISBN"     : 6563973589123,
    "year"     : 1989,
    "genre"    : "novel",
    "title"    : "Joy Luck Club",
    "author"   : {"given_name": "Amy", "family_name": "Tan"},
    "editors"  : ["Ruilin", "Aiping"]}');
```

Some of the rows have some of the keys missing. But the row with `k = 6` has every key.

You will probably want at least to know if your corpus contains a non-conformant document and, in some cases, you will want to disallow non-conformant documents. You might want to insist that the ISBN is always defined and is a positive 13-digit number.

You will almost certainly want to retrieve documents, not simply by providing the key, but rather by using predicates on their content—the primitive values that they contain. You will probably want, also, to project out values of interest.

For example, you might want to see the title and author of books whose publication year is later than 1850.

Of course, then, you will want these queries to be supported by indexes. (The alternative – a table scan over a huge corpus where each document is analyzed on the fly to evaluate the selection predicates is simply unworkable.)

### Check constraints on _jsonb_ columns

Coming soon.

### Indexes on _jsonb_ columns

Coming soon.
