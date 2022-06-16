---
title: YugabyteDB Voyager
headerTitle: YugabyteDB Voyager
linkTitle: YugabyteDB Voyager
description: Migrate to YugabyteDB with YugabyteDB Voyager.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
image: /images/section_icons/develop/learn.png
headcontent: Migrate to YugabyteDB with YugabyteDB Voyager.
menu:
  preview:
    identifier: yb-voyager
    parent: migrate
    weight: 100
---

YugabyteDB Voyager is a powerful open-source database migration engine provided by Yugabyte.
It accelerates cloud native adoption by removing barriers while moving applications to the public or private cloud; thereby migrating databases to YugabyteDB securely.

The engine manages the entire lifecycle of a database migration, including cluster preparation for data import, schema-migration, and data-migration, using the [yb-voyager](https://github.com/yugabyte/yb-voyager) command line utility.

## Migration workflow

A typical migration workflow using yb-voyager consists of the following steps:

- [Set up yb-voyager](../yb-voyager/prerequisites/#install-yb-voyager).
- Convert the source database schema to PostgreSQL format using the [`yb-voyager export schema`](../yb-voyager/migrate-data/#export-schema) command.
- Generate a *Schema Analysis Report* using the [`yb-voyager analyze-schema`](../yb-voyager/migrate-data/#analyze-schema) command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB.
- [Manually](../yb-voyager/migrate-data/#manually-edit-the-schema) change the exported schema as suggested in the Schema Analysis Report.
- Dump the source database in the local files on the migrator machine using the [`yb-voyager export data`](../yb-voyager/migrate-data/#export-data) command.
- Import the schema to the target YugabyteDB database using the [`yb-voyager import schema`](../yb-voyager/migrate-data/#import-schema) command.
- Import the data to the target YugabyteDB database using the [`yb-voyager import data`](../yb-voyager/migrate-data/#import-data) command.

```goat
                                              .------------------.
                                              |  Analysis        |
                                              |                  |
                                              | .--------------. |
                                              | |Analyze schema| |
                                              | .--.-----------. |
.-------------------.    .---------------.    |    |      ^      |
|                   |    |               |    |    v      |      |
| Set up yb_voyager .--->| Export schema .--->| .---------.----. |
|                   |    |               |    | |Manual changes| |
.---------.---------.    .---------.-----.    | .--------------. |
                                              |                  |
                                              .-------.----------.
                                                      |
                                                      v
                                              .-------------.    .------------------.    .------------------.
                                              |             |    |  Import          |    |                  |
                                              | Export data .--->|                  .--->| Verify migration |
                                              |             |    | .--------------. |    |                  |
                                              .-----.-------.    | |Import schema | |    .------------------.
                                                                 | .------.-------. |
                                                                 |        |         |
                                                                 |        v         |
                                                                 | .--------------. |
                                                                 | | Import data  | |
                                                                 | .--------------. |
                                                                 |                  |
                                                                 .--------.---------.
```

## Migration modes

You typically do a migration in one of two modes, as follows:

- Live - The source database can continue to change during the migration. After the full initial migration, yb-voyager continues replicating source database changes to the target database. The process runs continuously until you decide to switch over to the YugabyteDB database.

- Offline - The source database should not change during the migration. An offline migration is considered complete when all the requested schema objects and data are migrated to the target database.

{{< note title="Note" >}}
yb-voyager supports only _offline_ migration mode. The _live_ migration mode is currently under development. For more details, refer to the [GitHub issue](https://github.com/yugabyte/yb-voyager/issues/50).
{{< /note >}}

## Source databases

YugabyteDB Voyager supports migrating schema and data from your existing RDBMS, including the following:

- [PostgreSQL](../yb-voyager/prepare-databases/#postgresql)
- [MySQL](../yb-voyager/prepare-databases/#mysql)
- [Oracle](../yb-voyager/prepare-databases/#oracle)

## Target database

You can migrate data to any one of the three YugabyteDB [products](https://www.yugabyte.com/compare-products/). To create a cluster:

- Create a local YugabyteDB cluster using the [Quick start](../../quick-start/).
- Deploy a YugabyteDB Anywhere universe; refer to [Create YugabyteDB universe deployments](../../yugabyte-platform/create-deployments/).
- [Deploy a cluster in YugabyteDB Managed](../../yugabyte-cloud/cloud-basics/).

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migration-steps/">
      <div class="head">
        <img class="icon" src="/images/section_icons/introduction/benefits.png" aria-hidden="true" />
        <div class="title">Migration steps</div>
      </div>
      <div class="body">
        Overview of the steps for migrating to YugabyteDB using YugabyteDB Voyager.
      </div>
    </a>
  </div>
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="prerequisites/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/checklist.png" aria-hidden="true" />
        <div class="title">Prerequisites</div>
      </div>
      <div class="body">
        Prepare the environment and install yb-voyager.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="prepare-databases/">
      <div class="head">
       <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">Prepare databases</div>
      </div>
      <div class="body">
        Set up the databases for migration.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migrate-data/">
      <div class="head">
       <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Migrate your data</div>
      </div>
      <div class="body">
        Convert schema, export data, import data, and verify migration to YugabyteDB.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yb-voyager-cli/">
      <div class="head">
       <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true">
        <div class="title">YugabyteDB Voyager CLI</div>
      </div>
      <div class="body">
        Learn about the yb-voyager CLI options and SSL connectivity.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="reference/">
      <div class="head">
       <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true">
        <div class="title">Reference</div>
      </div>
      <div class="body">
        Learn about the migration workflow, sharding strategies, and limitations.
      </div>
    </a>
  </div>
</div>
