---
title: Provider configurations
headerTitle: Provider configurations
linkTitle: Provider configurations
description: Create and manage provider configurations for deploying YugabyteDB universes.
menu:
  v2025.1_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: configure-yugabyte-platform
    weight: 620
    params:
      classes: separator
      hideLink: true
type: indexpage
---

After installing YugabyteDB Anywhere, the next step is to create provider configurations.

A provider configuration describes your cloud environment (such as its security group, regions and availability zones, NTP server, SSH credentials for connecting to VMs for provisioning, the Linux disk image to be used for configuring the nodes, and so on). The provider configuration is used as an input when deploying a universe, and can be reused for many universes.

{{<lead link="../yba-overview/#provider-configurations">}}
Not sure what type of provider to use? Refer to [Provider configurations](../yba-overview/#provider-configurations).
{{</lead>}}

{{< tip title="Automatic on-premises provisioning" >}}

If you are deploying on premises (private cloud), on-premises providers are created automatically when provisioning nodes. Refer to [Automatically provision on-premises nodes](../prepare/server-nodes-software/software-on-prem/).

{{< /tip >}}

Before you can deploy universes using YugabyteDB Anywhere, you must create a provider configuration.

| To&nbsp;Deploy&nbsp;universes&nbsp;to | Create&nbsp;provider | Description |
| :--- | :--- | :--- |
| - Private cloud<br>- Bare metal/racks<br>- Cloud provider (limited SSH) | [On-premises](on-premises/) | Deploy universes to your own infrastructure, or to cloud providers where (due to security policies or other restrictions) you can't provide YBA with cloud permissions or SSH access to cloud VMs.<br>Provides maximum flexibility.<br>On-premises providers are created automatically when provisioning nodes. Refer to [Automatically provision on-premises nodes](../prepare/server-nodes-software/software-on-prem/). |
| Cloud provider<br>(full SSH) | [AWS](aws/)<br>[GCP](gcp/)<br>[Azure](azure/) | Deploy universes to cloud providers where you can provide YBA with cloud permissions and SSH access to cloud VMs for full automation.<br>Provides maximum automation. |
| Kubernetes | [Kubernetes](kubernetes/)<br>[VMware Tanzu](vmware-tanzu/)<br>[OpenShift](openshift/) | Deploy universes on Kubernetes. |
