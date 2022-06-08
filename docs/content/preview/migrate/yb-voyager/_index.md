---
title: YB Voyager
headerTitle: YB Voyager
linkTitle: YB Voyager
description: Migrate to YugabyteDB with YB Voyager.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
image: /images/section_icons/develop/learn.png
headcontent: Migrate to YugabyteDB with YB Voyager.
menu:
  preview:
    identifier: yb-voyager
    parent: migrate
    weight: 100
---

YB Voyager is an open-source database migration engine provided by YugabyteDB. The engine manages the entire lifecycle of a database migration, including cluster preparation for data import, schema-migration and data-migration, using [yb-voyager](https://github.com/yugabyte/yb-voyager).

Learn more about the [Migration workflow](/preview/migrate/yb-voyager/reference/) using YB Voyager in the reference section.

## Migration modes

| Mode |  Description |
| :------------- | :----------- |
| Offline | In this mode, the source database should not change during the migration.<br> The offline migration is considered complete when all the requested schema objects and data are migrated to the target database. |
| Online | In this mode, the source database can continue to change. After the full initial migration, yb-voyager continues replicating source database changes to the target database. <br> The process runs continuously till you decide to switch over to the YugabyteDB database. |

{{< note title="Note" >}}
yb-voyager supports only `offline` migration mode. The `online` migration mode is currently under development. For more details, refer to this [github issue](https://github.com/yugabyte/yb-voyager/issues/50).
{{< /note >}}

## Source databases

YugabyteDB currently supports migrating schema and data from your existing RDBMS to a YugabyteDB cluster using  **YB Voyager**, which facilitates migration from your existing RDBMS to a YugabyteDB database. Using YB Voyager, you can:

- [Migrate from PostgreSQL](../yb-voyager/install-yb-voyager/#postgresql)
- [Migrate from MySQL](../yb-voyager/install-yb-voyager/#mysql)
- [Migrate from Oracle](../yb-voyager/install-yb-voyager/#oracle)

## Target database

You can migrate data to any one of the three YugabyteDB [products](https://www.yugabyte.com/compare-products/). To get started,

- Refer to [Quick start](../../quick-start/), and create a local YugabyteDB cluster.
- Refer to [Create YugabyteDB universe deployments](../../yugabyte-platform/create-deployments/) using YugabyteDB Anywhere.
- Refer to [Deploy clusters in YugabyteDB Managed](../../yugabyte-cloud/cloud-basics/).

<div class="row">
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migration-phases/">
      <div class="head">
        <img class="icon" src="/images/section_icons/introduction/benefits.png" aria-hidden="true" />
        <div class="title">Migration phases</div>
      </div>
      <div class="body">
        General overview of the phases in migration.
      </div>
    </a>
  </div>
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="prerequisites-1/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/checklist.png" aria-hidden="true" />
        <div class="title">Prerequisites</div>
      </div>
      <div class="body">
        Prepare the environment to install yb-voyager.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="install-yb-voyager/">
      <div class="head">
       <img class="icon" src="/images/section_icons/quick_start/install.png" aria-hidden="true" />
        <div class="title">Install yb-voyager</div>
      </div>
      <div class="body">
        Steps to install yb-voyager and set up the databases for migration.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="perform-migration-1/">
      <div class="head">
       <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Perform migration</div>
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
        <div class="title">YB Voyager CLI</div>
      </div>
      <div class="body">
        Learn about the yb-voyager CLI options and SSL connectivity.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yb-voyager-reference/">
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
