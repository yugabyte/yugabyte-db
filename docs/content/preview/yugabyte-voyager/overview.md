---
title: YugabyteDB Voyager overview
headerTitle: Overview
linkTitle: Overview
headcontent: Migrate your database using YugabyteDB Voyager
cascade:
  unversioned: true
description: YugabyteDB Voyager is a powerful open-source data migration engine that helps you migrate your database to YugabyteDB quickly and securely.
type: docs
showRightNav: true
menu:
  preview_yugabyte-voyager:
    identifier: overview-vgr
    parent: yugabytedb-voyager
    weight: 100
---

YugabyteDB Voyager is a powerful open-source data migration engine that accelerates cloud native adoption by removing barriers to moving applications to the public or private cloud. It helps you migrate databases to YugabyteDB quickly and securely.

![Voyager Architecture](/images/migrate/voyager-architecture.png)

You manage the entire lifecycle of a database migration, including cluster preparation for data import, schema migration, and data migration, using the [yb-voyager](https://github.com/yugabyte/yb-voyager) command line interface (CLI).

## Features

YugabyteDB Voyager has the following features:

- Free and completely open source.
- Supports widely-used databases for migration and, in most cases, doesn't require changes to the [source databases](#source-databases).
- Supports all YugabyteDB products as the [target database](#target-database). The target needs to be running YugabyteDB stable versions 2.14.5.0 and later, and preview versions 2.17.0.0 and later.
- [Unified CLI](../reference/yb-voyager-cli/) experience for all different source databases.
- [Auto-tuneable](../performance/) based on workloads, by analyzing the target cluster capacity; runs parallel jobs by default.
- Progress monitoring, including the expected time for data export and import to complete, using progress bars.
- In case of failures, data import can be resumed.
- Parallelism of data across tables.
- Support for direct data import from CSV files.
- Live migration (coming soon). For more details, refer to the [GitHub issue](https://github.com/yugabyte/yb-voyager/issues/50) and for any questions, contact [Yugabyte Support](https://support.yugabyte.com/hc/en-us/requests/new).

## Source databases

YugabyteDB Voyager supports migrating schema and data from your existing RDBMS, as described in the following table:

| Source database type | Supported versions and flavors |
| :--------------------| :----------------------------------- |
| PostgreSQL | PostgreSQL 9.x - 11.x <br> [Amazon Aurora PostgreSQL](https://docs.aws.amazon.com/AmazonRDS/latest/AuroraUserGuide/Aurora.AuroraPostgreSQL.html) <br> [Amazon RDS for PostgreSQL](https://aws.amazon.com/rds/postgresql/) <br> [Cloud SQL for PostgreSQL](https://cloud.google.com/sql/docs/postgres) <br> [Azure Database for PostgreSQL](https://azure.microsoft.com/en-ca/services/postgresql/) |
| MySQL | MySQL 8.x <br> MariaDB <br> [Amazon Aurora MySQL](https://docs.aws.amazon.com/AmazonRDS/latest/AuroraUserGuide/Aurora.AuroraMySQL.html) <br> [Amazon RDS for MySQL](https://aws.amazon.com/rds/mysql/) <br> [Cloud SQL for MySQL](https://cloud.google.com/sql/docs/mysql) |
| Oracle | Oracle 11g - 19c <br> [Amazon RDS for Oracle](https://aws.amazon.com/rds/oracle/) |

## Target database

You can migrate data to any one of the three YugabyteDB [products](https://www.yugabyte.com/compare-products/).

YugabyteDB Voyager supports YugabyteDB stable versions 2.14.5.0 and later, and preview versions 2.17.0.0 and later.

| Product | Deployment instructions |
| :--- | :--- |
| YugabyteDB | [Deploy YugabyteDB](../../deploy/) |
| YugabyteDB Anywhere | [Deploy a universe](../../yugabyte-platform/create-deployments/) |
| YugabyteDB Managed | [Deploy a cluster](../../yugabyte-cloud/cloud-basics/) |

## Migration workflow

A typical migration workflow using yb-voyager consists of the steps shown in the following illustration:

![Migration workflow](/images/migrate/migration-workflow.png)

| Step | Description |
| :--- | :---|
| [Install yb-voyager](../install-yb-voyager/#install-yb-voyager) | yb-voyager supports RHEL, CentOS, Ubuntu, and macOS, as well as airgapped and Docker-based installations. |
| [Prepare source](../migrate-steps/#prepare-the-source-database) | Create a new database user with READ access to all the resources to be migrated. |
| [Prepare target](../migrate-steps/#prepare-the-target-database) | Deploy a YugabyteDB database and create a user with superuser privileges. |
| [Export schema](../migrate-steps/#export-schema) | Convert the database schema to PostgreSQL format using the `yb-voyager export schema` command. |
| [Analyze schema](../migrate-steps/#analyze-schema) | Generate a *Schema&nbsp;Analysis&nbsp;Report* using the `yb-voyager analyze-schema` command. The report suggests changes to the PostgreSQL schema to make it appropriate for YugabyteDB. |
| [Modify schema](../migrate-steps/#manually-edit-the-schema) | Using the report recommendations, manually change the exported schema. |
| [Export data](../migrate-steps/#export-data) | Dump the source database to the target machine (where yb-voyager is installed), using the `yb-voyager export data` command. |
| [Import schema](../migrate-steps/#import-schema) | Import the modified schema to the target YugabyteDB database using the `yb-voyager import schema` command. |
| [Import data](../migrate-steps/#import-data) | Import the data to the target YugabyteDB database using the `yb-voyager import data` command. |
| [Import&nbsp;indexes&nbsp;and triggers](../migrate-steps/#import-indexes-and-triggers) | Import indexes and triggers to the target YugabyteDB database using the `yb-voyager import schema` command with an additional `--post-import-data` flag. |

<!--
<div class="row">
   <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="../install-yb-voyager/">
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
    <a class="section-link icon-offset" href="../migrate-steps/">
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
    <a class="section-link icon-offset" href="../performance/">
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
    <a class="section-link icon-offset" href="../known-issues/">
      <div class="head">
       <img class="icon" src="/images/section_icons/troubleshoot/troubleshoot.png" aria-hidden="true">
        <div class="title">Known issues</div>
      </div>
      <div class="body">
        Learn about the existing issues and workarounds you can do before migrating data using yb-voyager.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="../reference/">
      <div class="head">
       <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true">
        <div class="title">Reference</div>
      </div>
      <div class="body">
        Learn about the yb-voyager CLI options, data modeling strategies, and data type mapping support.
      </div>
    </a>
  </div>
  <div class="col-12 col-md-6 col-lg-12 col-xl-6">
    <a class="section-link icon-offset" href="../release-notes/">
      <div class="head">
       <img class="icon" src="/images/section_icons/architecture/concepts.png" aria-hidden="true">
        <div class="title">What's new</div>
      </div>
      <div class="body">
        Learn about new features, enhancements, and bugs fixes.
      </div>
    </a>
  </div>
</div>
-->
