---
title: Migrate applications to YugabyteDB
headerTitle: Migrate applications to YugabyteDB
linkTitle: Migrate
description: Migrate existing PostgreSQL and other RDBMS applications to YugabyteDB.
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

## Overview

Migrate applications and data from your existing RDBMS (PostgreSQL, MySQL, or Oracle) to a YugabyteDB cluster using **YB migration engine**: a utility which facilities seamless data migration across databases.

This page provides general information and references to help you get started with planning your migration.

### YB migration engine

YB migration engine is an open-source database migration engine provided by YugabyteDB. The engine manages the entire lifecycle of a database migration including cluster preparation for data import, schema-migration and data-migration using [yb_migrate](https://github.com/yugabyte/yb-db-migration).

yb_migrate is a command line executable program that supports migrating databases from PostgreSQL, Oracle, and MySQL to a YugabyteDB database. yb_migrate keeps all of its migration state, including exported schema and data, in a local directory called the *export directory*. For more information, refer to [Export directory](../reference/connectors/yb-migration-reference/#export-directory) in the Reference section.

### Migration modes

| Mode |  Description |
| :------------- | :----------- |
| Offline | In this mode, the source database should not change during the migration.<br> The offline migration is considered complete when all the requested schema objects and data are migrated to the target database. |
| Online | In this mode, the source database can continue to change. After the full initial migration, yb_migrate continues replicating source database changes to the target database. <br> The process runs continuously till you decide to switch over to the YugabyteDB database. |

{{< note title="Note" >}}
yb_migrate supports only `offline` migration mode. The `online` migration mode is currently under development. For more details, refer to the [github issue](https://github.com/yugabyte/yb-db-migration/issues/50).
{{< /note >}}

### Migration workflow

A typical migration workflow using yb_migrate consists of the following steps:

- [Install yb_migrate](db-migration-process/#1-install-yb-migrate) on a *migrator machine*.
- Convert the source database schema to PostgreSQL format using the `yb_migrate export schema` command.
- Generate a *Schema Analysis Report* using the `yb_migrate analyze-schema` command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB.
- Manually change the exported schema as suggested in the Schema Analysis Report.
- Dump the source database in the local files on the migrator machine using the `yb_migrate export data` command.
- Import the schema in the target YugabyteDB database using the `yb_migrate import schema` command.
- Import the data in the target YugabyteDB database using the `yb_migrate import data` command.

![img](/images/migrate/yb_migrate.png)
