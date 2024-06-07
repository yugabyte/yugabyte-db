---
title: Upgrade YugabyteDB Anywhere
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade
description: Upgrade YugabyteDB Anywhere.
image: /images/section_icons/manage/enterprise.png
menu:
  v2.18_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: upgrade
    weight: 700
type: indexpage
---

Keep YugabyteDB Anywhere (YBA) up to date for the latest fixes and improvements, and to be able to [upgrade your universes](../manage-deployments/upgrade-software/) to the latest version of YugabyteDB. You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere.

For information on which versions of YugabyteDB are compatible with your version of YBA, see [YugabyteDB Anywhere releases](/preview/releases/yba-releases/).

You can upgrade YBA using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| [YBA&nbsp;Installer](./upgrade-yp-installer/) | yba-ctl CLI | Your installation already uses YBA Installer. |
| [Replicated](./upgrade-yp-replicated/) | Replicated Admin Console | Your installation already uses Replicated.<br>Before you can migrate from a Replicated installation, upgrade to v2.20.1.3 or later using Replicated. |
| [Kubernetes](./upgrade-yp-kubernetes/) | Helm chart | You're deploying in Kubernetes. |

If you are upgrading a YBA installation with high availability enabled, follow the instructions provided in [Upgrade instances](../administer-yugabyte-platform/high-availability/#upgrade-instances).

If you have upgraded YBA to version 2.12 or later and [xCluster replication](../../explore/multi-region-deployments/asynchronous-replication-ysql/) for your universe was set up via yb-admin instead of the UI, follow the instructions provided in [Synchronize replication after upgrade](upgrade-yp-xcluster-ybadmin/).

{{<index/block>}}

  {{<index/item
    title="Upgrade YugabyteDB Anywhere"
    body="Upgrade your YugabyteDB Anywhere installation."
    href="upgrade-yp-installer/"
    icon="/images/section_icons/quick_start/install.png">}}

  {{<index/item
    title="Synchronize replication after upgrade"
    body="Synchronize xCluster replication after an upgrade for universes set up using yb-admin."
    href="upgrade-yp-xcluster-ybadmin/"
    icon="/images/section_icons/manage/pitr.png">}}

{{</index/block>}}
