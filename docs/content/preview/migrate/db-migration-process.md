---
title: Database migration process
headerTitle: Database migration process
linkTitle: Database migration process (beta)
description: Overview of the yb_migrate database engine for migrating data and applications from other databases to YugabyteDB.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
menu:
  preview:
    identifier: db-migration-process
    parent: migrate
    weight: 730
isTocNested: true
showAsideToc: true
---

This page describes the migration process from other RDBMS (PostgreSQL, MySQL or Oracle) to YugabyteDB (YSQL API).

## Prerequisites

### 1. Install yb_migrate

Set up a machine which satisfies the [Migrator machine requirements](../../reference/connectors/yb-migration-reference/#migrator-machine-requirements) using the following steps:

- Clone the yb_migrate repository.

```sh
git clone https://github.com/yugabyte/yb-db-migration.git
```

- Change the directory to `yb-db-migration/installer_scripts`.

```sh
cd yb-db-migration/installer_scripts
```

- Depending on the Linux distribution (CentOS or Ubuntu) you're running, execute the appropriate installer script:

```sh
//CentOS
./yb_migrate_installer__centos.sh
```

```sh
//Ubuntu
./yb_migrate_installer__ubuntu.sh
```

It is safe to execute the script multiple times. On each run, the script regenerates the `yb_migrate` executable based on the latest commit in the git repository. If the script fails for some reason, check the `yb_migrate_installer.log` in the current working directory.

- The script generates a `.yb_migrate_installer_bashrc` file in the home directory. Source the file to ensure that the correct environment variables are set using the following command:

```sh
source ~/.yb_migrate_installer_bashrc
```

- Check that yb_migrate is installed using the following command:

```sh
yb_migrate --help
```

### 2. Create an export directory

Create an export directory in the local file system on the migrator machine. yb_migrate uses the directory to store source data, schema files, and the migration state. The file system in which the directory resides must have enough free space to hold the entire source database. Create the directory and place its path in an environment variable.

```sh
mkdir -p ~/export-dirs/sakila
export EXPORT_DIR=~/export-dirs/sakila
```

Proceed with the database migration from your source database to YugabyteDB (YSQL API) using the following steps:

## Step 1: Prepare the source database

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#postgresql" class="nav-link active" id="postgresql-tab" data-toggle="tab" role="tab" aria-controls="postgresql" aria-selected="true">
      <i class="icon-postgres" aria-hidden="true"></i>
      PostgreSQL
    </a>
  </li>
  <li>
    <a href="#mysql" class="nav-link" id="mysql-tab" data-toggle="tab" role="tab" aria-controls="mysql" aria-selected="false">
      <i class="icon-mysql" aria-hidden="true"></i>
      MySQL
    </a>
  </li>
  <li>
    <a href="#oracle" class="nav-link" id="oracle-tab" data-toggle="tab" role="tab" aria-controls="oracle" aria-selected="false">
      <i class="icon-oracle" aria-hidden="true"></i>
      Oracle
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="postgresql" class="tab-pane fade show active" role="tabpanel" aria-labelledby="postgresql-tab">
    {{% includeMarkdown "./postgresql.md" /%}}
  </div>
  <div id="mysql" class="tab-pane fade" role="tabpanel" aria-labelledby="mysql-tab">
    {{% includeMarkdown "./mysql.md" /%}}
  </div>
  <div id="oracle" class="tab-pane fade" role="tabpanel" aria-labelledby="oracle-tab">
    {{% includeMarkdown "./oracle.md" /%}}
  </div>
</div>

{{< note title="Note" >}}

Currently `yb_migrate` supports migrating all schemas of the source database. It does not support migrating _only a subset_ of the schemas.

{{< /note >}}

- If you want yb_migrate to connect to the source database over SSL, refer to [SSL Connectivity](../../reference/connectors/yb-migration-reference/#ssl-connectivity) in the Reference section.

## Step 2: Prepare the target database

### Create the target database

- Create the target database in a YugabyteDB cluster. The database name can be same or different from the source database name. If the target database name is not provided, yb_migrate assumes the same name as the source database. If you choose the target database name different from the source database name, you'll have to provide the `--target-db-name` argument to the `yb_migrate import` commands.

```sql
CREATE DATABASE sakila;
```

- Capture the database name in an environment variable.

```sh
export TARGET_DB_NAME=sakila
```

### Create a user

User creation steps differ depending on the type of YugabyteDB deployment and version.

- For YugabyteDB Managed or YugabyteDB Anywhere versions (2.13.1 and above) or (2.12.4 and above), create a user with `yb_db_admin` and `yb_extension` role using the following commands:

```sql
CREATE USER ybmigrate PASSWORD 'password';
GRANT yb_db_admin TO ybmigrate;
GRANT yb_extension TO ybmigrate;
```

- For YugabyteDB Anywhere versions below (2.13.1 or 2.12.4), create a user and role with the superuser privileges.

```sql
CREATE USER ybmigrate SUPERUSER PASSWORD 'password';
```

- Capture the user and database details in environment variables.

```sh
export TARGET_DB_HOST=127.0.0.1
export TARGET_DB_PORT=5433
export TARGET_DB_USER=ybmigrate
export TARGET_DB_PASSWORD=password
```

If you want yb_migrate to connect to the target database over SSL, refer to [SSL Connectivity](./../reference/connectors/yb-migration-reference/#ssl-connectivity) in the References section.

{{< warning title="Warning while deleting the ybmigrate user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybmigrate` user. You should transfer the ownership of the objects to some other user (example: `yugabyte`) and then delete the `ybmigrate` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybmigrate TO yugabyte;
DROP OWNED BY ybmigrate;
DROP USER ybmigrate;
```

{{< /warning >}}

## Step 3: Export and analyze schema

### Export schema

`yb_migrate export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL); and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

An example invocation of the command is as follows:

```sh
yb_migrate export schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

### Analyze Schema

Using [ora2pg](https://ora2pg.darold.net) and [pg_dump](https://www.postgresql.org/docs/current/app-pgdump.html), yb_migrate can extract and convert the source database schema to an equivalent PostgreSQL schema. The schema, however, may not be suitable yet to be imported into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need some minor changes to the schema.

Refer to [Data modeling](/../../database-migration-engine/references/#data-modeling) to know more about modeling data for YugabyteDB.

The `yb_migrate analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) phase and prepares a report that lists the DDL statements which need changes. An example invocation of the command is as follows:

```sh
yb_migrate analyze-schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA} \
        --output-format txt
```

The `--output-format` can be `html`, `txt`, `json`, or `xml`. The above command generates a report file under the `EXPORT_DIR/reports/` directory.

### Manually edit the schema

- Fix all the issues listed in the generated schema analysis report by manually editing the SQL DDL files from the `EXPORT_DIR/schema/*`.

- Re-run the `yb_migrate analyze-schema` command after making the manual changes. The command will generate a fresh report taking into account your changes. Repeat these steps until the generated report contains no issues.

## Step 4: Export data

Dump the source data into the `EXPORT_DIR/data` directory using the `yb_migrate export data` command as follows:

```sh
yb_migrate export data --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

The options passed to the command are similar to the `yb_migrate export schema` command. To export only a subset of the tables, pass a comma separated list of table names in the `--table-list` argument. To speed up the data export of larger source databases, you can pass values greater than 1 to the `--parallel-jobs` argument. It will cause yb_migrate to dump multiple tables concurrently.

## Step 5: Import the schema

Import the schema with the `yb_migrate import schema` command as follows:

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

## Step 6: Import data

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

Run the `yb_migrate import data status --export-dir ${EXPORT_DIR}` command to get an overall progress of the data import operation. While importing a very large database, you should run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops. If the `yb_migrate import data` command terminates before it could complete the data ingestion, you can rerun it with the same arguments and the command will resume the data import operation.

## Step 7: Finalize DDL

The creation of indexes are automatically handled by the `yb_migrate import data` command after it successfully loads the data in the [import data](#step-6-import-data) phase. The command creates the indexes listed in the schema.

**TODO** Add about how triggers are enabled or handled.

## Step 8: Verify migration

After the successful execution of the `yb_migrate import data` command, the automated part of the database migration process is considered complete. You should manually run validation queries on both the source and target database to ensure that the data is correctly migrated. A sample query to validate the databases can include checking the row count in each table.

[Verify a migration](../../migrate/migrate-from-postgresql/verify-migration/) to validate queries and ensure a successful migration.
<!-- The validation queries can be as simple as checking the row count in each table or it can utilise some domain knowledge e.g. match the sum of the `amount` column in the `payments` table. -->
