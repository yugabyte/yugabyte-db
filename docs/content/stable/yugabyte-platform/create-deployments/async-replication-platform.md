---
title: Deploy to two data centers with xCluster replication
headerTitle: xCluster replication
linkTitle: xCluster replication
description: Enable deployment using asynchronous replication between two data centers
menu:
  stable_yugabyte-platform:
    parent: create-deployments
    identifier: async-replication-platform
    weight: 50
type: docs
---

YugabyteDB Anywhere allows you to use its UI or [API](https://api-docs.yugabyte.com/docs/yugabyte-platform/) to manage asynchronous replication between independent YugabyteDB clusters. You can perform deployment via unidirectional (master-follower) or [bidirectional](#set-up-bidirectional-replication) (multi-master) xCluster replication between two data centers.

In the concept of replication, universes are divided into the following categories:

- A source universe contains the original data that is subject to replication.

- A target universe is the recipient of the replicated data. One source universe can replicate to one or more target universes.

For additional information on xCluster replication in YugabyteDB, see the following:

- [xCluster replication: overview and architecture](/preview/architecture/docdb-replication/async-replication/)
- [xCluster replication between universes in YugabyteDB](/preview/deploy/multi-dc/async-replication/)

You can use the YugabyteDB Anywhere UI to set up and configure xCluster replication for universes. In addition, you can perform monitoring by accessing the information about the replication lag and enabling alerts on excessive lag.

## Set up replication

You can set up xCluster replication as follows:

1. Open the YugabyteDB Anywhere UI and navigate to **Universes**.

1. Select the universe you want to replicate and navigate to **Replication**.

1. Click **Configure Replication** to open the dialog shown in the following illustration:

    ![Configure Replication](/images/yp/asynch-replication-221.png)

1. Provide a name for your replication. This name cannot contain [SPACE '_' '*' '<' '>' '?' '|' '"' NULL].

1. Select the target universe.

    Note that paused universes are not included in the selection list.

1. Click **Next: Select Tables**.

1. From a list of common tables between source and target universes, select the tables you want to include in the replication, as per the following illustration:

    ![Create Replication](/images/yp/asynch-replication-333.png)

    See [About the table selection](#about-the-table-selection) for additional information.

    Note that you cannot set up replication for colocated tables.

1. Select the **Enable transactional atomicity** option (YSQL tables only) so that the read operations on the target universe will be transactional. For more information, refer to [Transactional xCluster deployment](../../../deploy/multi-dc/async-replication-transactional/). Currently, this has the following limitations:

    - Point in time restore must be enabled on the target universe for the databases that you want to replicate, and matching databases must exist on both universes prior to creating the replication.
    - Neither the source universe nor the target universe should be a participant in any other xCluster configuration.

1. Click **Validate Table Selection**.

    YugabyteDB Anywhere checks whether or not bootstrapping is required for the selected database and its tables. Depending on the result, do the following:

    - If bootstrapping is not required, **Validate Table Selection** changes to **Enable Replication** which you need to click in order to set up replication.

    - If bootstrapping is required, **Validate Table Selection** changes to **Next: Configure Bootstrap** which you need to click in order to access the bootstrapping settings, and then proceed by completing the fields of the dialog shown in the following illustration:

      ![Create Replication](/images/yp/asynch-replication-335.png)

      Select the storage configuration to be used during backup and restore part of bootstrapping. For information on how to configure storage, see [Configure backup storage](../../back-up-restore-universes/configure-backup-storage/).

      Optionally, specify the number of parallel threads that can run during bootstrapping. The greater number would result in a more significant decrease of the backup and restore time, but put  more pressure on the universes.

      Clicking **Bootstrap and Enable Replication** starts the process of setting up replication. Tables that do not require bootstrapping are set up for replication first, followed by tables that need bootstrapping, database per database.

    You can view the progress on xCluster tasks by navigating to **Universes**, selecting the source universe, and then selecting **Tasks**.

1. Optionally, configure alerts on lag, as follows:

    - Click **Max acceptable lag time**.
    - Use the **Define Max Acceptable Lag Time** dialog to enable or disable alert issued when the replication lag exceeds the specified threshold, as per the following illustration:

      ![Create Replication](/images/yp/asynch-replication-91.png)

    In addition to the replication lag alert, an alert for unreplicated YSQL tables on the source universe in a database with replicated tables, is added by default. This alert will fire if, for example, a user has added a new table to a database but neglected to add it to the replication group. After receiving this alert, you can add the table to the replication; refer to [View, manage, and monitor replication](#view-manage-and-monitor-replication).

### About the table selection

Tables in an xCluster configuration must be of the same type: either YCQL or YSQL. If you are planning to replicate tables of different types, you need to create separate xCluster configurations.

To be considered eligible for xCluster replication, tables must not already be in use. That is, the table is not involved in another xCluster configuration between the same two universes in the same direction.

If a YSQL keyspace includes tables considered ineligible for replication, this keyspace cannot be selected in the YugabyteDB Anywhere UI and the database as a whole cannot be replicated.

{{< note title="Note" >}}

Even though you could use yb-admin to replicate a subset of tables from a YSQL keyspace, it is not recommended, as it might result in issues due to bootstrapping involving unsupported operations of backup and restore.

{{< /note >}}

Because replication is a table-level task, selecting a keyspace adds all its current tables to the xCluster configuration. Any tables created later must be manually added to the xCluster configuration if replication is required.

<!--

Bootstrapping is automatically enabled for any new and existing replication. When the source universe has data, its write-ahead logs are garbage-collected. If bootstrapping is not required for tables that are to be added, these tables are added to the replication group. However, if there is a table that requires bootstrapping, all other tables in the same database are removed from the replication group and the replication is set up using the bootstrap flow for all the tables, including the new ones requested to be added. Note that a replication may contain tables from different databases which are not affected by the remove operation.

-->

## View, manage, and monitor replication

To view and manage an existing replication, as well as perform monitoring, click the replication name to open the details page shown in the following illustration:

![Replication Details](/images/yp/asynch-replication-441.png)

This page allows you to do the following:

- View the replication details.

- Pause the replication process (stop the traffic) by clicking **Pause Replication**. This is helpful when performing maintenance. Paused replications can be resumed from the last checkpoint.

- Restart the replication by clicking **Actions > Restart Replication** and using the dialog shown in the following illustration to select the tables or database for which to restart the replication:

  ![Replication Details](/images/yp/asynch-replication-551.png)

  Note that even though index tables are not displayed, the replication is automatically restarted for them if it is restarted for the main table.

- Delete the replication configuration by clicking **Actions > Delete Replication**.

- Rename the replication by clicking **Actions > Edit Replication Name**.

- View and modify the list of tables included in the replication, as follows:

  - Select **Tables**.

  - To find out the replication lag for a specific table, click the graph icon corresponding to that table.

  - To check if the replication has been properly configured for a table, check the status. If properly configured, the table's status is shown as _Operational_.

    The status will be _Not Reported_ momentarily after the replication configuration is created until the source universe reports the replication lag for that table and Prometheus can successfully retrieve it. This should take about 10 seconds.

    If the replication lag increases beyond maximum acceptable lag defined during the replication setup or the lag is not being reported, the table's status is shown as _Warning_.

    If the replication lag has increased to the extent that the replication cannot continue without bootstrapping of current data, the status is shown as _Error: the table's replication stream is broken_, and restarting the replication is required for those tables. If a lag alert is enabled on the replication, you are notified when the lag is behind the specified limit, in which case you may open the table on the replication view to check if any of these tables have their replication status as Error.

    If YugabyteDB Anywhere is unable to obtain the status (for example, due to a heavy workload being run on the universe) the status for that table will be _UnableToFetch_. You may refresh the page to retry gathering information.

  - To delete a table from the replication, click **... > Remove Table**. This removes both the table and its index tables from replication. If you decide to remove an index table from the replication group, it does not remove its main table from the replication group.

  - To add more tables to the replication, click **Add Tables** to open the **Add Tables to Replication** dialog which allows you to make a selection from a list of tables that have not been replicated, and then click **Validate Table Selection**.

    To make the add table operation faster, it is recommended to add new tables to the replication as soon as they are created. When using YSQL, if a subset of tables of a database is already in replication in a configuration, the remaining tables in that database can only be added to the same configuration. Moreover, if adding a YSQL table requires bootstrapping, the other tables in the same database which are already in replication will be removed from replication. The replication will be set up again for all the tables in that database.

- View detailed statistics on the asynchronous replication lag by selecting **Metrics**, as per the following illustration:

  ![Alert](/images/yp/asynch-replication-61.png)

If for any reason the source universe is no longer accessible, you can pause or delete the replication configuration. This is especially beneficial for unplanned failover. Refer to [Unplanned failover](../../../deploy/multi-dc/async-replication-transactional/#unplanned-failover).

Make sure to select the **Ignore errors and force delete** option when you delete the configuration in such cases.

## Set up bidirectional replication

You can set up bidirectional replication using either the YugabyteDB Anywhere UI or API by creating two separate replication configurations. Under this scenario, a source universe of the first replication becomes the target universe of the second replication, and vice versa.

Bootstrapping is skipped for tables on the target universe that are already in replication. It means that, while setting up bidirectional replication, bootstrapping can be done in only one direction, that of the first replication configuration. For example, when you set up replication for table  t1 from universe u1 to u2, it may go through bootstrapping. However, if  you set up replication in the reverse direction, from universe u2 to u1 for table t1, bootstrapping is skipped.

## Limitations

The current implementation of xCluster replication in YugabyteDB Anywhere has the following limitations:

- The source and target universes must exist on the same instance of YugabyteDB Anywhere because data needs to be backed up from the source universe and restored to the target universe.
- The chosen storage configuration for bootstrapping must be accessible from both universes.
- Active-active bidirectional replication is not supported because the backup or restore would wipe out the existing data. This means that bootstrapping can be done only if an xCluster configuration with reverse direction for a table does not exist. It is recommended to set up replication from your active universe to the passive target, and then set up replication for the target to the source universe. To restart a replication with bootstrap, the reverse replication must be deleted.
- If you are setting up the replication for YSQL with bootstrapping enabled, you must select all the tables in one database.
- You cannot use the YugabyteDB Anywhere UI to create two separate replication configurations for YSQL, each containing a subset of the database tables.
- The xCluster replication creation process (bootstrapping) is not aborted if the source universe is running out of space. Instead, a warning is displayed notifying that the source universe has less than 100 GB of space remaining. Therefore, you must ensure that there is enough space available on the source universe before attempting to set up the replication. A recommended approach would be to estimate that the available disk space on the source universe is the same as the used disk space.
- When using xCluster replication, it is recommended to use the same version of YugabyteDB for both universes. In case of software upgrades, the target universe should be upgraded first.
- If you rotate the root certificate on the source universe, you need to restart the replication so that the target nodes get the new root certificate for TLS verifications.
- The xCluster replication creation task might clash with scheduled backups. It is recommended to wait for the scheduled backup to finish, and then restart the replication.
- To set up a transactional xCluster replication for a database, a matching database (not necessarily with its tables) must exist on the target universe so you can set up PITR on the target.

<!--

## Use the REST API

You may choose to use the API to manage universes. You can call the following REST API endpoint on your YugabyteDB Anywhere instance for the source universe and the target universe involved in the xCluster replication between two data sources:

```http
PUT /api/customers/<customerUUID>/universes/<universeUUID>/setup_universe_2dc
```

*customerUUID* represents your customer UUID, and *universeUUID* represents the UUID of the universe (producer or consumer). The request should include an `X-AUTH-YW-API-TOKEN` header with your YugabyteDB Anywhere API key, as shown in the following example `curl` command:

```sh
curl -X PUT \
  -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
https://myPlatformServer/api/customers/customerUUID/universes/universeUUID/setup_universe_2dc
```

You can find your user UUID in YugabyteDB Anywhere as follows:

- Click the person icon at the top right of any YugabyteDB Anywhere page and open **Profile > General**.

- Copy your API token. If the **API Token** field is blank, click **Generate Key**, and then copy the resulting API token. Generating a new API token invalidates your existing token. Only the most-recently generated API token is valid.

- From a command line, issue a `curl` command of the following form:

  ```sh
  curl \
    -H "X-AUTH-YW-API-TOKEN: myPlatformApiToken" \
      [http|https]://myPlatformServer/api/customers
  ```

  <br>For example:

  ```sh
  curl -X "X-AUTH-YW-API-TOKEN: e5c6eb2f-7e30-4d5e-b0a2-f197b01d9f79" \
    http://localhost/api/customers
  ```

- Copy your UUID from the resulting JSON output shown in the following example, omitting the double quotes and square brackets:

  ```
  ["6553ea6d-485c-4ae8-861a-736c2c29ec46"]
  ```

  <br>To find a universe's UUID in YugabyteDB Anywhere, click **Universes** in the left column, then click the name of the universe. The URL of the universe's **Overview** page ends with the universe's UUID. For example, `http://myPlatformServer/universes/d73833fc-0812-4a01-98f8-f4f24db76dbe`

-->
