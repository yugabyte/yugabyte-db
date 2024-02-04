---
title: uuid-ossp extension
headerTitle: uuid-ossp extension
linkTitle: uuid-ossp
description: Using the uuid-ossp extension in YugabyteDB
menu:
  v2.18:
    identifier: extension-uuid-ossp
    parent: pg-extensions
    weight: 20
type: docs
---

The [uuid-ossp](https://www.postgresql.org/docs/11/uuid-ossp.html) module provides functions to generate universally unique identifiers (UUIDs) using one of several standard algorithms.

First, enable the extension:

```sql
CREATE EXTENSION "uuid-ossp";
```

Connect using `ysqlsh` and run the following:

```sql
SELECT uuid_generate_v1(), uuid_generate_v4(), uuid_nil();
```

```output
           uuid_generate_v1           |           uuid_generate_v4           |               uuid_nil
--------------------------------------+--------------------------------------+--------------------------------------
 69975ce4-d827-11e9-b860-bf2e5a7e1380 | 088a9b6c-46d8-4276-852b-64908b06a503 | 00000000-0000-0000-0000-000000000000
(1 row)
```
