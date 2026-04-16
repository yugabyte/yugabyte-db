---
title: YSQL major upgrade - logical replication
headerTitle: YSQL major upgrade
linkTitle: YSQL major upgrade
description: Upgrade logical replication streams to YugabyteDB version that supports PG 15
headcontent: Upgrade YugabyteDB to a version that supports PG 15
menu:
  v2025.1:
    identifier: ysql-major-upgrade-3
    parent: manage-upgrade-deployment
    weight: 706
type: docs
---

Upgrading YugabyteDB from a version based on PostgreSQL 11 (all versions prior to v2.25) to a version based on PostgreSQL 15 (v2025.1 or later (stable) or v2.25 or later (preview)) requires additional steps. For instructions on upgrades within a major PostgreSQL version, refer to [Upgrade YugabyteDB](../upgrade-deployment/).

The upgrade is fully online. While the upgrade is in progress, you have full and uninterrupted read and write access to your cluster.

Performing a YSQL major upgrade on a universe with [CDC with logical replication](../../additional-features/change-data-capture/using-logical-replication/) requires additional steps.

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../ysql-major-upgrade-yugabyted/" class="nav-link">
      <img src="/icons/database.svg" alt="Server Icon">
      Yugabyted
    </a>
  </li>

  <li>
    <a href="../ysql-major-upgrade-local/" class="nav-link">
      <i class="icon-shell"></i>
      Manual
    </a>
  </li>

  <li>
    <a href="../ysql-major-upgrade-logical-replication/" class="nav-link active">
      <i class="icon-shell"></i>
      Logical Replication
    </a>
  </li>

</ul>

Logical replication streams can be upgraded to YugabyteDB v2025.1.1 and later.

When performing a YSQL major upgrade on a universe with CDC using logical replication, perform the following additional steps.

## Before you begin

1. Determine a time (say `stop_ddl_time`), after which be no DDLs will be performed on the tables being replicated using logical replication.

    {{< warning title="Do not make DDL changes" >}}
Do not perform any DDLs on relevant tables or modify the publication(s) between `stop_ddl_time` and completing the upgrade of logical replication streams. Any such DDL will render the slot useless, and you will need to create a new slot, potentially requiring an initial snapshot.
    {{< /warning >}}

1. Ensure that restart time of all the replication slots has crossed the `stop_ddl_time`. You can use the following query to find the restart time of a slot:

    ```sql
    SELECT to_timestamp((yb_restart_commit_ht / 4096) / 1000000) \
      AS yb_restart_time FROM pg_replication_slots;
    ```

    ```output
        yb_restart_time
    ------------------------
    2025-09-15 19:58:26+00
    (1 row)
    ```

1. After the `yb_restart_time` for all the slots has crossed `stop_ddl_time`, delete the connector and start the upgrade process.

## After finalizing the upgrade

After the upgrade is finalized and complete, do the following:

1. Note down the time at which the process was completed. Call it `upgrade_complete_time`.

1. Redeploy the connector, but add the following configuration to the existing list of configurations:

    ```yaml
    "ysql.major.upgrade":"true"
    ```

    If the connector is deployed without this configuration, you will see the following error:

    ```output
    ERROR:  catalog version for database 16640 was not found.
    HINT:  Database might have been dropped by another user
    STATEMENT:  START_REPLICATION SLOT "test_slot" LOGICAL 0/2D3B0 ("proto_version" '1', "publication_names" 'test_pub', "messages" 'true')
    ```

    This is not a problem as long as you deploy the connector with the specified configuration before the stream expires. The duration between killing the connector before upgrade and then redeploying it with the new configuration should be less than the expiry period.

    The connector will start streaming the records and covering up the lag which it accumulated. As the connector streams records, the restart time will move forward for the slot.

1. After the restart time of all the replication slots has exceeded `upgrade_complete_time`, restart the connector using the following:

    ```yaml
    "ysql.major.upgrade":"false"
    ```

    This marks the successful upgrade of the logical replication streams.

1. Resume the DDLs and make changes to the publications as desired.
