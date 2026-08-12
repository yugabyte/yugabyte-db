---
title: Scale YugabyteDB universe deployments
headerTitle: Scale and edit universes
linkTitle: Scale and edit universes
description: Scale YugabyteDB universe deployments horizontally and vertically
menu:
  stable_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: scale-deployments
    weight: 635
type: indexpage
---

Scale universes horizontally and vertically, edit configuration flags and instance tags, configure Kubernetes overrides, and pause or delete universes that are not needed.

For upgrades, node troubleshooting, and task monitoring, refer to [Manage universes](../manage-deployments/).

{{<index/block>}}

  {{<index/item
    title="Scale universes"
    body="Scale universes horizontally and vertically."
    href="edit-universe/"
    icon="fa-thin fa-pen">}}

  {{<index/item
    title="Edit configuration flags"
    body="Customize the database server configuration."
    href="edit-config-flags/"
    icon="fa-thin fa-flag">}}

  {{<index/item
    title="Kubernetes overrides"
    body="Configure Helm chart overrides for Kubernetes universes."
    href="edit-helm-overrides/"
    icon="fa-thin fa-dharmachakra">}}

  {{<index/item
    title="Kubernetes full move"
    body="Change storage class, volume count, and volume size on Kubernetes universes."
    href="kubernetes-full-move/"
    icon="fa-thin fa-arrows-rotate">}}

  {{<index/item
    title="Configure instance tags"
    body="Create and edit instance tags for cloud resources."
    href="instance-tags/"
    icon="fa-thin fa-tags">}}

  {{<index/item
    title="Pause, resume, and delete universes"
    body="Pause or delete a universe that is not needed."
    href="delete-universe/"
    icon="fa-thin fa-traffic-light-go">}}

{{</index/block>}}
