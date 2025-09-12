---
title: Pause, Resume, and Delete universes
headerTitle: Pause, resume, and delete universes
linkTitle: Pause or delete universe
description: Use YugabyteDB Anywhere to pause, resume, and delete a universe.
aliases:
  - /preview/manage/enterprise-edition/delete-universe/
menu:
  preview_yugabyte-platform:
    identifier: delete-universe
    parent: manage-deployments
    weight: 50
type: docs
---

To reduce costs on unused universes, you can pause or delete them.

## Pause a universe

To pause a universe, navigate to **Universes**, select your universe, then click **Actions > Pause Universe**.

Note that you can't pause universes created using an on-premises provider.

You can't change the configuration, or read and write data to a paused universe. Alerts and backups are also stopped. Existing backups remain until they expire.

Note that for public clouds, such as Amazon Web Services (AWS) and Google Cloud Platform (GCP), pausing a universe reduces costs for instance vCPU capacity, but disk and backup storage costs are not affected.

## Resume a universe

To resume a paused universe, navigate to **Universes**, select the paused universe, then click **Actions > Resume Universe**.

## Delete a universe

To delete a universe, navigate to **Universes**, select your universe, then click **Actions > Delete Universe**.

For public clouds, the underlying compute instances are terminated after the database has been uninstalled from those nodes.

For on-premises data centers, the underlying compute instances are no longer marked as `In Use`, which then opens those instances up to be reused for new universes.
