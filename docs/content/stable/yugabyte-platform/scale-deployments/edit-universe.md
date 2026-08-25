---
title: Use YugabyteDB Anywhere to modify a universe
headerTitle: Scale universes
linkTitle: Scale universes
description: Use YugabyteDB Anywhere to scale a universe.
headcontent: Scale universes horizontally and vertically
aliases:
  - /stable/manage/enterprise-edition/edit-universe/
  - /stable/yugabyte-platform/manage-deployments/edit-universe/
menu:
  stable_yugabyte-platform:
    identifier: edit-universe
    parent: scale-deployments
    weight: 10
type: docs
---

YugabyteDB Anywhere supports both horizontal and vertical scaling of your universe. If your workloads have increased, you can add nodes or change to more powerful instance types to improve latency, throughput, and memory. Likewise, if your cluster is over-scaled, you can reduce nodes or use smaller instances to reduce costs.

YugabyteDB automatically ensures that new nodes start hosting the tablet leaders for a set of tablets in such a way that the tablet leader count remains evenly balanced across all the available nodes.

<!--
-> For information on changing configuration flags, refer to [Edit configuration flags](../edit-config-flags/).

-> For information on changing user tags, refer to [Create and edit instance tags](../instance-tags/).

-> For information on changing Kubernetes overrides, refer to [Edit Kubernetes overrides](../edit-helm-overrides/).

-> For information on changing storage class and volume count on Kubernetes universes, refer to [Full move for Kubernetes universes](../kubernetes-full-move/).

-> For information on managing Kubernetes universes using the YugabyteDB Kubernetes Operator, refer to [YugabyteDB Kubernetes Operator](../../anywhere-automation/yb-kubernetes-operator/).
-->

