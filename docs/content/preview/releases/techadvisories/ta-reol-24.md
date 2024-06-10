---
title: TA-REOL-24
headerTitle: Replicated End of Life
headcontent: 30 Apr 2024
type: docs
showRightNav: true
cascade:
  unversioned: true
menu:
  preview_releases:
    identifier: ta-reol-24
    weight: 1
rightNav:
  hideH2: true
type: docs
---

|          Product           |  Affected Versions   | Fixed In |
| :------------------------- | :------------------ | :------- |
| {{<product "yba">}}  | {{<release "all">}} | {{<release "2.18, 2.20+">}}      |

## Critical Considerations

Obtain a YugabyteDB Anywhere (YBA) Installer license before attempting any migration. Contact {{% support-platform %}} to request a license.

## Description

YugabyteDB Anywhere will end support for Replicated installation at the end of 2024.
This means updates will no longer be available, preventing YBA and YugabyteDB database upgrades, in perpetuity.

## Mitigation

Migrate your YBA installation from Replicated by performing the following steps:

1. Upgrade YBA instances to the latest v2.18 or v2.20 using Replicated.
1. Migrate from the Replicated-based install of YBA to a YBA Installer-based install of YBA using the [Migration Steps](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated).

## Details

To guarantee interoperability, upgrading to a YBA version greater than or equal to latest v2.18 may require additional actions, such as first upgrading the database cluster(s) version (also known as universes), or performing an upgrade to Docker.

For more details, refer to [YBA version compatibility with YugabyteDB](../../../releases/yba-releases/#compatibility-with-yugabytedb).

{{< warning title="Important" >}}
Do not upgrade your YBA to a version that no longer supports your currently running YugabyteDB database versions.
{{< /warning >}}

If there are no version compatibility issues, YBA upgrades should be performed before any additional upgrades to the database.

When performing step 1 of the mitigation, review the [prerequisites](../../../yugabyte-platform/install-yugabyte-platform/prerequisites/default) for Replicated-based installs to avoid any potential upgrade issues.

### Recommended upgrade path (Ideal)

Yugabyte recommends upgrading YBA and YugabyteDB database to the latest v2.20 LTS.

### Acceptable upgrade path (Satisfactory)

**Important YugabyteDB release considerations**

- v2.14 database releases reach End of Maintenance in July 2024, End of Life in January 2025.
- v2.16 database releases reach End of Life in June 2024.
- v2.18 database releases reach End of Life in November 2024.

For more details, refer to [Releases](../../ybdb-releases/#releases).

**Scenario 1**

Suppose you are running a YBA earlier than the latest v2.18, with universes running YuagbyteDB database earlier than the latest v2.14.

If you want to upgrade YBA to the latest v2.18 or v2.20, and only upgrade database nodes to v2.14, do the following:

1. Check Replicated [prerequisites](../../../yugabyte-platform/install-yugabyte-platform/prerequisites/default).
1. Upgrade YBA to the latest version of your current branch (using Replicated).
1. Upgrade YBA to the latest v2.18 (using Replicated).
1. Upgrade YugabyteDB database nodes to latest v2.14.
1. [Migrate YBA off Replicated](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated), to YBA Installer.
1. Plan the upgrade of database nodes prior to EOL in January 2025.

**Scenario 2**

Suppose you are running a YBA earlier than the latest v2.18, with universes running YugabyteDB at v2.14, v2.16, or v2.18.

If you want to upgrade YBA to the latest v2.18 or 2.20, and leave your database on its current version, do the following:

1. Check Replicated [prerequisites](../../../yugabyte-platform/install-yugabyte-platform/prerequisites/default).
1. Upgrade YBA to the latest version of your current branch (using Replicated).
1. Upgrade YBA to the latest v2.18 or v2.20 (using Replicated).
1. [Migrate YBA off Replicated](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated), to YBA Installer.
1. Plan the upgrade of database nodes prior to EOL for the respective versions. For more details, refer to [Releases](../../ybdb-releases/#releases).

### Minimum requirements (Sub-optimal)

**Important YugabyteDB release considerations**

- v2.14 database releases reach End of Maintenance in July 2024, End of Life in January 2025.
- v2.16 database releases reach End of Life in June 2024.
- v2.18 database releases reach End of Life in November 2024.
For more details, refer to [Releases](../../ybdb-releases/#releases).

**Scenario**

Suppose you are running YBA v2.14, with universes running YugabyteDB nodes on v2.12.

To upgrade YBA to the latest v2.18 or v2.20, and only upgrade your database to the latest v2.14, do the following:

1. Check Replicated [prerequisites](../../../yugabyte-platform/install-yugabyte-platform/prerequisites/default).
1. Upgrade YBA to the latest v2.14 (using Replicated).
1. Upgrade YugabyteDB database to the latest v2.14.
1. Upgrade YBA to the latest v2.18 or v2.20 (using Replicated).
1. [Migrate YBA off Replicated](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated), to YBA Installer.
