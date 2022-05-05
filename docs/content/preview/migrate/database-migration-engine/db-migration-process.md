---
title: Database migration process
headerTitle: Database migration process
linkTitle: Database migration process
description: Overview of the yb_migrate database engine for migrating data and applications from other databases to YugabyteDB.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: db-migration-process
    parent: db-migration-engine
    weight: 700
isTocNested: true
showAsideToc: true
---

Migrate from the source database to the target database using the following steps:

<!-- - Prepare the source database.
- Prepare the target database.
- Generate report.
- Export schema.
- Manually edit schema.
- Export data.
- Import schema.
- Import data.
- Verify target database. -->
<!--
Following sections provide details of each of the above steps. -->

### Prepare the source database

- Create a database user and provide the user with READ access to all the resources which need to be migrated. For PostgreSQL and MySQL, yb_migrate also needs READ access on tables/views from the `information_schema`.

- You'll need to provide the user and the source database details in the subsequent invocations of yb_migrate. For convenience, you can populate the information in the following environment variables:

```sh
export SOURCE_DB_TYPE=oracle
export SOURCE_DB_HOST=localhost
export SOURCE_DB_PORT=1521
export SOURCE_DB_USER=sakila
export SOURCE_DB_PASSWORD=password
export SOURCE_DB_NAME=pdb1
export SOURCE_DB_SCHEMA=sakila
```

Replace values of the above environment variables as per your database details. SOURCE_DB_TYPE can be one of [`postgresql`, `mysql`, `oracle`].

- If you want yb_migrate to connect to the source database over SSL, refer to [SSL Connectivity](/../../database-migration-engine/references/#ssl-connectivity) in the References section.

### Prepare the target database

- Create the target database in the YugabyteDB cluster. The database name can be the same or different from the source database name. If the target database name is not provided, yb_migrate assumes the same name as the source database.

```sql
CREATE DATABASE sakila;
```

- For convenience, capture the database name in an environment variable.

```sh
export TARGET_DB_NAME=sakila
```

- Create a role with the superuser privileges. yb_migrate will use the role to connect to the target database. Capture the user and database details in environment variables.

```sh
export TARGET_DB_HOST=127.0.0.1
export TARGET_DB_PORT=5433
export TARGET_DB_USER=yugabyte
export TARGET_DB_PASSWORD=password
```

By default, yb_migrate creates tables in the `public` schema. To migrate the database in a non-public schema, you should provide the name of the target schema and yb_migrate takes care of creating it.

- If you want yb_migrate to connect to the target database over SSL, refer to [SSL Connectivity](/../../database-migration-engine/references/#ssl-connectivity) in the References section.

### Create an export directory

Create an export directory in the local file system on the migrator machine. yb_migrate uses the directory to store source data, schema files, and migration state. The file system in which the directory resides must have enough free space to hold the entire source database. Create the directory and place its path in an environment variable.

```sh
mkdir -p ~/export-dirs/sakila
export EXPORT_DIR=~/export-dirs/sakila
```

### Generate report

<!-- Using `ora2pg` and `pg_dump`, yb_migrate can extract and convert the source database schema to an equivalent PostgreSQL schema. The schema may not be suitable yet, to be imported into YugabyteDB and may require minor changes. -->

<!-- Refer [this document](#https://docs.google.com/document/d/1jCLiHDEHiYpgVObILDC_2Ormr-Kx36YhkqHXUCVGO1Q/edit#) to know more about modeling data for YugabyteDB
The above doc needs to be into a new page-->

The `yb_migrate generateReport` command analyses the PostgreSQL schema and prepares a report that lists the DDL statements that need changes. Here is a sample invocation of the command:

```sh
yb_migrate generateReport --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA} \
        --output-format txt
```

The `--output-format` can be `html`, `txt`, `json`, or `xml`.

The above command generates a report file under `EXPORT_DIR/reports/`.

### Export schema

The `yb_migrate export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL), and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories. Here is a sample invocation of the command:

```sh
yb_migrate export schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

### Manually edit the schema

Fix all the issues listed in the generated migration report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

### Export data

Run the `yb_migrate export data` command to dump the source data into the `EXPORT_DIR/data` directory.

```sh
yb_migrate export data --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

The options passed to the command are similar to the `export schema` command. To export only a subset of the tables, pass a comma separated list of table names in the `--table-list` argument. To speed up the data export of larger source databases, you can pass values greater than 1 to the `--parallel-jobs` argument. It will cause yb_migrate to dump multiple tables concurrently.

### Import the schema

Use the `yb_migrate import schema` command to import the schema.

```sh
yb_migrate import schema --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

If yb_migrate terminates before it imports the entire schema, you can rerun it by adding `--ignore-exist` option.

{{< note title="Note" >}}

The `yb_migrate import schema` command does not import indexes yet. This is done to speed up the data import phase. The indexes will be created by `yb_migrate import data` command after importing the data.

{{< /note >}}

### Import data

After you have successfully exported the source data and imported the schema in the target database, you can now import the data using the `yb_migrate import data` command:

```sh
yb_migrate import data --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

The `yb_migrate import data` command reads data files located in the `EXPORT_DIR/data`. The command, by default, creates one database connection to each of the nodes of the target YugabyteDB cluster. You can increase the number of connections by specifying the total connection count in the `--parallel-jobs` argument. The command will equally distribute the connections among all the nodes of the cluster. It splits the larger tables into smaller chunks, each containing at most `--batch-size` number of records. By default, the `--batch-size` is 100,000 records.

Run the `yb_migrate import data status --export-dir ${EXPORT_DIR}` command to get the overall progress of the data import operation.While importing a very large database, you should run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops. If the `yb_migrate import data` command terminates before it could complete the data ingestion, you can rerun it with the same arguments and the command will resume the data import operation.
After successfully loading the data, the command creates the indexes listed in the schema.

### Verify target database

After the successful execution of the `yb_migrate import data` command, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count in each table.
<!-- The validation queries can be as simple as checking the row count in each table or it can utilise some domain knowledge e.g. match the sum of the `amount` column in the `payments` table. -->
