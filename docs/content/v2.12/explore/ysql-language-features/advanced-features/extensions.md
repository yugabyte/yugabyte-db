---
title: Extensions
headerTitle: Pre-bundled extensions
linkTitle: Extensions
description: Pre-bundled extensions for YugabyteDB
image: /images/section_icons/explore/monitoring.png
menu:
  v2.12:
    identifier: advanced-features-extensions
    parent: advanced-features
    weight: 245
type: docs
---

YugabyteDB comes bundled with a number of [PostgreSQL extensions](../../../../api/ysql/extensions/#pre-bundled-extensions) that are tested to work with YSQL. We're incrementally developing support for as many extensions as possible. (Since YugabyteDB's underlying storage architecture is not the same as PostgreSQL, many PostgreSQL extensions, particularly those that interact with the storage layer, won't work as-is on YugabyteDB.)

## Pre-bundled extensions

The following extensions are bundled with YugabyteDB. Click an extension's name for more detailed documentation and example code.

* [**fuzzystrmatch**](../../../../api/ysql/extensions/#fuzzystrmatch) provides functions to determine similarities and distance between strings.

<!--
* [orafce](/preview/api/ysql/extensions/#orafce) provides compatibility with Oracle functions and packages that are either missing or implemented differently in YugabyteDB and PostgreSQL. This compatibility layer can help you port your Oracle applications to YugabyteDB.
-->

* [**pg_stat_statements**](../../../../api/ysql/extensions/#pg-stat-statements) lets you track execution statistics for all SQL statements executed by a server.

* [**pgAudit**](../../../../secure/audit-logging/audit-logging-ysql/) allows you to collect detailed session and object audit logging via YugabyteDB TServer logging. Audit logs are often required as part of government, financial, or other certifications, such as ISO.

* [**pgcrypto**](../../../../api/ysql/extensions/#pgcrypto) provides cryptographic functions, including hashing, encryption, and decryption functions.

* [**SPI**, the server programming interface](../../../../api/ysql/extensions/#server-programming-interface-spi-module) module, allows you to create functions and stored procedures in C.

* [**uuid-ossp**](../../../../api/ysql/extensions/#uuid-ossp) provides functions to generate universally unique identifiers (UUIDs), and functions to produce certain special UUID constants.

## Other verified extensions

The following extensions are verified to work with YugabyteDB, but aren't pre-bundled. Click an extension's name for detailed installation and usage documentation.

* [**PostGIS**](../../../../api/ysql/extensions/#postgis) is a spatial database extender for PostgreSQL-compatible object-relational databases. (Note that YugabyteDB does not currently support GiST indexes.)
* [**postgresql-hll**](../../../../api/ysql/extensions/#postgresql-hll-postgresql-extension-for-hyperloglog) provides support for HyperLogLog, a fixed-size, set-like structure used for distinct value counting with tunable precision.
