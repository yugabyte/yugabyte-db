---
title: PostgreSQL extensions
headerTitle: PostgreSQL extensions
linkTitle: PostgreSQL extensions
description: List of supported PostgreSQL extensions
summary: Reference for YSQL extensions
menu:
  latest:
    identifier: api-ysql-postgresql-extensions
    parent: api-ysql
    weight: 4400
isTocNested: true
showAsideToc: true
---

PostgreSQL provides a way to extend the functionality of a database by bundling SQL objects into a package and using them as a unit. 
This page describes the PostgreSQL extensions and describes the extensions supported by YugabyteDB.


# PostgreSQL extensions supported by YugabyteDB
For information about using a specific extension, see the documentation link in one of the tables below.
 Extension | Status |  Description
-----------|-----------|---------
[fuzzystrmatch](https://www.postgresql.org/docs/current/fuzzystrmatch.html)  | Bundled | The fuzzystrmatch module provides several functions to determine similarities and distance between strings.
[orafce](https://github.com/orafce/orafce)| Bundled |The orafce extension provides compatibility with Oracle functions and packages that are either missing or implemented differently in YugabyteDB and PostgreSQL. This compatibility layer can help you port your Oracle applications to YugabyteDB.
[pgcrypto](https://www.postgresql.org/docs/current/pgcrypto.html)| Bundled |The pgcrypto extension provides various cryptographic functions.
pg_stat_statements| |
pg_hint_plan| |
pgaudit| |
pg_stat_monitor| |
spi module| |
uuid_ossp| |
hstore| |
pg_trgm| |
postgres_fdw| |
file_fdw| |
sslinfo| |

## Using PostgreSQL extensions
You can install only the extensions that are supported by YugabyteDB.

{{< note title="Note" >}}

You can only install extensions on the primary instance, not on the read replica. Once installed, the extension replicates to the read replica.

{{< /note >}}

### Installing an extension
Before using an extension, install it:
In the ysql shell, run the CREATE EXTENSION command.
### Requirement for superuser privileges
Extensions can only be created by users that are part of the superuser role?

### Inter-database connections
The target instances for connections must be in the same VPC network as the connecting instance.

## Requesting support for a new extension
You cannot create your own extensions in YugabyteDB.

To request support for an extension, add a Me, too! vote for its issue in the Issue Tracker, or create a new issue there.
