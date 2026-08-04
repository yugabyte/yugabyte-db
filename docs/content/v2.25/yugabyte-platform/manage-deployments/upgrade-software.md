---
title: Upgrade universes with a new version of YugabyteDB
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade database
description: Upgrade universes managed by YugabyteDB Anywhere.
headcontent: Perform rolling upgrades on live universe deployments
menu:
  v2.25_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 20
type: docs
---

{{< page-finder/head text="Upgrade YugabyteDB" subtle="across different products">}}
  {{< page-finder/list icon="/icons/database-hover.svg" text="YugabyteDB" url="../../../manage/upgrade-deployment/" >}}
  {{< page-finder/list icon="/icons/server-hover.svg" text="YugabyteDB Anywhere" current="" >}}
  {{< page-finder/list icon="/icons/cloud-hover.svg" text="YugabyteDB Aeon" url="/preview/yugabyte-cloud/cloud-clusters/database-upgrade/" >}}
{{< /page-finder/head >}}

You can upgrade the YugabyteDB release that is powering a universe to get new features and fixes included in the release.

{{< note title="Upgrading YugabyteDB on deprecated operating systems" >}}

If your universe is running on a [deprecated OS](../../../reference/configuration/operating-systems/), you will need to update your OS before you can upgrade to the next major YugabyteDB release. Refer to [Patch and upgrade the Linux operating system](../upgrade-nodes/).

{{< /note >}}

{{< note title="Upgrading universes in xCluster deployments" >}}
When upgrading universes in [xCluster Replication](../../manage-deployments/xcluster-replication/xcluster-replication-setup/#upgrading-the-database-version) or [xCluster Disaster Recovery](../../back-up-restore-universes/disaster-recovery/#upgrading-universes-in-dr):

- Use the same version of YugabyteDB on both the source/DR primary and target/DR replica.
- Upgrade and finalize the target/DR replica before upgrading and finalizing the source/DR primary.
{{< /note >}}

{{< warning title="Upgrading YugabyteDB to v2.25" >}}
Upgrading universes to YugabyteDB v2.25 from previous preview versions is not yet available.
{{< /warning >}}

When performing a database upgrade, do the following:

1. [Upgrade YugabyteDB Anywhere](../../upgrade/). You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere.

    For information on which versions of YugabyteDB are compatible with your version of YugabyteDB Anywhere, refer to [Compatibility with YugabyteDB](/preview/releases/yba-releases/#compatibility-with-yugabytedb).

1. [Prepare to upgrade a universe](../upgrade-software-prepare/). Depending on the upgrade you are planning, you may need to make changes to your automation or upgrade your Linux operating system.

1. [View and import releases](../ybdb-releases/). Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available in YugabyteDB Anywhere and, if necessary, import the release.

1. [Upgrade the universe](../upgrade-software-install/). Perform a rolling upgrade on a live universe deployment.

{{<index/block>}}

  {{<index/item
    title="Prepare to upgrade"
    body="Review changes that may affect your automation."
    href="../upgrade-software-prepare/"
    icon="fa-thin fa-diamond-exclamation">}}

  {{<index/item
    title="Manage releases"
    body="View and import the latest releases of YugabyteDB."
    href="../ybdb-releases/"
    icon="fa-thin fa-download">}}

  {{<index/item
    title="Upgrade a universe"
    body="Perform a rolling upgrade on a live universe deployment."
    href="../upgrade-software-install/"
    icon="fa-thin fa-up-from-bracket">}}

{{</index/block>}}
