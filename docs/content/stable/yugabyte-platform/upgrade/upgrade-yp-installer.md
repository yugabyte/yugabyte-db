---
title: Upgrade YugabyteDB Anywhere using YBA Installer
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade installation
description: Use YBA Installer to upgrade YugabyteDB Anywhere
menu:
  stable_yugabyte-platform:
    identifier: upgrade-yp-1-installer
    parent: upgrade
    weight: 80
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../upgrade-yp-installer/" class="nav-link active">
      <i class="fa-solid fa-building"></i>YBA Installer</a>
  </li>

  <li>
    <a href="../upgrade-yp-kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

If your YugabyteDB Anywhere installation was installed using [YBA Installer](../../install-yugabyte-platform/install-software/installer/), use YBA Installer to upgrade to a newer version. Note that you can only upgrade to a newer version; downgrades are not supported.

For more information, refer to [Compatibility with YugabyteDB](/stable/releases/yba-releases/#compatibility-with-yugabytedb).

## Before you begin

Review the requirements for your upgrade:

- If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

- If you are running YugabyteDB Anywhere on a [deprecated OS](../../../reference/configuration/operating-systems/), you need to update your OS before you can upgrade YugabyteDB Anywhere to the next major release.

- YugabyteDB Anywhere v25.1 and later requires Python v3.10-3.11. If you are running YugabyteDB Anywhere on a system with Python earlier than 3.10, you will need to update Python on your system before you can upgrade YugabyteDB Anywhere to v25.1 or later.

- YugabyteDB Anywhere v2025.2 and later require all universes have node agent running on their nodes.

- cron-based universes are no longer supported in YugabyteDB Anywhere v2025.2 and later.

For more information, refer to [Prepare to upgrade](../prepare-to-upgrade/).

## Upgrade using YBA Installer

To upgrade using YBA Installer, first download the version of YBA Installer corresponding to the version of YugabyteDB Anywhere you want to upgrade to.

Download and extract the YBA Installer by entering the following commands:

```sh
wget https://downloads.yugabyte.com/releases/{{<yb-version version="stable" format="long">}}/yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
tar -xf yba_installer_full-{{<yb-version version="stable" format="build">}}-linux-x86_64.tar.gz
cd yba_installer_full-{{<yb-version version="stable" format="build">}}/
```

When ready to upgrade, run the `upgrade` command from the untarred directory of the target version of the YugabyteDB Anywhere upgrade:

```sh
sudo ./yba-ctl upgrade
```

YBA Installer runs a pre-check to ensure your existing installation fulfills the necessary prerequisites for the upgrade.

The upgrade takes a few minutes to complete.

When finished, use the status command to verify that YugabyteDB Anywhere has been upgraded to the target version:

```sh
sudo yba-ctl status
```

If you encounter errors or have any problems with an upgrade, contact {{% support-platform %}}.

For more information about using YBA Installer, refer to [Install YugabyteDB Anywhere](../../install-yugabyte-platform/install-software/installer/).
