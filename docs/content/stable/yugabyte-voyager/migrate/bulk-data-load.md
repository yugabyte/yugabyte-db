---
title: Steps for a bulk data load
headerTitle: Bulk data load from files
linkTitle: Bulk data load
headcontent: Import data from flat files using YugabyteDB Voyager
description: Import data from flat files using YugabyteDB Voyager.
menu:
  stable_yugabyte-voyager:
    identifier: bulk-data-load
    parent: migration-types
    weight: 105
type: docs
rightNav:
  hideH4: true
---

The following page describes the steps to import data in CSV or TEXT format from flat files on your local disk or in cloud storage, including AWS S3, GCS buckets, and Azure Blob.

## Prerequisite

* Before you perform a bulk load, in your target YugabyteDB database, create the schema of the tables into which the data in the flat files will be imported.

## Prepare the target database

Prepare your target YugabyteDB database cluster by creating a user.

### Create a user

Create a user with [`SUPERUSER`](../../../api/ysql/the-sql-language/statements/dcl_create_role/#syntax) role.

* For a local YugabyteDB cluster or YugabyteDB Anywhere, create a user and role with the superuser privileges using the following command:

     ```sql
     CREATE USER ybvoyager SUPERUSER PASSWORD 'password';
     ```

* For YugabyteDB Aeon, create a user with [`yb_superuser`](../../../yugabyte-cloud/cloud-secure-clusters/cloud-users/#admin-and-yb-superuser) role using the following command:

     ```sql
     CREATE USER ybvoyager PASSWORD 'password';
     GRANT yb_superuser TO ybvoyager;
     ```

If you want yb-voyager to connect to the target YugabyteDB database over SSL, refer to [SSL Connectivity](../../reference/yb-voyager-cli/#ssl-connectivity).

{{< warning title="Deleting the ybvoyager user" >}}

After migration, all the migrated objects (tables, views, and so on) are owned by the `ybvoyager` user. You should transfer the ownership of the objects to some other user (for example, `yugabyte`) and then delete the `ybvoyager` user. Example steps to delete the user are:

```sql
REASSIGN OWNED BY ybvoyager TO yugabyte;
DROP OWNED BY ybvoyager;
DROP USER ybvoyager;
```

{{< /warning >}}

## Create an export directory

yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the _export directory_.

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire source database. Ideally, create this export directory inside a parent folder named after your migration for better organization. You need to provide the full path to the export directory in the `export-dir` parameter of your [configuration file](#set-up-a-configuration-file), or in the `--export-dir` flag when running `yb-voyager` commands.

```sh
mkdir -p $HOME/<migration-name>/export-dir
```

The export directory has the following sub-directories and files:

* `metainfo` and `temp` are used by yb-voyager for internal bookkeeping.
* `logs` contains the log files for each command.

## Set up a configuration file

You can use a [configuration file](../../reference/configuration-file/) to specify the parameters required when running Voyager commands (v2025.7.1 or later).

To get started, copy the `bulk-data-load.yaml` template configuration file from one of the following locations to the migration folder you created (for example, `$HOME/my-migration/`):

{{< tabpane text=true >}}

  {{% tab header="Linux (apt/yum/airgapped)" lang="linux" %}}

```bash
/opt/yb-voyager/config-templates/bulk-data-load.yaml
```

  {{% /tab %}}

  {{% tab header="MacOS (Homebrew)" lang="macos" %}}

```bash
$(brew --cellar)/yb-voyager@<voyager-version>/<voyager-version>/config-templates/bulk-data-load.yaml
```

Replace `<voyager-version>` with your installed Voyager version, for example, `2025.7.1`.

  {{% /tab %}}

{{< /tabpane >}}

Set the export-dir, source, and target arguments in the configuration file:

```yaml
# Replace the argument values with those applicable for your migration.

export-dir: <absolute-path-to-export-dir>

target:
  db-host: <target-db-host>
  db-port: <target-db-port>
  db-name: <target-db-name>
  db-schema: <target-db-schema> # MySQL and Oracle only
  db-user: <target-db-username>
  db-password: <target-db-password> # Enclose the password in single quotes if it contains special characters.
```

Refer to the [bulk-data-load.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/bulk-data-load.yaml) template for more information on the available global and target configuration parameters supported by Voyager.

## Import data files from the local disk

If your data files are in CSV or TEXT format and are present on the local disk, and you have already created a schema in your target YugabyteDB database, you can use the following `yb-voyager import data file` command with required arguments to load the data into the target table directly from the flat file(s).

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

1. Specify the following parameters in the import-data-file section of your configuration file:

    ```conf
    ...
    import-data-file:
     data-dir: </path/to/files/dir/>
     file-table-map: <filename1>:<table1>,<filename2>:<table2>
     format: csv|text # default csv
     # Optional arguments as per data format
     delimiter: <DELIMITER> # default ',' for csv and '\t' for text
     escape-char: <ESCAPE_CHAR> # for csv format only. default: '"'
     quote-char: <QUOTE_CHAR>  # for csv format only. default: '"'
     has-header: true  # for csv format only. default: false
     null-string: <NULL_STRING> # default '' (empty string) for csv and '\N'  for text
    ...

    ```

1. Run the command:

    ```sh
    yb-voyager import data file --config-file <PATH_TO_CONFIG_FILE>
    ```

You can specify additional parameters in the `import data file` section of the configuration file. For more details, refer to the [bulk-data-load.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/bulk-data-load.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

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

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import data file](../../reference/bulk-data-load/import-data-file/) for details about the arguments.

{{< note title= "Migrating data files with large size rows" >}}
For CSV file format, the import data file has a default row size limit of 32MB. If a row exceeds this limit but is smaller than the `batch-size * max-row-size`, you can increase the limit for the import data file process by setting the following environment variable:

```sh
export CSV_READER_MAX_BUFFER_SIZE_BYTES = <MAX_ROW_SIZE_IN_BYTES>
```

{{< /note >}}

### Import data status

To get an overall progress of the import data operation, you can run the `yb-voyager import data status` command. You specify the `<EXPORT_DIR>` to push data in using the `export-dir` parameter (configuration file), or `--export-dir` flag (CLI).

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

```sh
yb-voyager import data status --config-file <path-to-config-file>
```

You can specify additional `import data status` parameters in the `import-data-status` section of the configuration file. For more details, refer to the [bulk-data-load.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/bulk-data-load.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

```sh
yb-voyager import data status --export-dir <EXPORT_DIR>
```

  {{% /tab %}}

{{< /tabpane >}}

Refer to [import data status](../../reference/data-migration/import-data/#import-data-status) for more information.

### Load multiple files into the same table

The import data file command also supports importing multiple files to the same table by providing the `file-table-map` configuration parameter or the `--file-table-map` flag with a `<fileName>:<tableName>` entry for each file, or by passing a glob expression in place of the file name. For example, `fileName1:tableName,fileName2:tableName` or `fileName*:tableName`.

### Incremental data loading

You can also import files to the same table across multiple runs. For example:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

1. Import `orders1.csv` by modifying the configuration file and running the `import data file` command as follows:

    ```conf
    ...
    import-data-file:
      ...
      file-table-map: 'orders1.csv:orders'
      ...
    ...
    ```

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
    ```

1. Import a different file, for example `orders2.csv`, to the same table by modifying the configuration file and running the `import data file` command as follows:

    ```conf
    ...
    import-data-file:
      ...
      file-table-map: 'orders2.csv:orders'
      ...
    ...
    ```

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
    ```

    To import an updated version of the same file (that is, having the same file name and data-dir), use the `start-clean` parameter and proceed without truncating the table. yb-voyager ingests the data present in the file in upsert mode.

    For example, importing `orders.csv` under `data-dir` to the `orders` table updates the same file:

    ```conf
    ...
    import-data-file:
      ...
      data-dir: /dir/data-dir
      file-table-map: 'orders.csv:orders'
      ...
    ...
    ```

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
    ```

1. After adding new rows to `orders.csv`, make the following change in the configuration file and run the `import data file` command again:

    **Warning**: Ensure that tables on the target YugabyteDB database do not have secondary indexes. If a table has secondary indexes, using `enable-upsert: true` may corrupt the indexes.

    ```conf
    ...
    import-data-file:
      ...
      data-dir: /dir/data-dir
      file-table-map: 'orders.csv:orders'
      enable-upsert: true
      start-clean: true
      ...
    ...
    ```

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
    ```

  You can specify additional `import data file` parameters in the `import-data-file` section of the configuration file. For more details, refer to the [bulk-data-load.yaml](https://github.com/yugabyte/yb-voyager/blob/{{< yb-voyager-release >}}/yb-voyager/config-templates/bulk-data-load.yaml) template.

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

1. Import `orders1.csv` by running the `import data file` command as follows:

    ```sh
    yb-voyager import data file --file-table-map 'orders1.csv:orders' ...
    ```

1. Import a different file, for example `orders2.csv`, to the same table by modifying the configuration file and running the `import data file` command as follows:

    ```sh
    yb-voyager import data file --file-table-map 'orders2.csv:orders' ...
    ```

1. To import an updated version of the same file (that is, having the same file name and data-dir), use the `--start-clean` flag and proceed without truncating the table. yb-voyager ingests the data present in the file in upsert mode.

    For example, importing `orders.csv` under `data-dir` to the `orders` table updates the same file:
    ```sh
    yb-voyager import data file --data-dir /dir/data-dir --file-table-map 'orders.csv:orders' ...
    ```

1. After adding new rows to `orders.csv`, run the `import data file` again as follows:

    **Warning**: Ensure that tables on the target YugabyteDB database do not have secondary indexes. If a table has secondary indexes, using the `–enable-upsert true` flag may corrupt the indexes.

    ```sh
    yb-voyager import data file --data-dir /dir/data-dir \
            --file-table-map 'orders.csv:orders' \
            --start-clean true \
            --enable-upsert true
    ```

For details about the argument, refer to the [arguments table](../../reference/bulk-data-load/import-data-file/#arguments).

  {{% /tab %}}

{{< /tabpane >}}

## Import data files from cloud storage

Using the `import data file` command, you can import data from files in cloud storage, including AWS S3, GCS buckets, and Azure blob. Importing from cloud storage reduces local disk requirements for storing the data while it is imported to your YugabyteDB database.

{{< tabpane text=true >}}

  {{% tab header="AWS S3" %}}

To import data from AWS S3, provide the S3 bucket URI in the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir s3://voyager-data
```

If you are using a configuration file, do the following instead:

1. Make the following change in the configuration file:

    ```conf
    ...
    import-data-file:
      ...
      data-dir: s3://voyager-data
      ...
    ...
    ```

1. Run the command as follows:

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
    ```

The authentication mechanism for accessing an S3 bucket using yb-voyager is the same as that used by the AWS CLI. Refer to [Configure the AWS CLI](https://docs.aws.amazon.com/cli/latest/userguide/cli-chap-configure.html) for additional details on setting up your S3 bucket.

  {{% /tab %}}

  {{% tab header="GCS buckets" %}}
To import data from GCS buckets, provide the GCS bucket URI in the `data-dir` flag as follows:

```sh
yb-voyager import data file .... \
        --data-dir gs://voyager-data
```

If you are using a configuration file, do the following instead:

1. Make the following change in the configuration file:

    ```conf
    ...
    import-data-file:
      ...
      data-dir: gs://voyager-data
      ...
    ...
    ```

1. Run the command as follows:

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
    ```

The authentication mechanism for accessing a GCS bucket using yb-voyager is the Application Default Credentials (ADC) strategy for GCS. Refer to [Set up Application Default Credentials](https://cloud.google.com/docs/authentication/provide-credentials-adc) for additional details on setting up your GCS buckets.
  {{% /tab %}}

  {{% tab header="Azure blob" %}}
To import data from Azure blob storage containers, provide the Azure container URI in the `data-dir` flag as follows:

```sh

yb-voyager import data file .... \
        --data-dir https://<account_name>.blob.core.windows.net/<container_name>...
```

If you are using a configuration file, do the following instead:

1. Make the following change in the configuration file:

    ```conf
    ...
    import-data-file:
      ...
      data-dir: https://<account_name>.blob.core.windows.net/<container_name>...
      ...
    ...
    ```

1. Run the command as follows:

    ```sh
    yb-voyager import data file –config-file <path_to_config_file>
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

Run the `yb-voyager end migration` command to perform the clean up, and to back up the migration report and the log files by providing the backup related flags (mandatory) as follows:

Run the command as follows:

{{< tabpane text=true >}}

  {{% tab header="Config file" lang="config" %}}

Specify the following parameters in the `end-migration` section of the configuration file:

```yaml
...
end-migration:
  backup-schema-files: <true, false, yes, no, 1, 0>
  backup-data-files: <true, false, yes, no, 1, 0>
  save-migration-reports: <true, false, yes, no, 1, 0>
  backup-log-files: <true, false, yes, no, 1, 0>
  # Set optional argument to store a back up of any of the above  arguments.
  backup-dir: <BACKUP_DIR>
...
```

Run the command:

```sh
yb-voyager end migration --config-file <path-to-config-file>
```

  {{% /tab %}}

  {{% tab header="CLI" lang="cli" %}}

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

  {{% /tab %}}

{{< /tabpane >}}

{{< note title="Note" >}}

* After performing end migration, you can't continue import data file operations using the specified export directory (export-dir).

* Because import data file only imports data (and doesn't import or export the schema or export data), set the `--backup-data-files` and `backup-schema-files` arguments to false.

{{< /note >}}

If you want to back up the log file and import data status output for future reference, use the `--backup-dir` argument, and provide the path of the directory where you want to save the backup content (based on what you choose to back up).

Refer to [end migration](../../reference/end-migration/) for more details on the arguments.
