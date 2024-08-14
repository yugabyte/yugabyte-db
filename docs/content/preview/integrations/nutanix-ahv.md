---
title: Nutanix AHV
linkTitle: Nutanix AHV
description: Use Nutanix AHV with YugabyteDB and YugabyteDB Anywhere
menu:
  preview_integrations:
    identifier: nutanix-ahv
    parent: integrations-platforms
    weight: 571
type: docs
---

[Nutanix AHV](https://www.nutanix.com/products/ahv) is a modern and secure virtualization platform that powers VMs and containers for applications and cloud-native workloads on-premises and in public clouds.

## Deploy

The following steps describe how to deploy YugabyteDB Anywhere and YugabyteDB using Nutanix AHV VMs:

1. Choose an [operating system](../../reference/configuration/operating-systems/) for deploying YugabyteDB.

1. [Create AHV VMs](https://portal.nutanix.com/page/documents/details?targetId=Prism-Central-Guide-vpc_2023_4:mul-vm-create-acropolis-pc-t.html) on Nutanix Prism with Network Interface Controller (NIC).

1. [Create, add, and mount storage volumes](https://portal.nutanix.com/page/documents/solutions/details?targetId=RA-2107-SAP-High-Availability-Using-Nutanix-Volumes:set-up-disks-using-nutanix-volumes-for-os-clustering.html) on the AHV VMs.

1. [Create and attach volume groups](https://portal.nutanix.com/page/documents/solutions/details?targetId=RA-2012-Informatica-PowerCenter-Grid:nutanix-volume-groups.html) to the AHV VMs.

1. Install YugabyteDB Anywhere using [YBA Installer](../../yugabyte-platform/install-yugabyte-platform/install-software/installer/).

1. Create additional [AHV VMs](https://portal.nutanix.com/page/documents/details?targetId=Prism-Central-Guide-vpc_2023_4:mul-vm-create-acropolis-pc-t.html) to act as on-premises database nodes.

1. [Provision the VMs as on-premises](../../yugabyte-platform/prepare/server-nodes-software/software-on-prem/) database nodes.

1. Create an [on-premises provider](../../yugabyte-platform/configure-yugabyte-platform/on-premises/) and add the nodes.

1. Create an [on-premises universe](../../yugabyte-platform/create-deployments/).

{{< note title="Tip" >}}
For high availability, create an [affinity rule](https://portal.nutanix.com/page/documents/details?targetId=Prism-Central-Guide-vpc_2023_4:mul-affinity-policies-pc-c.html) on the Prism side that hosts AHV nodes (database nodes) on separate controller VMs.
{{< /note >}}
