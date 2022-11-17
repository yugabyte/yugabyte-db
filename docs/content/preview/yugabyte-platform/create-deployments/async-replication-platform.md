---
title: Deploy to two data centers with xCluster replication
headerTitle: xCluster replication
linkTitle: xCluster replication
description: Enable deployment using asynchronous replication between two data centers
menu:
  preview_yugabyte-platform:
    parent: create-deployments
    identifier: async-replication-platform
    weight: 633
aliases:
type: docs
---

YugabyteDB Anywhere allows you to use its UI or [API](https://api-docs.yugabyte.com/docs/yugabyte-platform/) to manage asynchronous replication between independent YugabyteDB clusters. You can perform deployment via unidirectional (master-follower) or [bidirectional](#set-up-bidirectional-replication) (multi-master) xCluster replication between two data centers.

Within the concept of replication, universes are divided into the following categories:

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

1. Click **Configure Replication** to open the dialog shown in the following illustration:<br>

   ![Configure Replication](/images/yp/asynch-replication-221.png)

1. Provide a name for your replication. 

   This name cannot contain any of the following: [SPACE '_' '*' '<' '>' '?' '|' '"' NULL].

1. Select the target universe.

   Note that the paused universes are not included in the selection list.

1. Click **Next: Select Tables**.

1. From a list of common tables between source and target universes, select the tables you want to include in the replication, as per the following illustration:

   ![Create Replication](/images/yp/asynch-replication-333.png)

   Note that even though index tables are not displayed, their replication is set up if the main table is selected for replication.

   See [About the table selection](#about-the-table-selection) for additional information.

1. Click **Validate Table Selection**.

   This triggers YugabyteDB Anywhere to check whether or not bootstrapping is required for the selected database and its tables: 
   
   - If bootstrapping is not required, **Validate Table Selection** changes to **Enable Replication** which you need to click in order to proceed. 
   
     ![Create Replication](/images/yp/asynch-replication-332.png)
   
   - If bootstrapping is required, **Validate Table Selection** changes to **Next: Configure Bootstrap** which you need to click in order to proceed, as per the following illustration:
   
     ![Create Replication](/images/yp/asynch-replication-331.png)
   
     Complete the fields of the dialog shown in the following illustration:
   
     ![Create Replication](/images/yp/asynch-replication-335.png)
   
     Select the storage configuration to be used during backup and restore part of bootstrapping. For information on how to configure storage, see [Configure backup storage](../../back-up-restore-universes/configure-backup-storage/).
   
     Optionally, specify the number of parallel threads that can run during bootstrapping. The greater number would result in a more significant decrease of the backup and restore time, but put  more pressure on the universes.
   
     Clicking **Bootstrap and Enable Replication** starts the process. Tables that do not require bootstrapping are set up for replication first, followed by tables that need bootstrapping, database per database.
   
     You can view the progress on xCluster tasks by navigating to **Universes**, selecting the source universe, and then selecting **Tasks**.
   
1. Optionally, configure alerts on lag, as follows:

   - Click **Max acceptable lag time**.
   - Use the **Define Max Acceptable Lag Time** dialog to enable or disable alert issued when the replication lag exceeds the specified threshold, as per the following illustration:<br><br>
   ![Create Replication](/images/yp/asynch-replication-91.png)

### About the table selection

Tables in an xCluster configuration must be of the same type: either YCQL or YSQL. If you are planning to replicate tables of different types, you need to create separate xCluster configurations. 

To be considered eligible for xCluster replication, tables must meet the following criteria:

1. The table is not already in use. That is, the table is not involved in another xCluster configuration between the same two universes in the same direction.
2. A matching table exists on the target universe. That is, a table with the same name in the same keyspace and with the same schema exists on the target universe.

If a YSQL keyspace includes tables considered ineligible for replication, this keyspace cannot be selected in the YugabyteDB Anywhere UI and the database as a whole cannot be replicated.

{{< note title="Note" >}}

Even though you could use yb-admin to replicate a subset of tables from a YSQL keyspace, it is not recommended, as it might result in issues due to bootstrapping involving unsupported operations of backup and restore.

{{< /note >}}

Since replication is a table-level task, selecting a keyspace adds all its current tables to the xCluster configuration. Any tables created later must be manually added to the xCluster configuration if replication is required.

<!--

Bootstrapping is automatically enabled for any new and existing replication. When the source universe has data, its write-ahead logs are garbage-collected. If bootstrapping is not required for tables that are to be added, these tables are added to the replication group. However, if there is a table that requires bootstrapping, all other tables in the same database are removed from the replication group and the replication is set up using the bootstrap flow for all the tables, including the new ones requested to be added. Note that a replication may contain tables from different databases which are not affected by the remove operation.

-->

## View, manage, and monitor replication

To view and manage an existing replication, as well as perform monitoring, click the replication name to open the details page shown in the following illustration:

![Replication Details](/images/yp/asynch-replication-441.png)

This page allows you to do the following:

- View the replication details.

- Pause the replication process (stop the traffic) by clicking **Pause Replication**. This is useful when performing maintenance. Paused replications can be resumed from the last checkpoint.

- Restart the replication by clicking **Actions > Restart Replication** and using the dialog shown in the following illustaration to select the tables or database for which to restart the replication:

  ![Replication Details](/images/yp/asynch-replication-551.png)

  Note that even though index tables are not displayed in the preceding dialog, the replication is automatically restarted for them if it is restarted for the main table.

- Delete the replication regardless of its status or errors by clicking **Actions > Delete Replication**.

- Rename the replication by clicking **Actions > Edit Replication Name**.

- View and modify the list of tables included in the replication, as follows:

  - Select **Tables**.

  - To find out the replication lag for a specific table, click the graph icon corresponding to that table.

  - To check if the replication has been properly configured for a table. If so, the table's status is shown as Operational. If the replication lag increase beyond maximum acceptable lag defined during the replication setup or the lag is not being reported, the table's status is shown as Warning. If the replication lag has increased to the extend that the replication cannot continue without bootstrapping of current data, the status is shown as Error: the table's replication stream is broken and data is missing on the target universe. If a lag alert is enabled on the replication, you will be notified when the excessive lag has caused the error, in which case you would be able to restart the replication so the data is bootstrapped and the replication is recreated.

  - To delete a table from the replication, click **... > Remove Table**. This removes both the table and its index tables from replication. Note that you may prevent an index table from being replicated without affecting its main table.

  - To add more tables to the replication, click **Add Tables** to open the **Add Tables to Replication** dialog which allows you to make a selection from a list of tables that have not been replicated, and then click **Validate Table Selection**.

    Note that a table with the same name in the same database (or the same namespace) with the same schema must exist on the target universe before the table can be added. Otherwise the table is tagged as No Match and cannot be selected. See [About the table selection](#about-the-table-selection) for additional information.
    
    When using YCQL, it is recommended to add new tables as soon as they are created. If the table contains enough data, and then you decide to add it to the replication configuration, bootstrapping will be required, in which case other tables in the same database must be removed from the replication and also subjected to bootstrapping.
    
    When using YSQL, new tables in a database can be added to the replication configuration. Moreover, if a subset of YSQL tables of a database is already in replication in a configuration, the remaining tables in that database must be added to the same configuration.

- View detailed statistics on the asynchronous replication lag by selecting **Metrics**, as per the following illustration:

  ![Alert](/images/yp/asynch-replication-61.png)<br>

## Set up bidirectional replication

You can set up bidirectional replication using either the YugabyteDB Anywhere UI or API by creating two separate replication configurations. Under this scenario, a source universe of the first replication becomes the target universe of the second replication, and vice versa.

Bootstrapping is not enabled for bidirectional replication.

## Limitations

The current implementation of xCluster replication in YugabyteDB Anywhere has the following limitations:

- The source and target universes must exist on the same instance of YugabyteDB Anywhere because data needs to be backed up data from the source universe and restored to the target universe.
- The chosen storage configuration for bootstrapping must be accessible from both universes.
- Active-active bidirectional replication is not supported because the backup or restore would wipe out the existing data. This means that bootstrapping can be done only if an xCluster configuration with reverse direction for a table does not exist. It is recommended to set up replication from your active universe to the passive target, and then set up replication for the target to the source universe. To restart a replication with bootstrap, the reverse replication must be deleted.
- The tables with the same name (`database.schema_name.table_name` for YSQL and `keyspace.table_name` for YCQL) and schema must exist on both universes before xCluster replication can be set up.
- An xCluster configuration can contain only YSQL or YCQL tables.
- If you are setting up the replication for YSQL with bootstrapping enabled, you must select all the tables in one database.
- You cannot use the YugabyteDB Anywhere UI to create two separate replication configurations for YSQL, each containing a subset of the database tables.
- The xCluster replication creation process (bootstrapping) is not aborted if the source universe is running out of space. Instead, a warning is displayed notifying that the source universe has less than 100 GB of space remaining. Therefore, you must ensure that there is enough space available on the source universe before attempting to set up the replication. A recommended approach would be to estimate that the available disk space on the source universe is the same as the used disk space.
- Bootstrapping may not work with universes that use encryption-at-rest.
- When using xCluster replication, it is recommended to use the same version of YugabyteDB for both universes. In case of software upgrades, the target universe should be upgraded first.
- If, after setting up xCluster replication, you add a node to the target universe with TLS enabled, you need to manually copy the source universe's root certificate to the added node.
- The xCluster replication creation task might clash with scheduled backups. It is recommended to wait for the scheduled backup to finish, and then restart the replication.



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
