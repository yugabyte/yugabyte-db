---
title: Upgrade YugabyteDB Anywhere
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade
description: Upgrade YugabyteDB Anywhere.
menu:
  v2.20_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: upgrade
    weight: 700
type: indexpage
---

Keep YugabyteDB Anywhere (YBA) up to date for the latest fixes and improvements, and to be able to [upgrade your universes](../manage-deployments/upgrade-software/) to the latest version of YugabyteDB. You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere.

You can upgrade YBA using the following methods:

| Method | Using | Use If |
| :--- | :--- | :--- |
| YBA&nbsp;Installer | yba-ctl CLI | Your installation already uses YBA Installer. |
| Replicated | Replicated Admin Console | Your installation already uses Replicated.<br>Before you can migrate from a Replicated installation, upgrade to v2.20.1.3 or later using Replicated. |
| Kubernetes | Helm chart | You're deploying in Kubernetes. |

{{<index/block>}}

  {{<index/item
    title="Prepare for your upgrade"
    body="Review changes that may affect your upgrade."
    href="prepare-to-upgrade/"
    icon="fa-thin fa-diamond-exclamation">}}

  {{<index/item
    title="Upgrade YugabyteDB Anywhere"
    body="Upgrade your YugabyteDB Anywhere installation."
    href="upgrade-yp-installer/"
    icon="fa-thin fa-up-from-bracket">}}

{{</index/block>}}
