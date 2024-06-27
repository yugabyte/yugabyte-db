---
title: Connect using Cloud Shell
linkTitle: Cloud Shell
description: Connect to YugabyteDB Aeon clusters from any browser using Cloud Shell
headcontent: Use your browser to connect to YugabyteDB Aeon databases 
aliases:
  - /preview/yugabyte-cloud/cloud-quickstart/qs-connect/
menu:
  preview_yugabyte-cloud:
    identifier: connect-cloud-shell
    parent: cloud-connect
    weight: 10
type: docs
---

Connect to your cluster database using any modern browser with Cloud Shell. Cloud Shell doesn't require a CA certificate or any special network access configured.

When you connect to your cluster using Cloud Shell with the YSQL API, the shell window also incorporates a [Quick Start Guide](../../cloud-quickstart/qs-explore/), with a series of pre-built queries for you to run.

## Command line interface

You have the option of using the following command line interfaces (CLIs) in Cloud Shell:

- [ysqlsh](../../../admin/ysqlsh/) - YSQL shell for interacting with YugabyteDB using the [YSQL API](../../../api/ysql/).
- [ycqlsh](../../../admin/ycqlsh/) - YCQL shell, which uses the [YCQL API](../../../api/ycql/).

## Limitations

Cloud Shell has the following security limitations:

- Sessions are limited to one hour. If the session is inactive, it may disconnect after five minutes. If your session disconnects, close the browser tab and start a new session.
- You can run up to five concurrent Cloud Shell sessions.
- You can only use a subset of ysqlsh [meta-commands](#ysqlsh-meta-commands-in-cloud-shell).

{{< tip title="Cloud Shell known issues" >}}

Cloud Shell is updated regularly. Check the [known issues list](../../release-notes/#known-issues-in-cloud-shell) in the release notes for the most-current list of known issues.

{{< /tip >}}

## Connect via Cloud Shell

To connect to a cluster via Cloud Shell:

1. On the **Clusters** tab, select a cluster.

1. Click **Connect**.

1. Click **Launch Cloud Shell**.

1. Enter the database name and user name.

1. Select the API to use (YSQL or YCQL) and click **Confirm**.

    The shell displays in a separate browser page. Cloud Shell can take up to 30 seconds to be ready.

1. Enter the password for the user you specified.

The `ysqlsh` or `ycqlsh` prompt appears and is ready to use.

```output
ysqlsh (11.2-YB-2.6.1.0-b0)
SSL connection (protocol: TLSv1.2, cipher: ECDHE-RSA-AES256-GCM-SHA384, bits: 256, compression: off)
Type "help" for help.

yugabyte=#
```

```output
Connected to local cluster at 3.69.145.48:9042.
[ycqlsh 5.0.1 | Cassandra 3.9-SNAPSHOT | CQL spec 3.4.2 | Native protocol v4]
Use HELP for help.
admin@ycqlsh:yugabyte>
```

## ysqlsh meta-commands in Cloud Shell

Cloud Shell supports the use of ysqlsh [meta-commands](../../../admin/ysqlsh-meta-commands/). However, for security reasons, some commands are not available. With the exception of read-only access to the `/share` directory to load the [sample datasets](../../../sample-data/), commands that access the filesystem do not work in Cloud Shell.

The following table lists common meta-commands that can be used in Cloud Shell.

| Command | Description |
| :--- | :--- |
| \c [database name] | Connect to a database. For example, `\c yb_demo`. |
| \l | List all databases. |
| \d | Display tables, views, and sequences. |
| \d [name] | List columns with their types and attributes for the named table, view, materialized view, index, sequence, or foreign table. |
| \dt | Display tables. |
| \dv | Display views. |
| \dm | Display materialized views. |
| \di | Display indexes. |
| \dn | Display schemas. |
| \dT | Display data types. |
| \du | Display roles. |
| \sv [view name] | Show a view definition. |
| \x [ on \| off \| auto ] | Toggle the expanded display. Used for viewing tables with many columns. Can be toggled on or off, or set to auto. |
| \set | List all internal variables. |
| \set [Name] [Value] | Set new internal variable. |
| \unset [Name] | Delete internal variable. |
| \timing | Toggles timing on queries. |
| \echo [message] | Print the message to the console. |
| \i [filename] | Execute commands from a file in the /share directory only. For example, `\i share/chinook_ddl.sql`. |
| \q | Exits ysqlsh. |

## Related information

- [YSQL API](../../../api/ysql/) — Reference for supported YSQL statements, data types, functions, and operators.
- [YCQL API](../../../api/ycql/) — Reference for supported YCQL statements, data types, functions, and operators.
- [ysqlsh](../../../admin/ysqlsh/) — Overview of the CLI, syntax, and meta-commands.
- [ycqlsh](../../../admin/ycqlsh/) — Overview of the CLI, syntax, and commands.

## Next steps

- [Add database users](../../cloud-secure-clusters/add-users/)
- [Connect an application](../connect-applications/)
