---
title: Upgrade YugabyteDB Anywhere using Replicated
headerTitle: Upgrade YugabyteDB Anywhere using Replicated
linkTitle: Upgrade using Replicated
description: Use Replicated to upgrade YugabyteDB Anywhere
menu:
  v2.14_yugabyte-platform:
    identifier: upgrade-yp-replicated
    parent: upgrade
    weight: 80
type: docs
---

You can use [Replicated](https://www.replicated.com/) to upgrade your YugabyteDB Anywhere to a newer version.

To start the upgrade, you need to log in to the Replicated Admin Console via https://:8800 and then perform the following:

- Navigate to **Dashboard** and click **View release history** to open **Release History**, as shown in the following illustration:<br><br>
  ![image](/images/yb-platform/upgrade-replicated1.png)



<br>

- Find the required release version and click the corresponding **Install**.<br>

  If the required version is not in the list, click **Check for updates** to refresh of a list of releases available in the channel to which you are subscribed, and then click **Install** corresponding to the release.<br>

  There is a possiblity that the required release version is in a different channel (for example, you want to upgrade from 2.4.*n* release family to 2.6.*n*). In this case, you start by updating the channel, as follows:
  - Click the gear icon and select **View License**, as per the following illustration:<br><br>

    ![image](/images/yb-platform/upgrade-replicated2.png)

  - In the **License** view, click **change** for **Release Channel**, as per the following illustration:<br><br>

    ![image](/images/yb-platform/upgrade-replicated3.png)

    <br><br>Note that if you do not have permissions to access the new release channel, you should contact Yugabyte Support.

  - Click **Sync License**.

  - Navigate back to **Release History**, locate the release you need, and then click the corresponding **Install**.
