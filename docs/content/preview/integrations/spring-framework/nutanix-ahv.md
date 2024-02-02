---
title: Nutanix AHV
linkTitle: Nutanix AHV
description: Use Nutanix AHV with YugabyteDB and YugabyteDB Anywhere
menu:
  preview_integrations:
    identifier: nutanix-ahv
    parent: application-frameworks
    weight: 571
type: docs
---

[Nutanix AHV](https://www.nutanix.com/products/ahv) is a modern and secure virtualization platform that powers VMs and containers for applications and cloud-native workloads on-premises and in public clouds.

## Deploy

Following are the steps to deploy YugabyteDB Anywhere (YBA) and YugabyteDB on Nutanix AHV:

1. Choose an operating system from the [list of YBA supported operating systems](../../../yugabyte-platform/configure-yugabyte-platform/supported-os-and-arch/) for deploying YugabyteDB.

1. [Create AHV VMs](https://portal.nutanix.com/page/documents/details?targetId=Prism-Central-Guide-vpc_2023_4:mul-vm-create-acropolis-pc-t.html) on Nutanix Prism with Network Interface Controller (NIC).

1. [Create, add, and mount storage volume](https://portal.nutanix.com/page/documents/solutions/details?targetId=RA-2107-SAP-High-Availability-Using-Nutanix-Volumes:set-up-disks-using-nutanix-volumes-for-os-clustering.html) on the AHV VMs.

1. [Create and attach volume groups](https://portal.nutanix.com/page/documents/solutions/details?targetId=RA-2012-Informatica-PowerCenter-Grid:nutanix-volume-groups.html) to the AHV VMs.

1. Install YBA software using [YBA Installer](../../../yugabyte-platform/install-yugabyte-platform/install-software/installer/).

1. Create an [on-premises provider](../../../yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/#configure-the-on-premises-provider).

1. Create additional [AHV VMs](https://portal.nutanix.com/page/documents/details?targetId=Prism-Central-Guide-vpc_2023_4:mul-vm-create-acropolis-pc-t.html) to act as on-premises database nodes.

1. Configure [on-premises database AHV VMs](../../../yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/#advanced).

1. [Provision on-premises](../../../yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises-script/) database VMs.

1. Create an [on-premises YBA universe](../../../yugabyte-platform/create-deployments/).

{{< note title="Tip" >}}
Create an [affinity rule](https://portal.nutanix.com/page/documents/details?targetId=Prism-Central-Guide-vpc_2023_4:mul-affinity-policy-create-pc-t.html) on the prism side that brings AHV nodes ( database nodes) on separate controller VM for high availability.
{{< /note >}}
