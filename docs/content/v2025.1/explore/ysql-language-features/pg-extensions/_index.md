---
title: PostgreSQL extensions
headerTitle: PostgreSQL extensions
linkTitle: PostgreSQL extensions
description: Summary of supported PostgreSQL extensions
summary: Reference for YSQL extensions
menu:
  v2025.1:
    identifier: pg-extensions
    parent: explore-ysql-language-features
    weight: 1000
showRightNav: true
type: indexpage
---

PostgreSQL extensions provide a way to extend the functionality of a database by bundling SQL objects into a package and using them as a unit. YugabyteDB supports a variety of PostgreSQL extensions.

Supported extensions are either pre-bundled with YugabyteDB or require installation:

* **Pre-bundled** extensions are included in the standard YugabyteDB distribution and can be enabled in YSQL by running the [CREATE EXTENSION](../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement.
* **Requires installation** - you must install these extensions manually before you can enable them using CREATE EXTENSION. Refer to [Install extensions](install-extensions/).

You can install only extensions that are supported by YugabyteDB. If you are interested in an extension that is not yet supported, contact {{% support-general %}}, or [reach out on Slack](https://yugabyte-db.slack.com/).

## Supported extensions

### PostgreSQL modules

YugabyteDB supports the following [PostgreSQL modules](https://www.postgresql.org/docs/15/contrib.html). All of these modules are pre-bundled.

| Module | Description |
| :----- | :---------- |
| [auto_explain](extension-auto-explain/) | Provides a means for logging execution plans of slow statements automatically. |
| cube| Implements a data type cube for representing multidimensional cubes. <br/>For more information, see [cube](https://www.postgresql.org/docs/15/cube.html) in the PostgreSQL documentation. |
| earthdistance| Provides two different approaches to calculating great circle distances on the surface of the Earth. <br/>For more information, see [earthdistance](https://www.postgresql.org/docs/15/earthdistance.html) in the PostgreSQL documentation. |
| [file_fdw](extension-file-fdw/) | Provides the foreign-data wrapper file_fdw, which can be used to access data files in the server's file system. |
| [fuzzystrmatch](extension-fuzzystrmatch/) | Provides several functions to determine similarities and distance between strings. |
| hstore | Implements the hstore data type for storing sets of key-value pairs in a single PostgreSQL value.<br/>For more information, see [hstore](https://www.postgresql.org/docs/15/hstore.html) in the PostgreSQL documentation. |
| [passwordcheck](extension-passwordcheck/) | Checks user passwords whenever they are set with CREATE ROLE or ALTER ROLE. If a password is considered too weak, it is rejected. |
| [pgcrypto](extension-pgcrypto/) | Provides various cryptographic functions. |
| [pg_stat_statements](extension-pgstatstatements/) | Provides a means for tracking execution statistics of all SQL statements executed by a server. |
| [pg_trgm](../../../develop/learn/text-search/similarity-matching/#trigrams) | Provides functions and operators for determining the similarity of alphanumeric text based on trigram matching, as well as index operator classes that support fast searching for similar strings.<br/>For more information, see [pg_trgm](https://www.postgresql.org/docs/15/pgtrgm.html) in the PostgreSQL documentation. |
| [postgres_fdw](extension-postgres-fdw/) | Provides the foreign-data wrapper postgres_fdw, which can be used to access data stored in external PostgreSQL servers. |
| [spi](extension-spi/) | Lets you use the Server Programming Interface (SPI) to create user-defined functions and stored procedures in C, and to run YSQL queries directly against YugabyteDB. |
| sslinfo | Provides information about the SSL certificate that the current client provided when connecting to PostgreSQL.<br/>For more information, see [sslinfo](https://www.postgresql.org/docs/15/sslinfo.html) in the PostgreSQL documentation. |
| [tablefunc](extension-tablefunc/) | Provides several table functions. For example, `normal_rand()` creates values, picked using a pseudorandom generator, from an ideal normal distribution. You specify how many values you want, and the mean and standard deviation of the ideal distribution. You use it in the same way that you use `generate_series()` |
| [uuid-ossp](extension-uuid-ossp/) | Provides functions to generate universally unique identifiers (UUIDs), and functions to produce certain special UUID constants. |

### Other extensions

YugabyteDB supports the following additional extensions, some of which you must install manually.

| <div style="width:120px">Extension</div> | <div style="width:100px">Status</div> | Description |
| :-------- | :----- | :---------- |
| [Anonymizer](extension-pganon/) {{<tags/feature/ea idea="1497">}}| Pre-bundled | Mask or replace personally identifiable information (PII) or commercially sensitive data in a database. |
| [HypoPG](extension-hypopg/) | Pre-bundled | Create hypothetical indexes to test whether an index can increase performance for problematic queries without consuming any actual resources. |
| Orafce | Pre-bundled | Provides compatibility with Oracle functions and packages that are either missing or implemented differently in YugabyteDB and PostgreSQL. This compatibility layer can help you port your Oracle applications to YugabyteDB.<br/>For more information, see the [Orafce](https://github.com/yugabyte/yugabyte-db/blob/master/src/postgres/third-party-extensions/orafce/README.asciidoc) documentation. |
| [PGAudit](../../../secure/audit-logging/audit-logging-ysql/) | Pre-bundled | The PostgreSQL Audit Extension (pgaudit) provides detailed session and/or object audit logging via the standard PostgreSQL logging facility. |
| [pg_cron](extension-pgcron/) | Pre-bundled | Provides a cron-based job scheduler that runs inside the database. |
| [pg_hint_plan](../../query-1-performance/pg-hint-plan/) | Pre-bundled | Tweak execution plans using "hints", which are descriptions in the form of SQL comments.<br/>For more information, see the [pg_hint_plan](https://pghintplan.osdn.jp/pg_hint_plan.html) documentation. |
| [pg_partman](extension-pgpartman/) | Pre-bundled | Creates and manages time-based and serial-based table partition sets.|
| pg_stat_monitor | Pre-bundled | A PostgreSQL query performance monitoring tool, based on the PostgreSQL pg_stat_statements module.<br/>For more information, see the [pg_stat_monitor](https://docs.percona.com/pg-stat-monitor/index.html) documentation. |
| [pgvector](extension-pgvector) | Pre-bundled | Allows you to store and query vectors, for use in vector similarity searching. |
| [postgresql-hll](extension-postgresql-hll) | Pre-bundled | Adds the data type `hll`, which is a HyperLogLog data structure. |
