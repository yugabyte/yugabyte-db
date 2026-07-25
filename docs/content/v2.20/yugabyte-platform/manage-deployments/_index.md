---
title: Manage YugabyteDB universe deployments
headerTitle: Manage universes
linkTitle: Manage universes
description: Manage YugabyteDB universe deployments
menu:
  v2.20_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: manage-deployments
    weight: 640
type: indexpage
---
To see your deployed universes, navigate to **Dashboard** or **Universes**. To view a universe, select it in the list.

{{<index/block>}}

  {{<index/item
    title="Patch and upgrade the Linux OS"
    body="Apply operating system patches and upgrades to universe nodes."
    href="upgrade-nodes/"
    icon="fa-thin fa-bandage">}}

  {{<index/item
    title="Upgrade universes"
    body="Upgrade YugabyteDB software powering your universes."
    href="upgrade-software/"
    icon="fa-thin fa-cloud-plus">}}

  {{<index/item
    title="Modify a universe"
    body="Scale a universe horizontally and vertically."
    href="edit-universe/"
    icon="fa-thin fa-pen">}}

  {{<index/item
    title="Edit configuration flags"
    body="Edit configuration flags to customize your processes."
    href="edit-config-flags/"
    icon="fa-thin fa-sliders">}}

  {{<index/item
    title="Configure instance tags"
    body="Create and edit universe instance tags."
    href="instance-tags/"
    icon="fa-thin fa-tags">}}

  {{<index/item
    title="Edit Kubernetes overrides"
    body="Modify the Helm chart overrides."
    href="edit-helm-overrides/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Pause, resume, and delete universes"
    body="Pause or delete a universe that is not needed."
    href="delete-universe/"
    icon="fa-thin fa-traffic-light-go">}}

  {{<index/item
    title="Troubleshoot and manage nodes"
    body="Remove unresponsive nodes, start and stop node processes, and recover nodes."
    href="remove-nodes/"
    icon="fa-thin fa-wrench">}}

  {{<index/item
    title="Retry universe tasks"
    body="Resolve failures by retrying the task."
    href="retry-failed-task/"
    icon="fa-thin fa-rotate-right">}}

{{</index/block>}}
