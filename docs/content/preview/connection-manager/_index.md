---
title: YSQL Connection Manager
headerTitle: YSQL Connection Manager
linkTitle: YSQL Connection Manager
description: Built-in server-side connection pooler for YSQL
headcontent: Built-in server-side connection pooler for YSQL
tags:
  feature: early-access
menu:
  preview:
    identifier: connection-manager
    parent: launch-and-manage
    weight: 40
type: indexpage
---

YugabyteDB includes a built-in connection pooler, YSQL Connection Manager. Because the manager is bundled with the product, it is convenient to manage, monitor, and configure the server connections without additional third-party tools. When combined with [smart drivers](../../../drivers-orms/smart-drivers/), YugabyteDB simplifies application architecture and enhances developer productivity.

{{<index/block>}}

  {{<index/item
    title="Set up connection manager"
    body="Single-zone deployments on popular Kubernetes distributions."
    href="ycm-setup/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Best practices"
    body="Multi-zone deployments on Amazon EKS and GKE."
    href="ycm-best-practices/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Observability"
    body="Monitor your connections."
    href="ycm-monitor/"
    icon="fa-thin fa-globe-wifi">}}

  {{<index/item
    title="Migrate"
    body="Migrate from your current pooling solution."
    href="ycm-migrate/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Troubleshoot"
    body="Troubleshoot connection problems."
    href="ycm-troubleshoot/"
    icon="fa-thin fa-dharmachakra">}}

{{</index/block>}}

## Limitations

- Changes to [configuration parameters](../../../reference/configuration/yb-tserver/#postgresql-configuration-parameters) for a user or database that are set using ALTER ROLE SET or ALTER DATABASE SET queries may reflect in other pre-existing active sessions.
- YSQL Connection Manager can route up to 10,000 connection pools. This includes pools corresponding to dropped users and databases.
- Prepared statements may be visible to other sessions in the same connection pool. [#24652](https://github.com/yugabyte/yugabyte-db/issues/24652)
- Attempting to use DEALLOCATE/DEALLOCATE ALL queries can result in unexpected behavior. [#24653](https://github.com/yugabyte/yugabyte-db/issues/24653)
- Currently, you can't apply custom configurations to individual pools. The YSQL Connection Manager configuration applies to all pools.
- When YSQL Connection Manager is enabled, the backend PID stored using JDBC drivers may not be accurate. This does not affect backend-specific functionalities (for example, cancel queries), but this PID should not be used to identify the backend process.
- By default, `currval` and `nextval` functions do not work when YSQL Connection Manager is enabled. They can be supported with the help of the `ysql_conn_mgr_sequence_support_mode` flag.
- YSQL Connection Manager does not yet support IPv6 connections. [#24765](https://github.com/yugabyte/yugabyte-db/issues/24765)
- Currently, [auth-method](https://docs.yugabyte.com/preview/secure/authentication/host-based-authentication/#auth-method) `cert` is not supported for host-based authentication. [#20658](https://github.com/yugabyte/yugabyte-db/issues/20658)
- Although the use of auth-backends (`ysql_conn_mgr_use_auth_backend=true`) to authenticate logical connections can result in higher connection acquisition latencies, using auth-passthrough (`ysql_conn_mgr_use_auth_backend=false`) may not be suitable depending on your workload. Contact the YSQL Connection Manager Development team before setting `ysql_conn_mgr_use_auth_backend` to false. [#25313](https://github.com/yugabyte/yugabyte-db/issues/25313)
