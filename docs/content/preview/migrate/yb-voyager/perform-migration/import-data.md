---
title: Import data
linkTitle: Import data
description: Import data to YugabyteDB.
menu:
  preview:
    identifier: import-data
    parent: perform-migration-1
    weight: 503
isTocNested: true
showAsideToc: true
---


## Step 5: Import the schema

Import the schema with the `yb-voyager import schema` command as follows:

```sh
yb-voyager import schema --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

yb-voyager applies the DDL SQL files located in the `$EXPORT_DIR/schema` directory to the target database. If yb-voyager terminates before it imports the entire schema, you can rerun it by adding `--ignore-exist` option.

{{< note title="Note" >}}

The `yb-voyager import schema` command does not import indexes yet. This is done to speed up the data import phase. The indexes will be created by `yb-voyager import data` command after importing the data in the next step.

{{< /note >}}

## Step 6: Import data

After you have successfully exported the source data and imported the schema in the target database, you can now import the data using the `yb-voyager import data` command:

```sh
yb-voyager import data --export-dir ${EXPORT_DIR} \
        --target-db-host ${TARGET_DB_HOST} \
        --target-db-port ${TARGET_DB_PORT} \
        --target-db-user ${TARGET_DB_USER} \
        --target-db-password ${TARGET_DB_PASSWORD:-''} \
        --target-db-name ${TARGET_DB_NAME}
```

In the import data phase, yb-voyager splits the data dump files (from the `$EXPORT_DIR/data` directory) into smaller _batches_ ,each of which contains at most `--batch-size` number of records. yb-voyager concurrently ingests the batches such that all nodes of the target YugabyteDB cluster are utilized. This phase is designed to be _restartable_ if yb-voyager terminates when the data import is in progress. Upon a restart, the data import resumes from its current state.

The `yb-voyager import data` command reads data files located in the `EXPORT_DIR/data`. The command, by default, creates one database connection to each of the nodes of the target YugabyteDB cluster. You can increase the number of connections by specifying the total connection count in the `--parallel-jobs` argument. The command will equally distribute the connections among all the nodes of the cluster. It splits the larger tables into smaller chunks, each containing at most `--batch-size` number of records. By default, the `--batch-size` is 100,000 records.

Run the `yb-voyager import data status --export-dir ${EXPORT_DIR}` command to get an overall progress of the data import operation. While importing a very large database, you should run the import data command in a `screen` session, so that the import is not terminated when the terminal session stops. If the `yb-voyager import data` command terminates before it could complete the data ingestion, you can re-run it with the same arguments and the command will resume the data import operation.

## Step 7: Finalize DDL

The creation of indexes are automatically handled by the `yb-voyager import data` command after it successfully loads the data in the [import data](#step-6-import-data) phase. The command creates the indexes listed in the schema.

## Next step

- [Verify migration]()