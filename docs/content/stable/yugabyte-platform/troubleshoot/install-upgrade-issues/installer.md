---
title: Install and upgrade issues on virtual machines
headerTitle: Install and upgrade issues
linkTitle: Install and upgrade issues
description: Troubleshoot issues encountered when installing or upgrading YugabyteDB Anywhere on virtual machines.
menu:
  stable_yugabyte-platform:
    identifier: install-upgrade-installer-issues
    parent: troubleshoot-yp
    weight: 10
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../installer/" class="nav-link active">
      <i class="fa-solid fa-building"></i>
      YBA Installer</a>
  </li>

  <li>
    <a href="../vm/" class="nav-link">
      <i class="fa-solid fa-cloud"></i>
      Virtual machine</a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

Occasionally, you might encounter issues during installation and upgrade of YugabyteDB Anywhere using [YBA Installer](../../../install-yugabyte-platform/install-software/installer/).

If you experience difficulties while troubleshooting, contact [Yugabyte Support](https://support.yugabyte.com).

## Permissions issue with find command

YBA Installer preflight check fails with the following error:

```output
ERROR[2023-12-08T08:25:58Z] Command 'find as user yugabyte' failed with exit code '1' and stderr 'find: Failed to change directory: Permission denied
find: Failed to change directory: Permission denied
find: Failed to change directory: Permission denied
find: Failed to change directory: Permission denied
find: failed to restore initial working directory: Permission denied
```

This happens when the user doesn't have sufficient privileges on the `yba_installer_full` directory where the YBA Installer installation package was untarred.

Affected releases: All

Workaround: Change permissions on `yba_installer_full` directory to 755.

## Preflight check for Prometheus fails

Upgrades fail the preflight check with the following error when run as `yba-ctl preflight --upgrade`:

```output
time=2024-01-03T19:19:04Z level=error msg=preflight prometheus failed: couldn't parse either scrapeInterval: 15 or scrapeTimeout: 10 to duration. check https://pkg.go.dev/time#ParseDuration for appropriate syntax
```

Affected releases: Upgrading from YBA versions earlier than v2.18.5 to v2.18.5 and later using YBA Installer.

Workaround: Ignore this preflight check. YBA Installer automatically converts the invalid settings to valid values and performs the upgrade.

If you want to have green preflight checks prior to the upgrade, change the `scrapeInterval` and `scrapeTimeout` settings in `/opt/yba-ctl/yba-ctl.yml` to `10s` and `15s` respectively.

## New settings are not automatically updated in yba-ctl.yml

When new default settings are added to `yba-ctl.yml` in a newer release, they aren't automatically added to `yba-ctl.yml` of an existing installation after an upgrade.

Affected releases: All

Workaround: If you want to configure the new settings to non-default values, manually add the keys and values to `/opt/yba-ctl/yba-ctl.yml`. Otherwise, no action is needed.

## High availability on Replicated installation with HTTP no longer works after migration

If you have a Replicated installation that uses HTTP and you [migrate to YBA Installer](../../../install-yugabyte-platform/install-software/installer/#migrate-from-replicated), your [high availability](../../../administer-yugabyte-platform/high-availability/) (HA) setup will no longer work after the migration. This is because YBA Installer always uses HTTPS. YBA Installer will attempt to block the migration if it detects a Replicated installation with HA enabled that uses HTTP.

Affected releases: All

Workaround: Refer to [Migration and high availability](../../../install-yugabyte-platform/install-software/installer/#migration-and-high-availability).
