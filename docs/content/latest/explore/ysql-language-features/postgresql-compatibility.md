---
title: PostgreSQL Compatibility
linkTitle: PostgreSQL Compatibility
description: Summary of YugabyteDB's PostgreSQL Compatibility
image: /images/section_icons/secure/create-roles.png
menu:
  latest:
    identifier: explore-ysql-postgresql-compatibility
    parent: explore-ysql-language-features
    weight: 70
isTocNested: true
showAsideToc: true
---
YugabyteDB is a PostgreSQL compatible distributed database that supports the majority of PostgreSQL syntax. This means that existing applications built on PostgreSQL can often be migrated to YugabyteDB without changing application code.

Since YugabyteDB is PostgreSQL compatible, it works with the majority of PostgreSQL database tools such as various language drivers, ORM tools, schema migration tools, and many more third-party database tools. 

Since YugabyteDB is a distributed database, supporting all PostgreSQL features easily in a distributed system is not always feasible. This page documents the known list of differences between PostgreSQL and YugabyteDB. You need to consider these differences while porting an existing application to YugabyteDB.

## Unsupported PostgreSQL features
The following PostgreSQL features are not supported in YugabyteDB:
- Pessimistic locking
- Table locks
- [Inheritance](https://www.postgresql.org/docs/11/tutorial-inheritance.html)
- Exclusion Constraints
- GiST indexes
- Kerberos/GSSAPI 
- Events (Listen/Notify) 
- Drop primary key
- XML Functions
- XA syntax

## Features that differ from PostgreSQL
- Tablespaces 
- Logical replication is CDC 
- Primary key (hash/range)
