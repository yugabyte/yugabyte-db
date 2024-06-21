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

Obtain a YugabyteDB Anywhere license before attempting any migration. Contact {{% support-platform %}} to request a license.

## Description

YugabyteDB Anywhere will end support for Replicated installation at the end of 2024.
This means updates will no longer be available, preventing YugabyteDB Anywhere and YugabyteDB database upgrades, in perpetuity.

## Mitigation

Migrate your YugabyteDB Anywhere installation from Replicated by performing the following steps:

1. Upgrade YugabyteDB Anywhere instances to the latest v2.18 or v2.20 using Replicated.
1. Migrate from the Replicated-based install to a YBA Installer-based install using the [Migration Steps](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated).

## Details

To guarantee interoperability, upgrading to a YugabyteDB Anywhere version greater than or equal to latest v2.18 may require additional actions, such as first upgrading the database cluster(s) version (also known as universes), or performing an upgrade to Docker.

For more details, refer to [YugabyteDB Anywhere version compatibility with YugabyteDB](../../../releases/yba-releases/#compatibility-with-yugabytedb).

{{< warning title="Important" >}}
Do not upgrade YugabyteDB Anywhere to a version that no longer supports your currently running YugabyteDB database versions.
{{< /warning >}}

If there are no version compatibility issues, YugabyteDB Anywhere upgrades should be performed before any additional upgrades to the database.

When performing step 1 of the mitigation, review the [prerequisites](../../../yugabyte-platform/prepare/server-yba/) for Replicated-based installs to avoid any potential upgrade issues.

### Recommended upgrade path (Ideal)

Yugabyte recommends upgrading YugabyteDB Anywhere and YugabyteDB database to the latest v2.20 LTS.

### Acceptable upgrade path (Satisfactory)

**Important YugabyteDB release considerations**

- v2.14 database releases reach End of Maintenance in July 2024, End of Life in January 2025.
- v2.16 database releases reach End of Life in June 2024.
- v2.18 database releases reach End of Life in November 2024.

For more details, refer to [Releases](../../ybdb-releases/#releases).

**Scenario 1**

Suppose you are running YugabyteDB Anywhere earlier than the latest v2.18, with universes running YuagbyteDB database earlier than the latest v2.14.

If you want to upgrade YugabyteDB Anywhere to the latest v2.18 or v2.20, and only upgrade database nodes to v2.14, do the following:

1. Check Replicated [prerequisites](../../../yugabyte-platform/prepare/server-yba/).
1. Upgrade YBA to the latest version of your current branch (using Replicated).
1. Upgrade YBA to the latest v2.18 (using Replicated).
1. Upgrade YugabyteDB database nodes to latest v2.14.
1. [Migrate YugabyteDB Anywhere off Replicated](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated), to YBA Installer.
1. Plan the upgrade of database nodes prior to EOL in January 2025.

**Scenario 2**

Suppose you are running YugabyteDB Anywhere earlier than the latest v2.18, with universes running YugabyteDB at v2.14, v2.16, or v2.18.

If you want to upgrade YugabyteDB Anywhere to the latest v2.18 or 2.20, and leave your database on its current version, do the following:

1. Check Replicated [prerequisites](../../../yugabyte-platform/prepare/server-yba/).
1. Upgrade YBA to the latest version of your current branch (using Replicated).
1. Upgrade YBA to the latest v2.18 or v2.20 (using Replicated).
1. [Migrate YugabyteDB Anywhere off Replicated](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated), to YBA Installer.
1. Plan the upgrade of database nodes prior to EOL for the respective versions. For more details, refer to [Releases](../../ybdb-releases/#releases).

### Minimum requirements (Sub-optimal)

**Important YugabyteDB release considerations**

- v2.14 database releases reach End of Maintenance in July 2024, End of Life in January 2025.
- v2.16 database releases reach End of Life in June 2024.
- v2.18 database releases reach End of Life in November 2024.
For more details, refer to [Releases](../../ybdb-releases/#releases).

**Scenario**

Suppose you are running YugabyteDB Anywhere v2.14, with universes running YugabyteDB nodes on v2.12.

To upgrade YugabyteDB Anywhere to the latest v2.18 or v2.20, and only upgrade your database to the latest v2.14, do the following:

1. Check Replicated [prerequisites](../../../yugabyte-platform/prepare/server-yba/).
1. Upgrade YugabyteDB Anywhere to the latest v2.14 (using Replicated).
1. Upgrade YugabyteDB database to the latest v2.14.
1. Upgrade YugabyteDB Anywhere to the latest v2.18 or v2.20 (using Replicated).
1. [Migrate YugabyteDB Anywhere off Replicated](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated), to YBA Installer.
