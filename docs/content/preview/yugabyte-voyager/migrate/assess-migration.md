---
title: YB Voyager Migration Assessment
headerTitle: Migration assessment
linkTitle: Migration assessment
headcontent: Assess the migration complexity and get schema changes, data distribution and cluster sizing
description: Steps to create a migration assessment report to ensure successful migration using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: assess-migration
    parent: migration-types
    weight: 101
tags:
  feature: tech-preview
type: docs
---

The Voyager Migration Assessment feature streamlines database migration from PostgreSQL and Oracle to YugabyteDB. It analyzes the source database, captures essential metadata, and generates a report with recommended migration strategies and cluster configurations for optimal performance with YugabyteDB.

## Overview

When you run an assessment, Voyager gathers key metadata and metrics from the source database, such as table column details, sizes of tables and indexes, and read/write IOPS.

![Migration Assessment Overview](/images/migrate/voyager-migration-assessment-overview.png)

Voyager then generates a report that includes:

- **Recommended schema changes:** Analyzes compatibility with YugabyteDB, highlighting unsupported features and data types. Also, analyzes the schema for any caveats to ensure smooth migration.
- **Recommended cluster sizing:** Estimates the resources needed for the target environment based on table sizes, number of tables, and throughput requirements.
- **Recommended data distribution:** Suggests effective sharding strategies for tables and indexes.
- **Performance metrics:** Analyzes workload characteristics to recommend optimizations in YugabyteDB.
- **Performance optimizations:** Identifies schema DDLs that may impact application performance after migrating to YugabyteDB. It highlights potentially inefficient DDLs and provides recommendations to optimize them for better performance.
- **Migration time estimate:** Provides an estimated time for data import into YugabyteDB based on experimental data.
- **Unsupported query constructs:** Identifies SQL features and constructs not supported by YugabyteDB, such as advisory locks, system columns, and XML functions, and provides a list of queries containing these constructs.
- **Unsupported PL/pgSQL objects:** Identifies SQL features and constructs that are not supported by YugabyteDB, such as advisory locks, system columns, and XML functions, within PL/pgSQL objects in the source schema. It reports the individual queries within these objects that are not supported, such as queries in the PL/pgSQL block for functions and procedures, or the select statements in views and materialized views that contain unsupported constructs.

When running migration assessment, keep in mind the following:

