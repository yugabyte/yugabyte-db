---
title: Create provider configurations
headerTitle: Create provider configurations
linkTitle: Create provider configurations
description: Create provider configurations.
image: /images/section_icons/deploy/manual-deployment.png
menu:
  v2.20_yugabyte-platform:
    parent: configure-yugabyte-platform
    identifier: set-up-cloud-provider
    weight: 20
type: indexpage
---

After installing YugabyteDB Anywhere (YBA), the next step is to create provider configurations.

A provider configuration describes your cloud environment (such as its security group, regions and availability zones, NTP server, SSH credentials for connecting to VMs for provisioning, the Linux disk image to be used for configuring the nodes, and so on). The provider configuration is used as an input when deploying a universe, and can be reused for many universes.

Before you can deploy universes using YBA, you must create a provider configuration.

| To deploy YugbayteDB universes to | Create provider |
| :--- | :--- |
| Cloud provider | [AWS](aws/)<br>[GCP](gcp/)<br>[Azure](azure/) |
| Kubernetes | [Kubernetes](kubernetes/)<br>[VMware Tanzu](vmware-tanzu/)<br>[OpenShift](openshift/) |
| Private cloud | [On-premises](on-premises/) |

{{<index/block>}}

  {{<index/item
    title="Cloud providers"
    body="Create provider configurations for AWS, Azure, and GCP."
    href="aws/"
    icon="fa-solid fa-cloud">}}

  {{<index/item
    title="Kubernetes"
    body="Create provider configurations for Kubernetes, including VMWare Tanzu and OpenShift."
    href="kubernetes/"
    icon="fa-solid fa-dharmachakra">}}

  {{<index/item
    title="On-premises"
    body="Create provider configurations for on-premises deployments."
    href="on-premises/"
    icon="fa-solid fa-building">}}

{{</index/block>}}
