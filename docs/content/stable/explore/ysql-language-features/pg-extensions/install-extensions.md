---
title: Install PostgreSQL extensions
headerTitle: Install PostgreSQL extensions
linkTitle: Install extensions
description: Install PostgreSQL extensions for use with YugabyteDB
headcontent: Add extensions to your YugabyteDB installation
menu:
  stable:
    identifier: install-extensions
    parent: pg-extensions
    weight: 10
type: docs
---

If an extension is not pre-bundled, you need to install it manually before you can enable it using the [CREATE EXTENSION](../../../../api/ysql/the-sql-language/statements/ddl_create_extension/) statement. You can install only [extensions that are supported](../#supported-extensions) by YugabyteDB.

Currently, in a multi-node setup, you need to install the extension on _every_ node in the cluster.

In a read replica setup, install extensions on the primary instance, not on the read replica. Once installed, the extension replicates to the read replica.

You cannot install new extensions in YugabyteDB Managed. If you need a database extension that is not pre-bundled with YugabyteDB added to a YugabyteDB Managed cluster, contact {{% support-cloud %}} with the names of the cluster and extension, or [reach out on Slack](https://yugabyte-db.slack.com/).

## Install an extension

Typically, extensions need three types of files:

* Shared library files (`<name>.so`)
* SQL files (`<name>--<version>.sql`)
* Control files (`<name>.control`)

To install an extension, you need to copy these files into the respective directories of your YugabyteDB installation.

Shared library files go in the `pkglibdir` directory, while SQL and control files go in the `extension` subdirectory of the `libdir` directory.

You can obtain the installation files for the target extension in two ways:

* Build the extension from scratch following the extension's build instructions.
* Copy the files from an existing PostgreSQL installation.

After copying the files, restart the cluster (or the respective node in a multi-node install).

### Locate installation directories using `pg_config`

To find the directories where you install the extension files on your local installation, use the YugabyteDB `pg_config` executable.

First, alias it to `yb_pg_config` by replacing `<yugabyte-path>` with the path to your YugabyteDB installation as follows:

```sh
alias yb_pg_config=/<yugabyte-path>/postgres/bin/pg_config
```

List existing shared libraries with:

```sh
ls "$(yb_pg_config --pkglibdir)"
```

List SQL and control files for already-installed extensions with:

```sh
ls "$(yb_pg_config --sharedir)"/extension/
```

### Copy extensions from PostgreSQL

The easiest way to install an extension is to copy the files from an existing PostgreSQL installation.

Ideally, use the same version of the PostgreSQL extension as that used by YugabyteDB. To see the version of PostgreSQL used in your YugabyteDB installation, enter the following `ysqlsh` command:

```sh
./bin/ysqlsh --version
```

```output
psql (PostgreSQL) 11.2-YB-2.11.2.0-b0
```

If you already have PostgreSQL (use version `11.2` for best YSQL compatibility) with the extension installed, you can find the extension's files as follows:

```sh
ls "$(pg_config --pkglibdir)" | grep <name>
```

```sh
ls "$(pg_config --sharedir)"/extension/ | grep <name>
```

If you have multiple PostgreSQL versions installed, make sure you're selecting the correct `pg_config`. On an Ubuntu 18.04 environment with multiple PostgreSQL versions installed:

```sh
pg_config --version
```

```output
PostgreSQL 13.0 (Ubuntu 13.0-1.pgdg18.04+1)
```

```sh
/usr/lib/postgresql/11/bin/pg_config --version
```

```output
PostgreSQL 11.9 (Ubuntu 11.9-1.pgdg18.04+1)
```

In this case, you should be using `/usr/lib/postgresql/11/bin/pg_config`.

On CentOS, the correct path is `/usr/pgsql-11/bin/pg_config`.
