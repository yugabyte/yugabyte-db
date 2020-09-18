---
title: Requirements for installing Yugabyte Platform
headerTitle: Requirements for installing Yugabyte Platform
linkTitle: Yugabyte Platform requirements
description: Requirements for installing Yugabyte Platform.
menu:
  latest:
    parent: deploy
    identifier: reqs-data-node
    weight: 625
type: page
isTocNested: true
showAsideToc: true
---

The following requirements are Yugabyte Platform installations.

## Operating systems

Only Linux-based systems are supported by Replicated at this point. This Linux operating system should be

- Kernel: 3.10 or later
- 64-bit
- Ready to run docker-engine 1.7.1 - 17.06.2-ce (with 17.06.2-ce being the recommended version). 

Supported operating system versions include:

- Ubuntu: 16.04 or later.
- Red Hat Enterprise Linux: 6.5 or later.
- CentOS: 7 or later.
- Amazon AMI: 2014.03, 2014.09, 2015.03, 2015.09, 2016.03, and 2016.09.
- Other [operating systems supported by Replicated](https://www.replicated.com/docs/distributing-an-application/supported-operating-systems/)

## Access and privilege requirements for an Internet-connected host

To install Yugabyte Platform, you must have access and privileges:

- Connect to the Internet (directly or through an HTTP proxy).
- Install and configure [Docker Engine](https://docs.docker.com/engine/).
- Install and configure [Replicated](https://www.replicated.com/), which is a containerized application itself and needs to pull containers from the [Replicated Docker Registries](https://help.replicated.com/docs/native/getting-started/docker-registries/).
- Pull Yugabyte container images from [Quay.io](https://quay.io/) container registry. Required images are pulled automatically by Replicated.

## Access and privilege requirements for an airgapped host

Airgapped hosts, without access to any Internet traffic (inbound or outbound), must meet the following additional requirements:

- `sudo` user privileges for installation.
- Docker Engine: supported versions `1.7.1` to `17.03.1-ce`. If not installed, see [Installing Docker in airgapped]](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/).
- The following ports should be open on the YugaWare host:
  - `8800` – HTTP access to the Replicated UI
  - `80` – HTTP access to the YugabyteDB Admin Console)
  - `22` – SSH
- Attached disk storage (such as persistent EBS volumes on AWS): 100 GB minimum
- A Yugabyte Platform license file (attached to your welcome email from Yugabyte Support)
- Ability to connect from the Yugabyte Platform host to all YugabyteDB data nodes. If this is not set up, [setup passwordless ssh](#step-5-troubleshoot-yugaware).