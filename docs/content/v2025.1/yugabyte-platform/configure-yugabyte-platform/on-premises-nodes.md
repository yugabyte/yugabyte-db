---
title: Add nodes to on-premises provider
headerTitle: Add nodes to the on-premises provider
linkTitle: Add nodes
description: Configure the on-premises provider configuration.
headContent: Add node instances to the free pool of nodes for your provider
menu:
  v2025.1_yugabyte-platform:
    identifier: on-premises-nodes
    parent: set-up-on-premises
    weight: 20
type: docs
---

After creating the on-premises provider, you can add instances to its free pool of nodes.

1. Navigate to **Integrations > Infrastructure > On-Premises Datacenters**, and select the on-premises configuration you created.
1. Select **Instances**.

This displays the configured instance types and instances for the selected provider.

![Configure on-prem instances](/images/yb-platform/config/yba-onprem-config-instances.png)

{{< note title="Legacy assisted manual script" >}}
For legacy manual provisioning (deprecated), the **Instances** page additionally displays the command for running the assisted manual provisioning script (provision_instance.py). This script is strictly for legacy manual provisioning.

If you provisioned your nodes automatically using the [node agent script](../../prepare/server-nodes-software/software-on-prem/) (node-agent-provision.sh), you can add your nodes (if they have not already been added to the provider) by clicking **Add Instances**.
{{< /note >}}

To add nodes, do the following:

1. Specify the compute [instance types](#add-instance-types) that will be used in this provider.
1. [Add the compute instances](#add-instances).
1. [Run preflight checks](#run-preflight-checks).

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
- If you are doing legacy [assisted manual provisioning](../on-premises/#stage-3-add-nodes-to-the-provider-free-pool-1) (deprecated), you must provision the nodes using the script. Follow the instructions in [Legacy assisted manual provisioning](../on-premises-script/).

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
