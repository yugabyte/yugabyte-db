---
title: Export data
linkTitle: Export data
description: Export data from the source database.
menu:
  preview:
    identifier: export-data
    parent: perform-migration-1
    weight: 502
isTocNested: true
showAsideToc: true
---


Dump the source data into the `EXPORT_DIR/data` directory using the `yb-voyager export data` command as follows:

```sh
yb-voyager export data --export-dir ${EXPORT_DIR} \
        --source-db-type ${SOURCE_DB_TYPE} \
        --source-db-host ${SOURCE_DB_HOST} \
        --source-db-user ${SOURCE_DB_USER} \
        --source-db-password ${SOURCE_DB_PASSWORD} \
        --source-db-name ${SOURCE_DB_NAME} \
        --source-db-schema ${SOURCE_DB_SCHEMA}
```

The options passed to the command are similar to the [`yb-voyager export schema`](#export-schema) command. To export only a subset of the tables, pass a comma separated list of table names in the `--table-list` argument. To speed up the data export of larger source databases, you can pass values greater than 1 to the `--parallel-jobs` argument. It will cause yb-voyager to dump multiple tables concurrently.

## Next step

- [Import schema and data]()