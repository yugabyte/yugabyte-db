---
title: gen_random_uuid() function [YSQL]
headerTitle: gen_random_uuid()
linkTitle: gen_random_uuid()
description: Generate a UUID.
menu:
  preview_api:
    identifier: api-ysql-exprs-gen-random-uuid
    parent: api-ysql-exprs
    weight: 20
type: docs
---

## Synopsis

Use the `gen_random_uuid()` function to generate a UUID.

This function returns a version 4 (random) UUID. This is the most commonly used type of UUID and is appropriate for most applications.

The [uuid-ossp](../../../../explore/ysql-language-features/pg-extensions/extension-uuid-ossp) and [pgcrypto](../../../../explore/ysql-language-features/pg-extensions/extension-pgcrypto) modules provide additional functions that implement other standard algorithms for generating UUIDs.

## Example

You can generate a UUID and check its type using the `pg_typeof` function.

```sql
select gen_random_uuid(), pg_typeof(gen_random_uuid());
```

You will get an output similar to:

```caddyfile
           gen_random_uuid            | pg_typeof
--------------------------------------+-----------
 15684ea8-0505-4fb3-b3f7-1be2f253e0ed | uuid
```

## Learn more

- [uuid-ossp](../../../../explore/ysql-language-features/pg-extensions/extension-uuid-ossp) module
