---
title: Manage on-premises provider nodes
headerTitle: Manage on-premises provider nodes
linkTitle: Manage nodes
description: Manage the nodes in your on-premises provider free pool.
headContent: Manage node instances in the free pool of nodes for your provider
menu:
  stable_yugabyte-platform:
    identifier: on-premises-nodes
    parent: set-up-on-premises
    weight: 20
type: docs
---

Provisioned nodes are added to the on-premises provider's free pool of nodes.

To view a provider's nodes:

1. Navigate to **Integrations > Infrastructure > On-Premises Datacenters**, and select your on-premises configuration.
1. Select **Instances**.

This displays the configured instance types and instances for the selected provider.

![Configure on-prem instances](/images/yb-platform/config/yba-onprem-config-instances.png)

{{< note title="Legacy assisted manual script" >}}
For legacy manual provisioning (deprecated), the **Instances** page additionally displays the command for running the assisted manual provisioning script (provision_instance.py). Use of this script is deprecated and strictly for legacy manual provisioning.

Instead, provision your nodes automatically using the [node agent script](../../prepare/server-nodes-software/software-on-prem/) (node-agent-provision.sh).
{{< /note >}}

To add nodes to a provider manually, do the following:

1. Specify the compute [instance types](#add-instance-types) that will be used in this provider.
1. [Add the compute instances](#add-instances).
1. [Run preflight checks](#run-preflight-checks).

{{< tip title="Automatic provisioning" >}}

If you are using automatic provisioning, nodes that you provision are automatically added to the provider free pool when provisioning nodes. Refer to [Automatically provision on-premises nodes](../../prepare/server-nodes-software/software-on-prem/).

{{< /tip >}}

## Add instance types

An instance type defines some basic properties of a VM.

To add an instance type, do the following:

1. On the configuration **Instances** page, click **Add Instance Type**.

1. Complete the **Add Instance Type** dialog fields, as follows:

    - Use the **Name** field to define a value to be used internally as an identifier in the **Instance Type** universe field.
    - Use the **Number of Cores** field to define the number of cores to be assigned to a node.
    - Use the **Memory Size (GB)** field to define the memory allocation of a node.
    - Use the **Volume Size (GB)** field to define the disk volume of a node.
    - Use the **Mount Paths** field to define a mount point with enough space to contain your node density. Use `/data`. If you have multiple drives, add these as a comma-separated list, such as, for example, `/mnt/d0,/mnt/d1`.

1. Click **Add Instance Type**.

## Add instances

Before you add instances, you need the following:

- The IP addresses of your VMs. See [Software requirements for nodes](../../prepare/server-nodes-software/).
- Instance type to assign each instance. The instance types define properties of the instances, along with the mount points. See [Add instance types](#add-instance-types).

### Add instances to the on-premises provider

To add the instances, do the following:

1. On the configuration **Instances** page, click **Add Instances**.

    ![On-prem Add Instance Types dialog](/images/yb-platform/config/yba-onprem-config-add-instances.png)

1. For each node in each region, provide the following:

    - Select the zone.
    - Select the instance type.
    - Enter the IP address of the instance. You can use DNS names or IP addresses when adding instances.
    - Optionally, enter an Instance ID; this is a user-defined identifier.

    Note that if you provide a hostname, the universe might experience issues communicating. To resolve this, you need to delete the failed universe and then recreate it with the `use_node_hostname_for_local_tserver` flag enabled.

1. Click **+ Add** to add additional nodes in a region.

1. Click **Add** when you are done.

    The instances are added to the **Instances** list.

## Run preflight checks

After the instances are available in the **Instances** list, validate them by performing a preflight check.

- For each instance, click **Actions**, choose **Perform check**, and click **Apply**.

YugabyteDB Anywhere runs the check and displays the status in the **Preflight Check** column. Click in the column to view details; you can also view the results under **Tasks**.

If all your instances successfully pass the preflight check, your on-premises provider configuration is ready, and you can begin [deploying universes](../../create-deployments/).
