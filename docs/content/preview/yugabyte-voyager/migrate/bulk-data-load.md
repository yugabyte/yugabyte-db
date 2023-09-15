---
title: Steps for a bulk data load
headerTitle: Bulk data load from files
linkTitle: Bulk data load
headcontent: Steps for loading bulk data from flat files to YugabyteDB using YugabyteDB Voyager.
description: Run the steps to ensure a successful offline migration using YugabyteDB Voyager.
menu:
  preview_yugabyte-voyager:
    identifier: bulk-data-load
    parent: migration-types
    weight: 104
type: docs
---


This page describes the import feature YugabyteDB Voyager provides to bulk load data from flat files present on local disk or cloud storage like AWS S3, GCS buckets, and Azure Blob.

## Prerequisite

* Before you perform bulk load, manually create the schema of the tables on which data files need to be imported to the target YugabyteDB database.

## Import data files from the local disk

If your data files are in CSV or TEXT format and are present on the local disk, and you have already created a schema in your target YugabyteDB database, you can use the following `yb-voyager import data file` command to load the data into the target table directly from the flat file(s).

```sh
# Replace the argument values with those applicable to your migration.
yb-voyager import data file --export-dir <EXPORT_DIR> \
       --target-db-host <TARGET_DB_HOST> \
       --target-db-user <TARGET_DB_USER> \
       --target-db-password <TARGET_DB_PASSWORD> \ # Enclose the password in single quotes if it contains special characters.
       --target-db-name <TARGET_DB_NAME> \
       --target-db-schema <TARGET_DB_SCHEMA> \
       –-data-dir </path/to/files/dir/> \
       --file-table-map <filename1:table1,filename2:table2> \ # Optional arguments as per data format
       --delimiter <DELIMITER> \
       --escape-char <ESCAPE_CHAR> \
       --quote-char <QUOTE_CHAR> \
       –-has-header \
       --null-string "<NULL_STRING>"
```

Refer to [import data file](../../reference/yb-voyager-cli/#import-data-file) for details about the arguments.

### Incremental data loading

The `import data file` command also supports importing multiple files to the same table by providing the [--file-table-map](../../reference/yb-voyager-cli/#file-table-map) flag <fileName>:<tableName> entry for each file, or by passing a glob expression in place of the file name.
For example, `fileName1:tableName,fileName2:tableName` OR `fileName*:tableName`.

## Import data files from cloud storage

The `import data file` command supports importing data from files present on cloud storage like AWS S3, GCS buckets, and Azure blob. This feature helps reduce disk requirements to store the downloaded files on a local disk and then import them to YugabyteDB.

{{< tabpane text=true >}}

  {{% tab header="AWS S3" %}}

Import data file allows you to load directly from your data files stored on AWS S3. The S3 bucket URI can be provided to the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir s3://voyager-data
```

The authentication mechanism for accessing an S3 bucket using yb-voyager is the same as that used by the AWS CLI. Refer to [Configure the AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-configure.html) for additional details on setting up your S3 bucket.

  {{% /tab %}}

  {{% tab header="GCS buckets" %}}
Import data file allows you to load directly from your data files stored on GCS buckets. The GCS bucket URI can be provided to the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir gs://voyager-data
```

The authentication mechanism for accessing a GCS bucket using yb-voyager is the Application Default Credentials (ADC) strategy for GCS. Refer to [Set up Application Default Credentials](https://cloud.google.com/docs/authentication/provide-credentials-adc) for additional details on setting up your GCS buckets.
  {{% /tab %}}

  {{% tab header="Azure blob" %}}
Import data file allows you to load directly from your data files stored on Azure blob storage containers. The Azure container URI can be provided to the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir https://<account_name>.blob.core.windows.net/<container_name>...
```

The authentication mechanism for accessing blobs using yb-voyager is the same as that used by the Azure CLI. The Azure storage account used for the import should at least have the [Storage Blob Data Reader](https://learn.microsoft.com/en-us/azure/role-based-access-control/built-in-roles#storage-blob-data-reader) role assigned to it.
Refer to [Sign in with Azure CLI](https://learn.microsoft.com/en-us/cli/azure/authenticate-azure-cli) for additional details on setting up your Azure blobs.
  {{% /tab %}}

{{< /tabpane >}}