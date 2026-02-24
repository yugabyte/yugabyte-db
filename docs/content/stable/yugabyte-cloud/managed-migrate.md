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

Use the Migration Hub to manage database migrations, including database preparation, migration assessment, schema changes, and data migration. Migration Hub uses [YugabyteDB Voyager](/stable/yugabyte-voyager/) to manage migrations from Oracle, PostgreSQL, and MySQL databases to YugabyteDB Aeon.

To run a migration in Migration Hub:

1. [Create a cluster](../cloud-basics/).
1. Navigate to your cluster and click **Migration Hub**.

    If you have previously performed migrations, the Migration Hub lists them. You can view migration details by selecting the migration in the list.

    ![Migration Hub](/images/yb-cloud/managed-hub.png)

1. Click **Migrate Database** to create a new migration.

## Migrate a database

There are five stages to a migration. Migration Hub updates the status of your migration as you complete each stage.

### Set up

Migration Hub uses YugabyteDB Voyager to perform migrations. The setup process requires the following:

- Download and install YugabyteDB Voyager (v2025.11.2 or later).
- Prepare the source and the target databases required for migration.
- Edit the configuration file with the parameters needed to connect the source database (the database you are migrating from) to the target database (your YugabyteDB Aeon cluster).

For full instructions, see the YugabyteDB Voyager [Quick Start](/stable/yugabyte-voyager/quickstart/).

### Assess

After your source and target databases are configured, run a migration assessment using the command provided on the **Assess** page.

The `assess-migration` command generates a detailed assessment report with:

- Schema complexity analysis
- Data distribution recommendations
- Cluster sizing suggestions
- Performance optimization tips

![Migration Assessment](/images/yb-cloud/migrate-hub-assess.png)

Address the recommendations in the report:

- If the assessment suggests cluster resizing, [adjust your cluster](../cloud-clusters/configure-clusters/).
- Enable any recommended features in your cluster settings.
- Note any schema changes recommended for optimal performance.

### Migrate Schema

Schema migration is performed in three steps:

1. Export the schema from the source database using the command provided under **Export Schema** on the **Migrate Schema** page.

1. Analyze the exported schema using the command provided under **Analyze Schema** on the **Migrate Schema** page.

    When the analysis is complete, review schema analysis report:

    - Review any manual changes recommended
    - Modify to the exported schema files if needed

1. Import the exported schema using the command provided under **Import Schema** on the **Migrate Schema** page.

![Migrate Schema](/images/yb-cloud/migrate-hub-schema.png)

### Migrate Data

Data migration is performed in two steps:

1. Export the data from the source database using the command provided under **Export Data** on the **Migrate Schema** page.

1. Import the exported data using the command provided under **Import Data** on the **Migrate Schema** page.

![Migrate Data](/images/yb-cloud/migrate-hub-data.png)

### Verify

After the migration is complete, make sure the database is performing correctly.

![Migration Verify](/images/yb-cloud/migrate-hub-verify.png)

Manually run validation queries on both the source and target YugabyteDB database to ensure that the data is correctly migrated. For example, you can validate the databases by running queries to check the row count of each table.

For example, connect to your target YugabyteDB Aeon cluster via [Cloud shell](/stable/yugabyte-cloud/cloud-connect/connect-cloud-shell/#connect-via-cloud-shell) and verify data integrity:

```sql
-- Check table count
SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public';

-- Verify row counts for key tables
SELECT COUNT(*) FROM your_table_name;

-- Sample data verification
SELECT * FROM your_table_name LIMIT 10;
```

In addition, you may want to verify application connectivity:

- Update your application connection strings
- Test basic CRUD operations
- Verify application functionality

## Limitations

- Migration assessment is only supported for Oracle and PostgreSQL migrations.
- The status for the Migrate Data step can display 100% completed before the migration is done.
