---
title: PostgreSQL extensions
headerTitle: PostgreSQL extensions
linkTitle: PostgreSQL extensions
description: Summary of supported PostgreSQL extensions
menu:
  stable:
    identifier: explore-pg-extensions
    parent: explore-ysql-language-features
    weight: 1000
rightNav:
  hideH3: true
type: docs
---

PostgreSQL extensions provide a way to extend the functionality of a database by bundling SQL objects into a package and using them as a unit. YugabyteDB supports a variety of PostgreSQL extensions.

Most supported extensions are pre-bundled with YugabyteDB and can be enabled in YSQL by running the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement.

For example, to enable the [pgvector extension](../../../additional-features/pg-extensions/extension-pgvector/):

```sql
CREATE EXTENSION vector;
```

You can then use the extension as you would in PostgreSQL.

For example, create a vector column with 3 dimensions:

```sql
CREATE TABLE items (id bigserial PRIMARY KEY, embedding vector(3));
```

Insert vectors:

```sql
INSERT INTO items (embedding) VALUES ('[1,2,3]'), ('[4,5,6]');
```

Get the nearest neighbors by L2 distance:

```sql
SELECT * FROM items ORDER BY embedding <-> '[3,1,2]' LIMIT 5;
```

You can install only extensions that are supported by YugabyteDB. If you are interested in an extension that is not yet supported, contact {{% support-general %}}, or [reach out on Slack](https://yugabyte-db.slack.com/).

{{<lead link="../../../additional-features/pg-extensions/">}}
For information on supported extensions, see [PostgreSQL Extensions](../../../additional-features/pg-extensions/)
{{</lead>}}
