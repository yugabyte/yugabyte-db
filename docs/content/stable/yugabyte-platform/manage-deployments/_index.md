---
title: Manage YugabyteDB universe deployments
headerTitle: Manage universes
linkTitle: Manage universes
description: Manage YugabyteDB universe deployments
image: /images/section_icons/quick_start/sample_apps.png
menu:
  stable_yugabyte-platform:
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
    icon="/images/section_icons/deploy/manual-deployment.png">}}

  {{<index/item
    title="Upgrade universes"
    body="Upgrade YugabyteDB software powering your universes."
    href="upgrade-software/"
    icon="/images/section_icons/manage/enterprise/upgrade_universe.png">}}

  {{<index/item
    title="Modify a universe"
    body="Scale a universe horizontally and vertically."
    href="edit-universe/"
    icon="/images/section_icons/manage/enterprise/edit_universe.png">}}

  {{<index/item
    title="Edit configuration flags"
    body="Edit configuration flags to customize your processes."
    href="edit-config-flags/"
    icon="/images/section_icons/manage/enterprise/edit_flags.png">}}

  {{<index/item
    title="Configure instance tags"
    body="Create and edit universe instance tags."
    href="instance-tags/"
    icon="/images/section_icons/deploy/manual-deployment.png">}}

  {{<index/item
    title="Edit Kubernetes overrides"
    body="Modify the Helm chart overrides."
    href="edit-helm-overrides/"
    icon="/images/section_icons/deploy/kubernetes.png">}}

  {{<index/item
    title="Pause, resume, and delete universes"
    body="Pause or delete a universe that is not needed."
    href="delete-universe/"
    icon="/images/section_icons/manage/enterprise/delete_universe.png">}}

  {{<index/item
    title="Troubleshoot and manage nodes"
    body="Remove unresponsive nodes, start and stop node processes, and recover nodes."
    href="remove-nodes/"
    icon="/images/section_icons/manage/enterprise/create_universe.png">}}

{{</index/block>}}
