---
title: Extensions
headerTitle: Pre-bundled extensions
linkTitle: Extensions
description: Pre-bundled extensions for YugabyteDB
headcontent: Pre-bundled extensions for YugabyteDB
aliases:
image: /images/section_icons/explore/monitoring.png
menu:
  latest:
    identifier: explore-extensions
    parent: explore
    weight: 800
---

YugabyteDB comes bundled with a number of [PostgreSQL extensions](https://docs.yugabyte.com/latest/api/ysql/extensions/#pre-bundled-extensions) that are tested to work with YSQL. We're incrementally developing support for as many extensions as possible. (Since YugabyteDB’s underlying storage architecture is not the same as PostgreSQL, many PostgreSQL extensions, particularly those that interact with the storage layer, won't work as-is on YugabyteDB.)

## Pre-bundled extensions

The following extensions are bundled with YugabyteDB. Click an extension's name for more detailed documentation and example code.

* [**fuzzystrmatch**](/latest/api/ysql/extensions/#fuzzystrmatch) provides functions to determine similarities and distance between strings.

<!--
* [orafce](/latest/api/ysql/extensions/#orafce) provides compatibility with Oracle functions and packages that are either missing or implemented differently in YugabyteDB and PostgreSQL. This compatibility layer can help you port your Oracle applications to YugabyteDB.
-->

* [**pg_stat_statements**](/latest/api/ysql/extensions/#pg-stat-statements) lets you track execution statistics for all SQL statements executed by a server.

* [**pgAudit**](/latest/secure/audit-logging/audit-logging-ysql/) allows you to collect detailed session and/or object audit logging via YugabyteDB TServer logging. Audit logs are often required as part of government, financial, or other certifications, such as ISO.

* [**pgcrypto**](/latest/api/ysql/extensions/#pgcrypto) provides cryptographic functions, including hashing and encryption/decryption functions.

* [**SPI**, the server programming interface](/latest/api/ysql/extensions/#server-programming-interface-spi-module) module, provides

## Other verified extensions

The following extensions are verified to work with YugabyteDB, but aren't pre-bundled. Click an extension's name for detailed installation and usage documentation.

* [**PostGIS**](/latest/api/ysql/extensions/#postgis) . (Note that YugabyteDB does not currently support GiST indexes.)
* [**uuid-ossp**](/latest/api/ysql/extensions/#uuid-ossp)
