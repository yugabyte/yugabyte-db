---
title: Review the system requirements
headerTitle: Review the system requirements
linkTitle: Review system requirements
description: System requirements for Yugabyte Platform.
menu:
  latest:
    parent: plan-yugabyte-platform
    identifier: review-system-reqs-yp
    weight: 623
type: page
isTocNested: true
showAsideToc: true
---

The Yugabyte Platform (aka YugaWare) first needs to be installed on a host machine. Then you need to configure the Yugabyte Platform to work in your on-premises, private cloud, or public cloud environment. In public clouds, Yugabyte Platform spawns the machines to orchestrate starting the YugabyteDB universe. In private clouds, you need to use the Yugabyte Platform to add nodes that you want to be in a YugabyteDB universe. To manage the nodes, Yugabyte Platform requires SSH access to each of the nodes.

To install Yugabyte Platform, you must meet the following requirements.

## Supported Linux distributions

Yugabyte Platform can be installed on the following Linux distributions:

- Ubuntu: 16.04 or later.
- Red Hat Enterprise Linux: 6.5 or later.
- CentOS: 7 or later.
- Amazon AMI: 2014.03, 2014.09, 2015.03, 2015.09, 2016.03, and 2016.09.
- Other [operating systems supported by Replicated](https://www.replicated.com/docs/distributing-an-application/supported-operating-systems/)

## Supported operating systems

Only Linux-based systems are supported by Replicated. The Linux operating system should be meet these requirements:

- Kernel: 3.10 or later
- 64-bit
- Ready to run Docker Engine, versions 1.7.1 to 17.06.2-ce (with 17.06.2-ce being the recommended version). 

## Access and privilege requirements

### Internet-connected hosts

To install Yugabyte Platform, you must have access and privileges:

- Connect to the Internet (directly or through an HTTP proxy).
- Install and configure [Docker Engine](https://docs.docker.com/engine/).
- Install and configure [Replicated](https://www.replicated.com/), which is a containerized application itself and needs to pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).
- Pull Yugabyte container images from [Quay.io](https://quay.io/) container registry. Required images are pulled automatically by Replicated.

## Airgapped hosts

To install Yugabyte Platform on Airgapped hosts, without access to any Internet traffic (inbound or outbound), you must have access and privileges to:

- Install and configure [Docker Engine](https://docs.docker.com/engine/).
- Install and configure [Replicated](https://www.replicated.com/), which is a containerized application itself and needs to pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).
- Pull Yugabyte container images from [Quay.io](https://quay.io/) container registry. Required images are pulled automatically by Replicated.
- Install using `sudo`.
- Docker Engine: supported versions `1.7.1` to `17.03.1-ce`. If not installed, see [Installing Docker in airgapped]](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/).
- The following ports should be open on the Yugabyte Platform host:
  - `8800` – HTTP access to the Replicated UI
  - `80` – HTTP access to the YugabyteDB Admin Console)
  - `22` – SSH
- Attached disk storage (such as persistent EBS volumes on AWS): 100 GB minimum
- A Yugabyte Platform license file (attached to your welcome email from Yugabyte Support)
- Ability to connect from the Yugabyte Platform host to all YugabyteDB data nodes. If this is not set up, [setup passwordless ssh](#step-5-troubleshoot-yugaware).



-----------

???

From requirements for creating a Yugabyte Platform VM:

OS Requirements: Ubuntu 16.04 / 18.04 LTS, CentOS or RHEL 7.x
Connectivity to the Internet, either directly or via a http proxy. If no connectivity follow “airgapped” instructions
Ability to connect from the Yugabyte Platform host to all YugabyteDB data nodes via ssh. If not setup passwordless ssh
Ability to install and configure docker
Ability to install and configure Replicated, which is a containerized application itself and needs to pull containers from its own Replicated.com container registry
Ability to pull Yugabyte container images from Quay.io  container registry, this will be done by Replicated automatically
Open ports 22, 880, and 80