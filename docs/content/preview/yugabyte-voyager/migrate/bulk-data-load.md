---
title: Steps for a bulk data load
headerTitle: Bulk data load from files
linkTitle: Bulk data load
headcontent: Import data from flat files using YugabyteDB Voyager
description: Import data from flat files using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: bulk-data-load
    parent: migration-types
    weight: 105
type: docs
---

The following page describes the steps to import data in CSV or TEXT format from flat files on your local disk or in cloud storage, including AWS S3, GCS buckets, and Azure Blob.

## Prerequisite

* Before you perform a bulk load, in your target YugabyteDB database, create the schema of the tables into which the data in the flat files will be imported.

## Import data files from the local disk

If your data files are in CSV or TEXT format and are present on the local disk, and you have already created a schema in your target YugabyteDB database, you can use the following `yb-voyager import data file` command with required arguments to load the data into the target table directly from the flat file(s).

```sh
# Replace the argument values with those applicable to your migration.
yb-voyager import data file --export-dir <EXPORT_DIR> \
       --target-db-host <TARGET_DB_HOST> \
       --target-db-user <TARGET_DB_USER> \
       --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
       --target-db-name <TARGET_DB_NAME> \
       --target-db-schema <TARGET_DB_SCHEMA> \
       --data-dir </path/to/files/dir/> \
       --file-table-map <filename1>:<table1>,<filename2>:<table2> \
       --format csv|text \ # default csv
       # Optional arguments as per data format
       --delimiter <DELIMITER> \ # default ',' for csv and '\t' for text
       --escape-char <ESCAPE_CHAR> \ # for csv format only. default: '"'
       --quote-char <QUOTE_CHAR> \ # for csv format only. default: '"'
       --has-header \ # for csv format only. default: false
       --null-string <NULL_STRING> # default '' (empty string) for csv and '\N'  for text
```

Refer to [import data file](../../reference/bulk-data-load/import-data-file/) for details about the arguments.

### Import data status

Run the `yb-voyager import data status --export-dir <EXPORT_DIR>` command to get an overall progress of the data import operation.

Refer to [import data status](../../reference/data-migration/import-data/#import-data-status) for details about the arguments.

### Load multiple files into the same table

The import data file command also supports importing multiple files to the same table by providing the `--file-table-map` flag with a `<fileName>:<tableName>` entry for each file, or by passing a glob expression in place of the file name. For example, `fileName1:tableName,fileName2:tableName` or `fileName*:tableName`.

### Incremental data loading

You can also import files to the same table across multiple runs. For example, you could import `orders1.csv` as follows:

```sh
yb-voyager import data file --file-table-map 'orders1.csv:orders' ...
```

And then subsequently import `orders2.csv` to the same table as follows:

```sh
yb-voyager import data file --file-table-map 'orders2.csv:orders' ...
```

To import an updated version of the same file (that is, having the same file name and data-dir), use the `--start-clean` flag and proceed without truncating the table. yb-voyager ingests the data present in the file in upsert mode. For example:

```sh
yb-voyager import data file --data-dir /dir/data-dir --file-table-map 'orders.csv:orders' ...
```

After new rows are added to `orders.csv`, use the following command to load them with `--start-clean`:

```sh
yb-voyager import data file --data-dir /dir/data-dir --file-table-map 'orders.csv:orders' --start-clean ...`
```

For details about the argument, refer to the [arguments table](../../reference/bulk-data-load/import-data-file/#arguments).

## Import data files from cloud storage

Using the `import data file` command, you can import data from files in cloud storage, including AWS S3, GCS buckets, and Azure blob. Importing from cloud storage reduces local disk requirements for storing the data while it is imported to your YugabyteDB database.

{{< tabpane text=true >}}

  {{% tab header="AWS S3" %}}

To import data from AWS S3, provide the S3 bucket URI in the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir s3://voyager-data
```

The authentication mechanism for accessing an S3 bucket using yb-voyager is the same as that used by the AWS CLI. Refer to [Configure the AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-configure.html) for additional details on setting up your S3 bucket.

  {{% /tab %}}

  {{% tab header="GCS buckets" %}}
To import data from GCS buckets, provide the GCS bucket URI in the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir gs://voyager-data
```

The authentication mechanism for accessing a GCS bucket using yb-voyager is the Application Default Credentials (ADC) strategy for GCS. Refer to [Set up Application Default Credentials](https://cloud.google.com/docs/authentication/provide-credentials-adc) for additional details on setting up your GCS buckets.
  {{% /tab %}}

  {{% tab header="Azure blob" %}}
To import data from Azure blob storage containers, provide the Azure container URI in the `data-dir` flag as follows:

```sh

yb-voyager import data file .... \
        --data-dir https://<account_name>.blob.core.windows.net/<container_name>...
```

The authentication mechanism for accessing blobs using yb-voyager is the same as that used by the Azure CLI. The Azure storage account used for the import should at least have the [Storage Blob Data Reader](https://learn.microsoft.com/en-us/azure/role-based-access-control/built-in-roles#storage-blob-data-reader) role assigned to it.
Refer to [Sign in with Azure CLI](https://learn.microsoft.com/en-us/cli/azure/authenticate-azure-cli) for additional details on setting up your Azure blobs.
  {{% /tab %}}

{{< /tabpane >}}

## Verify migration

After the data import is complete, manually run validation queries on the target YugabyteDB database to ensure that the data is correctly imported. For example, run queries to check the row count of each table.

{{< warning title = "Row count reported by import data status" >}}

Suppose you have the following scenario:

* The [import data file](../bulk-data-load/#import-data-files-from-the-local-disk) command fails.
* To resolve this issue, you delete some of the rows from the split files.
* After retrying, the import data to target command completes successfully.

In this scenario, the [import data status](#import-data-status) command reports an incorrect imported row count because it doesn't take into account the deleted rows.

For more details, refer to the GitHub issue [#360](https://github.com/yugabyte/yb-voyager/issues/360).

{{< /warning >}}

## End migration

To complete the migration, you need to clean up the export directory (export-dir) and Voyager state (Voyager-related metadata) stored in the target YugabyteDB database.

Run the `yb-voyager end migration` command to perform the clean up, and to back up the migration report, and the log files by providing the backup related flags (mandatory) as follows:

```sh
# Replace the argument values with those applicable for your migration.
yb-voyager end migration --export-dir <EXPORT_DIR> \
        --backup-log-files <true, false, yes, no, 1, 0> \
        --backup-data-files false \
        --backup-schema-files false \
        --save-migration-reports <true, false, yes, no, 1, 0> \
        # Set optional argument to store a back up of any of the above arguments.
        --backup-dir <BACKUP_DIR>
```

{{< note title="Note" >}}

* After performing end migration, you can't continue import data file operations using the specified export directory (export-dir).

* Set arguments `--backup-data-files` and `backup-schema-files` to false as import data file operation is not a complete migration that includes export/import of schema, or export of data.

{{< /note >}}

If you want to back up the log file and import data status output for future reference, use the `--backup-dir` argument, and provide the path of the directory where you want to save the backup content (based on what you choose to back up).

Refer to [end migration](../../reference/end-migration/) for more details on the arguments.
