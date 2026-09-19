---
title: Manage YugabyteDB universe deployments
headerTitle: Manage universes
linkTitle: Manage universes
description: Manage YugabyteDB universe deployments
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: manage-deployments
    weight: 640
type: indexpage
---

Upgrade the database and operating system on universe nodes, troubleshoot and manage nodes, monitor universe tasks, and pause or delete universes.

For scaling and configuration changes, refer to [Scale and edit universes](../scale-deployments/).

{{<index/block>}}

  {{<index/item
    title="Patch and upgrade the Linux OS"
    body="Apply operating system patches and upgrades to universe nodes."
    href="upgrade-nodes/"
    icon="fa-thin fa-bandage">}}

  {{<index/item
    title="Upgrade database"
    body="Upgrade YugabyteDB software powering your universes."
    href="upgrade-software/"
    icon="fa-thin fa-cloud-plus">}}

  {{<index/item
    title="Troubleshoot and manage nodes"
    body="Remove unresponsive nodes, start and stop node processes, and recover nodes."
    href="remove-nodes/"
    icon="fa-thin fa-wrench">}}

  {{<index/item
    title="Monitor tasks"
    body="Monitor and manage universe tasks."
    href="retry-failed-task/"
    icon="fa-thin fa-magnifying-glass">}}

  {{<index/item
    title="Pause, resume, and delete universes"
    body="Pause or delete a universe that is not needed."
    href="delete-universe/"
    icon="fa-thin fa-traffic-light-go">}}

{{</index/block>}}
