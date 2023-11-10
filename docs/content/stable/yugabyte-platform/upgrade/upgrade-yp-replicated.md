---
title: Upgrade YugabyteDB Anywhere using Replicated
headerTitle: Upgrade YugabyteDB Anywhere using Replicated
linkTitle: Upgrade using Replicated
description: Use Replicated to upgrade YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: upgrade-yp-replicated
    parent: upgrade
    weight: 80
type: docs
---

You can use [Replicated](https://www.replicated.com/) to upgrade your YugabyteDB Anywhere to a newer version.

To start the upgrade, log in to the Replicated Admin Console via <https://:8800> and then perform the following:

- Navigate to **Dashboard** and click **View release history** to open **Release History**, as shown in the following illustration:

  ![image](/images/yb-platform/upgrade-replicated1.png)

- Find the required release version and click the corresponding **Install**.

If the required version is not in the list, click **Check for updates** to refresh the list of releases available in the channel to which you are subscribed.

If the required release version is in a different channel (for example, you want to upgrade from 2.4.*n* release family to 2.6.*n*), start by updating the channel, as follows:

- Click the gear icon and select **View License**, as per the following illustration:

  ![image](/images/yb-platform/upgrade-replicated2.png)

- In the **License** view, click **change** for **Release Channel**, as per the following illustration:

  ![image](/images/yb-platform/upgrade-replicated3.png)

  Note that if you do not have permissions to access the new release channel, you should contact {{% support-platform %}}.

- Click **Sync License**.

- Navigate back to **Release History**, locate the release you need, and then click the corresponding **Install**.

If you are performing an upgrade to YugabyteDB Anywhere version 2.14 or later, the process can take some time depending on the amount of data present in YugabyteDB Anywhere.

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/multi-region-deployments/asynchronous-replication-ysql/) for your universe was set up via `yb-admin` instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).
