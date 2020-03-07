---
title: Indexes and check constraints
linktitle: Indexes and check constraints
summary: Create indexes and check constraints on JSON columns
description: Create indexes and check constraints on JSON columns
menu:
  latest:
    identifier: to-jsonb
    parent: functions-operators
isTocNested: true
showAsideToc: true
---

Often, when JSON documents are inserted into a table, the table will have just the columns `k` (as a self-populating surrogate primary key) and `v` of data type `jsonb`. This choice allows the use of a broader range of operators and functions, and allows these to execute more efficiently, then when a `json` column is used.

It's most likely that each document will be a JSON _object_ and that all will conform to the same structure definition. In other words, each _object_ will have the same set of possible key names (but some might be missing) and the same JSON data type for the value for each key. And when a data type is compound, the same notion of common structure definition will apply, extending the notion recursively to arbitrary depth. Here is an example. To reduce clutter, primary key is not defined to be self-populating.

```postgresql
create table books(k int primary key, json_doc jsonb not null);

insert into books(k, json_doc) values
  (1,
  '{ "ISBN"   " 1234567890123",
     "title"  : "Macbeth", 
     "author" : {"given_name": "William", "family_name": "Shakespeare"},
     "year"   : 1623}'),

  (2,
  '{ "title"  : "Hamlet",
     "author" : {"given_name": "William", "family_name": "Shakespeare"},
     "year"   : 1603,
     "editors": ["Lysa", "Elizabeth"] }'),

  (3,
  '{ "title"  : "Oliver Twist",
     "author" : {"given_name": "Charles", "family_name": "Dickens"},
     "year"   : 1838,
     "genre"  : "novel",
     "editors": ["Mark", "Tony", "Britney"] }'),
  (4,
  '{ "title"  : "Great Expectations",
     "author" : {"family_name": "Dickens"},
     "year"   : 1950,
     "genre"  : "novel",
     "editors": ["Robert", "John", "Melisa", "Elizabeth"] }'),

  (5,
  '{ "title"  : "A Brief History of Time",
     "author" : {"given_name": "Stephen", "family_name": "Hawking"},
     "year"   : 1988,
     "genre"  : "science",
     "editors": ["Melisa", "Mark", "John", "Fred", "Jane"] }'),

  (6,
  '{
    "year": 1989,
    "genre": "novel",
    "title": "Joy Luck Club",
    "author": {"given_name": "Amy", "family_name": "Tan"},
    "editors": ["Ruilin", "Jueyin"]}');
```

Some of the rows have some of the keys missing. But the row with `k = 6` has every key.

You will probably want at least to know if your corpus contains a nonconformant document and, in some cases, that you will want to disallow nonconformant documents.

You will almost certainly want to retrieve documents, not simply by providing the key, but rather by using predicates on their content—the primitive values that they contain. You will probably want, also, to project out values of interest.

For example, a probable query will be "Show me the books whose hire publication year is between  and whose phone number list includes a _US_ number with area code _415_.

Of course, then, you will want these queries to be supported by indexes. (The alternative – a table scan over a huge corpus where each document is analyzed on the fly to evaluate the selection predicates is simply unworkabåle.)

### Indexes on _jsonb_ columns

Coming soon.

### Check constraints  on _jsonb_ columns

Coming soon.
