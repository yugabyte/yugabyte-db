---
title: Built-in connection pooling in YSQL
headerTitle: Built-in connection pooling
linkTitle: Built-in connection pooling
description: YSQL Connection Manager 
headcontent: YSQL Connection Manager
tags:
  feature: early-access
menu:
  v2.25:
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

To provide the advantages of connection pooling, but without the limitations, YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](/preview/develop/drivers-orms/smart-drivers/), YugabyteDB simplifies application architecture and enhances developer productivity.

![Connection manager](/images/explore/ysql-connection-manager.png)

## Key features

YSQL Connection Manager is a modified version of the open-source connection pooler Odyssey. YSQL Connection Manager uses Odyssey in the transaction pooling mode and has been modified at the wire protocol level for tighter integration with YugabyteDB to overcome some SQL limitations.

YSQL Connection Manager has the following key features:

- No SQL limitations. Unlike other pooling solutions running in transaction mode, YSQL Connection Manager supports SQL features such as TEMP TABLE, WITH HOLD CURSORS, and more.

- Per user and database pool, with quota sharing. Like PgBouncer and Odyssey, YSQL Connection Manager creates a pool for each unique combination of user and database. However, it also allows connection quotas to be shared across multiple such pools, enabling more efficient use of resources and improved scalability.

- Support for session parameters. YSQL Connection Manager supports SET statements, which are not supported by other connection poolers.

- Support for prepared statements. Odyssey supports protocol-level prepared statements and YSQL Connection Manager inherits this feature.

## Start YSQL Connection Manager

<ul class="nav nav-tabs nav-tabs-yb">
  <li >
    <a href="#yugabyted" class="nav-link active" id="yugabyted-tab" data-bs-toggle="tab" role="tab" aria-controls="yugabyted" aria-selected="true">
      <img src="/icons/database.svg" alt="Server Icon">
      Local
    </a>
  </li>
  <li>
    <a href="#platform" class="nav-link" id="platform-tab" data-bs-toggle="tab" role="tab" aria-controls="platform" aria-selected="false">
      <img src="/icons/server.svg" alt="Server Icon">
      YugabyteDB Anywhere
    </a>
  </li>
  <li>
    <a href="#aeon" class="nav-link" id="aeon-tab" data-bs-toggle="tab" role="tab" aria-controls="aeon" aria-selected="false">
      <img src="/icons/cloud.svg" alt="Cloud Icon">
      YugabyteDB Aeon
    </a>
  </li>
</ul>

<div class="tab-content">
  <div id="yugabyted" class="tab-pane fade show active" role="tabpanel" aria-labelledby="yugabyted-tab">

To start a YugabyteDB cluster with YSQL Connection Manager, set the [yb-tserver](../../../reference/configuration/yb-tserver/) flag `enable_ysql_conn_mgr` to true.

For example, to create a single-node cluster with YSQL Connection Manager using [yugabyted](../../../reference/configuration/yugabyted/), use the following command:

```sh
./bin/yugabyted start --tserver_flags "enable_ysql_conn_mgr=true" --ui false
```

When `enable_ysql_conn_mgr` is set, each YB-TServer starts the YSQL Connection Manager process along with the PostgreSQL process. You should see one YSQL Connection Manager process per YB-TServer.

{{< note >}}

To create a large number of client connections, ensure that "SHMMNI" (the maximum number of concurrent shared memory segments an OS allows) as well as [ulimit](../../../deploy/manual-deployment/system-config/#set-ulimits) is set correctly as follows:

1. Open the file `/etc/sysctl.conf`.
1. Add `kernel.shmmni = 32768` (support for 30000 clients) at the end of the file.
1. To refresh the settings, use `sudo sysctl -p`.
{{< /note >}}

  </div>
  <div id="platform" class="tab-pane fade" role="tabpanel" aria-labelledby="platform-tab">

{{<tags/feature/ea idea="1368">}}While in Early Access, YSQL Connection Manager is not available in YugabyteDB Anywhere by default. To make connection pooling available, set the **Allow users to enable or disable connection pooling** Global Runtime Configuration option (config key `yb.universe.allow_connection_pooling`) to true. Refer to [Manage runtime configuration settings](../../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/). You must be a Super Admin to set global runtime configuration flags.

To enable built-in connection pooling for universes deployed using YugabyteDB Anywhere:

- Turn on the **Connection pooling** option when creating a universe. Refer to [Create a multi-zone universe](../../../yugabyte-platform/create-deployments/create-universe-multi-zone/#advanced-configuration).
- Edit connection pooling on an existing universe. Refer to [Edit connection pooling](../../../yugabyte-platform/manage-deployments/edit-universe/#edit-connection-pooling).

Note that when managing universes using YugabyteDB Anywhere, do not set connection pooling flags, `enable_ysql_conn_mgr`, `ysql_conn_mgr_port`, and `pgsql_proxy_bind_address`.

**Connect**

To connect to the YSQL Connection Manager, use the [ysqlsh](../../../api/ysqlsh/) command with the [`-h <IP>`](../../../api/ysqlsh/#h-hostname-host-hostname) flag, instead of specifying the Unix-domain socket directory.

Using the socket directory along with [`-p`](../../../api/ysqlsh/#p-port-port-port) (custom PostgreSQL port or default 6433) will connect you to the PostgreSQL process, not the YSQL connection manager process.

  </div>
  <div id="aeon" class="tab-pane fade" role="tabpanel" aria-labelledby="aeon-tab">

{{<tags/feature/ea idea="1368">}}You can enable built-in connection pooling on YugabyteDB Aeon clusters in the following ways:

- When [creating a cluster](../../../yugabyte-cloud/cloud-basics/create-clusters/), turn on the **Connection Pooling** option. (Connection Pooling is enabled by default for [Sandbox clusters](../../../yugabyte-cloud/cloud-basics/create-clusters/create-clusters-free/).)
- For clusters that are already created, navigate to the cluster **Settings>Connection Pooling** tab.

Enabling connection pooling on an Aeon cluster gives 10 client connections for every server connection by default.

  </div>
</div>

## Learn more

- [YSQL Connection Manager documentation](../../../additional-features/connection-manager-ysql/)
- Blog: [Built-in Connection Manager Turns Key PostgreSQL Weakness into a Strength](https://www.yugabyte.com/blog/connection-pooling-management/)
