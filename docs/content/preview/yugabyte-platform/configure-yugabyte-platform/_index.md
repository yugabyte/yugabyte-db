---
title: Create provider configurations
headerTitle: Create provider configurations
linkTitle: Create providers
description: Create provider configurations for deploying YugabyteDB universes.
image: fa-thin fa-folder-gear
menu:
  preview_yugabyte-platform:
    parent: yugabytedb-anywhere
    identifier: configure-yugabyte-platform
    weight: 620
aliases:
  - /preview/yugabyte-platform/overview/configure/
  - /preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/
type: indexpage
---

After installing YugabyteDB Anywhere (YBA), the next step is to create provider configurations.

A provider configuration describes your cloud environment (such as its security group, regions and availability zones, NTP server, SSH credentials for connecting to VMs for provisioning, the Linux disk image to be used for configuring the nodes, and so on). The provider configuration is used as an input when deploying a universe, and can be reused for many universes.

Before you can deploy universes using YBA, you must create a provider configuration.

| To deploy YugbayteDB universes to | Create provider |
| :--- | :--- |
| Private cloud<br>Bare metal, racks<br>Cloud provider (limited SSH permissions) | [On-premises](on-premises/) |
| Cloud provider (full SSH permissions) | [AWS](aws/)<br>[GCP](gcp/)<br>[Azure](azure/) |
| Kubernetes | [Kubernetes](kubernetes/)<br>[VMware Tanzu](vmware-tanzu/)<br>[OpenShift](openshift/) |

{{<index/block>}}

  {{<index/item
    title="On-premises"
    body="Create provider configurations for on-premises deployments."
    href="on-premises/"
    icon="fa-thin fa-building">}}

  {{<index/item
    title="Cloud"
    body="Create provider configurations for AWS, Azure, and GCP."
    href="aws/"
    icon="fa-thin fa-cloud">}}

  {{<index/item
    title="Kubernetes"
    body="Create provider configurations for Kubernetes, including VMWare Tanzu and OpenShift."
    href="kubernetes/"
    icon="fa-thin fa-dharmachakra">}}

{{</index/block>}}
