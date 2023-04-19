---
title: Delete a universe
headerTitle: Delete a universe
linkTitle: Delete a universe
description: Use YugabyteDB Anywhere to delete a universe.
menu:
  v2.14_yugabyte-platform:
    identifier: delete-universe
    parent: manage-deployments
    weight: 70
type: docs
---

To delete a universe via the YugabyteDB Anywhere UI, navigate to **Universes**, select your universe, then click **Actions > Delete Universe**.

For public clouds, such as Amazon Web Services (AWS) and Google Cloud Platform (GCP), the underlying compute instances are terminated after the database has been installed from those nodes.

For on-premises data centers, the underlying compute instances are no longer marked as `In Use`, which then opens those instances up to be reused for new universes.
