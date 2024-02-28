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
    title="Start and stop processes"
    body="Start and stop the node processes."
    href="start-stop-processes/"
    icon="/images/section_icons/manage/enterprise/edit_universe.png">}}

  {{<index/item
    title="Eliminate an unresponsive node"
    body="Remove and eliminate unresponsive nodes from a universe."
    href="remove-nodes/"
    icon="/images/section_icons/deploy/manual-deployment.png">}}

  {{<index/item
    title="Recover a node"
    body="Recover removed nodes in your YugabyteDB universe."
    href="add-nodes/"
    icon="/images/section_icons/deploy/system.png">}}

  {{<index/item
    title="Edit configuration flags"
    body="Edit configuration flags to customize your processes."
    href="edit-config-flags/"
    icon="/images/section_icons/manage/enterprise/edit_flags.png">}}

  {{<index/item
    title="Edit Kubernetes overrides"
    body="Modify the Helm chart overrides."
    href="edit-helm-overrides/"
    icon="/images/section_icons/manage/enterprise/edit_universe.png">}}

  {{<index/item
    title="Edit a universe"
    body="Use YugabyteDB Anywhere to edit a universe."
    href="edit-universe/"
    icon="/images/section_icons/manage/enterprise/edit_universe.png">}}

  {{<index/item
    title="Pause, resume, and delete universes"
    body="Pause or delete a universe that is not needed."
    href="delete-universe/"
    icon="/images/section_icons/manage/enterprise/delete_universe.png">}}

  {{<index/item
    title="Configure instance tags"
    body="Use YugabyteDB Anywhere to create and edit instance tags."
    href="instance-tags/"
    icon="/images/section_icons/deploy/manual-deployment.png">}}

  {{<index/item
    title="Upgrade YugabyteDB software"
    body="Upgrade YugabyteDB software powering your universes."
    href="upgrade-software/"
    icon="/images/section_icons/manage/enterprise/upgrade_universe.png">}}

  {{<index/item
    title="Migrate to Helm 3"
    body="Migrate your deployment from Helm 2 to Helm 3."
    href="migrate-to-helm3/"
    icon="/images/section_icons/manage/enterprise.png">}}

{{</index/block>}}
