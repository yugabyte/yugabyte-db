---
title: Orchestration Readiness
linkTitle: 4. Orchestration Readiness 
description: Orchestration Readiness
block_indexing: true
menu:
  v1.1:
    identifier: orchestration-readiness
    parent: explore-cloud-native
    weight: 217
---

YugabyteDB is orchestration-ready on all major infrastructure layers including containers, virtual machines (VMs) and bare metal.

## On Containers

### Kubernetes

Instructions for running YugabyteDB on Kubernetes are available [here](../../../deploy/kubernetes/). Integrations with managed Kubernetes offerings such as [Google Kubernetes Engine (GKE)](../../deploy/public-clouds/gcp/#gke) and [Azure Kubernetes Service (AKS)](../../deploy/public-clouds/azure/#aks) are also available.

### Docker Swarm

Instructions for running YugabyteDB on Docker Swarm are available [here](../../../deploy/docker-swarm/).

### Mesosphere DC/OS

Integration with Mesosphere DC/OS is in the works.

## On Virtual Machines and Bare Metal

### Terraform

Instructions for running YugabyteDB on AWS using Terraform are available [here](../../../deploy/public-clouds/aws/#terraform).

## Using Enterprise Edition

[YugabyteDB Enterprise](../../../deploy/enterprise-edition/) has a built-in orchestration engine that manages multiple YugabyteDB universes (including Read Replicas) on the infrastructure layer and platform of your choice.

{{< note title="Note" >}}
Reach out to us on [Slack](https://www.yugabyte.com/slack) or [GitHub](https://github.com/yugabyte/yugabyte-db/issues) if you need orchestration using a new system.
{{< /note >}}


