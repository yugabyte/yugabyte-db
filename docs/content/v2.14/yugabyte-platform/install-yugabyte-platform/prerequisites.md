---
title: Prerequisites
headerTitle: Prerequisites
linkTitle: Prerequisites
description: Prerequisites for installing YugabyteDB Anywhere.
menu:
  v2.14_yugabyte-platform:
    identifier: prerequisites
    parent: install-yugabyte-platform
    weight: 20
type: docs
---

YugabyteDB Anywhere first needs to be installed on a host computer, and then you configure YugabyteDB Anywhere to work in your on-premises private cloud or in a public cloud environment. In a public cloud environment, YugabyteDB Anywhere spawns instances for starting a YugabyteDB universe. In a private cloud environment, you use YugabyteDB Anywhere to add nodes in which you want to be in the YugabyteDB universe. To manage these nodes, YugabyteDB Anywhere requires SSH access to each of the nodes.

## Supported Linux distributions

You can install YugabyteDB Anywhere on the following Linux distributions:

- Ubuntu 16.04, 18.04, or 20.04 LTS
- Red Hat Enterprise Linux (RHEL) 7.x
- Oracle Linux 7.x
- CentOS 7.x
- Amazon Linux (AMI) 2014.03, 2014.09, 2015.03, 2015.09, 2016.03, 2016.09, 2017.03, 2017.09, 2018.03, or 2.0
- Other [operating systems supported by Replicated](https://www.replicated.com/docs/distributing-an-application/supported-operating-systems/)

## Hardware requirements

A node running YugabyteDB Anywhere is expected to meet the following requirements:

- 4 cores (minimum) or 8 cores (recommended)
- 8 GB RAM (minimum) or 10 GB RAM (recommended)
- 100 GB SSD disk or more
- 64-bit CPU architecture

## Preparing the host

You prepare the host as follows:

- For a Docker-based installation, YugabyteDB Anywhere uses [Replicated scheduler](https://www.replicated.com/) for software distribution and container management. You need to ensure that the host can pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).

  Replicated installs a compatible Docker version if its not pre-installed on the host. The current supported Docker version is 20.10.n.

- For a Kubernetes-based installation, you need to ensure that the host can pull container images from the [Quay.io](https://quay.io/) container registry.

### Airgapped hosts

Installing YugabyteDB Anywhere on Airgapped hosts, without access to any Internet traffic (inbound or outbound) requires the following:

- Whitelisting endpoints: to install Replicated and YugabyteDB Anywhere on a host with no Internet connectivity, you have to first download the binaries on a computer that has Internet connectivity, and then copy the files over to the appropriate host. In case of restricted connectivity, the following endpoints have to be whitelisted to ensure that they are accessible from the host marked for installation:
  `https://downloads.yugabyte.com`
  `https://download.docker.com`

- Ensuring that Docker Engine version 20.10.n is available. If it is not installed, you need to follow the procedure described in [Installing Docker in airgapped](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/).
- Ensuring that the following ports are open on the YugabyteDB Anywhere host:
  - `8800` – HTTP access to the Replicated UI
  - `80` – HTTP access to the YugabyteDB Anywhere UI
  - `22` – SSH
- Ensuring that attached disk storage (such as persistent EBS volumes on AWS) is 100 GB minimum
- Having YugabyteDB Anywhere airgapped install package. Contact Yugabyte Support for more information.
- Signing the Yugabyte license agreement. Contact Yugabyte Support for more information.
