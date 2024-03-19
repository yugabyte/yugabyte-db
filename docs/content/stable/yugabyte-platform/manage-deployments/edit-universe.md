---
title: Use YugabyteDB Anywhere to modify a universe
headerTitle: Modify universe
linkTitle: Modify universe
description: Use YugabyteDB Anywhere to modify a universe.
headcontent: Scale universes horizontally and vertically
menu:
  stable_yugabyte-platform:
    identifier: edit-universe
    parent: manage-deployments
    weight: 30
type: docs
---

YugabyteDB Anywhere allows you to expand a universe to add more nodes and shrink the universe to reduce the number of nodes.

- For information on changing configuration flags, refer to [Edit configuration flags](../edit-config-flags/).

- For information on changing user tags, refer to [Create and edit instance tags](../instance-tags/).

- For information on changing Kubernetes overrides, refer to [Edit Kubernetes overrides](../edit-helm-overrides/).

## Edit a universe

To change the configuration of a universe, navigate to **Universes > UniverseName > Actions > Edit Universe**, as per the following illustration:

![Edit universe](/images/ee/edit-univ-220.png)

Using the **Edit Universe** page, you can modify the following:

- Regions - you can select any region configured in the provider used to deploy the universe
- Number of nodes and Availability zones - as you add nodes, they are automatically distributed among the availability zones; you can also add, configure, and remove availability zones
- [Master placement](../../create-deployments/dedicated-master/)
- Instance type and volume size - you can select instance types configured in the provider
- [User tags](../instance-tags/)

YugabyteDB Anywhere performs these modifications through the [YB-Masters](../../../architecture/concepts/yb-master/) powering the universe. The YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

Note that you can't change the replication factor of a universe.

To change the number of nodes of universes created with an on-premises cloud provider and secured with third-party certificates obtained from external certification authorities, follow the instructions in [Expand the universe](../../security/enable-encryption-in-transit#expand-the-universe).

### Smart resize

For universes that use Google Cloud Provider (GCP), Amazon Web Services (AWS), or Microsoft Azure, YBA allows you to change the VM images and increase the volume size without moving the data from the old nodes to the new nodes. This is known as smart resize and is subject to the following:

- For Azure universes, you can't increase the volume size for ultra SSDs.

- To avoid potential data loss, you can't do a smart resize of instances with ephemeral disks. Smart resize _to_ ephemeral disks is supported.

- Smart resize cannot decrease the volume size.

- You can't do a smart resize if you change any options on the **Edit Universe** page other than the **Instance Type** and the size portion of the **Volume Info** field.

If you change the **Instance Type** or both the **Instance Type** and the **Volume Info** size and then click **Save**, YBA gives you the option to either do a full migration of the universe and its data to new nodes, or do a smart resize, as per the following illustrations:

  ![Full or smart resize1](/images/ee/edit-univ-1.png)
  ![Full or smart resize2](/images/ee/edit-univ-2.png)

If you change only the **Volume Info** size and click **Save**, YBA automatically performs a smart resize.
