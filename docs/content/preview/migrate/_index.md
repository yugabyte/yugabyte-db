---
title: Migrate other databases to YugabyteDB
headerTitle: Migrate other databases to YugabyteDB
linkTitle: Migrate
description: Migrate your existing applications from another RDBMS to YugabyteDB.
image: /images/section_icons/explore/high_performance.png
headcontent: Migrate your existing applications from another RDBMS to YugabyteDB.
section: YUGABYTEDB CORE
menu:
  preview:
    identifier: migrate
    weight: 625
---

<!-- <div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="db-migration-engine/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Database migration engine</div>
      </div>
      <div class="body">
        Use the yb_migrate database engine to migrate data and applications from other databases to YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migration-process-overview/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Migration process</div>
      </div>
      <div class="body">
        An overview of the migration process to YugabyteDB.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migrate-from-postgresql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Migrate from PostgreSQL</div>
      </div>
      <div class="body">
        Migrate your PostgreSQL data and applications to YugabyteDB.
      </div>
    </a>
  </div>
</div> -->

YugabyteDB currently supports:

- migrating schema and data from your existing RDBMS to a YugabyteDB cluster using  **YB migration engine**, which facilitates migration from your existing RDBMS to a YugabyteDB database. You can use the engine to perform migration from the following databases:

  - PostgreSQL

  - MySQL

  - Oracle

- migrating from PostgreSQL using ysql_dump and ysql_restore; YugabyteDB-specific versions of the `pg_dump` and `pg_restore` tools respectively.

The [Migration process overview](../migrate/migration-process-overview/) page includes the general list of steps involved in a database migration.

## Get started

<!-- - Refer to [Migrate using YB migration engine](../migrate/db-migration-process/), to get started with database migration using the YB migration engine. -->

<!-- - Refer to [Migrate from PostgreSQL using ysql_dump and ysql_restore](../migrate/migrate-from-postgresql/) to migrate from PostgreSQL to YugabyteDB. -->

<div class="row">
 <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yb-migration-engine/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Migrate using YB migration engine (beta) </div>
      </div>
      <div class="body">
        Perform database migration ( PostgreSQL, MySQL, or Oracle) to YugabyteDB using the YB migration engine.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migrate-from-postgresql/">
      <div class="head">
        <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Migrate from PostgreSQL using ysql_dump and ysql_restore</div>
      </div>
      <div class="body">
        Perform database migration from PostgreSQL to YugabyteDB using ysql_dump and ysql_restore.
      </div>
    </a>
  </div>
</div>

## Learn more

- Learn details about yb_migrate, SSL connectivity, data sharding strategies, and so on in [References for using YB Migration Engine](../reference/connectors/yb-migration-reference/).

- Blog posts on:

  - [Zero-Downtime Migrations from Oracle to a Cloud-Native PostgreSQL](https://blog.yugabyte.com/zero-downtime-migrations-from-oracle-to-a-cloud-native-postgresql/).

  - [YugabyteDB Migration: What About Those 19 Oracle Features I Thought I Would Miss?](https://blog.yugabyte.com/oracle-versus-yugabytedb/)

  - [A Migration Journey from Amazon DynamoDB to YugabyteDB and Hasura](https://blog.yugabyte.com/distributed-sql-summit-recap-a-migration-journey-from-amazon-dynamodb-to-yugabytedb-and-hasura/).
