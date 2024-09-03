---
title: YB Voyager Migration Assessment
headerTitle: Migration assessment
linkTitle: Migration assessment
headcontent: Assess the migration complexity and get schema chnages, data distirbution and cluster sizing
description: Steps to create a migration assessment report to ensure successful migration using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: assess-migration
    parent: migration-types
    weight: 101
badges: tp
type: docs
---

The Voyager Migration Assessment feature streamlines database migration from PostgreSQL and Oracle to YugabyteDB. It analyzes the source database, captures essential metadata, and generates a report with recommended migration strategies and cluster configurations for optimal performance with YugabyteDB.

## Overview
When you run an assessment, Voyager gathers key metadata and metrics from the source database, such as table column details, sizes of tables and indexes, and read/write IOPS. It then generates a report that includes:

- **Recommended schema changes:** Analyzes compatibility with YugabyteDB, highlighting unsupported features and data types.

- **Recommended cluster sizing:** Estimates the resources needed for the target environment based on table sizes, number of tables, and throughput requirements.

- **Recommended data distribution:** Suggests effective sharding strategies for tables and indexes.

- **Performance metrics:** Analyzes workload characteristics to recommend optimizations in YugabyteDB.

- **Migration time estimate:** Provides an estimated time for data import into YugabyteDB based on experimental data.


