---
title: Spring Data YugabyteDB
linkTitle: Spring Data YugabyteDB
description: Spring Data YugabyteDB
aliases:
menu:
  latest:
    identifier: sdyb
    parent: spring-framework
    weight: 578
isTocNested: true
showAsideToc: true
---

Spring Data modules are used for accessing databases and performing various tasks via Java APIs, therefor eliminating the need to learn a database-specific query language.

Spring Data YugabyteDB (SDYB) modules provide support for YSQL APIs and enable you to build cloud-native applications.

SDYB YSQL includes the following features:

- Spring Data `YsqlTemplate` and `YsqlRepository` for querying YugabyteDB
- `@EnableYsqlRepostiory` annotation
- Support for YugabyteDB Smart JDBC Driver
- Spring starter support for YugabyteDB



------------------------------------------

- Annotations for entity and field mapping with support for:
  - Primary keys with with `HASH`, `ASC`, and `DESC` options
  - Compound keys
  - Data types
  - `SPLIT INTO` and `SPLIT AT` clauses
  - `UUID`, `TIMEUUID`, and JSON version for generated key columns

- The template and repository with support for:
  - Read and write isolation
  - Follower reads.
  - Transactions.
  - YugabyteDB Smart Driver.
- Auto-reconfiguration with `spring.data.yugabytedb.ysql.*`

-----------------------------------------------------------



The project definition includes the following dependency:

```properties
<dependency>
    <groupId>com.yugabyte</groupId>
    <artifactId>spring-data-yugabytedb-ysql</artifactId>
    <version>1.0.0.RELEASE</version>
</dependency>
```

