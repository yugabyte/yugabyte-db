---
title: Troubleshoot YugabyteDB Voyager
headerTitle: Troubleshoot
linkTitle: Troubleshoot
description: Troubleshoot issues in YugabyteDB Voyager.
headcontent: Diagnose and troubleshoot issues when migrating to YugabyteDB using YugabyteDB Voyager
menu:
  stable_yugabyte-voyager:
    identifier: voyager-troubleshoot
    parent: yugabytedb-voyager
    weight: 105
type: docs
---

## Schema migration

### Invalid constraint type with PostgreSQL 17

When running the [assess migration](../reference/assess-migration/#assess-migration) or [export schema](../reference/schema-migration/export-schema/) commands on a PostgreSQL 17.0–17.2 source database, you may see the following error:

```output
pg_dump: error: query failed: ERROR:  invalid constraint type "n"
pg_dump: detail: Query was: EXECUTE getDomainConstraints('16452')
```

This error occurs when using pg_dump version 17.6 with a PostgreSQL source database running versions 17.0, 17.1, or 17.2 that contains domain objects with NOT NULL constraints.
A bug in PostgreSQL versions 17.0-17.2 causes `pg_get_constraintdef()` to not interpret NOT NULL domain constraints, leading to failure in pg_dump version 17.6 when using this function to fetch schema information.

- Use an older version of pg_dump.

    Use pg_dump version 17.5 or earlier as follows:

    1. Install the pg_dump version. The best practice is to use a pg_dump version that matches your PostgreSQL server version (for example, use pg_dump 17.2 with PostgreSQL 17.2).
    1. Ensure this version is the first pg_dump executable in your system's PATH before you run Voyager.

- Upgrade PostgreSQL.

    Upgrade your PostgreSQL source database to version 17.3 or later, as the bug has been fixed in these releases.