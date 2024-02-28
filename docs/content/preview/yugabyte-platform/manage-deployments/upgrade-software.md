---
title: Upgrade universes with a new version of YugabyteDB
headerTitle: Upgrade the YugabyteDB software
linkTitle: Upgrade database
description: Use YugabyteDB Anywhere to upgrade the YugabyteDB software on universes.
headcontent: Perform rolling upgrades on live universe deployments
menu:
  preview_yugabyte-platform:
    identifier: upgrade-software
    parent: manage-deployments
    weight: 20
type: docs
---

The YugabyteDB release that is powering a universe can be upgraded to get new features and fixes included in the release.

When performing a database upgrade, do the following:

- [Upgrade YugabyteDB Anywhere](../../upgrade/). You cannot upgrade a universe to a version of YugabyteDB that is later than the version of YugabyteDB Anywhere. To upgrade to a more recent version of YugabyteDB, you may first have to upgrade YugabyteDB Anywhere.

- [Review major changes in previous YugabyteDB releases](../upgrade-software-prepare/). Depending on the upgrade you are planning, you may need to make changes to your automation.

- [View and import YugabyteDB releases into YugabyteDB Anywhere](./upgrade-software-install/#view-and-import-yugabytedb-releases-into-yugabytedb-anywhere). Before you can upgrade your universe to a specific version of YugabyteDB, verify that the release is available and, if necessary, import the release.

- [Upgrade a universe](./upgrade-software-install/#view-and-import-yugabytedb-releases-into-yugabytedb-anywhere). Perform a rolling upgrade on a live universe deployment.
