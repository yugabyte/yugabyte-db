---
title: Upgrade universes with a new version of YugabyteDB
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade YugabyteDB
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software on universes.
menu:
  stable_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 80
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to get new features and fixes included in the release.

## Before you begin

Before starting the upgrade, review the following major changes to YugabyteDB. Depending on the upgrade you are planning, you may need to make changes to your automation.

### Upgrading to v2.16.0

The YB Controller (YBC) service was introduced for all universes (except Kubernetes).

YBC is used to manage backup and restore, providing faster full backups, and introduces support for incremental backups.

**Impact**

- On-premises provider - if you use on-premises providers with manually-provisioned nodes, you will need to update your current procedures for manually provisioning instances, to accommodate YBC. This includes setting systemd-specific database service unit files (if used), and configuring TCP ports. Refer to [Manually provision on-premises nodes](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises-manual/).

- Upgrade OS - for universes created using an on-premises provider with manually-provisioned nodes, if your OS patching procedures involve re-installing YugabyteDB software on a node, you will need to update those procedures to accommodate YBC.

- Firewall ports - update your firewall rules to allow incoming TCP traffic on port 18018, which is used by YBC, for all nodes in a universe.

### Upgrading to v2.18.0

YBC was introduced for Kubernetes clusters. Refer to [Upgrading to 2.16.0](/#upgrading-to-v2-16-0).

### Upgrading to v2.18.2

The Node Agent was introduced for all universes. Node agent is an RPC service running on a YugabyteDB node, and is used to manage communication between YugabyteDB Anywhere and the nodes in universes. Except for Day 0 tasks during initial installation, YBA no longer uses SSH and SCP to manage nodes; instead, YBA connects to the Node agent process listening on port 9000, and performs all its management via this secure connection. For more information, refer to the [Node agent FAQ](../../../faq/yugabyte-platform/#node-agent).

**Impact**

- On-premises provider - if you use on-premises providers with manually-provisioned nodes, you will need to update your current procedures for manually provisioning instances, to include installing node agent. Refer to [Manually provision on-premises nodes](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises-manual/#install-node-agent).

- Upgrade OS - for universes created using an on-premises provider with manually-provisioned nodes, if your OS patching procedures involve re-installing YugabyteDB software on a node, you will need to update those procedures to accommodate node agent.

- Firewall ports - update your firewall rules to allow incoming TCP traffic on port 9000 for all nodes in a universe.

## View and import YugabyteDB releases into YugabyteDB Anywhere

Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available and, if necessary, import the release.

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

You can perform a rolling upgrade on a live universe deployment as follows:

1. Navigate to **Universes** and select your universe.

1. Click **Actions > Upgrade Software**.

1. In the **Upgrade Software** dialog, ensure that **Rolling Upgrade** is enabled, define the delay between servers or accept the default value, and then use the **Server Version** field to select the new YugabyteDB version, as per the following illustration:

    ![Upgrade Universe Confirmation](/images/ee/upgrade-univ-2.png)

    To trigger an upgrade that involves downtime, deselect **Rolling Upgrade**.

For information on how rolling upgrades are performed in YugabyteDB, see [Upgrade a deployment](../../../manage/upgrade-deployment/).

{{< note title="Note" >}}

Currently, you cannot downgrade a universe to an older YugabyteDB release. For assistance with downgrades, contact Yugabyte Support.

{{< /note >}}

{{< note title="Manually-provisioned on-premises universe upgrades" >}}

If you are upgrading a manually-provisioned [On-Premises](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/) universe, you must additionally manually install YB Controller after the otherwise-automated software upgrade procedure completes. YB Controller was introduced in YugabyteDB Anywhere 2.16.0, and is required for YugabyteDB Anywhere 2.16.0 and later.

To install YB Controller, call the following API after the software upgrade:

```sh
curl --location --request PUT '<YBA-url>/api/v1/customers/<customerID>/universes/<UniverseID>/ybc/install' \
     --header 'X-AUTH-YW-API-TOKEN: <YBA-api-auth-token>'
```

To view your Customer ID and API Token, click the **Profile** icon in the top right corner of the YugabyteDB Anywhere window.

You can view your Universe ID from your YugabyteDB Anywhere universe URL (`<YB-Anywhere-IP-address>/universes/<universeID>`).

{{< /note >}}
