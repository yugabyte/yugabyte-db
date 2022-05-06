---
title: Use TablePlus with YugabyteDB YSQL and YCQL
headerTitle: TablePlus
linkTitle: TablePlus
description: Learn how to connect TablePlus to YugabyteDB and query using YSQL, YCQL, and YEDIS.
aliases:
  - /develop/tools/tableplus/
  - /preview/develop/tools/tableplus/
menu:
  preview:
    identifier: tableplus
    parent: tools
    weight: 2750
isTocNested: true
showAsideToc: true
---

[TablePlus](https://tableplus.io/) is a popular database developer console with built-in integrations with major databases including PostgreSQL, Cassandra, and Redis. It is free to get started with the option of upgrading to a [perpetual paid license](https://tableplus.io/pricing). TablePlus works with YugabyteDB without any issues because the YugabyteDB APIs are compatible at the wire protocol level with databases already supported by TablePlus.

This tutorial shows how to connect TablePlus to a YugabyteDB cluster.

## Before you begin

To use TablePlus with YugabyteDB, you need to have YugabyteDB up and running. Refer to [YugabyteDB Prerequisites](../#yugabytedb-prerequisites).

## Install TablePlus

TablePlus is available on both macOS and Windows. Follow the links below to download.

- [TablePlus on macOS](https://tableplus.io/release/osx/tableplus_latest)
- [TablePlus on Windows](https://tableplus.io/windows)

Install TablePlus after the download completes.

## Connect with PostgreSQL-compatible YSQL

Click `Create a new connection` on TablePlus and then choose `Postgres` from the list of database types.

![Choose DB](/images/develop/tools/tableplus/choose-db.png)

Enter the connection details for your cluster as shown in the following illustration and then click `Test` to ensure that TablePlus is indeed able to establish connectivity with the YugabyteDB API. The color of the fields changes to green if the test succeeds.

![YSQL](/images/develop/tools/tableplus/ysql-connection.png)

## Connect with Cassandra-compatible YCQL

Repeat the above steps for the Cassandra type as shown below.

![YCQL](/images/develop/tools/tableplus/ycql-connection.png)

<!--## Connect with Redis-compatible YEDIS

Repeat the above steps for the Redis type as shown below.

![YEDIS](/images/develop/tools/tableplus/yedis-connection.png)
-->
## Explore with TablePlus

Now you have connected to all the YugabyteDB APIs and can start exploring them by double-clicking the connection name.

![YB ALL](/images/develop/tools/tableplus/yb-all-connection.png)

Follow the instructions from [TablePlus Getting Started](https://tableplus.io/blog/2018/04/getting-started-with-tableplus.html) on how to best use TablePlus.

<!--## Known issue

Following is a known issue that we hope to address soon. You can track the issue directly on GitHub.

[tableplus integration: redis metadata commands should gracefully error for redis compatible yedis](https://github.com/yugabyte/yugabyte-db/issues/503) -->
