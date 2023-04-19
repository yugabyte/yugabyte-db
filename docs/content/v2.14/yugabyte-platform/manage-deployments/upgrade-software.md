---
title: Upgrade the YugabyteDB software
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade YugabyteDB
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software.
menu:
  v2.14_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 80
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to get the new features and fixes included in the release.

Before you start the upgrade, you might want to import a specific YugabyteDB release into YugabyteDB Anywhere, as follows:

- Click the user profile icon and select **Releases**.

- Click **Import** to open the **Import Releases** dialog shown in the following illustration:<br><br>

  ![Import Releases](/images/yp/import-releases.png)<br><br>

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

1. In the **Upgrade Software** dialog, ensure that **Rolling Upgrade** is enabled, define the delay between servers or accept the default value, and then use the **Server Version** field to select the new YugabyteDB version, as per the following illustration:<br><br><br>

    ![Upgrade Universe Confirmation](/images/ee/upgrade-univ-2.png)<br><br>

    <br>To trigger an upgrade that involves downtime, deselect **Rolling Upgrade**.
