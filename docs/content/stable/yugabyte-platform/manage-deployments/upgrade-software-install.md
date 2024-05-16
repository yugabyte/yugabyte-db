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

You can perform a rolling upgrade on a live universe deployment as follows:

1. Navigate to **Universes** and select your universe.

1. Click **Actions > Upgrade Software**.

1. In the **Upgrade Software** dialog, ensure that **Rolling Upgrade** is enabled, define the delay between servers or accept the default value, and then use the **Server Version** field to select the new YugabyteDB version, as per the following illustration:

    ![Upgrade Universe Confirmation](/images/ee/upgrade-univ-2.png)

    To trigger an upgrade that involves downtime, deselect **Rolling Upgrade**.

For information on how rolling upgrades are performed in YugabyteDB, see [Upgrade a deployment](../../../manage/upgrade-deployment/).

{{< note title="Downgrading" >}}

Currently, you cannot downgrade a universe to an older YugabyteDB release. For assistance with downgrades, contact Yugabyte Support.

{{< /note >}}

### Upgrade manually-provisioned on-premises universe

If you are upgrading a manually-provisioned [On-Premises](../../configure-yugabyte-platform/set-up-cloud-provider/on-premises/) universe from versions earlier than v2.16.0, you must additionally manually install YB Controller (YBC) after the otherwise-automated software upgrade procedure completes.

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