{{< warning title="Note" >}}
The recommendations are based on testing using a [RF3](../../../architecture/docdb-replication/replication/#replication-factor) YugabyteDB cluster on instance types with 4GiB memory per core and running v2024.1.
{{< /warning >}}


The following table describes the type of data that is collected during a migration assessment.

| Data | Collected | Details |
| :--- | :-------- | :------ |
| Application or user data  | No | No application or user data is collected. |
| Passwords | No | The assessment does not store any passwords. |
| Database metadata<br>schema,&nbsp;object,&nbsp;object&nbsp;names | Yes | Voyager collects the schema metadata including table IOPS, table size, and so on, and the actual schema. |
| Database name | Yes | Voyager collects database and schema names to be used in the generated report. |
| Performance metrics | Optional | Voyager captures performance metrics from the database (IOPS) for rightsizing the target environment. |
| Server or database credentials | No | No server or database credentials are collected. |


## Get started with migration assessment

1. [Install yb-voyager](../../install-yb-voyager/).
1. Prepare the source database.

    {{< tabpane text=true >}}

      {{% tab header="PostgreSQL" %}}

1. Create a new user, `ybvoyager` as follows:

    ```sql
    CREATE USER ybvoyager PASSWORD 'password';
    ```

1. Grant necessary permissions to the `ybvoyager` user.

    ```sql
    /* Switch to the database that you want to migrate.*/
    \c <database_name>

    /* Grant the USAGE permission to the ybvoyager user on all schemas of the database.*/

    SELECT 'GRANT USAGE ON SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \gexec

    /* The above SELECT statement generates a list of GRANT USAGE statements which are then executed by psql because of the \gexec switch. The \gexec switch works for PostgreSQL v9.6 and later. For older versions, you'll have to manually execute the GRANT USAGE ON SCHEMA schema_name TO ybvoyager statement, for each schema in the source PostgreSQL database. */

    /* Grant SELECT permission on all the tables. */

    SELECT 'GRANT SELECT ON ALL TABLES IN SCHEMA ' || schema_name || ' TO ybvoyager;' FROM information_schema.schemata; \gexec
    ```

1. Execute the following command to refresh statistics for all tables in your database or schema:

    ```sql
    ANALYZE;
    ```

    {{% /tab %}}

    {{% tab header="Oracle" %}}

1. Create a role that has the privileges as listed in the following table:

   | Permission | Object type in the source schema |
   | :--------- | :---------------------------------- |
   | `SELECT` | VIEW, SEQUENCE, TABLE PARTITION, TABLE, SYNONYM, MATERIALIZED VIEW |
   | `EXECUTE` | TYPE |

   Change the `<SCHEMA_NAME>` appropriately in the following snippets, and run the following steps as a privileged user.

   ```sql
   CREATE ROLE <SCHEMA_NAME>_reader_role;

   BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type in ('VIEW','SEQUENCE','TABLE PARTITION','SYNONYM','MATERIALIZED VIEW'))
       LOOP
          EXECUTE IMMEDIATE 'grant select on '||R.owner||'."'||R.object_name||'" to <SCHEMA_NAME>_reader_role';
       END LOOP;
   END;
   /

   BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type ='TABLE' MINUS SELECT owner, table_name from all_nested_tables where owner = UPPER('<SCHEMA_NAME>'))
       LOOP
          EXECUTE IMMEDIATE 'grant select on '||R.owner||'."'||R.object_name||'" to  <SCHEMA_NAME>_reader_role';
       END LOOP;
   END;
   /

   BEGIN
       FOR R IN (SELECT owner, object_name FROM all_objects WHERE owner=UPPER('<SCHEMA_NAME>') and object_type = 'TYPE')
       LOOP
          EXECUTE IMMEDIATE 'grant execute on '||R.owner||'."'||R.object_name||'" to <SCHEMA_NAME>_reader_role';
       END LOOP;
   END;
   /

   GRANT SELECT_CATALOG_ROLE TO <SCHEMA_NAME>_reader_role;
   GRANT SELECT ANY DICTIONARY TO <SCHEMA_NAME>_reader_role;
   GRANT SELECT ON SYS.ARGUMENT$ TO <SCHEMA_NAME>_reader_role;

   ```

1. Create a user `ybvoyager` and grant `CONNECT` and `<SCHEMA_NAME>_reader_role` to the user:

   ```sql
   CREATE USER ybvoyager IDENTIFIED BY password;
   GRANT CONNECT TO ybvoyager;
   GRANT <SCHEMA_NAME>_reader_role TO ybvoyager;
   ```

1. Execute the following PL/SQL block to gather up-to-date statistics for your schema:

```sql
BEGIN
  DBMS_STATS.GATHER_SCHEMA_STATS('YOUR_SCHEMA_NAME');
END;
/
```

    {{% /tab %}}

{{< /tabpane >}}

1. Assess migration - Voyager supports two primary modes for conducting migration assessments, depending on your access to the source database as follows:

    1. **With source database connectivity**: This mode requires direct connectivity to the source database from the client machine where voyager is installed. You initiate the assessment by executing the `assess-migration` command of `yb-voyager`. This command facilitates a live analysis by interacting directly with the source database, to gather metadata required for assessment. A sample command is as follows:

        ```sh
        yb-voyager assess-migration --source-db-type postgresql \
            --source-db-host hostname --source-db-user ybvoyager \
            --source-db-password password --source-db-name dbname \
            --source-db-schema schema1,schema2 --export-dir /path/to/export/dir
        ```

    1. **Without source database connectivity** (only PostgreSQL): In situations where direct access to the source database is restricted, there is an alternative approach. Voyager includes packages with scripts for PostgreSQL at `/etc/yb-voyager/gather-assessment-metadata`. You can perform the following steps with these scripts:

        1. On a machine which has access to the source database, copy the scripts and install dependencies psql, and pg_dump version 14 or later. Alternatively, you can install yb-voyager on the machine to automatically get the dependencies.
        1. Run the `yb-voyager-pg-gather-assessment-metadata.sh` script by providing the source connection string, the schema names, path to a directory where metadata will be saved, and an optional argument of an interval to capture the IOPS metadata of the source (in seconds with a default value of 120). For example,

            ```sh
            /path/to/yb-voyager-pg-gather-assessment-metadata.sh 'postgresql://ybvoyager@host:port/dbname' 'schema1|schema2' '/path/to/assessment_metadata_dir' '60'
            ```

        1. Copy the metadata directory to the client machine on which voyager is installed, and run the `assess-migration` command by specifying the path to the metadata directory as follows:

            ```sh
            yb-voyager assess-migration --source-db-type postgresql \
                 --assessment-metadata-dir /path/to/assessment_metadata_dir --export-dir /path/to/export/dir
            ```

        The output of both the methods is a migration assessment report, and its path is printed on the console.

      {{< warning title="Important" >}}
For the most accurate migration assessment, the source database must be actively handling its typical workloads at the time the metadata is gathered. This ensures that the recommendations for sharding strategies and cluster sizing are well-aligned with the database's real-world performance and operational needs.
      {{< /warning >}}

1. Create a target YugabyteDB cluster as follows:

    1. Create a cluster in [Enhanced Postgres Compatibility Mode](/preview/releases/ybdb-releases/v2024.1/#highlights) based on the sizing recommendations in the assessment report. For a universe in YugabyteDB Anywhere, [enable the compatibility mode](/preview/releases/yba-releases/v2024.1/#highlights) by setting some flags on the universe.
    1. Create a database with colocation set to TRUE.

        ```sql
        CREATE DATABASE <TARGET_DB_NAME> with COLOCATION=TRUE;
        ```

1. Proceed with migration with one of the migration workflows:

    - [Offline migration](../../migrate/migrate-steps/)
    - [Live migration](../../migrate/live-migrate/)
    - [Live migration with fall-forward](../../migrate/live-fall-forward/)
    - [Live migration with fall-back](../../migrate/live-fall-back/)


## Migration assessment of a fleet of databases (Oracle only)
The Bulk Assessment command ('assess-migration-bulk') allows you to assess multiple schemas across one or more database instances simultaneously. It offers:
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
  
The table below outlines the fields that can be included in the fleet configuration file.

| Field | Description |
| :--- | :--- |
| source-db-type | Required. The type of source database. Currently, only Oracle is supported. |
| source-db-user | Required. The username used to connect to the source database. |
| source&#8209;db&#8209;password | Optional. The password for the source database user. If not provided, you will be prompted for the password during assessment of that schema. |
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
├── bulkAssessmentReport.html
├── bulkAssessmentReport.json
├── DBNAME-SCHEMA1-export-dir/
│    └── assessment/
│          └── reports/
│                 ├── assessmentReport.html
│                 └── assessmentReport.json
├── SID-SCHEMA2-export-dir/
│    └── assessment/
│          └── reports/
│                 ├── assessmentReport.html
│                 └── assessmentReport.json
└── logs/
     └── yb-voyager-assess-migration-bulk.log
```
## Visualize the Migration Assessment report

[yugabyted](/preview/reference/configuration/yugabyted/) UI allows you to visualize the database migrations performed by YugabyteDB Voyager. The UI provides details of migration complexity, SQL objects details from the source database, YugabyteDB sharding strategy, conversion issues (if any), and also allows you to track the percentage completion of data export from the source database and data import to the target YugabyteDB cluster.

### Prerequisite

Before you begin the Voyager migration, start a local YugabyteDB cluster. Refer to the steps described in [Use a local cluster](/preview/quick-start/).

### Send Voyager details to a local YugabyteDB cluster

Set the following environment variables before starting the migration:

```sh
export CONTROL_PLANE_TYPE=yugabyted
export YUGABYTED_DB_CONN_STRING=<ysql-connection-string-to-yugabyted-instance>
```

For example, `postgresql://yugabyte:yugabyte@127.0.0.1:5433`

### Assess Migration

Voyager Migration Assessment conducts a detailed analysis of the source database by capturing essential metadata and metrics. Yugabyted UI allows you to go over the assessment report which includes recommendations of effective migration strategies, migration complexity, and provides an overview on effort involved in migrating from the source database.

After [generating a Migration Assessment Report](#generate-a-migration-assessment-report), from yugabyted UI, navigate to **Migrations** tab, available at [http://127.0.0.1:15433](http://127.0.0.1:15433) to see a list of the available migrations.

![Migration Landing Page](/images/migrate/ybd-landing-page.png)

#### Migration Assessment UI

![Migration Assessment Page](/images/migrate/ybd-assessment-page.png)

## Learn more

- [Assess migration CLI](../../reference/assess-migration/)
