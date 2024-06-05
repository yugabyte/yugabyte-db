---
title: Overview of YugabyteDB Anywhere software
headerTitle: Introduction to YugabyteDB Anywhere
linkTitle: Introduction
description: YugabyteDB Anywhere architecture and introduction.
headcontent: YugabyteDB Anywhere from 50,000 ft
menu:
  stable_yugabyte-platform:
    identifier: yba-overview
    parent: yugabytedb-anywhere
    weight: 600
type: docs
---

YugabyteDB Anywhere (YBA) is a self-managed database-as-a-service that allows you to deploy and operate YugabyteDB database clusters (also known as universes) at scale.

In YBA, a database cluster is called a [universe](../../architecture/key-concepts/#universe), and the terms are used interchangeably. More precisely, a universe in YBA always consists of one (and only one) primary cluster, and can optionally also include a single [read replica](../../architecture/docdb-replication/read-replicas/) cluster attached to the primary cluster.

## Features

You can use YBA to deploy YugabyteDB database clusters that will tolerate single node failures, multiple node failures, single availability zone failures, multiple availability zone failures, single region failures, cloud provider failures, and more.

YugabyteDB Anywhere also supports [xCluster](../../architecture/docdb-replication/async-replication/) deployments (including for disaster recovery), where two YugabyteDB universes are deployed, and data is replicated asynchronously between them.

YBA supports these deployments in the following environments:

- On-premises - YBA can deploy YugabyteDB on VMs or bare metal hosts running various flavors of Linux, with the flexibility required to accommodate organizational security and compliance needs.
- Public clouds - YBA can deploy cloud-native YugabyteDB clusters in AWS, Azure, and GCP. YBA understands the native instance types, volume types, availability zones, regions, and OS image availability on each cloud, and maps them seamlessly to YugabyteDB's fault tolerance and performance configurations.
- Kubernetes - YBA can deploy YugabyteDB in Kubernetes clusters and both map the zones available in a single Kubernetes cluster, and map multiple regions across different Kubernetes clusters to YugabyteDB's fault tolerance capabilities.

### Additional features

YBA supports the following additional features:

- Encryption in transit, with support for CA and self-signed certificates.
- Encryption at rest, with integration with major Key Management Services (KMS), including AWS, GCP, Azure, and Hashicorp Vault.
- Scheduled backups to cloud native storage such as AWS S3, Google GCS, and Azure Storage, as well as to vanilla NFS storage.
- Alerting and monitoring, using [Prometheus](https://prometheus.io).
- Integration with LDAP and OIDC for authentication.
- High availability configuration for fast recovery of YBA in case of an outage.

### Day 2 management

YBA supports common Day 2 management operations, including the following:

- Online scale in and out with no disruptions to existing database clients.
- Vertical online scale up and down.
- Online database software upgrades and configuration changes.
- Online OS patching.
- Incremental and full backups, and point-in-time recovery.
- Rotation of CA and server certificates for encryption in transit.
- Rotation of KMS keys used for database encryption at rest.
- Monitoring and alerting of database health.
- Monitoring performance and metrics, including finding slow queries and getting insights on performance tuning using [Performance Advisor](../alerts-monitoring/performance-advisor/).

YBA operations can be performed through a GUI, or via automation tools that use its exposed REST APIs or Terraform provider.

## YugabyteDB Anywhere architecture

YBA runs on standalone VMs or Kubernetes pods. YBA can in turn be used to deploy YugabyteDB database clusters on either VMs or Kubernetes pods.

![YugabyteDB Architecture](/images/yb-platform/prepare/yba-architecture.png)

A VM that is part of a YugabyteDB cluster runs multiple processes, including the [YB-Master](../../architecture/yb-master/) and [YB-TServer](../../architecture/yb-tserver/) services of the cluster, and other secondary agents, including the [node agent](../../faq/yugabyte-platform/#node-agent-1), YB-Controller backup agent, and [Prometheus Node Exporter](https://prometheus.io/docs/guides/node-exporter/) for host metrics export. The YB-Master and YB-TServer services can be deployed in the same VM or, for better isolation, in separate dedicated VMs. On Kubernetes clusters, the YB-Master and YB-TServer services always run in isolated pods. For more information on the architecture of YugabyteDB, see [YugabyteDB Architecture](../../architecture/).

YBA requires network connectivity to the YugabyteDB database clusters to perform day 2 operations and monitoring. And, of course, in any cluster, database cluster nodes require connectivity to each other. Network connectivity to the public Internet is optional: YBA supports both Internet-connected and air-gapped deployments.

YBA stores cloud provider metadata and other cluster metadata in its own embedded database. YBA also uses a local Prometheus instance to scrape and store metrics from YugabyteDB clusters.

To ensure fault tolerance of YBA itself, YBA can be configured in [High Availability](../administer-yugabyte-platform/high-availability/) mode. In this setup, the primary YBA instance is synchronized with one or more standby instances to provide quick failover in case of an outage.

## Provider configurations

A provider configuration (also referred to as provider) describes your cloud. For example, the regions and zones that you will allow YugabyteDB to be deployed to. Additionally, in the case of public IaaS clouds, the service account YBA should use to create servers/VMs in your cloud.

YBA uses the cloud configuration information in a provider to deploy and manage universes.

YBA supports three major types of provider configurations:

1. On-premises.
1. Public Cloud (AWS, GCP, or Azure).
1. Kubernetes (for example, VMware Tanzu, Red Hat OpenShift, or Managed Kubernetes Service).

| | On-premises | Cloud | Kubernetes |
| :--- | :--- | :--- | :--- |
| Advantages | Maximum flexibility | Maximum automation | It's&nbsp;Kubernetes |
| Platforms | Private cloud, bare metal,<br>AWS, Azure, GCP | AWS, Azure, GCP | Kubernetes |
| Permissions for YBA | Minimal sudo access during provisioning | Cloud and OS permissions | As required for Kubernetes |
| Node&nbsp;provisioning | Manually created, with fully manual to automatic provisioning | Automatically created and provisioned | Via Helm |

### Public cloud

If you are deploying a universe to a public cloud (AWS, Azure, or GCP) and want maximum automation when managing clusters (creating them, scaling them, patching the OS, and so on), use a public cloud provider configuration. This approach does require that you provide YBA with cloud and OS privileges.

For example:

- YBA must be given privileges to create a VM.
- YBA must initially have root-level SSH login access to that VM for initial installation of a management agent. After installation of the agent, SSH access can be removed.

This approach allows for maximum automation. Also, you do have the option to specify a custom OS image that otherwise meets all other enterprise security rules.

### On-premises

For all other cases, use an on-premises provider. Unlike the Kubernetes and cloud providers, where YBA has privileges and creates the VMs (or pods) automatically, with an on-premises provider *you* create the server VMs manually.

Use this option for any of the following situations:

- You are deploying YugabyteDB clusters truly on-premises (for example, on VMware or Nutanix).
- You are deploying YugabyteDB clusters to public cloud, but (due to security policies or other restrictions) you can't provide YBA with cloud permissions or SSH access to cloud VMs. In this case, you must create your VMs and/or deploy your Linux OS manually in the cloud.
- You are deploying a single [stretched cluster across multiple clouds](../create-deployments/create-universe-multi-cloud/) (for example, one cluster with some nodes on AWS, others on GCP, and/or others on Azure).
- Any other cases where you must retain control over creating the VMs and/or Linux OS, and can't give this control to YBA.

With the on-premises provider, after creating VMs manually (that is, outside of YBA), you will add them to your on premises provider's free pool of servers. Subsequently, when creating the universe, database nodes are taken from the on-premises provider's free pool of servers and added to the universe.

### Kubernetes

If you are deploying a universe to Kubernetes, use a Kubernetes provider.
