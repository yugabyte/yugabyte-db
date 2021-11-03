---
title: FAQs about Yugabyte Platform
headerTitle: Yugabyte Platform FAQ
linkTitle: Yugabyte Platform FAQ
description: Answers to common questions about Yugabyte Platform.
aliases:
  - /latest/faq/enterprise-edition/
menu:
  latest:
    identifier: faq-yugabyte-platform
    parent: faq
    weight: 2750
isTocNested: false
showAsideToc: true
---

## What is Yugabyte Platform?

Yugabyte Platform (previously known as YugaWare) is a private database-as-a-service, used to create and manage YugabyteDB universes and clusters. Yugabyte Platform can be used to deploy YugabyteDB in any public or private cloud.

You deploy and manage your Yugabyte universes using the highly available Yugabyte Platform console.

See also Yugabyte Platform at [yugabyte.com](https://www.yugabyte.com/platform/).

## How are the build artifacts packaged and stored for Yugabyte Platform?

Yugabyte Platform software is packaged as a set of Docker container images hosted on the [Quay.io](https://quay.io/) container registry and managed by the [Replicated](https://www.replicated.com/) management tool. Replicated ensures that Yugabyte Platform remains highly available, and allows for instant upgrades by simply pulling the incremental container images associated with a newer platform release. If the host running the Yugabyte Platform console does not have Internet connectivity, a fully air-gapped installation option is also available.

The data node (YugabyteDB) software is packaged into the Yugabyte Platform application.

## How does Yugabyte Platform installation work?

Yugabyte Platform first needs to be installed on a machine. The next step is to configure Yugabyte Platform to work with public and/or private clouds. In the case of public clouds, Yugabyte Platform spawns the machines to orchestrate bringing up the data platform. In the case of private clouds, you add the nodes you want to be a part of the data platform into Yugabyte Platform. Yugabyte Platform needs SSH access into these nodes to manage them.

Installation of Yugabyte Platform starts with installing Replicated on a Linux host. Replicated installs the [docker-engine](https://docs.docker.com/engine/), the Docker container runtime, and then pulls its own container images from the [Replicated.com container registry](https://help.replicated.com/docs/native/getting-started/docker-registries/). Yugabyte Platform then becomes a managed application of Replicated, which starts by pulling the Yugabyte Platform (`yugaware`) container images from Quay.io for the very first time. Yugabyte Platform then distributes and installs YugabyteDB on the hosts identified to run the data nodes. Since the YugabyteDB software is already packaged into existing artifacts, the data node does not require any Internet connectivity.

For instructions on installing Yugabyte Platform, refer to [Install Yugabyte Platform](../../yugabyte-platform/install-yugabyte-platform/).

## What are the OS requirements and permissions to run Yugabyte Platform?

Yugabyte Platform requires Replicated; currently, Replicated only supports Linux-based operating systems. The Linux OS should be 3.10+ kernel, 64-bit, and ready to run docker-engine 1.7.1 - 17.06.2-ce (with 17.06.2-ce being the recommended version).

For a complete list of operating systems supported by Replicated, see [Supported Operating Systems](https://help.replicated.com/docs/native/customer-installations/supported-operating-systems/).

{{< note title="Note" >}}

This requirement applies only to Yugabyte Platform. For a list of OSs supported by YugabyteDB, see the [Deployment checklist](../../../deploy/checklist/) for YugabyteDB.

{{< /note >}}

Yugabyte Platform also requires the following:

- Connectivity to the Internet, either directly or via an HTTP proxy.
- Ability to install and configure [docker-engine](https://docs.docker.com/engine/).
- Ability to install and configure [Replicated](https://www.replicated.com/install-options/), which is a containerized application itself and needs to pull containers from its own [Replicated.com container registry](https://help.replicated.com/docs/native/getting-started/docker-registries/).
- Ability to pull Yugabyte container images from the [Quay.io](https://quay.io/) container registry (this will be done by Replicated automatically).
- The following ports open on the platform host: `8800` (replicated ui), `80` (http access to the Yugabyte Platform Console), `22` (ssh).
- Attached disk storage (such as persistent EBS volumes on AWS): 100 GB SSD minimum.
- A Yugabyte Platform license file from [Yugabyte](https://www.yugabyte.com/platform/#request-trial-form).
- Ability to connect from the Yugabyte Platform host to all YugabyteDB data nodes via SSH.

For a complete list of prerequisites, refer to [Prerequisites](../../yugabyte-platform/install-yugabyte-platform/prerequisites/).

## What are the requirements to run YugabyteDB data nodes?

Prerequisites for YugabyteDB data nodes are listed in the YugabyteDB [Deployment checklist](../../../deploy/checklist).

## How does the Yugabyte Platform console interact with YugabyteDB data nodes?

The Yugabyte Platform console creates a passwordless SSH connection to interact with the data nodes.

## Can I access the database machines that get spawned in public clouds?

Yes, you have access to all machines spawned. The machines are spawned by Yugabyte Platform. Yugabyte Platform runs on your machine in your region/data center. If you have configured Yugabyte Platform to work with any public cloud (such as AWS or GCP), it will spawn YugabyteDB nodes using your credentials on your behalf. These machines run in your account, but are created and managed by Yugabyte Platform on your behalf. You can log on to these machines any time. The Yugabyte Platform console additionally displays metrics per node and per universe.

## How many machines do I need to try out Yugabyte Platform against my load?

You need:  

- One server to install Yugabyte Platform on.
- A minimum number of servers for the data nodes as determined by the replication factor (RF). For example, one server for RF=1, and 3 servers in case of RF=3.
- A server to run the load tests on.

Typically, you can saturate a database server (or three in case of RF=3) with just one large enough test machine running a synthetic load tester that has a light usage pattern. YugabyteDB ships with some synthetic load-testers, which can simulate a few different workloads. For example, one load tester simulates a time series or IoT-style workload and another does a stock-ticker like workload. But if you have a load tester that emulates your planned usage pattern, you can use that.

## Can I control the properties (such as VPC, IOPS, tenancy etc.) of the machines Yugabyte Platform spins up?

Yes, you can control what Yugabyte Platform is spinning up. For example:

- You can choose if Yugabyte Platform should spawn a new VPC with peering to the VPC on which application servers are running (to isolate the database machines into a separate VPC) AWS, or ask it to reuse an existing VPC.

- You can choose dedicated IOPs EBS drives on AWS and specify the number of dedicated IOPS you need.

Yugabyte Platform also allows creating these machines out of band and importing these as an "on-premises" install.

## How do I report a security vulnerability?

Follow the steps in the [vulnerability disclosure policy](/latest/secure/vulnerability-disclosure-policy) to report a vulnerability to our security team. The policy outlines our commitments to you when you disclose a potential vulnerability, the reporting process, and how we will respond.
