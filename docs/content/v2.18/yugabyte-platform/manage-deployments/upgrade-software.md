---
title: Upgrade the YugabyteDB software
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade YugabyteDB
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software.
menu:
  v2.18_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 80
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to get new features and fixes included in the release.

Before starting the upgrade:

- If you're upgrading a universe from v2.15.x or earlier to v2.16.0 or later, ensure that port 18018 is open on all YugabyteDB nodes so that YB Controller (introduced in v2.16.0) can operate.

- Consider importing a specific YugabyteDB release into YugabyteDB Anywhere, as follows:

  - Click the user profile icon and select **Releases**.

  - Click **Import** to open the **Import Releases** dialog shown in the following illustration:

    ![Import Releases](/images/yp/import-releases.png)

  - Specify the release version to import.

  - Select the storage or URL that contains the release. If the release is located on Amazon S3, you would need to provide the access information; if the release is located on Google Cloud Storage, you would need to copy and paste the contents of the JSON file with the access credentials; if the release is accessible via a HTTP, you would need to specify the checksum value of the download.

  - Provide the path to the storage location.

  - Click **OK**.

When imported, the release is added to the **Releases** list shown in the following illustration:

![Releases](/images/yp/releases-list.png)<br>

To delete or disable a release, click its corresponding **Actions**.

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

{{< note title="Upgrading manually-provisioned on-premises universes" >}}

If you are upgrading a manually-provisioned [On-Premises](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/) universe, you must additionally manually install YB Controller after the otherwise-automated software upgrade procedure completes. YB Controller was introduced in YugabyteDB Anywhere 2.16.0, and is required for YugabyteDB Anywhere 2.16.0 and later.

To install YB Controller, call the following API after the software upgrade:

```sh
curl --location --request PUT '<YBA-url>/api/v1/customers/<customerID>/universes/<UniverseID>/ybc/install' \
     --header 'X-AUTH-YW-API-TOKEN: <YBA-api-auth-token>'
```

To view your Customer ID and API Token, click the **Profile** icon in the top right corner of the YugabyteDB Anywhere window.

You can view your Universe ID from your YugabyteDB Anywhere universe URL (`<YB-Anywhere-IP-address>/universes/<universeID>`).

{{< /note >}}
