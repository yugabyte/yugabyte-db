---
title: Migration Hub
linkTitle: Migration Hub
description: Simplify migration from legacy and cloud databases to YugabyteDB.
headcontent: Simplify migration from legacy databases to YugabyteDB Aeon
menu:
  stable_yugabyte-cloud:
    identifier: managed-migrate
    parent: yugabytedb-managed
    weight: 50
type: docs
---

Use the Migration Hub to manage database migrations, including database preparation, migration assessment, schema changes, and data migration. Migration Hub uses [YugabyteDB Voyager](../../yugabyte-voyager/) to manage migrations from Oracle, PostrgeSQL, and MySQL databases to YugabyteDB Aeon.

To run a migration in Migration Hub:

1. [Create a cluster](../cloud-basics/).
1. Navigate to your cluster and click **Migration Hub**.

    If you have previously performed migrations, the Migration Hub lists them. You can view migration details by selecting the migration in the list.

    ![Migration Hub](/images/yb-cloud/managed-hub.png)

1. Click **Migrate Database** to create a new migration.

## Migrate a database

There are five stages to a migration:

1. Set up.

    Migration Hub uses YugabyteDB Voyager to perform migrations. The setup process requires the following:

    - Download and install YugabyteDB Voyager (v2025.11.2 or later).
    - Edit the configuration file with the parameters needed to connect the source database (the database you are migrating from) to the target database (your YugabyteDB Aeon cluster).

    For full instructions, see the YugabyteDB Voyager [Quick Start](../../yugabyte-voyager/quickstart/).

1. Assess.

    After your source and target databases are configured, run a migration assessment. The assessment evaluates the source database and makes recommendations for sizing your YugabyteDB Aeon cluster, along with recommended changes to your schema.

    The output is a migration assessment report, which you can view at any time by navigating to the Migration Hub and selecting the migration.

1. Migrate Schema.

    After you have implemented the recommended sizing and schema changes, migrate the schema.

1. Migrate Data.

    After the schema is migrated, migrate the data.

1. Verify.

    Make sure the database is performing correctly. After the schema and data import is complete, manually run validation queries on both the source and target YugabyteDB database to ensure that the data is correctly migrated. For example, you can validate the databases by running queries to check the row count of each table.

## Limitations

- Migration assessment is only supported for Oracle and PostgreSQL migrations.
- The status for the Migrate Data step can display 100% completed before the migration is done.
