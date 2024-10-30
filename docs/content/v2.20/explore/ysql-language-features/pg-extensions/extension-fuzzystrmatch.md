---
title: fuzzystrmatch extension
headerTitle: fuzzystrmatch extension
linkTitle: fuzzystrmatch
description: Using the fuzzystrmatch extension in YugabyteDB
menu:
  v2.20:
    identifier: extension-fuzzystrmatch
    parent: pg-extensions
    weight: 20
type: docs
---

The [fuzzystrmatch](https://www.postgresql.org/docs/11/fuzzystrmatch.html) module provides several functions to determine similarities and distance between strings.

```sql
CREATE EXTENSION fuzzystrmatch;

SELECT levenshtein('Yugabyte', 'yugabyte'), metaphone('yugabyte', 8);
```

```output
 levenshtein | metaphone
-------------+-----------
           2 | YKBT
(1 row)
```
