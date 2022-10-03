---
title: Install
headerTitle: Install
linkTitle: Install
description: Explore the prerequisites, YugabyteDB Voyager installation, and so on.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
image: /images/section_icons/develop/learn.png
menu:
  preview:
    identifier: install-yb-voyager
    parent: voyager
    weight: 101
type: docs
---

## Prerequisites

The following sections describe the prerequisites for installing YugabyteDB Voyager.

### Operating system

You can install YugabyteDB Voyager on the following:

- CentOS 7
- Ubuntu 18.04, 20.04
- MacOS (currently supported only for PostgreSQL source database)

### Hardware requirements

- Disk space must be at least 1.5 times the estimated size of the source database.
- 2 cores minimum (recommended)

### Prepare the host

The node where you'll run the yb-voyager command should:

- connect to both the source and the target database.
- have sudo access.

### Create an export directory

Before starting migration, you should create the export directory on a file system that has enough space to keep the entire data dump. Next, you should provide the path of the export directory as a mandatory argument (`--export-dir`) to each invocation of the yb-voyager command.

yb-voyager uses the directory to store source data, schema files, and the migration state. The file system in which the directory resides must have enough free space to hold the entire source database. Create an export directory in the local file system on the machine where YugabyteDB Voyager will be installed, and place its path in an environment variable.

```sh
mkdir $HOME/export-dir
export EXPORT_DIR=$HOME/export-dir
```

The export directory has the following sub-directories and files:

- `reports` directory contains the generated *Schema Analysis Report*
- `schema` directory contains the source database schema translated to PostgreSQL. The schema is partitioned into smaller files by the schema object type such as tables, views, and so on.
- `data` directory contains TSV (Tab Separated Values) files that are passed to the COPY command on the target database.
- `metainfo` and `temp` directories are used by yb-voyager for internal bookkeeping.
- `yb-voyager.log` contains log messages.

<!-- For more information, refer to [Export directory](../../yb-voyager/reference/#export-directory). -->

## Install yb-voyager

YugabyteDB Voyager consists of the yb-voyager command line executable. yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the [*export directory*](#create-an-export-directory).
<!-- For more information, refer to [Export directory](../../yb-voyager/reference/#export-directory). -->

To install yb-voyager on a machine which satisfies the [Prerequisites](#prerequisites), do the following:

- Clone the yb-voyager repository.

  ```sh
  git clone https://github.com/yugabyte/yb-voyager.git
  ```

- Change the directory to `yb-voyager/installer_scripts`.

  ```sh
  cd yb-voyager/installer_scripts
  ```

- Install yb-voyager using the following script:

  ```sh
  ./install-yb-voyager
  ```

  It is safe to execute the script multiple times. If the script fails, check the `/tmp/install-yb-voyager.log` file.

- The script generates a `.yb-voyager.rc` file in the home directory. Source the file to ensure that the environment variables are set using the following command:

  ```sh
  source ~/.yb-voyager.rc
  ```

- Check that yb-voyager is installed using the following command:

  ```sh
  yb-voyager --help
  ```

To learn more about yb-voyager, refer to [YugabyteDB Voyager CLI](../yb-voyager-cli/).

## Next step

- [Migration steps](../migrate-steps/)
