---
title: TablePlus
linkTitle: TablePlus
description: TablePlus
aliases:
  - /develop/tools/tableplus/
menu:
  v1.1:
    identifier: tableplus
    parent: tools
    weight: 547
isTocNested: true
showAsideToc: true
---

## Introduction

[TablePlus](https://tableplus.io/) is an increasingly popular database developer console with built-in integrations with major databases including PostgreSQL, Cassandra and Redis. It is free to get started with the option of upgrading to a [perpetual paid license](https://tableplus.io/pricing) for a small cost.

In this tutorial, we will show how to connect TablePlus with all the 3 YugaByte DB APIs on a local cluster. TablePlus works without any issues with YugaByte DB because the YugaByte DB APIs are compatible at the wire protocol level with databases already supported by TablePlus.

## Download TablePlus

TablePlus is available on both macOS and Windows. Follow the links below to download.

- [TablePlus on macOS](https://tableplus.io/release/osx/tableplus_latest)
- [TablePlus on Windows](https://tableplus.io/windows)

Install TablePlus after the download completes.

## Start a Local Cluster

Follow the instructions in the [Quick Start](../../../quick-start/install) to create a local YugaByte DB cluster. We will use the macOS install instructions for the rest of this tutorial.

```sh
$ ./bin/yb-ctl create --enable_postgres
```

At this point, we have YSQL running on 127.0.0.1:5433, YCQL on 127.0.0.1:9042 and YEDIS on 127.0.0.1:6379. We are now ready to connect these API endpoints with TablePlus.

## Connect with PostgreSQL-compatible YSQL

Click `Create a new connection` on TablePlus and then choose `Postgres` from the list of database types.

![Choose DB](/images/develop/tools/tableplus/choose-db.png)

Now enter the connection details as shown in the screenshot below and then click `Test` to ensure that TablePlus is indeed able to establish connectivity with the YugaByte DB API. The color of the fields will change to green if the test succeeds.

![YSQL](/images/develop/tools/tableplus/ysql-connection.png)

## Connect with Cassandra-compatible YCQL

Repeat the above steps for the Cassandra type as shown below.

![YCQL](/images/develop/tools/tableplus/ycql-connection.png)

## Connect with Redis-compatible YEDIS

Repeat the above steps for the Redis type as shown below.

![YEDIS](/images/develop/tools/tableplus/yedis-connection.png)

## Explore with TablePlus

Now you have connected to all the YugaByte DB APIs and can start exploring them by simply double-clicking on the connection name.

![YB ALL](/images/develop/tools/tableplus/yb-all-connection.png)

Follow the instructions from [TablePlus Getting Started](https://tableplus.io/blog/2018/04/getting-started-with-tableplus.html) on how to best use TablePlus.

## Known Issue

Following is a known issue that we hope to address soon. You can track the issue directly on GitHub.

[tableplus integration: redis metadata commands should gracefully error for redis compatible yedis](https://github.com/YugaByte/yugabyte-db/issues/503)
