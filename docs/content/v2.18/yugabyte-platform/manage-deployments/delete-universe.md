---
title: Pause, Resume, and Delete universes
headerTitle: Pause, resume, and delete universes
linkTitle: Pause or delete universe
description: Use YugabyteDB Anywhere to pause, resume, and delete a universe.
menu:
  v2.18_yugabyte-platform:
    identifier: delete-universe
    parent: manage-deployments
    weight: 70
type: docs
---

To reduce costs on unused universes, you can pause or delete them.

## Pause a universe

To pause a universe via the YugabyteDB Anywhere UI, navigate to **Universes**, select your universe, then click **Actions > Pause Universe**.

You can't change the configuration, or read and write data to a paused universe. Alerts and backups are also stopped. Existing backups remain until they expire.

Note that for public clouds, such as Amazon Web Services (AWS) and Google Cloud Platform (GCP), pausing a universe reduces costs for instance vCPU capacity, but disk and backup storage costs are not affected.

## Resume a universe

To resume a paused universe via the YugabyteDB Anywhere UI, navigate to **Universes**, select the paused universe, then click **Actions > Resume Universe**.

## Delete a universe

To delete a universe via the YugabyteDB Anywhere UI, navigate to **Universes**, select your universe, then click **Actions > Delete Universe**.

For public clouds, the underlying compute instances are terminated after the database has been uninstalled from those nodes.

For on-premises data centers, the underlying compute instances are no longer marked as `In Use`, which then opens those instances up to be reused for new universes.
