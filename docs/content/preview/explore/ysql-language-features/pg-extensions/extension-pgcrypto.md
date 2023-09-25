---
title: pgcrypto extension
headerTitle: pgcrypto extension
linkTitle: pgcrypto
description: Using the pgcrypto extension in YugabyteDB
menu:
  preview:
    identifier: extension-pgcrypto
    parent: pg-extensions
    weight: 20
type: docs
---

The [pgcrypto](https://www.postgresql.org/docs/11/pgcrypto.html) module provides cryptographic functions for PostgreSQL.

```sql
CREATE EXTENSION pgcrypto;
CREATE TABLE pgcrypto_example(id uuid PRIMARY KEY DEFAULT gen_random_uuid(), content text, digest text);
INSERT INTO pgcrypto_example (content, digest) values ('abc', digest('abc', 'sha1'));

SELECT * FROM pgcrypto_example;
```

```output
                  id                  | content |                   digest
--------------------------------------+---------+--------------------------------------------
 b8f2e2f7-0b8d-4d26-8902-fa4f5277869d | abc     | \xa9993e364706816aba3e25717850c26c9cd0d89d
(1 row)
```
