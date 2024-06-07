---
title: Upgrade universes with a new version of YugabyteDB
headerTitle: Upgrade a universe
linkTitle: Upgrade a universe
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software on universes.
headcontent: Upgrade the YugabyteDB software on your universe
menu:
  stable_yugabyte-platform:
    identifier: upgrade-software-install
    parent: upgrade-software
    weight: 20
type: docs
---

YugabyteDB is a distributed database that can be installed on multiple nodes. Upgrades happen in-place with minimal impact on availability and performance. This is achieved using a rolling upgrade, where each node is upgraded one at a time. YugabyteDB [automatically re-balances](../../../explore/linear-scalability/data-distribution/) the universe as nodes are taken down and brought back up during the upgrade.

If you have issues after upgrading a universe, you can roll back in-place and restore the universe to its state before the upgrade.
<!-- Roll back is available for universes being upgraded from YugabyteDB version 2.20.3 and later. -->

## View and import releases

Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available and, if necessary, import the release into YugabyteDB Anywhere.

To view the releases that are available, do the following:

- Click the user profile icon and choose **Releases**.

    ![Releases](/images/yp/releases-list.png)

If a release that you want to install on a universe is not available, import it as follows:

1. On the **Releases** page, click **Import** to open the **Import Releases** dialog as shown in the following illustration:

    ![Import Releases](/images/yp/import-releases.png)

1. Specify the release version to import.

1. Select the storage or URL that contains the release.

    - If the release is located on Amazon S3, you would need to provide the access information, in the form of your secret access key.
    - If the release is located on Google Cloud Storage, you would need to copy and paste the contents of the JSON file with the access credentials.
    - If the release is accessible via a HTTP, you would need to specify the checksum value of the download.

1. Provide the path to the storage location.

1. Click **OK**.

When imported, the release is added to the **Releases** list.

To delete or disable a release, click its corresponding **Actions**.

## Upgrade a universe

You upgrade a universe in the following phases:

- Upgrade - Update the nodes in the universe to the new database version.
- Monitor - Evaluate the performance and functioning of the new version.
- Rollback - If you encounter any issues while monitoring the universe, you have the option to roll back in-place and restore the universe to its state before the upgrade.
<!-- (Roll back is available for universes being upgraded from YugabyteDB version 2.20.3 and later.) -->
- Finalize - Depending on the changes included in the upgrade, you may need to finalize the upgrade to make the upgrade permanent. The system will tell you if this step is necessary. After finalizing, you can no longer roll back.

### Perform the upgrade

You perform a rolling upgrade on a live universe deployment as follows:

1. Navigate to **Universes** and select your universe.

1. Click **Actions > Upgrade Database Version** to display the **Upgrade Database** dialog.

    ![Upgrade Database](/images/yb-platform/upgrade/upgrade-database.png)

1. Choose the target version you want to upgrade to.

    {{< note title="Downgrading" >}}
Currently, you cannot downgrade a universe to an older YugabyteDB release. For assistance with downgrades, contact Yugabyte Support.
    {{< /note >}}

1. Choose the **Rolling Upgrade** option.

    Select rolling upgrade to minimize application disruption (at the expense of a longer node-by-node iterative operation). Deselect this option if application downtime is not a concern, and you favor speed; the database cluster is taken offline to perform the upgrade.

    For internal details about the steps involved in a YugabyteDB rolling upgrade, refer to [Upgrade a deployment](../../../manage/upgrade-deployment/).

1. If you are performing a rolling upgrade, specify the delay between node upgrades.

    The delay allows the newly restarted node to stabilize before proceeding to the next node. This is a heuristic that varies according to the workload profile of the application hitting the database.

    For a database with very little activity, it's typically safe to set this duration to as low as 0 seconds (for example, the cluster is new and has little data, no leaders getting re-elected, and no database smart clients needing updates to cluster membership).

    For a database with a lot of data and activity, you may want to increase the 180 second default, to wait for tablet load-balancing to stabilize before proceeding to the next node.

1. Click **Upgrade**.

YugabyteDB Anywhere starts the upgrade process, and you can view the progress on the **Tasks** tab.

#### Upgrade manually-provisioned on-premises universe

If you are upgrading a manually-provisioned [On-Premises](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/) universe from a database version prior to 2.18.0 to a version at 2.18.0 or later, you must additionally manually install YB Controller (YBC) after the otherwise-automated software upgrade procedure completes.

To install YBC, call the following API after the software upgrade:

```sh
curl --location --request PUT '<YBA-url>/api/v1/customers/<customerID>/universes/<UniverseID>/ybc/install' \
     --header 'X-AUTH-YW-API-TOKEN: <YBA-api-auth-token>'
```

To view your Customer ID and API Token, click the **Profile** icon in the top right corner of the YBA window.

You can view your Universe ID from your YBA universe URL, as follows:

```sh
https://<YB-Anywhere-IP-address>/universes/<universe-ID>
```

### Monitor the universe

Once all the nodes have been upgraded, monitor the universe to ensure it is healthy:

- Make sure workloads are running as expected and there are no errors in the logs.
- Check that all nodes are up and reachable.
- Check the [performance metrics](../../alerts-monitoring/anywhere-metrics/) for spikes or anomalies.

If you have problems, you can roll back any time.

For upgrades that require finalizing, you can monitor for as long as you need, but it is recommended to finalize the upgrade sooner in order to avoid operator errors that can arise from having to maintain two versions. A subset of features that require format changes will not be available until the upgrade is finalized. Also, you cannot perform another upgrade until you have finalized the current one.

If you are satisfied with the upgrade:

- For upgrades that do not require finalizing, the upgrade is effectively complete.

- For upgrades that require finalizing, proceed to [Finalize](#finalize-an-upgrade) the upgrade.

### Roll back an upgrade

To roll back an upgrade, do the following:

1. Navigate to **Universes** and select your universe.

1. Click **Actions > Roll Back Upgrade** to display the **Roll Back Upgrade** dialog.

1. Choose the **Rolling Upgrade** option and set the delay between nodes restarting.

1. Click **Proceed With Rollback**.

YugabyteDB Anywhere starts the rollback process, and you can view the progress on the **Tasks** tab.

### Finalize an upgrade

To finalize an upgrade, do the following:

1. Navigate to **Universes** and select your universe.

1. Click **Finalize Upgrade**.