- The recommendations are based on testing using a [RF3](../../../architecture/docdb-replication/replication/#replication-factor) YugabyteDB cluster on instance types with 4GiB memory per core and running v2024.1.

- To detect unsupported query constructs, ensure the [pg_stat_statements extension](../../../explore/ysql-language-features/pg-extensions/extension-pgstatstatements/) is properly installed and enabled on source.

- To disable unsupported query construct detection, set the environment variable `REPORT_UNSUPPORTED_QUERY_CONSTRUCTS=false`.

- To disable unsupported PL/pgSQL object detection, set the environment variable `REPORT_UNSUPPORTED_PLPGSQL_OBJECTS=false`.

- To detect certain performance optimizations, ensure that [ANALYZE](https://www.postgresql.org/docs/current/sql-analyze.html) (PostgreSQL) is run on the source database.

The following table describes the type of data that is collected during a migration assessment.

| Data | Collected | Details |
| :--- | :-------- | :------ |
| Application or user data  | No | No application or user data is collected. |
| Passwords | No | The assessment does not store any passwords. |
| Database metadata<br>schema,&nbsp;object,&nbsp;object&nbsp;names | Yes | Voyager collects the schema metadata including table IOPS, table size, and so on, and the actual schema. |
| Database name | Yes | Voyager collects database and schema names to be used in the generated report. |
| Performance metrics | Optional | Voyager captures performance metrics from the database (IOPS) for rightsizing the target environment. |
| Server or database credentials | No | No server or database credentials are collected. |

## Prepare for migration assessment

Before you run a migration assessment, do the following:

1. [Install yb-voyager](../../install-yb-voyager/).

1. Prepare the source database as follows: <br>

    Create a new database user, and assign the necessary user permissions.
    <ul class="nav nav-tabs nav-tabs-yb">
      <li >
        <a href="#postgresql" class="nav-link active" id="postgresql-tab" data-bs-toggle="tab" role="tab" aria-controls="postgresql" aria-selected="true">
          <i class="icon-postgres" aria-hidden="true"></i>
          PostgreSQL
        </a>
      </li>
      <li>
        <a href="#oracle" class="nav-link" id="oracle-tab" data-bs-toggle="tab" role="tab" aria-controls="oracle" aria-selected="true">
          <i class="icon-oracle" aria-hidden="true"></i>
          Oracle
        </a>
      </li>
    </ul>

    <div class="tab-content">
      <div id="postgresql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="postgresql-tab">
      {{% includeMarkdown "./postgresql.md" %}}
      </div>
      <div id="oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="oracle-tab">
      {{% includeMarkdown "./oracle.md" %}}
      </div>
    </div>

1. Create an export directory on a file system that has enough space to keep the entire source database. Ideally, create this export directory inside a parent folder named after your migration for better organization. For example:

    ```sh
    mkdir -p $HOME/<migration-name>/export-dir
    ```

    You need to provide the full path to your export directory in the `export-dir` parameter of your [configuration file](../../reference/configuration-file/), or in the `--export-dir` flag when running `yb-voyager` commands.

1. Set up a [configuration file](../../reference/configuration-file/) to specify the parameters required when running Voyager commands (v2025.6.2 or later).

    To get started, copy the `offline-migration.yaml` template configuration file from one of the following locations to the migration folder you created (for example, `$HOME/my-migration/`):<br><br>

    {{< tabpane text=true >}}

  {{% tab header="Linux (apt/yum/airgapped)" lang="linux" %}}

```bash
/opt/yb-voyager/config-templates/offline-migration.yaml
```

  {{% /tab %}}

  {{% tab header="MacOS (Homebrew)" lang="macos" %}}

```bash
$(brew --cellar)/yb-voyager@<voyager-version>/<voyager-version>/config-templates/offline-migration.yaml
```

Replace `<voyager-version>` with your installed Voyager version, for example, `2025.5.2`.

  {{% /tab %}}

  {{< /tabpane >}}

    Set the export-dir and source arguments in the configuration file:

    ```yaml
    # Replace the argument values with those applicable for your migration.

    export-dir: <absolute-path-to-export-dir>

    source:
      db-type: <source-db-type>
      db-host: <source-db-host>
      db-port: <source-db-port>
      db-name: <source-db-name>
      db-schema: <source-db-schema> # Not applicable for MySQL
      db-user: <source-db-user>
      db-password: <source-db-password> # Enclose the password in single quotes if it contains special characters.
    ```

## Assess migration

Assess your migration using the following steps:

1. Run the assessment.

    Voyager supports two primary modes for conducting migration assessments, depending on your access to the source database as follows:<br><br>

    {{< tabpane text=true >}}

    {{% tab header="With source database connectivity" %}}

This mode requires direct connectivity to the source database from the client machine where voyager is installed. You initiate the assessment by executing the `assess-migration` command of `yb-voyager`. This command facilitates a live analysis by interacting directly with the source database, to gather metadata required for assessment. A sample command is as follows:

```sh
yb-voyager assess-migration --source-db-type postgresql \
    --source-db-host hostname --source-db-user ybvoyager \
    --source-db-password password --source-db-name dbname \
    --source-db-schema schema1,schema2 --export-dir /path/to/export/dir
```

If you are using a [configuration file](../../reference/configuration-file/), use the following:

```sh
yb-voyager assess-migration --config-file <path-to-config-file>
```

    {{% /tab %}}

    {{% tab header="Without source database connectivity" %}}

PostgreSQL only. In situations where direct access to the source database is restricted, there is an alternative approach. Voyager includes packages with scripts for PostgreSQL at `/etc/yb-voyager/gather-assessment-metadata`.

You can perform the following steps with these scripts:

1. On a machine which has access to the source database, copy the scripts and install dependencies psql and pg_dump version 14 or later. Alternatively, you can install yb-voyager on the machine to automatically get the dependencies.

1. Run the `yb-voyager-pg-gather-assessment-metadata.sh` script by providing the source connection string, the schema names, path to a directory where metadata will be saved, and an optional argument of an interval to capture the IOPS metadata of the source (in seconds with a default value of 120). For example:

    ```sh
    /path/to/yb-voyager-pg-gather-assessment-metadata.sh 'postgresql://ybvoyager@host:port/dbname' 'schema1|schema2' '/path/to/assessment_metadata_dir' '60'
    ```

1. Copy the metadata directory to the client machine on which voyager is installed, and run the `assess-migration` command by specifying the path to the metadata directory as follows:

    ```sh
    yb-voyager assess-migration --source-db-type postgresql \
        --assessment-metadata-dir /path/to/assessment_metadata_dir --export-dir /path/to/export/dir
    ```

    If you are using a [configuration file](../../reference/configuration-file/), use the following:

    ```sh
    yb-voyager assess-migration --config-file <path-to-config-file>
    ```

    {{% /tab %}}

    {{< /tabpane >}}

    The output is a migration assessment report, and its path is printed on the console.

    {{< warning title="Important" >}}
For the most accurate migration assessment, the source database must be actively handling its typical workloads at the time the metadata is gathered. This ensures that the recommendations for sharding strategies and cluster sizing are well-aligned with the database's real-world performance and operational needs.
    {{< /warning >}}

1. View the assessment report by configuring the [yugabyted](/preview/reference/configuration/yugabyted/) UI. Use the yugabyted UI to review the assessment report, which includes migration strategies, complexity, and effort estimates.
    1. Start a local YugabyteDB cluster. Refer to the steps described in [Use a local cluster](/preview/tutorials/quick-start/macos/).

        {{< note title="Note" >}}
  After a migration assessment, if you choose to migrate using the open source YugabyteDB, you will be using this same local cluster as your [target database](../../introduction/#target-database).
        {{< /note >}}

    1. To see the Voyager migration workflow details in the UI, set the following configuration parameters before starting the assessment:

        ```yaml
        ### Control plane type refers to the deployment type of YugabyteDB
        control-plane-type: yugabyted

        ### YSQL connection string
        ### Provide the standard PostgreSQL connection parameters, including user name,
        ### host name, and port. For example, postgresql://yugabyte:yugabyte@127.0.0.1:5433
        yugabyted-db-conn-string: postgresql://yugabyte:yugabyte@127.0.0.1:5433
        ```

        Provide the standard PostgreSQL connection parameters, including user name, host name, and port. For example, `postgresql://yugabyte:yugabyte@127.0.0.1:5433`

        {{< note title="Note" >}}

  Don't include the `dbname` parameter in the connection string; the default `yugabyte` database is used to store the meta information for showing the migration in the yugabyted UI.
        {{< /note >}}

    1. After generating the report, navigate to the **Migrations** tab in the yugabyted UI at <http://127.0.0.1:15433> to see the available migrations.

        ![Migration Landing Page](/images/migrate/migration-list-page.png)
        ![Migration Assessment Page](/images/migrate/ybd-assessment-page.png)

        {{<tip title="Report recommendations">}}
Depending on the recommendations in the assessment report, do the following when you proceed with migration:

1. Create your target YugabyteDB cluster in [Enhanced PostgreSQL Compatibility Mode](../../../reference/configuration/postgresql-compatibility/).

    If you are using YugabyteDB Anywhere, [enable compatibility mode](../../../reference/configuration/postgresql-compatibility/#yugabytedb-anywhere) by setting the **More > Edit Postgres Compatibility** option.
1. The assessment provides recommendations on which tables in the source database to colocate, so when you prepare your target database, create the database with [colocation](../../../explore/colocation/#databases) set to TRUE.
        {{</tip>}}

1. Proceed with migration with one of the migration workflows:

    - [Offline migration](../../migrate/migrate-steps/)
    - [Live migration](../../migrate/live-migrate/)
    - [Live migration with fall-forward](../../migrate/live-fall-forward/)
    - [Live migration with fall-back](../../migrate/live-fall-back/)

## Assess a fleet of databases (Oracle only)

Use the Bulk Assessment command ([assess-migration-bulk](../../reference/assess-migration/#assess-migration-bulk)) to assess multiple schemas across one or more database instances simultaneously. It offers:

- Multi-Schema Assessment: Assess multiple schemas in different database instances with a single command, simplifying migration planning.
- Centralized Reporting: All assessment reports are generated and stored in one organized directory, making

### Command

To perform a bulk assessment, use the following command syntax:

```sh
yb-voyager assess-migration-bulk \
    --fleet-config-file /path/to/fleet_config_file.csv \
    --bulk-assessment-dir /path/to/bulk-assessment-dir \
    [--continue-on-error true|false] \
    [--start-clean true|false]
```

### Fleet configuration file

Bulk assessment is managed using a fleet configuration file, which specifies the schemas to be assessed. The file is in CSV format.

- Header Row: The first row contains headers that define the fields for each schema.
- Schema Rows: Each subsequent row corresponds to a different schema to be assessed.

The following table outlines the fields that can be included in the fleet configuration file.

| <div style="width:180px">Field</div> | Description |
| :--- | :--- |
| source-db-type | Required. The type of source database. Currently, only Oracle is supported. |
| source-db-user | Required. The username used to connect to the source database. |
| source-db-password | Optional. The password for the source database user. If not provided, you will be prompted for the password during assessment of that schema. |
| source-db-schema | Required. The specific schema in the source database to be assessed. |
| source-db-host | Optional. The hostname or IP address of the source database server. |
| source-db-port | Optional. The port number on which the source database is running. This is required if `oracle-tns-alias` is not used. |
| source-db-name | Optional. The database name for connecting to the Oracle database. This is required if `oracle-db-sid` or `oracle-tns-alias` is not used. |
| oracle-db-sid | Optional. The Oracle System Identifier (SID). This is required if `source-db-name` or `oracle-tns-alias` is not used. |
| oracle-tns-alias | Optional. The TNS alias used for Oracle databases, which can include connection details such as host, port, and service name. This is required if `source-db-name` or `oracle-db-sid` is not used. |

The following is an example fleet configuration file.

```text
source-db-type,source-db-host,source-db-port,source-db-name,oracle-db-sid,oracle-tns-alias,source-db-user,source-db-password,source-db-schema
oracle,example-host1,1521,ORCL,,,admin,password,schema1
oracle,example-host2,1521,,ORCL_SID,,admin,password,schema2
oracle,,,,,tns_alias,oracle_user,password,schema3
```

### Directory structure

After the bulk assessment is completed, the top-level directory specified using the `--bulk-assessment-dir` flag includes subdirectories for each assessed schema. Additionally, a top-level report is generated that provides links to the individual assessment reports for each schema.

```sh
/bulk-assessment-dir/
├── bulk_assessment_report.html
├── bulk_assessment_report.json
├── DBNAME-SCHEMA1-export-dir/
│    └── assessment/
│          └── reports/
│                 ├── migration_assessment_report.html
│                 └── migration_assessment_report.json
├── SID-SCHEMA2-export-dir/
│    └── assessment/
│          └── reports/
│                 ├── migration_assessment_report.html
│                 └── migration_assessment_report.json
└── logs/
     └── yb-voyager-assess-migration-bulk.log
```

## Learn more

- [Assess migration CLI](../../reference/assess-migration/)
