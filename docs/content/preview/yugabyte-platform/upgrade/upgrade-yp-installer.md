---
title: Upgrade YugabyteDB Anywhere using YBA Installer
headerTitle: Upgrade YugabyteDB Anywhere
linkTitle: Upgrade installation
description: Use YBA Installer to upgrade YugabyteDB Anywhere
menu:
  preview_yugabyte-platform:
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
    <a href="../upgrade-yp-replicated/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>Replicated</a>
  </li>

  <li>
    <a href="../upgrade-yp-kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>Kubernetes</a>
  </li>

</ul>

If your YugabyteDB Anywhere (YBA) installation was installed using [YBA Installer](../../install-yugabyte-platform/install-software/installer/), use YBA Installer to upgrade to a newer version. Note that you can only upgrade to a newer version; downgrades are not supported.

For more information, refer to [Compatibility with YugabyteDB](/preview/releases/yba-releases/#compatibility-with-yugabytedb).

If you are upgrading a YugabyteDB Anywhere installation with high availability enabled, follow the instructions provided in [Upgrade instances](../../administer-yugabyte-platform/high-availability/#upgrade-instances).

{{< note title="Upgrading YBA on deprecated operating systems" >}}

If you are running YBA on a [deprecated OS](../../../reference/configuration/operating-systems/), you need to update your OS before you can upgrade YBA to the next major release.

{{< /note >}}

## Upgrade using YBA Installer

To upgrade using YBA Installer, first download the version of YBA Installer corresponding to the version of YBA you want to upgrade to.

Download and extract the YBA Installer by entering the following commands:

```sh
$ wget https://downloads.yugabyte.com/releases/{{<yb-version version="preview" format="long">}}/yba_installer_full-{{<yb-version version="preview" format="build">}}-linux-x86_64.tar.gz
$ tar -xf yba_installer_full-{{<yb-version version="preview" format="build">}}-linux-x86_64.tar.gz
$ cd yba_installer_full-{{<yb-version version="preview" format="build">}}/
```

When ready to upgrade, run the `upgrade` command from the untarred directory of the target version of the YBA upgrade:

```sh
$ sudo ./yba-ctl upgrade
```

The upgrade takes a few minutes to complete. When finished, use the status command to verify that YBA has been upgraded to the target version:

```sh
$ sudo yba-ctl status
```

If you encounter errors or have any problems with an upgrade, contact {{% support-platform %}}.

For more information about using YBA Installer, refer to [Install YugabyteDB Anywhere](../../install-yugabyte-platform/install-software/installer/).
