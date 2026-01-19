---
title: Built-in connection pooling in YSQL
headerTitle: Built-in connection pooling
linkTitle: Built-in connection pooling
description: YSQL Connection Manager 
headcontent: YSQL Connection Manager
tags:
  feature: tech-preview
menu:
  v2024.1:
    name: Built-in connection pooling
    identifier: explore-conn-mgr-ysql
    parent: going-beyond-sql
    weight: 600
type: docs
---

YugabyteDB inherits the architecture of creating one backend process for every connection to the database from PostgreSQL. These backend processes consume memory and CPU, limiting the number of connections YugabyteDB can support. To solve this problem, you can use a connection pooler, which allows multiplexing multiple client connections to a smaller number of actual server connections, thereby supporting a larger number of connections from applications. [PgBouncer](https://github.com/pgbouncer/pgbouncer) and [Odyssey](https://github.com/yandex/odyssey) are some of the popular PostgreSQL-based server-side connection pooling mechanisms that are fully compatible with YugabyteDB.

However, connection poolers have some limitations:

- Added complexity. Deploying and maintaining a connection pooler adds complexity to your application stack.
- They don't support all PostgreSQL features. For example, neither PgBouncer nor Odyssey supports SET statements in the transaction pooling mode.

To provide the advantages of connection pooling, but without the limitations, YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](/stable/develop/drivers-orms/smart-drivers/), YugabyteDB simplifies application architecture and enhances developer productivity.

![Connection manager](/images/explore/ysql-connection-manager.png)

## Key features

YSQL Connection Manager is a modified version of the open-source connection pooler Odyssey. YSQL Connection Manager uses Odyssey in the transaction pooling mode and has been modified at the wire protocol level for tighter integration with YugabyteDB to overcome some SQL limitations.

YSQL Connection Manager has the following key features:

- No SQL limitations. Unlike other pooling solutions running in transaction mode, YSQL Connection Manager supports SQL features such as TEMP TABLE, WITH HOLD CURSORS, and more.

- Per user and database pool, with quota sharing. Like PgBouncer and Odyssey, YSQL Connection Manager creates a pool for each unique combination of user and database. However, it also allows connection quotas to be shared across multiple such pools, enabling more efficient use of resources and improved scalability.

- Support for session parameters. YSQL Connection Manager supports SET statements, which are not supported by other connection poolers.

- Support for prepared statements. Odyssey supports protocol-level prepared statements and YSQL Connection Manager inherits this feature.

## How to use

To start a YugabyteDB cluster with YSQL Connection Manager, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_ysql_conn_mgr` to true.

When `enable_ysql_conn_mgr` is set, each YB-TServer starts the YSQL Connection Manager process along with the PostgreSQL process. You should see one YSQL Connection Manager process per YB-TServer.

Because `enable_ysql_conn_mgr` is a preview flag, to use it, add the flag to the [allowed_preview_flags_csv](../../../reference/configuration/yb-tserver/#allowed-preview-flags-csv) list (that is, `allowed_preview_flags_csv=enable_ysql_conn_mgr`).

For example, to create a single-node cluster with YSQL Connection Manager using [yugabyted](../../../reference/configuration/yugabyted/), use the following  command:

```sh
./bin/yugabyted start --tserver_flags "enable_ysql_conn_mgr=true,allowed_preview_flags_csv={enable_ysql_conn_mgr}" --ui false
```

{{< note >}}

To create a large number of client connections, ensure that "SHMMNI" (the maximum number of concurrent shared memory segments an OS allows) as well as [ulimit](../../../deploy/manual-deployment/system-config/#set-ulimits) is set correctly as follows:

1. Open the file `/etc/sysctl.conf`.
1. Add `kernel.shmmni = 32768` (support for 30000 clients) at the end of the file.
1. To refresh the settings, use `sudo sysctl -p`.
{{< /note >}}

## Learn more

- [YSQL Connection Manager documentation](../../../additional-features/connection-manager-ysql/)
- Blog: [Built-in Connection Manager Turns Key PostgreSQL Weakness into a Strength](https://www.yugabyte.com/blog/connection-pooling-management/)
