---
title: YugabyteDB Voyager Quick start
headerTitle: Quick start
linkTitle: Quick start
headcontent: Migrate a PostgreSQL database to YugabyteDB Aeon
description: Learn how to perform a fast, offline migration from PostgreSQL to YugabyteDB Aeon using Voyager. This guide covers setup, assessment, and data transfer.
menu:
  stable_yugabyte-voyager:
    identifier: quickstart-voyager
    parent: yugabytedb-voyager
    weight: 100
    params:
      classes: separator
type: docs
showRightNav: true
---

This guide describes how to perform an offline migration of a PostgreSQL database to a target database in YugabyteDB Aeon using YugabyteDB Voyager.

{{<note title="MySQL or Oracle">}}
This quick start describes the steps for an offline migration of a PostgreSQL database. If you want to migrate a MySQL or an Oracle source database, refer to [Prepare the source database](../migrate/migrate-steps/#prepare-the-source-database) section in Offline migration.
{{</note>}}

## Prerequisites

Before you start, ensure that you have the following:

- Java 17 installed
- 2+ CPU cores and 4GB+ RAM
- A PostgreSQL database to migrate (source)
- Network access - your source database must be accessible to YugabyteDB Aeon
- Sudo access on the machine where you'll run Voyager

## Setup

### Install YugabyteDB Voyager

Install YugabyteDB Voyager v2025.11.2 or later on your machine using the steps in [Install yb-voyager](../install-yb-voyager/#install-yb-voyager).

### Prepare source PostgreSQL database

On your sourse database, create a database user and provide the user with READ access to all the resources which need to be migrated.

Run the following commands in a psql session:

1. Create a new user named `ybvoyager`.

   ```sql
   CREATE USER ybvoyager PASSWORD 'password';
   ```

1. Grant permissions for migration.

    Use the [yb-voyager-pg-grant-migration-permissions.sql](../reference/yb-voyager-pg-grant-migration-permissions/) script (in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`) to grant the required permissions as follows:

    ```sql
    psql -h <postgresql-host-address> \
          -d <source-database-name> \
          -U <your-username> \ # Privileged user with enough permissions to grant privileges
          -v voyager_user='ybvoyager' \
          -v schema_list='<schema-list>' \
          -v is_live_migration=0 \
          -v is_live_migration_fall_back=0 \
          -f <path-to-the-script>
    ```

    For schema list, provide a comma-separated list of the schemas you want to migrate.

    The `ybvoyager` user can now be used for migration.

1. Optionally, create a sample table with some data as follows:

    ```sql
    CREATE TABLE employees (
        id SERIAL PRIMARY KEY,
        first_name TEXT,
        last_name TEXT,
        department TEXT,
        salary NUMERIC
    );
   
    INSERT INTO employees (first_name, last_name, department, salary) VALUES 
    ('Josh', 'Dev', 'Engineering', 95000),
    ('John', 'Smith', 'Sales', 75000),
    ('Elena', 'Rigby', 'Marketing', 82000),
    ('Dinesh', 'Chugtai', 'Engineering', 92000);
    ```

### Create YugabyteDB Aeon cluster

1. [Sign up](https://cloud.yugabyte.com) and then sign in to YugabyteDB Aeon.

1. Create a cluster.

    You can [create a free Sandbox cluster](/stable/yugabyte-cloud/cloud-quickstart/#create-your-sandbox-cluster) for testing, or to go beyond the capabilities of the Sandbox cluster, [start a free trial](/stable/yugabyte-cloud/managed-freetrial.) to create Dedicated clusters.

    - Click **Create a Free cluster** on the welcome screen, or click **Add Cluster** on the **Clusters** page to open the **Create Cluster** wizard.
    - Select **Sandbox** for testing or **Dedicated** for production.
    - Follow the instructions in the wizard.
      - Be sure to **Add Current IP Address** to allow connections from your machine.
      - Save your credentials in a secure location. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.
    - Click **Create Cluster** to finish.

YugabyteDB Aeon starts deploying the custer. When it finishes, proceed to the next section.

### Get Aeon cluster details

To be able to connect to your Aeon cluster from YugabyteDB Voyager, you will need the following cluster details:

- API key
- Account, Project, and Cluster IDs
- Host address and Database version of your cluster
- Cluster root certificate for SSL

In YugabyteDB Aeon, do the following:

1. Navigate to **Security>Access Control>Roles** to [create a role](/stable/yugabyte-cloud/managed-security/managed-roles/#create-a-role) with the following **Cluster Management** permissions:

    - Create Voyager migrations data
    - View Voyager migrations data
    - Update Voyager migrations data

1. Navigate to **Security>Access Control>API Keys** to [create an API key](/stable/yugabyte-cloud/managed-automation/managed-apikeys/#create-an-api-key).

    Be sure to assign the **Role** you created to the API key.

1. Click the Profile icon in the top right corner of the YugabyteDB Aeon window to obtain the following:

    - _Account ID_
    - _Project ID_

1. Navigate to your cluster and choose **Settings**.

    - Under **General**, obtain the _Cluster ID_ and _Database Version_.
    - Under **Connection Parameters**, obtain the _Host address_ of your cluster.

1. [Download the cluster certificate](/stable/yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate) (this is optionally used for SSL connectivity).

### Prepare the target database

Do the following:

1. In YugabyteDB Aeon, navigate to your cluster.

1. Click **Connect** and connect to your cluster using [cloud shell](/stable/yugabyte-cloud/cloud-connect/connect-client-shell/#connect-using-a-client-shell) (YSQL API). Use the credentials [you downloaded](#create-a-yugabytedb-aeon-cluster) when you created your cluster.

    The ysqlsh prompt appears and is ready to use.

1. Create a user with [`yb_superuser`](/stable/yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role as follows:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

1. Create a target database (this is optional, you can use the default `yugabyte` database):

    ```sql
    CREATE DATABASE target_db;
    ```

### Edit the configuration file

The Voyager [configuration file](../reference/configuration-file/) sets various connection parameters so that Voyager can connect to your Aeon cluster, using the [cluster details](#get-aeon-cluster-details) you collected earlier. The file is included with your Voyager installation in the config-templates folder.

1. On your machine, create an [export directory](../migrate/migrate-steps/#create-an-export-directory) for yb-voyager. For example:

    ```bash
    mkdir -p $HOME/my-migration/export-dir
    ```

1. Copy the offline configuration template file to the export directory. For example:

    ```sh
    cp /opt/yb-voyager/config-templates/offline-migration.yaml export-dir/migration-config.yaml
    ```

1. Edit the `migration-config.yaml` configuration file to define your migration environment using the following example.

    Fill in the cluster details you obtained previously.

    {{<note title="SSL modes">}}
Using `ssl-mode: prefer` or `require` allows for an encrypted connection without requiring the cluster certificate file. For production migrations, it is recommended to use `ssl-mode: verify-full` and provide the `ssl-root-cert: /path/to/target-root.crt`. For details, refer to [SSL Connectivity](../reference/yb-voyager-cli/#ssl-connectivity).
{{</note>}}

    Note that the following only shows the most relevant lines of the file. Where necessary, uncomment lines to enable settings. For full details on the file, refer to [configuration file](../reference/configuration-file/).

    ```yaml
    # Global settings
    export-dir: <absolute-path-to-export-dir>
    send-diagnostics: true
    control-plane-type: ybaeon
    ybaeon-control-plane:
      domain: https://cloud.yugabyte.com
      accountId: <YugabyteDB-Aeon-accountID>
      projectId: <YugabyteDB-Aeon-projectID>
      clusterId: <YugabyteDB-Aeon-clusterID>
      apiKey: <API_KEY>
    
    # Source database (PostgreSQL)
    source:
      db-type: postgresql
      db-host: <postgresql-host-address>
      db-port: 5432
      db-name: <source-database-name>
      db-schema: public
      db-user: ybvoyager
      db-password: 'your_postgresql_password'

    # Target database (YugabyteDB Aeon)
    target:
      name:
      db-host: <cluster-host-address>
      db-port: 5433
      db-name: target_db
      db-user: ybvoyager
      db-password: 'ybvoyager-user-password'
      ssl-mode: prefer

    assess-migration:
      iops-capture-interval: 0
      target-db-version: <db-version> # For example, `2025.2.1.0`
      report-unsupported-query-constructs: true
      report-unsupported-plpgsql-objects: true
    
    export-data:
      export-type: snapshot-and-changes

    import-schema:
      continue-on-error: true

    export-data-from-target:
      transaction-ordering: true
      disable-pb: false

    import-data-to-source:
      parallel-jobs: 2
      disable-pb: false

    initiate-cutover-to-target:
      prepare-for-fall-back: true

    archive-changes:
      delete-changes-without-archiving: true

    finalize-schema-post-data-import:
      continue-on-error: true
      refresh-mviews: false

    end-migration:
      backup-log-files: false
      backup-data-files: false
      save-migration-reports: false
      backup-schema-files: false
    ```

## Migration

There are five stages to a migration. Migration Hub in YugabyteDB Aeon updates the status of your migration as you complete each stage.

- Return to your migration in the [Migration Hub](/stable/yugabyte-cloud/managed-migrate/#assess) in YugabyteDB Aeon, and follow the instructions provided on the **Assess**, **Migrate Schema**, **Migrate Data**, and **Verify** pages.

<!--
{{<note title="Migration Hub">}}
If you are using [Migration Hub](/stable/yugabyte-cloud/managed-migrate/#assess) in YugabyteDB Aeon, you can return to the hub, where these commands are also available on the **Assess** page of your migration.
{{</note>}}

Execute the [migration assessment](../migrate/assess-migration/) to get recommendations:

```bash
yb-voyager assess-migration --config-file migration-config.yaml
```

The `assess-migration` command generates a detailed assessment report with:

- Schema complexity analysis
- Data distribution recommendations
- Cluster sizing suggestions
- Performance optimization tips

The report is saved in your export directory in HTML format, and you can view it in your browser. You can also view the report in the Migration Hub for your cluster in YugabyteDB Aeon.

### Review recommendations

1. Review the assessment report:

    - The report is saved in your export directory
    - Open the HTML report in your browser
    - Review recommendations for your specific workload

1. Address recommendations in YugabyteDB Aeon:

    - If the assessment suggests cluster resizing, [adjust your cluster](/stable/yugabyte-cloud/cloud-clusters/configure-clusters/) in Aeon
    - Enable any recommended features in your cluster settings
    - Note any schema changes recommended for optimal performance

## Migrate

Proceed with schema and data migration using the following steps:

### Export schema

[Export the schema](../migrate/migrate-steps/#export-and-analyze-schema) from the source database:

```bash
yb-voyager export schema --config-file migration-config.yaml
```

### Analyze schema

1. [Analyze](../migrate/migrate-steps/#analyze-schema) the PostgreSQL schema dumped in the export schema step:

    ```bash
    yb-voyager analyze-schema --config-file migration-config.yaml
    ```

1. Review schema analysis report in YugabyteDB Aeon:
   - Open the generated HTML report
   - Review any manual changes recommended
   - Make necessary modifications to the exported schema files if needed

### Import schema

[Import](../migrate/migrate-steps/#import-schema) the schema to your target YugabyteDB Aeon cluster:

```bash
yb-voyager import schema --config-file migration-config.yaml
```

### Export data

[Export data](../migrate/migrate-steps/#export-data) from source:

```bash
yb-voyager export data --config-file migration-config.yaml
```

### Import data

1. [Import data](../migrate/migrate-steps/#import-data) to target:

    ```bash
    yb-voyager import data --config-file migration-config.yaml
    ```

1. Monitor import progress:

    ```bash
    yb-voyager import data status --config-file migration-config.yaml
    ```

1. [Finalize schema](../migrate/migrate-steps/#finalize-schema-post-data-import) (if needed):

    ```bash
    yb-voyager finalize-schema-post-data-import --config-file migration-config.yaml
    ```

### Review import status in YugabyteDB Aeon

1. In your YugabyteDB Aeon cluster page, click the **Migrations** tab.
1. Review the import data status report and verify that all tables and data have been imported successfully

### Validate migration

1. Connect to your target YugabyteDB Aeon cluster via [Cloud shell](/stable/yugabyte-cloud/cloud-connect/connect-cloud-shell/#connect-via-cloud-shell).

1. In the `ysqlsh` prompt, connect to your target database:

   ```bash
    psql -h <your-cluster-host> -p 5433 -U ybvoyager -d target_db
    ```

1. Verify data integrity:

    ```sql
    -- Check table count
    SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public';

    -- Verify row counts for key tables
    SELECT COUNT(*) FROM your_table_name;

    -- Sample data verification
    SELECT * FROM your_table_name LIMIT 10;
    ```

1. Test application connectivity:
   - Update your application connection strings
   - Test basic CRUD operations
   - Verify application functionality

### End migration

[Complete the migration](../migrate/migrate-steps/#end-migration) as follows:

```bash
yb-voyager end migration --config-file migration-config.yaml \
  --backup-schema-files true \
  --backup-data-files true \
  --backup-log-files true \
  --save-migration-reports true
```
-->

## What's next?

- [Migration options](../migrate/): Perform offline or live migration without downtime
- [Performance tuning](../reference/performance/): Optimize migration speed
- [Bulk data loading](../migrate/bulk-data-load/): Import from CSV files

### Additional resources

- [YugabyteDB Aeon documentation](/stable/yugabyte-cloud/): Learn more about managing your target cluster
- [Voyager Troubleshooting](../voyager-troubleshoot/): Common issues and solutions
- [Schema review workarounds](../known-issues/): Known issues and workarounds
