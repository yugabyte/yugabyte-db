---
title: Upgrade YugabyteDB Anywhere using Replicated
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade installation
description: Use Replicated to upgrade YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
    identifier: upgrade-yp-2-replicated
    parent: upgrade
    weight: 80
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../upgrade-yp-installer/" class="nav-link">
      <i class="fa-solid fa-building"></i>YBA Installer</a>
  </li>

  <li>
    <a href="../upgrade-yp-replicated/" class="nav-link active">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../upgrade-yp-kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

{{< note title="Replicated end of life" >}}

YugabyteDB Anywhere will end support for Replicated installation at the end of 2024. You can migrate existing Replicated YugabyteDB Anywhere installations using YBA Installer.<br>Note that you must migrate from Replicated to YBA Installer if you are upgrading YBA to v2024.1 or later. See [Migrate from Replicated](../../install-yugabyte-platform/migrate-replicated/).

To perform the migration, you must first upgrade your installation to v2.20.1.3 or later using Replicated.

{{< /note >}}

If your installation was installed via [Replicated](https://www.replicated.com/), use Replicated to upgrade your YugabyteDB Anywhere to a newer version.

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

## Upgrade using Replicated

To start the upgrade, sign in to the Replicated Admin Console via <https://:8800> and then perform the following:

1. Navigate to **Dashboard** and click **View release history** to open **Release History**, as shown in the following illustration:

    ![Release History](/images/yb-platform/upgrade-replicated1.png)

1. Find the required release version; if the required version is not in the list, click **Check for updates** to refresh the list of releases available in the channel to which you are subscribed.

1. Click the corresponding **Install**.

If the required release version is in a different channel (for example, you want to upgrade from 2.4.*n* release family to 2.6.*n*), start by updating the channel, as follows:

1. Click the gear icon and select **View License**, as per the following illustration:

    ![View License](/images/yb-platform/upgrade-replicated2.png)

1. In the **License** view, click **change** for **Release Channel**, as per the following illustration:

    ![License view](/images/yb-platform/upgrade-replicated3.png)

    Note that if you do not have permissions to access the new release channel, you should contact {{% support-platform %}}.

1. Click **Sync License**.

1. Navigate back to **Release History**, locate the release you need, and then click the corresponding **Install**.

If you are performing an upgrade to YugabyteDB Anywhere version 2.14 or later, the process can take some time depending on the amount of data present in YugabyteDB Anywhere.

If you have upgraded YugabyteDB Anywhere to version 2.12 or later and [xCluster replication](../../../explore/going-beyond-sql/asynchronous-replication-ysql/) for your universe was set up via `yb-admin` instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](../upgrade-yp-xcluster-ybadmin/).

## Upgrade airgapped installation

You can upgrade your airgapped installation of YugabyteDB Anywhere to a newer version as follows:

1. Manually obtain and move the binary Replicated license file `<filename>.rli` to the `/home/{username}/`  directory.

1. Manually obtain and move the YugabyteDB Anywhere airgapped package to the `/opt/yugabyte/releases/<new_version_dir>` directory.

   For example, if you are upgrading to the latest YugabyteDB Anywhere stable version, you would start by executing the following command to obtain the package:

   ```sh
   wget https://downloads.yugabyte.com/releases/{{<yb-version version="preview">}}/yugaware-{{<yb-version version="preview" format="build">}}-linux-x86_64.airgap
   ```

   Then you would create the `/opt/yugabyte/releases/yugaware-{{<yb-version version="preview" format="build">}}/` directory and move (or SCP) the `yugaware-{{<yb-version version="preview" format="build">}}-linux-x86_64.airgap` file into that directory.

1. Sign in to the Replicated Admin Console at <https://:8800/> and navigate to **Settings** to load the new license file, as per the following illustration:

   ![Airgap Settings](/images/yp/airgap-settings.png)

   Change the two directories to match the ones you used. For example, enter `/opt/yugabyte/releases/yugaware-{{<yb-version version="preview" format="build">}}/` in the **Update Path** field and `/home/{user}/` in the **License File** field.

   Replicated detects updates based on the updated path information and applies them in the same way it does for connected YugabyteDB Anywhere installations.

1. Proceed with the YugabyteDB Anywhere upgrade process by following instructions provided in [Upgrade using Replicated](#upgrade-using-replicated).

1. Upgrade your YugabyteDB universe by following instructions provided in [Upgrade the YugabyteDB software](../../manage-deployments/upgrade-software/).
