---
title: PostgreSQL extensions
headerTitle: PostgreSQL extensions
linkTitle: PostgreSQL extensions
description: PostgreSQL extensions
menu:
  v2025.1:
    identifier: explore-pg-extensions
    parent: explore-ysql-language-features
    weight: 1000
rightNav:
  hideH3: true
type: docs
---

PostgreSQL extensions provide a way to extend the functionality of a database by bundling SQL objects into a package and using them as a unit. YugabyteDB supports a variety of popular PostgreSQL extensions. Pre-bundled modules and extensions cover security/auditing (PGAudit, passwordcheck), scheduling (pg_cron), performance insight and tuning (pg_stat_statements, pg_stat_monitor, auto_explain, HypoPG, pg_hint_plan), data modeling and analytics (hstore, tablefunc, cube, earthdistance, HLL), partitioning (pg_partman), FDWs (file_fdw, postgres_fdw), and AI/vector search (pgvector).

Supported extensions are typically enabled in YSQL by running the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement.

For example, to enable the [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/):

```sql
CREATE EXTENSION vector;
```

You can then use the extension as you would in PostgreSQL.

{{<lead link="../../../additional-features/pg-extensions/">}}
For information on the extensions bundled with YugabyteDB, see [PostgreSQL Extensions](../../../additional-features/pg-extensions/)
{{</lead>}}
