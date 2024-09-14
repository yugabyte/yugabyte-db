---
title: Manage YugabyteDB universe deployments
headerTitle: Manage universes
linkTitle: Manage universes
description: Manage YugabyteDB universe deployments
image: /images/section_icons/quick_start/sample_apps.png
menu:
  preview_yugabyte-platform:
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
    title="Upgrade database"
    body="Upgrade YugabyteDB software powering your universes."
    href="upgrade-software/"
    icon="fa-thin fa-cloud-plus">}}

  {{<index/item
    title="Modify a universe"
    body="Scale and configure universes."
    href="edit-universe/"
    icon="fa-thin fa-pen">}}

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
    title="Retry tasks"
    body="Retry failed tasks."
    href="retry-failed-task/"
    icon="fa-thin fa-rotate-right">}}

  {{<index/item
    title="xCluster Replication"
    body="Replicate data between independent YugabyteDB universes."
    href="xcluster-replication/"
    icon="fa-thin fa-clouds">}}

{{</index/block>}}
