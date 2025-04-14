---
title: Use YugabyteDB Anywhere to modify a universe
headerTitle: Modify universe
linkTitle: Modify universe
description: Use YugabyteDB Anywhere to modify a universe.
headcontent: Scale universes horizontally and vertically
menu:
  v2.20_yugabyte-platform:
    identifier: edit-universe
    parent: manage-deployments
    weight: 30
type: docs
---

YugabyteDB Anywhere supports both horizontal and vertical scaling of your universe. If your workloads have increased, you can change to more powerful instance types or add nodes to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes to reduce costs.

-> For information on changing configuration flags, refer to [Edit configuration flags](../edit-config-flags/).

-> For information on changing user tags, refer to [Create and edit instance tags](../instance-tags/).

-> For information on changing Kubernetes overrides, refer to [Edit Kubernetes overrides](../edit-helm-overrides/).

## Edit a universe

To change the configuration of a universe, do the following:

1. Navigate to your universe and choose **Actions > Edit Universe** to display the **Edit universe** page.

    ![Edit universe](/images/ee/edit-univ-220.png)

1. Update the configuration.

    Using the **Edit Universe** page, you can modify the following:

    - **Cloud Configuration**
        - **Regions** - Select any region configured in the provider used to deploy the universe.
        - [Master Placement](../../create-deployments/dedicated-master/).
        - **Total Nodes** and **Availability Zones** - As you add nodes, they are automatically distributed among the availability zones; you can also add, configure, and remove availability zones.
    - **Instance Configuration** - Change instance type and storage volume size as configured in the provider. In some cases, these operations are available as a [smart resize](#smart-resize).

        For cloud providers, you can also change the storage volume count and type. On AWS, you can additionally change throughput and IOPS.

    - [User Tags](../instance-tags/). Changing tags doesn't require any node restarts or data migration.

    Note that you can't change the replication factor of a universe.

1. Click **Save**.

YugabyteDB automatically ensures that new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

To change the number of nodes of universes created with an on-premises cloud provider and secured with third-party certificates obtained from external certification authorities, you must first add the certificates to the nodes you will add to the universe. Refer to [Custom CA-signed self-provided certificates](../../security/enable-encryption-in-transit/#custom-ca-signed-self-provided-certificates). Ensure that the certificates are signed by the same external CA and have the same root certificate. In addition, ensure that you copy the certificates to the same locations that you originally used when creating the universe.

## Smart resize

Normally when resizing a universe, YugabyteDB moves the data from the old nodes to the new nodes. However, if the universe is deployed on AWS, GCP, or Azure using a [cloud provider configuration](../../configure-yugabyte-platform/aws/), you can perform some resizing operations without migrating the data. This is referred to as smart resize, and can be significantly faster than a full copy of the data.

Smart resize is available for the following operations:

- Change the Instance type.

    Note that smart resize is not available when changing the instance type from an AWS EBS-backed instance type (like c5.xlarge) to a local storage-backed instance type (like i3.xlarge), or vice-versa.

- Increase the Volume disk size.

    Note that smart resize is not available with Azure ultra disks, or when decreasing the volume size.

- Both together.

In addition, smart resize isn't available if you change any options on the **Edit Universe** page in addition to the **Instance Type** and the size portion of the **Volume Info** field.

When available, if you change the **Instance Type**, or both the **Instance Type** and **Volume Info** size, and then click **Save**, YugabyteDB Anywhere gives you the option to either migrate the universe and its data to new nodes, or do a smart resize.

![Smart resize dialog](/images/ee/edit-univ-2.png)

If you change only the **Volume Info** size and click **Save**, YugabyteDB Anywhere automatically performs a smart resize.
