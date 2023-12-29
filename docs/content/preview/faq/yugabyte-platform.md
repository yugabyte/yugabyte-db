---
title: FAQs about YugabyteDB Anywhere
headerTitle: YugabyteDB Anywhere FAQ
linkTitle: YugabyteDB Anywhere FAQ
description: Answers to common questions about YugabyteDB Anywhere.
aliases:
  - /preview/faq/enterprise-edition/
menu:
  preview_faq:
    identifier: faq-yugabyte-platform
    parent: faq
    weight: 50
type: docs
rightNav:
  hideH3: true
  hideH4: true
---

### Contents

##### YugabyteDB Anywhere

- [What is YugabyteDB Anywhere?](#what-is-yugabytedb-anywhere)
- [How do I report a security vulnerability?](#how-do-i-report-a-security-vulnerability)

##### Installation

- [How are the build artifacts packaged and stored for YugabyteDB Anywhere?](#how-are-the-build-artifacts-packaged-and-stored-for-yugabytedb-anywhere)
- [How does YugabyteDB Anywhere installation work?](#how-does-yugabytedb-anywhere-installation-work)
- [What are the OS requirements and permissions to run YugabyteDB Anywhere?](#what-are-the-os-requirements-and-permissions-to-run-yugabytedb-anywhere)

##### Data nodes

- [What are the requirements to run YugabyteDB data nodes?](#what-are-the-requirements-to-run-yugabytedb-data-nodes)
- [How does the YugabyteDB Anywhere UI interact with YugabyteDB data nodes?](#how-does-the-yugabytedb-anywhere-ui-interact-with-yugabytedb-data-nodes)
- [Can I access the database machines that get spawned in public clouds?](#can-i-access-the-database-machines-that-get-spawned-in-public-clouds)
- [How many machines do I need to try out YugabyteDB Anywhere against my load?](#how-many-machines-do-i-need-to-try-out-yugabytedb-anywhere-against-my-load)
- [Can I control the properties (such as VPC, IOPS, tenancy, and so on) of the machines YugabyteDB Anywhere spins up?](#can-i-control-the-properties-such-as-vpc-iops-tenancy-and-so-on-of-the-machines-yugabytedb-anywhere-spins-up)

## YugabyteDB Anywhere

### What is YugabyteDB Anywhere?

YugabyteDB Anywhere (previously known as Yugabyte Platform and YugaWare) is a private database-as-a-service, used to create and manage YugabyteDB universes and clusters. YugabyteDB Anywhere can be used to deploy YugabyteDB in any public or private cloud.

You deploy and manage your YugabyteDB universes using the YugabyteDB Anywhere UI.

See also YugabyteDB Anywhere at [yugabyte.com](https://www.yugabyte.com/platform/).

### How do I report a security vulnerability?

Follow the steps in the [vulnerability disclosure policy](/preview/secure/vulnerability-disclosure-policy) to report a vulnerability to our security team. The policy outlines our commitments to you when you disclose a potential vulnerability, the reporting process, and how Yugabyte will respond.

## Installation

<!--### How are the build artifacts packaged and stored for YugabyteDB Anywhere?

YugabyteDB Anywhere software is packaged as a set of Docker container images hosted on the [Quay.io](https://quay.io/) container registry and managed by the [Replicated](https://www.replicated.com/) management tool. Replicated ensures that YugabyteDB Anywhere remains highly available, and allows for instant upgrades by pulling the incremental container images associated with a newer YugabyteDB Anywhere release. If the host running the YugabyteDB Anywhere UI does not have the Internet connectivity, a fully air-gapped installation option is also available.

The data node (YugabyteDB) software is packaged into the YugabyteDB Anywhere application.-->

### How does YugabyteDB Anywhere installation work?

YugabyteDB Anywhere first needs to be installed on a machine. The next step is to configure YugabyteDB Anywhere to work with public and/or private clouds. In the case of public clouds, YugabyteDB Anywhere spawns the machines to orchestrate bringing up the data platform. In the case of private clouds, you add the nodes you want to be a part of the data platform into YugabyteDB Anywhere.

You install YugabyteDB Anywhere using a standalone installer that you download from Yugabyte.

{{< note title="Replicated end of life" >}}

YugabyteDB Anywhere was previously installed using Replicated. However, YugabyteDB Anywhere will end support for Replicated installation at the end of 2024. You can migrate existing Replicated YugabyteDB Anywhere installations using YBA Installer. See [Migrate from Replicated](../../yugabyte-platform/install-yugabyte-platform/install-software/installer/#migrate-from-replicated).

{{< /note >}}

YugabyteDB Anywhere distributes and installs YugabyteDB on the hosts identified to run the data nodes. Because the YugabyteDB software is already packaged into existing artifacts, the data node does not require any Internet connectivity.

For instructions on installing YugabyteDB Anywhere, refer to [Install YugabyteDB Anywhere](../../yugabyte-platform/install-yugabyte-platform/).

### What are the OS requirements and permissions to run YugabyteDB Anywhere?

YugabyteDB Anywhere runs on Linux-based operating systems. For a list of supported operating systems, see [YBA prerequisites](../../yugabyte-platform/install-yugabyte-platform/prerequisites/).

<!--The Linux OS should be 3.10+ kernel, 64-bit, and ready to run docker-engine 1.7.1 - 17.06.2-ce (with 17.06.2-ce being the recommended version).

For a complete list of operating systems supported by Replicated, see [Supported Operating Systems](https://help.replicated.com/docs/native/customer-installations/supported-operating-systems/).-->

{{< note title="Note" >}}

For a list of operating systems supported by YugabyteDB, see the [Deployment checklist](../../../deploy/checklist/) for YugabyteDB.

{{< /note >}}

YugabyteDB Anywhere also requires the following:

- Connectivity to the Internet, either directly or via an HTTP proxy.
<!-- Ability to install and configure [docker-engine](https://docs.docker.com/engine/).
- Ability to install and configure [Replicated](https://www.replicated.com/install-options/), which is a containerized application itself and needs to pull containers from its own [Replicated.com container registry](https://help.replicated.com/docs/native/getting-started/docker-registries/).
- Ability to pull Yugabyte container images from the [Quay.io](https://quay.io/) container registry (this will be done by Replicated automatically).-->
- The following ports open on the YugabyteDB Anywhere host: 443 (HTTP access to the YugabyteDB Anywhere UI), 22 (SSH).
- Attached disk storage (such as persistent EBS volumes on AWS): 100 GB SSD minimum.
- A YugabyteDB Anywhere license file from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form).
- Ability to connect from the YugabyteDB Anywhere host to all YugabyteDB data nodes via SSH.

For a complete list of prerequisites, refer to [YBA prerequisites](../../yugabyte-platform/install-yugabyte-platform/prerequisites/installer/).

## Data nodes

### What are the requirements to run YugabyteDB data nodes?

Prerequisites for YugabyteDB data nodes are listed in the YugabyteDB [Deployment checklist](../../../deploy/checklist/).

### How does the YugabyteDB Anywhere UI interact with YugabyteDB data nodes?

The YugabyteDB Anywhere UI creates a passwordless SSH connection to interact with the data nodes.

### Can I access the database machines that get spawned in public clouds?

Yes, you have access to all machines spawned. The machines are spawned by YugabyteDB Anywhere. YugabyteDB Anywhere runs on your machine in your region/data center. If you have configured YugabyteDB Anywhere to work with any public cloud (such as AWS or GCP), it will spawn YugabyteDB nodes using your credentials on your behalf. These machines run in your account, but are created and managed by YugabyteDB Anywhere on your behalf. You can log on to these machines any time. The YugabyteDB Anywhere UI additionally displays metrics per node and per universe.

### How many machines do I need to try out YugabyteDB Anywhere against my load?

You need the following:

- One server to install YugabyteDB Anywhere on.
- A minimum number of servers for the data nodes as determined by the replication factor (RF). For example, one server for RF=1, and 3 servers in case of RF=3.
- A server to run the load tests on.

Typically, you can saturate a database server (or three in case of RF=3) with just one large enough test machine running a synthetic load tester that has a light usage pattern. YugabyteDB ships with some synthetic load-testers, which can simulate a few different workloads. For example, one load tester simulates a time series or IoT-style workload and another does a stock-ticker like workload. But if you have a load tester that emulates your planned usage pattern, you can use that.

### Can I control the properties (such as VPC, IOPS, tenancy, and so on) of the machines YugabyteDB Anywhere spins up?

Yes, you can control what YugabyteDB Anywhere is spinning up. For example:

- You can choose if YugabyteDB Anywhere should spawn a new VPC with peering to the VPC on which application servers are running (to isolate the database machines into a separate VPC) AWS, or ask it to reuse an existing VPC.

- You can choose dedicated IOPs EBS drives on AWS and specify the number of dedicated IOPS you need.

YugabyteDB Anywhere also allows creating these machines out of band and importing them as an on-premises install.
