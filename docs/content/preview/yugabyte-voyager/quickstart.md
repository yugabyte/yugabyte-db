---
title: YugabyteDB Voyager Quick start
headerTitle: Quick start
linkTitle: Quick start
headcontent: Get started with YugabyteDB Voyager migration in minutes
description: Complete a basic database migration using YugabyteDB Voyager in the quickest possible way.
menu:
  preview_yugabyte-voyager:
    identifier: quickstart-voyager
    parent: yugabytedb-voyager
    weight: 100
type: docs
showRightNav: true
---

This quick start guide walks you through migrating a PostgreSQL database to YugabyteDB using YugabyteDB Voyager with YugabyteDB Aeon as the target database.

## Prerequisites

Before you start, ensure that you have the following:

- Java 17 installed
- 2+ CPU cores and 4GB+ RAM
- Network access to both source and target databases
- Sudo access on the machine where you'll run Voyager
- A PostgreSQL database to migrate (source)

## Create a YugabyteDB Aeon cluster

1. Sign up for YugabyteDB Aeon:
   - Go to [YugabyteDB Aeon](https://cloud.yugabyte.com).
   - Click **Sign up** and create your account.

1. Create a cluster:
   - Log in to your YugabyteDB Aeon account.
   - Click **Create a Free cluster** on the welcome screen, or click **Add Cluster** on the **Clusters** page to open the **Create Cluster** wizard.
   - Select **Sandbox** for testing or **Dedicated** for production.
   - Enter a cluster name, choose your cloud provider (AWS or GCP) and region in which to deploy the cluster, then click **Next**.
   - Click **Add Current IP Address** to allow connections from your machine, and click **Next**.
   - Click **Download credentials**. The default credentials are for a database user named "admin". You'll use these credentials when connecting to your YugabyteDB database.
   - Click **Create Cluster**.

## Install YugabyteDB Voyager

Install YugabyteDB Voyager on your machine as follows:

1. In your YugabyteDB Aeon cluster page, click **Migrations**, and select **Migrate Database**.
1. In **Prepare Voyager**, choose your operating system, run the installation commands on your machine, and verify the installation.

## Prepare source and target databases

### Prepare source PostgreSQL database

Create a database user and provide the user with READ access to all the resources which need to be migrated. Run the following commands in a psql session:

1. Create a new user named `ybvoyager`.

   ```sql
   CREATE USER ybvoyager PASSWORD 'password';
   ```

1. Grant permissions for migration. Use the [yb-voyager-pg-grant-migration-permissions.sql](../../reference/yb-voyager-pg-grant-migration-permissions/) script (in `/opt/yb-voyager/guardrails-scripts/` or, for brew, check in `$(brew --cellar)/yb-voyager@<voyagerversion>/<voyagerversion>`) to grant the required permissions as follows:

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

1. Create a target database (optional - you can use the default `yugabyte` database):

    ```sql
    -- Connect to your YugabyteDB Aeon cluster
    psql -h <your-cluster-host> -p 5433 -U admin -d yugabyte

    -- Create target database
    CREATE DATABASE target_db;
    ```

1. Create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

1. Configure SSL connectivity

    1. Create API key:
       - In YugabyteDB Aeon, go to **Security** → **API Keys**
       - Click **Create API Key**
       - Enter a name and click **Create**
       - Copy and save the API key securely

    1. Download SSL certificates:
       - In your cluster page, click **Connect**
       - Under **Download certificates**, click **Download**
       - Extract the certificates to a secure location on your machine

    1. **Note SSL certificate paths**:
       - Root certificate: `ca.crt`
       - Client certificate: `yugabyte.crt`
       - Client key: `yugabyte.key`

## Create export directory and configuration

1. **Create export directory**:

```bash
mkdir -p ~/voyager-migration/export-dir
cd ~/voyager-migration
```

2. **Copy configuration template**:

```bash
# Copy the offline migration template
cp /opt/yb-voyager/config-templates/offline-migration.yaml migration-config.yaml
```

3. **Configure the migration file**:

Edit `migration-config.yaml` with your specific values:

```yaml
# Global settings
export-dir: /home/$(whoami)/voyager-migration/export-dir
log-level: info

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
  db-host: <your-cluster-host>
  db-port: 5433
  db-name: target_db
  db-user: ybvoyager
  db-password: 'your_yugabytedb_password'
  ssl-mode: require
  ssl-cert: /path/to/yugabyte.crt
  ssl-key: /path/to/yugabyte.key
  ssl-root-cert: /path/to/ca.crt
```

## Run migration assessment

Execute the migration assessment to get recommendations:

```bash
yb-voyager assess-migration --config-file migration-config.yaml
```

This generates a detailed assessment report with:
- Schema complexity analysis
- Data distribution recommendations
- Cluster sizing suggestions
- Performance optimization tips

## Review and address assessment recommendations

1. **Review the assessment report**:
   - The report is saved in your export directory
   - Open the HTML report in your browser
   - Review recommendations for your specific workload

2. **Address recommendations in YugabyteDB Aeon**:
   - If the assessment suggests cluster resizing, adjust your cluster in Aeon
   - Enable any recommended features in your cluster settings
   - Note any schema changes recommended for optimal performance

## Export and analyze schema

1. **Export schema**:

```bash
yb-voyager export schema --config-file migration-config.yaml
```

2. **Analyze schema**:

```bash
yb-voyager analyze-schema --config-file migration-config.yaml
```

3. **Review schema analysis report**:
   - Open the generated HTML report
   - Review any manual changes recommended
   - Make necessary modifications to the exported schema files if needed

## Import schema

Import the schema to your target YugabyteDB Aeon cluster:

```bash
yb-voyager import schema --config-file migration-config.yaml
```

## Export and import data

1. **Export data from source**:

```bash
yb-voyager export data --config-file migration-config.yaml
```

2. **Import data to target**:

```bash
yb-voyager import data --config-file migration-config.yaml
```

3. **Monitor import progress**:

```bash
# Check import status
yb-voyager import data status --config-file migration-config.yaml
```

## Review import status in YugabyteDB Aeon

1. **Check the YugabyteDB Aeon UI**:
   - Go to your cluster page
   - Navigate to **Migrations** tab
   - Review the import data status report
   - Verify all tables and data have been imported successfully

## Validate migration

1. **Connect to your target database**:

```bash
psql -h <your-cluster-host> -p 5433 -U ybvoyager -d target_db
```

2. **Verify data integrity**:

```sql
-- Check table count
SELECT COUNT(*) FROM information_schema.tables WHERE table_schema = 'public';

-- Verify row counts for key tables
SELECT COUNT(*) FROM your_table_name;

-- Sample data verification
SELECT * FROM your_table_name LIMIT 10;
```

3. **Test application connectivity**:
   - Update your application connection strings
   - Test basic CRUD operations
   - Verify application functionality

## Finalize migration

1. **Finalize schema** (if needed):

```bash
yb-voyager finalize-schema-post-data-import --config-file migration-config.yaml
```

2. **End migration**:

```bash
yb-voyager end migration --config-file migration-config.yaml \
  --backup-schema-files true \
  --backup-data-files true \
  --save-migration-reports true
```

## What's next?

### Explore advanced features

- **[Live Migration](../migrate/live-migrate/)**: Migrate without downtime
- **[Performance Tuning](../reference/performance/)**: Optimize migration speed
- **[Bulk Data Loading](../migrate/bulk-data-load/)**: Import from CSV files

### Additional resources

- **[YugabyteDB Aeon Documentation](../../yugabyte-cloud/)**: Learn more about managing your cluster
- **[Voyager Troubleshooting](../voyager-troubleshoot/)**: Common issues and solutions
- **[Known Issues](../known-issues/)**: Limitations and workarounds