To scale a universe, change its [placement](#horizontal-scaling) (nodes, availability zones, and regions) or its [hardware](#vertical-scaling) (instance type and storage).

You can also change [user tags](../instance-tags/), [configuration flags](../edit-config-flags/), and [Kubernetes overrides](../edit-helm-overrides/). To place YB-Master processes on dedicated nodes, refer to [Dedicated YB-Masters](../../create-deployments/dedicated-master/).

## Horizontal scaling

Horizontal scaling adds or removes nodes (or pods, for Kubernetes) and can include changing availability zones or regions.

In on-premises universes, ensure your provider has enough free nodes to accommodate any increase in size of your cluster.

To change the number of nodes of universes created with an on-premises provider and secured with third-party certificates obtained from external certification authorities, you must first add the certificates to the nodes you will add to the universe. Refer to [Add certificates](../../security/enable-encryption-in-transit/add-certificate-ca/). Ensure that the certificates are signed by the same external CA and have the same root certificate. In addition, ensure that you copy the certificates to the same locations that you originally used when creating the universe.

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

{{<tags/ui/new>}} To scale a universe horizontally:

Navigate to the universe, select **Settings > Placement**, and on the **Primary Cluster** card, click **Edit** and choose one of the following:

- **Edit Regions** to add or remove regions, or to change resilience or replication factor. After you update regions, continue to the nodes and availability zones step.
- **Edit AZ and Node Placement** (or **Edit AZ and Pod Placement** for Kubernetes) to add or remove nodes and availability zones.

Either option displays the **Edit Placement** wizard.

Using the wizard, you can specify placement using **Guided** (suitable for most topologies) or **Expert** (more flexible) mode.

{{< tabpane text=true >}}

{{% tab header="Guided" lang="guide" %}}

In Guided mode, you set the following:

1. **Resilience**. This determines how many failures your primary cluster can tolerate without interruption or downtime. Currently, you can only _increase_ the resilience. Increasing resilience requires more nodes or availability zones.
1. **Regions**. Select the regions where you want to locate the primary cluster.
1. **Availability Zones and Nodes**.

    - Select the zones in the regions where you want to place the nodes.
    - Specify the number of nodes per region. As you add nodes, they are automatically distributed among the availability zones.
    - Specify the preferred region(s) in ranked order.

    All zones have the same number of nodes.

{{% /tab %}}

{{% tab header="Expert" lang="expert" %}}

In Expert mode, you set the following:

1. **Regions**. Select the regions where you want to locate the primary cluster.
1. {{<tags/feature/ea idea="56">}}**Replication Factor** - Currently, you can only _increase_ the replication factor. Note that this change may also require you to increase the number of nodes or availability zones. Contact {{% support-platform %}} for help with capacity planning and appropriate sizing.
1. **Availability Zones and Nodes**.

    - Select the zones in the regions where you want to place the nodes.
    - Specify the number of nodes for each zone.
    - Specify the preferred region(s) in ranked order.

    Depending on the number of regions you selected and the replication factor, you can add additional availability zones to regions.

{{% /tab %}}

{{< /tabpane >}}

When you are done, click **Review Changes**, confirm the summary, then click **Confirm and Apply**.

{{% /tab %}}

{{% tab header="Classic UI" lang="classic" %}}

{{<tags/ui/classic>}} To scale a universe horizontally:

1. Navigate to your universe and choose **Actions > Edit Universe**.

    ![Edit universe](/images/ee/edit-univ-220.png)

1. Under **Cloud Configuration**, update the following as needed:

    - **Regions** - Select any region configured in the provider used to deploy the universe.
    - [Master Placement](../../create-deployments/dedicated-master/).
    - **Total Nodes** and **Availability Zones** - As you add nodes, they are automatically distributed among the availability zones; you can also add, configure, and remove availability zones.
    - {{<tags/feature/ea idea="56">}}**Replication Factor** - Currently, you can only _increase_ the replication factor. Note that this change may also require you to increase the number of nodes or availability zones. Contact {{% support-platform %}} before modifying this field, for assistance on capacity planning and sizing appropriately.

1. Click **Save**.

{{% /tab %}}

{{< /tabpane >}}

## Vertical scaling

Vertical scaling changes the instance type or storage used by universe nodes. In some cases, these operations are available as a [smart resize](#smart-resize).

{{< tabpane text=true >}}

{{% tab header="New UI" lang="new" %}}

{{<tags/ui/new>}} To scale a universe vertically:

1. Navigate to the universe, then open **Settings > Hardware**.

1. On the **Cluster Instance** card, click **Edit**.

    If the universe uses [dedicated master nodes](../../create-deployments/dedicated-master/), edit the **T-Server Instance** and **Master Server Instance** cards separately.

1. Update the instance configuration as needed:

    - **Instance Type** and volume size - Change instance type and storage volume size as configured in the provider.
    - Storage type and volume count - For cloud providers, you can also change the storage volume count and type. On AWS, you can additionally change throughput and IOPS.
    
        For Kubernetes universes on YugabyteDB v2026.1.0.0 or later, you can change storage class and volume count using [full move](../kubernetes-full-move/).
    - For Kubernetes, you can also change cores and memory.

1. Click **Review Changes**.

1. Confirm the summary of current and new values. If more than one update option is available, choose how to apply the change:

    - **Rolling restart the current nodes** - Resizes the existing nodes (smart resize). No data migration is required. This option is faster and recommended when available.
    - **Migrate to a new set of nodes** - Moves data to new nodes with new IP addresses. The universe remains online.

    For details on when each option is available, refer to [Smart resize](#smart-resize).

1. Click **Confirm and Apply**.

{{% /tab %}}

{{% tab header="Classic UI" lang="classic" %}}

{{<tags/ui/classic>}} To scale a universe vertically:

1. Navigate to your universe and choose **Actions > Edit Universe**.

1. Under **Instance Configuration**, update the following as needed:

    - **Instance Type** and **Volume Info Size** - Change instance type and storage volume size as configured in the provider. In some cases, these operations are available as a [smart resize](#smart-resize).
    - **Storage Type** and **Volume Info Count** - For cloud providers, you can also change the storage volume count and type. On AWS, you can additionally change throughput and IOPS.
    
        For Kubernetes universes on YugabyteDB v2026.1.0.0 or later, you can change storage class and volume count using [full move](../kubernetes-full-move/).

1. Click **Save**.

When smart resize is available, YugabyteDB Anywhere prompts you to either migrate the universe and its data to new nodes, or do a smart resize. Refer to [Smart resize](#smart-resize).

{{% /tab %}}

{{< /tabpane >}}

## Smart resize

Normally when resizing a universe, YugabyteDB moves the data from the old nodes to the new nodes. However, if the universe is deployed on AWS, GCP, or Azure using a [cloud provider configuration](../../configure-yugabyte-platform/aws/), you can perform some resizing operations without migrating the data. This is referred to as smart resize, and can be significantly faster than a full copy of the data.

Smart resize is available for the following operations:

- Change the Instance type.

    Note that smart resize is not available when changing the instance type from an AWS EBS-backed instance type (like c5.xlarge) to a local storage-backed instance type (like i3.xlarge), or vice-versa.

- Increase the Volume disk size.

    Note that smart resize is not available with Azure ultra disks, or when decreasing the volume size.

- Both together.

In addition, smart resize isn't available if you change other universe settings in the same operation, such as node count, regions, or storage type, in addition to the instance type and volume size.

When smart resize is available, YugabyteDB Anywhere gives you the option to either migrate the universe and its data to new nodes, or do a smart resize.

![Smart resize dialog](/images/ee/edit-univ-2.png)

If you change only the volume size, YugabyteDB Anywhere automatically performs a smart resize.
