---
title: YugabyteDB Voyager
headerTitle: YugabyteDB Voyager
linkTitle: YugabyteDB Voyager
description: Migrate to YugabyteDB with YugabyteDB Voyager.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
image: /images/section_icons/develop/learn.png
headcontent: Migrate to YugabyteDB with YugabyteDB Voyager.
type: indexpage
showRightNav: true
menu:
  preview:
    identifier: voyager
    parent: migrate
---

YugabyteDB Voyager is a powerful open-source data migration engine that accelerates cloud native adoption by removing barriers to moving applications to the public or private cloud. It helps you migrate databases to YugabyteDB quickly and securely.

YugabyteDB Voyager manages the entire lifecycle of a database migration, including cluster preparation for data import, schema-migration, and data-migration, using the [yb-voyager](https://github.com/yugabyte/yb-voyager) command line utility.

## Features

- Free and completely open source.
- Supports widely used databases for migration and doesn't require changes to the [source databases](#source-databases) in most cases.
- Supports all YugabyteDB products (v2.12 and above) as the [target database](#target-database).
- Provides a unified [CLI](yb-voyager-cli/) experience for all different source databases.
- Auto-tuneable based on workloads, by analyzing the target cluster capacity; runs parallel jobs by default.
- Monitor the import status, and expected time for data export and import to complete using progress bars.
- In case of failures, data import can be resumed.
- Parallelism of data across tables.
- Supports direct data import from CSV files.
- Currently, supports migrating up to 1TB of data.
- Live migration - Coming soon. For more details, refer to the [GitHub issue](https://github.com/yugabyte/yb-voyager/issues/50) and for any questions, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new).

## Source databases

YugabyteDB Voyager supports migrating schema and data from your existing RDBMS, as described in the following table:

| Source database type | Supported versions and flavors |
| :--------------------| :----------------------------------- |
| PostgreSQL | PostgreSQL 9.x - 11.x <br> [Amazon Aurora PostgreSQL](https://docs.aws.amazon.com/AmazonRDS/latest/AuroraUserGuide/Aurora.AuroraPostgreSQL.html) <br> [Amazon RDS for PostgreSQL](https://aws.amazon.com/rds/postgresql/) <br> [Cloud SQL for PostgreSQL](https://cloud.google.com/sql/docs/postgres) <br> [Azure Database for PostgreSQL](https://azure.microsoft.com/en-ca/services/postgresql/) |
| MySQL | MySQL 8.x <br> MariaDB <br> [Amazon Aurora MySQL](https://docs.aws.amazon.com/AmazonRDS/latest/AuroraUserGuide/Aurora.AuroraMySQL.html) <br> [Amazon RDS for MySQL](https://aws.amazon.com/rds/mysql/) <br> [Cloud SQL for MySQL](https://cloud.google.com/sql/docs/mysql) |
| Oracle | Oracle 11g - 19c <br> [Amazon RDS for Oracle](https://aws.amazon.com/rds/oracle/) |

## Target database

You can migrate data to any one of the three YugabyteDB [products](https://www.yugabyte.com/compare-products/) (v2.12 and above). To create a cluster:

- Create a local YugabyteDB cluster using the [Quick start](../quick-start/).
- Deploy a YugabyteDB Anywhere universe; refer to [Create YugabyteDB universe deployments](../yugabyte-platform/create-deployments/).
- [Deploy a cluster in YugabyteDB Managed](../yugabyte-cloud/cloud-basics/).

## Migration workflow

A typical migration workflow using yb-voyager consists of the following steps:

- [Install yb-voyager](install-yb-voyager/#install-yb-voyager).
- Prepare the [source](migrate-steps/#prepare-the-source-database) database.
- Prepare the [target](migrate-steps/#prepare-the-target-database) database.
- Convert the source database schema to PostgreSQL format using the [`yb-voyager export schema`](migrate-steps/#export-schema) command.
- Generate a *Schema Analysis Report* using the [`yb-voyager analyze-schema`](migrate-steps/#analyze-schema) command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB.
- [Manually](migrate-steps/#manually-edit-the-schema) change the exported schema as suggested in the Schema Analysis Report.
- Dump the source database in the local files on the machine where yb-voyager is installed, using the [`yb-voyager export data`](migrate-steps/#export-data) command.
- Import the schema to the target YugabyteDB database using the [`yb-voyager import schema`](migrate-steps/#import-schema) command.
- Import the data to the target YugabyteDB database using the [`yb-voyager import data`](migrate-steps/#import-data) command.

![Migration workflow](/images/migrate/migration-workflow.png)

<div class="row">
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="install-yb-voyager/">
      <div class="head">
        <img class="icon" src="/images/section_icons/deploy/checklist.png" aria-hidden="true" />
        <div class="title">Install</div>
      </div>
      <div class="body">
        Prepare the environment and install yb-voyager.
      </div>
    </a>
  </div>

  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="migrate-steps/">
      <div class="head">
       <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true" />
        <div class="title">Migration steps</div>
      </div>
      <div class="body">
        Convert schema, export data, import data, and verify migration to YugabyteDB.
      </div>
    </a>
  </div>
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="performance/">
      <div class="head">
       <img class="icon" src="/images/section_icons/explore/high_performance.png" aria-hidden="true">
        <div class="title">Performance</div>
      </div>
      <div class="body">
        Learn about factors that can affect migration performance.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="yb-voyager-cli/">
      <div class="head">
       <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true">
        <div class="title">yb-voyager CLI</div>
      </div>
      <div class="body">
        Learn about the yb-voyager CLI options and SSL connectivity.
      </div>
    </a>
  </div>
</div>
