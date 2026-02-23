---
title: YugabyteDB Voyager Quick start
headerTitle: Quick start
linkTitle: Quick start
headcontent: Migrate PostgreSQL to YugabyteDB Aeon with YugabyteDB Voyager
description: Learn how to perform a fast, offline migration from PostgreSQL to YugabyteDB Aeon using Voyager. This guide covers setup, assessment, and data transfer.
menu:
  stable_yugabyte-voyager:
    identifier: quickstart-voyager
    parent: yugabytedb-voyager
    weight: 100
type: docs
showRightNav: true
---

This quick start guide describes the steps to perform an offline migration of a PostgreSQL database to YugabyteDB using YugabyteDB Voyager with your target database in YugabyteDB Aeon.

## Prerequisites

Before you start, ensure that you have the following:

- Java 17 installed
- 2+ CPU cores and 4GB+ RAM
- Network access to both source and target databases
- Sudo access on the machine where you'll run Voyager
- A PostgreSQL database to migrate (source)

## Create a YugabyteDB Aeon cluster

1. [Sign up](https://cloud.yugabyte.com) for YugabyteDB Aeon.

1. Create a cluster:
   - Log in to your YugabyteDB Aeon account.
   - Click **Create a Free cluster** on the welcome screen, or click **Add Cluster** on the **Clusters** page to open the **Create Cluster** wizard.
   - Select **Sandbox** for testing or **Dedicated** for production.
   - Enter a cluster name, choose your cloud provider (AWS or GCP) and region in which to deploy the cluster, then click **Next**.
   - Click **Add Current IP Address** to allow connections from your machine, and click **Next**.
   - Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.
   - Click **Create Cluster**.

## Install YugabyteDB Voyager

Install YugabyteDB Voyager v2025.11.2 or later on your machine using the [Install yb-voyager](../install-yb-voyager/#install-yb-voyager) steps.

## Prepare source and target databases

### Prepare source PostgreSQL database

{{<note title="For MySQL or Oracle">}}
This quick start describes the steps to migrate a PostgreSQL database. However, If you want to prepare a MySQL or an Oracle source database, refer to [Prepare the source database](../migrate/migrate-steps/#prepare-the-source-database) section in Offline migration.
{{</note>}}

Create a database user and provide the user with READ access to all the resources which need to be migrated. Run the following commands in a psql session:

1. Create a new user named `ybvoyager`.

   ```sql
   CREATE USER ybvoyager PASSWORD 'password';
   ```

1. Grant permissions for migration. Use the [yb-voyager-pg-grant-migration-permissions.sql](../reference/yb-voyager-pg-grant-migration-permissions/) script (in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`) to grant the required permissions as follows:

   ```sql
   psql -h <host> \
        -d <database> \
        -U <username> \ # A superuser or a privileged user with enough permissions to grant privileges
        -v voyager_user='ybvoyager' \
        -v schema_list='<comma_separated_schema_list>' \
        -v is_live_migration=0 \
        -v is_live_migration_fall_back=0 \
        -f <path_to_the_script>
   ```

   The `ybvoyager` user can now be used for migration.

### Prepare target YugabyteDB Aeon database

1. Connect to your YugabyteDB Aeon cluster via [Cloud shell](../../yugabyte-cloud/cloud-connect/connect-cloud-shell/#connect-via-cloud-shell).

1. In the `ysqlsh` prompt, create a target database (optional - you can use the default `yugabyte` database):

    ```sql
    -- Connect to your YugabyteDB Aeon cluster
    psql -h <your-cluster-host> -p 5433 -U admin -d yugabyte

    -- Create target database
    CREATE DATABASE target_db;
    ```

1. Create a user with [`yb_superuser`](../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following commands:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

1. [Create an API key](../../yugabyte-cloud/managed-automation/managed-apikeys/#create-an-api-key) to enable authentication.

1. [Download SSL certificates](../../yugabyte-cloud/cloud-secure-clusters/cloud-authentication/#download-your-cluster-certificate) to have Voyager connect to the target YugabyteDB database over SSL.

## Create an export directory and configuration file for Voyager

1. Create an [export directory](../migrate/migrate-steps/#create-an-export-directory) for yb-voyager as follows:

    ```bash
    mkdir -p $HOME/<migration-name>/export-dir
    ```

1. Copy the offline configuration template file to the export directory:

    ```sh
    cp /opt/yb-voyager/config-templates/offline-migration.yaml export-dir/migration-config.yaml
    ```

1. Edit the [configuration file](../reference/configuration-file/) `migration-config.yaml` to define your migration environment using the following example.

    {{<note title="SSL modes">}}
By default, setting `ssl-mode: prefer` or `require` allows for an encrypted connection without requiring a Root CA file. For production migrations, it is recommended to use `ssl-mode: verify-full` and provide the `ssl-root-cert: /path/to/target-root.crt`. For details, refer to [SSL Connectivity](../reference/yb-voyager-cli/#ssl-connectivity).
{{</note>}}

    ```yaml
    # Global settings
    export-dir: <absolute-path-to-export-dir>
    send-diagnostics: true
    control-plane-type: ybaeon
    ybaeon-control-plane:
      domain: https://cloud.yugabyte.com
      accountId: <your-YugabyteDB-Aeon-accountID>
      projectId: <your-YugabyteDB-Aeon-projectID>
      clusterId: <your-YugabyteDB-Aeon-clusterID>
      apiKey: <API_KEY>
    
    # Source database (PostgreSQL)
    source:
      db-type: postgresql
      db-host: <your-postgresql-host>
      db-port: 5432
      db-name: <your-source-database>
      db-schema: public
      db-user: ybvoyager
      db-password: 'your_postgresql_password'

    # Target database (YugabyteDB Aeon)
    target:
      name:
      db-host: <your-cluster-host>
      db-port: 5433
      db-name: target_db
      db-user: ybvoyager
      db-password: 'your_yugabytedb_password'
      ssl-mode: prefer

    assess-migration:
      iops-capture-interval: 0
      target-db-version: <db-version>
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

## Run migration assessment

Execute the [migration assessment](../migrate/assess-migration/) to get recommendations:

```bash
yb-voyager assess-migration --config-file migration-config.yaml
```

The `assess-migration` command generates a detailed assessment report with:

- Schema complexity analysis
- Data distribution recommendations
- Cluster sizing suggestions
- Performance optimization tips

## Review and address assessment recommendations

  1. Review the assessment report:
     - The report is saved in your export directory
     - Open the HTML report in your browser
    - Review recommendations for your specific workload

  1. Address recommendations in YugabyteDB Aeon:
     - If the assessment suggests cluster resizing, adjust your cluster in Aeon
     - Enable any recommended features in your cluster settings
     - Note any schema changes recommended for optimal performance

## Migrate to a YugabyteDB Aeon cluster

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

1. Connect to your target YugabyteDB Aeon cluster via [Cloud shell](/preview/yugabyte-cloud/cloud-connect/connect-cloud-shell/#connect-via-cloud-shell).

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

## What's next?

- [Migration options](../migrate/): Perform offline or live migration without downtime
- [Performance tuning](../reference/performance/): Optimize migration speed
- [Bulk data loading](../migrate/bulk-data-load/): Import from CSV files

### Additional resources

- [YugabyteDB Aeon documentation](../../yugabyte-cloud/): Learn more about managing your target cluster
- [Voyager Troubleshooting](../voyager-troubleshoot/): Common issues and solutions
- [Schema review workarounds](../known-issues/): Known issues and workarounds
