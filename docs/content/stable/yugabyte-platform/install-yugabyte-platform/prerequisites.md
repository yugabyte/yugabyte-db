---
title: Prerequisites
headerTitle: Prerequisites
linkTitle: Prerequisites
description: Prerequisites for installing YugabyteDB Anywhere.
menu:
  stable_yugabyte-platform:
    identifier: prerequisites
    parent: install-yugabyte-platform
    weight: 20
type: docs
---

YugabyteDB Anywhere first needs to be installed on a host computer, and then you configure YugabyteDB Anywhere to work in your on-premises private cloud or in a public cloud environment. In a public cloud environment, YugabyteDB Anywhere spawns instances for starting a YugabyteDB universe. In a private cloud environment, you use YugabyteDB Anywhere to add nodes in which you want to be in the YugabyteDB universe. To manage these nodes, YugabyteDB Anywhere requires SSH access to each of the nodes.

## Supported Linux distributions

You can install YugabyteDB Anywhere on the following Linux distributions:

- CentOS (default)
- Ubuntu 18 and 20, via Replicated
- Other [operating systems supported by Replicated](https://www.replicated.com/docs/distributing-an-application/supported-operating-systems/).

## Hardware requirements

The hardware requirements depend on the type of your YugabyteDB Anywhere installation:

### Docker-based installations

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores (minimum) or 8 cores (recommended)
- 8 GB RAM (minimum) or 10 GB RAM (recommended)
- 100 GB SSD disk or more
- 64-bit CPU architecture

### Kubernetes-based installations

A Kubernetes node is expected to meet the following requirements: 

- 5 cores (minimum) or 8 cores (recommended)

- 15 GB RAM (minimum)

- 250 GB SSD disk (minimum) allocated to YugabyteDB Anywhere 

  Since resizing StatefulSets after their creation is challenging, it is recommended to plan for long-term storage needs and allocate more storage initially.

- 64-bit CPU architecture

## Prepare the host

The requirements depend on the type of your YugabyteDB Anywhere installation:

- You prepare a [Docker-based installation](#docker-based-installations) via Replicated.
- A [Kubernetes-based installation](#kubernetes-based-installations) requires you to address concerns related to security and core dump collection.

### Docker-based installations

For a Docker-based installation, YugabyteDB Anywhere uses [Replicated scheduler](https://www.replicated.com/) for software distribution and container management. You need to ensure that the host can pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).

Replicated installs a compatible Docker version if it is not pre-installed on the host. The currently supported Docker version is 20.10.n.

Installing on airgapped hosts requires additional configurations, as described in [Airgapped hosts](#airgapped-hosts).

#### Airgapped hosts

Installing YugabyteDB Anywhere on airgapped hosts, without access to any Internet traffic (inbound or outbound) requires the following:

- Whitelisting endpoints: To install Replicated and YugabyteDB Anywhere on a host with no Internet connectivity, you have to first download the binaries on a computer that has Internet connectivity, and then copy the files over to the appropriate host. In case of restricted connectivity, the following endpoints have to be whitelisted to ensure that they are accessible from the host marked for installation:
  `https://downloads.yugabyte.com`
  `https://download.docker.com`

- Ensuring that Docker Engine version 20.10.n is available. If it is not installed, you need to follow the procedure described in [Installing Docker in airgapped](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/).
- Ensuring that the following ports are open on the YugabyteDB Anywhere host:
  - `8800` – HTTP access to the Replicated UI
  - `80` – HTTP access to the YugabyteDB Anywhere UI
  - `22` – SSH
- Ensuring that the attached disk storage (such as persistent EBS volumes on AWS) is 100 GB minimum.
- Having YugabyteDB Anywhere airgapped install package. Contact Yugabyte Support for more information.
- Signing the Yugabyte license agreement. Contact Yugabyte Support for more information.

### Kubernetes-based installations

The YugabyteDB Anywhere Helm chart has been tested using the following software versions:

- Kubernetes 1.22
- Helm 3.10


Before installing YugabyteDB Anywhere, verify that you have the following:

- A Kubernetes cluster configured with [Helm](https://helm.sh/).
- A Kubernetes secret obtained from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form).

In addition, ensure the following:

- The host can pull container images from the [quay.io](https://quay.io/) container registry. If the host cannot do this, you need to prepare these images in your internal registry by following instructions provided in [Pull and push YugabyteDB Docker images to private container registry](../prepare-environment/kubernetes#pull-and-push-yugabytedb-docker-images-to-private-container-registry). 
- Core dumps are enabled and configured on the underlying Kubernetes node. For details, see [Specify ulimit and remember the location of core dumps](#specify-ulimit-and-remember-the-location-of-core-dumps).
- You have the kube-state-metrics add-on version 1.9 in your Kubernetes cluster. For more information, see [Install kube-state-metrics](../prepare-environment/kubernetes#install-kube-state-metrics).
- A load balancer controller is available in your Kubernetes cluster.
- A StorageClass is available with the SSD and `WaitForFirstConsumer` preferably set to `allowVolumeExpansion`. For more information, see [Control placement of YugabyteDB Anywhere pod](../install-software/kubernetes/#control-placement-of-yugabytedb-anywhere-pod).

#### Specify ulimit and remember the location of core dumps

The core dump collection in Kubernetes requires special care due to the fact that `core_pattern` is not isolated in cgroup drivers.

You need to ensure that core dumps are enabled on the underlying Kubernetes node. Running the `ulimit -c` command within a Kubernetes pod or node must produce a large non-zero value or the `unlimited` value as an output. For more information, see [How to enable core dumps](https://www.ibm.com/support/pages/how-do-i-enable-core-dumps). 

To be able to locate your core dumps, you should be aware of the fact that the location to which core dumps are written depends on the sysctl `kernel.core_pattern` setting. For more information, see [Linux manual: core dump file](https://man7.org/linux/man-pages/man5/core.5.html#:~:text=Naming).

To inspect the value of the sysctl within a Kubernetes pod or node, execute the following:

```sh
cat /proc/sys/kernel/core_pattern
```

If the value of `core_pattern` contains a `|` pipe symbol (for example, `|/usr/share/apport/apport -p%p -s%s -c%c -d%d -P%P -u%u -g%g -- %E` ), the core dump is being redirected to a specific collector on the underlying Kubernetes node, with the location depending on the exact collector. To be able to retrieve core dump files in case of a crash within the Kubernetes pod, it is important that you understand where these files are written.

If the value of `core_pattern` is a literal path of the form `/var/tmp/core.%p`, no action is required on your part, as core dumps will be copied by the YugabyteDB node to the persistent volume directory `/mnt/disk0/cores` for future analysis. 

Note the following:

- ulimits and sysctl are inherited from Kubernetes nodes and cannot be changed for an individual pod. 
- New Kubernetes nodes might be using [systemd-coredump](https://www.freedesktop.org/software/systemd/man/systemd-coredump.html) to manage core dumps on the node. 