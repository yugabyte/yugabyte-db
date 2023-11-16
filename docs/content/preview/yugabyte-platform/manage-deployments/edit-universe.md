---
title: Use YugabyteDB Anywhere to modify a universe
headerTitle: Modify universe
linkTitle: Modify universe
description: Use YugabyteDB Anywhere to modify a universe.
aliases:
  - /preview/manage/enterprise-edition/edit-universe/
menu:
  preview_yugabyte-platform:
    identifier: edit-universe
    parent: manage-deployments
    weight: 60
type: docs
---

YugabyteDB Anywhere allows you to expand a universe to add more nodes and shrink the universe to reduce the number of nodes. Typically, you do this by navigating to **Universes > UniverseName > Actions > Edit Universe**, as per the following illustration:

![Edit universe](/images/ee/edit-univ.png)

Using the **Edit Universe** page, you can specify the new intent for the universe. This may include a new configuration of nodes powered by a different instance type. YugabyteDB Anywhere performs these modifications through the YB-Masters powering the universe. The YB-Masters ensure that the new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

Expansion of universes created with an on-premise cloud provider and secured with third-party certificates obtained from external certification authorities follows a different workflow. For details, see [Expand the universe](../../security/enable-encryption-in-transit#expand-the-universe).

## Smart resize

For universes that use Google Cloud Provider (GCP) or Amazon Web Services (AWS), YBA allows you to change the VM images and increase the volume size without moving the data from the old nodes to the new nodes. This is known as smart resize and is subject to the following:

- Smart resize cannot be applied to instances with ephemeral disks due to a potential loss of data, but smart resize to ephemeral disks is supported.

- Smart resize cannot decrease the volume size.

- You can't do a smart resize if you change any options on the **Edit Universe** page other than the **Instance Type** and the size portion of the **Volume Info** field.

If you change the **Instance Type** or both the **Instance Type** and the **Volume Info** size and then click **Save**, YBA gives you the option to either do a full migration of the universe and its data to new nodes, or do a smart resize, as per the following illustrations:

  ![Full or smart resize1](/images/ee/edit-univ-1.png)
  ![Full or smart resize2](/images/ee/edit-univ-2.png)

If you change only the **Volume Info** size and click **Save**, YBA automatically performs a smart resize.
