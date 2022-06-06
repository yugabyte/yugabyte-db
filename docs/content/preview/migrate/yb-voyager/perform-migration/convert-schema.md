---
title: Convert schema
linkTitle: Convert schema
description: Convert the source database schema to YugabyteDB schema.
menu:
  preview:
    identifier: convert-schema
    parent: perform-migration-1
    weight: 501
isTocNested: true
showAsideToc: true
---

This page describes the general migration process from other RDBMS to YugabyteDB (YSQL API).

## Step 3: Export and analyze schema

### Export schema

`yb-voyager export schema` command extracts the schema from the source database, converts it into PostgreSQL format (if the source database is Oracle or MySQL); and dumps the SQL DDL files in the `EXPORT_DIR/schema/*` directories.

An example invocation of the command is as follows:

```sh
yb-voyager export schema --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

### Analyze Schema

Using [ora2pg](https://ora2pg.darold.net) and [pg_dump](https://www.postgresql.org/docs/current/app-pgdump.html), yb-voyager can extract and convert the source database schema to an equivalent PostgreSQL schema. The schema, however, may not be suitable yet to be imported into YugabyteDB. Even though YugabyteDB is PostgreSQL compatible, given its distributed nature, you may need minor manual changes to the schema.

Refer to [Data modeling](../../reference/connectors/yb-migration-reference/#data-modeling) to learn more about modeling strategies using YugabyteDB.

The `yb-voyager analyze-schema` command analyses the PostgreSQL schema dumped in the [export schema](#export-schema) phase and prepares a report that lists the DDL statements which need manual changes. An example invocation of the command is as follows:

```sh
yb-voyager analyze-schema --export-dir ${EXPORT_DIR} \
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

- Re-run the `yb-voyager analyze-schema` command after making the manual changes. The command will generate a fresh report using your changes. Repeat these steps until the generated report contains no issues.

## Next step

- [Export data]()
