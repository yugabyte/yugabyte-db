---
title: FAQs about YugabyteDB Anywhere
headerTitle: YugabyteDB Anywhere FAQ
linkTitle: YugabyteDB Anywhere FAQ
description: Answers to common questions about YugabyteDB Anywhere.
aliases:
  - /stable/faq/enterprise-edition/
menu:
  stable_faq:
    identifier: faq-yugabyte-platform
    parent: faq
    weight: 20
type: docs
unversioned: true
rightNav:
  hideH3: true
  hideH4: true
---

## YugabyteDB Anywhere

### What is YugabyteDB Anywhere?

YugabyteDB Anywhere (previously known as Yugabyte Platform and YugaWare) is a private database-as-a-service, used to create and manage YugabyteDB universes and clusters. YugabyteDB Anywhere can be used to deploy YugabyteDB in any public or private cloud.

You deploy and manage your YugabyteDB universes using the YugabyteDB Anywhere UI.

{{<lead link="../../yugabyte-platform/yba-overview/">}}
Introduction to YugabyteDB Anywhere
{{</lead>}}

### How do I report a security vulnerability?

Follow the steps in the [vulnerability disclosure policy](/stable/secure/vulnerability-disclosure-policy) to report a vulnerability to our security team. The policy outlines our commitments to you when you disclose a potential vulnerability, the reporting process, and how Yugabyte will respond.

## Installation

<!--### How are the build artifacts packaged and stored for YugabyteDB Anywhere?

YugabyteDB Anywhere software is packaged as a set of Docker container images hosted on the [Quay.io](https://quay.io/) container registry and managed by the [Replicated](https://www.replicated.com/) management tool. Replicated ensures that YugabyteDB Anywhere remains highly available, and allows for instant upgrades by pulling the incremental container images associated with a newer YugabyteDB Anywhere release. If the host running the YugabyteDB Anywhere UI does not have the Internet connectivity, a fully air-gapped installation option is also available.

The data node (YugabyteDB) software is packaged into the YugabyteDB Anywhere application.-->

### How does YugabyteDB Anywhere installation work?

YugabyteDB Anywhere first needs to be installed on a machine. The next step is to configure YugabyteDB Anywhere to work with public and/or private clouds. In the case of public clouds, YugabyteDB Anywhere spawns the machines to orchestrate bringing up the data platform. In the case of private clouds, you add the nodes you want to be a part of the data platform into YugabyteDB Anywhere.

You install YugabyteDB Anywhere using a standalone installer that you download from Yugabyte.

{{< note title="Replicated end of life" >}}

YugabyteDB Anywhere was previously installed using Replicated. However, YugabyteDB Anywhere ended support for Replicated installation at the end of 2024. You can migrate existing Replicated YugabyteDB Anywhere installations using YBA Installer. See [Migrate from Replicated](/v2.20/yugabyte-platform/install-yugabyte-platform/migrate-replicated/).

{{< /note >}}

YugabyteDB Anywhere distributes and installs YugabyteDB on the hosts identified to run the data nodes. Because the YugabyteDB software is already packaged into existing artifacts, the data node does not require any Internet connectivity.

{{<lead link="../../yugabyte-platform/install-yugabyte-platform/">}}
Install YugabyteDB Anywhere
{{</lead>}}

### What are the OS requirements and permissions to run YugabyteDB Anywhere?

For a list of operating systems supported by YugabyteDB Anywhere, see [Operating system support](../../reference/configuration/operating-systems/). YugabyteDB Anywhere doesn't support ARM architectures (but can be used to deploy universes to ARM-based nodes).

YugabyteDB Anywhere also requires the following:

- Connectivity to the Internet, either directly or via an HTTP proxy.
- The following ports open on the platform host: 443 (HTTPS access to the YugabyteDB Anywhere UI), 9090 (Prometheus metrics); and the ability to connect from the YugabyteDB Anywhere host to all YugabyteDB data nodes.
- Attached disk storage (such as persistent EBS volumes on AWS).
- A YugabyteDB Anywhere license file from Yugabyte Support.

{{<lead link="../../yugabyte-platform/prepare/networking/">}}
For a complete list of networking requirements, see [Networking](../../yugabyte-platform/prepare/networking/).
{{</lead>}}

{{<lead link="../../yugabyte-platform/prepare/server-yba/">}}
For a complete list of hardware and software prerequisites, see [Prerequisites](../../yugabyte-platform/prepare/server-yba/).
{{</lead>}}

## Data nodes

### What are the requirements to run YugabyteDB data nodes?

{{<lead link="../../yugabyte-platform/prepare/server-nodes-hardware/">}}
For hardware prerequisites for YugabyteDB data nodes, refer to [Hardware prerequisites](../../yugabyte-platform/prepare/server-nodes-hardware/).
{{</lead>}}

{{<lead link="../../yugabyte-platform/prepare/server-nodes-software/">}}
For software prerequisites for YugabyteDB data nodes, refer to [Software prerequisites](../../yugabyte-platform/prepare/server-nodes-software/).
{{</lead>}}

### How does the YugabyteDB Anywhere UI interact with YugabyteDB data nodes?

YugabyteDB Anywhere communicates with nodes using a service installed on each node called the [YugabyteDB Anywhere node agent](#what-is-a-node-agent). The node agent is an RPC service, allowing YugabyteDB Anywhere to interact with data nodes without the need for SSH.

For universes deployed using earlier versions of YugabyteDB Anywhere (that is, prior to the introduction of node agent), YugabyteDB Anywhere creates a passwordless SSH connection to interact with the data nodes.

### Can I access the database machines that get spawned in public clouds?

Yes, you have access to all machines spawned by YugabyteDB Anywhere. YugabyteDB Anywhere runs on your machine in your region/data center.

If you have configured YugabyteDB Anywhere to work with any public cloud (such as AWS or GCP), it will spawn YugabyteDB nodes using your credentials. These machines run in your account, but are created and managed by YugabyteDB Anywhere on your behalf. You can log on to these machines any time. The YugabyteDB Anywhere UI additionally displays metrics per node and per universe.

### How many machines do I need to try out YugabyteDB Anywhere against my load?

You need the following:

- One server to install YugabyteDB Anywhere on.
- A minimum number of servers for the data nodes as determined by the [replication factor](../../architecture/key-concepts/#replication-factor-rf) (RF). For example, one server for RF=1, and 3 servers in case of RF=3.
- A server to run the load tests on.

Typically, you can saturate a database server (or three in case of RF=3) with just one large enough test machine running a synthetic load tester that has a light usage pattern. YugabyteDB ships with some synthetic load-testers, which can simulate a few different workloads. For example, one load tester simulates a time series or IoT-style workload and another does a stock-ticker like workload. But if you have a load tester that emulates your planned usage pattern, you can use that.

### Can I control the properties (such as VPC, IOPS, tenancy, and so on) of the machines YugabyteDB Anywhere spins up?

Yes, you can control what YugabyteDB Anywhere is spinning up. For example:

- You can choose if YugabyteDB Anywhere should spawn a new VPC with peering to the VPC on which application servers are running (to isolate the database machines into a separate VPC) AWS, or ask it to reuse an existing VPC.

- You can choose dedicated IOPS EBS drives on AWS and specify the number of dedicated IOPS you need.

YugabyteDB Anywhere also allows creating these machines out of band and importing them as an on-premises install.

## Node agent

### What is a node agent?

Node agent is an RPC service running on a YugabyteDB node, and is used to manage communication between YugabyteDB Anywhere and the nodes in universes. It includes the following functions:

- Invoke shell commands directly on the remote host, similar to running commands over SSH. Like SSH, the agent also does shell-login such that any previous command modifying the environment in the resource files (for example, `~/.bashrc`) gets reflected in the subsequent command.
- Additionally, for on-premises (manually provisioned nodes) deployments, node agent also functions as a utility to run preflight checks, and add node instances.

### How is node agent installed on a YugabyteDB node?

Installation depends on how you choose to prepare the nodes.

#### Run the provisioning script

For [on-premises providers](../../yugabyte-platform/yba-overview/#provider-configurations), after creating VMs and installing a supported Linux operating system and additional software (such as Python) (see [Software requirements for nodes](../../yugabyte-platform/prepare/server-nodes-software/)), you download the YugabyteDB Anywhere node agent package to the VM, modify the configuration file, and run the included script (node-agent-provision.sh) as root or via sudo.

This process prepares the node for YugabyteDB, including installation of node agent on the node.

If you have already installed YugabyteDB Anywhere and it is running, the script can additionally create (or update) an on-premises provider with the node already added.

{{<lead link="/stable/yugabyte-platform/prepare/server-nodes-software/software-on-prem/">}}
Automatically provision on-premises nodes
{{</lead>}}

#### Legacy provisioning

In addition, for on-premises providers, there are three legacy methods (now deprecated) for preparing a node, depending on the level of access provided to YugabyteDB Anywhere, as follows:

- _Automatic provisioning_, where an SSH user with sudo access for the node is provided to YugabyteDB Anywhere (for example, the `ec2-user` for an AWS EC2 instance).

- _Assisted manual provisioning_, where YugabyteDB Anywhere doesn't have access to an SSH user with sudo access, but you can run a script (provision_instance.py) interactively in YugabyteDB Anywhere, providing parameters for credentials for the SSH user with sudo access.

- _Fully manual provisioning_, where neither you nor YugabyteDB Anywhere has access to an SSH user with sudo access. In this case, only a local (non-SSH) user with sudo access is available, and you must follow a series of manual steps to provision the node.

For cloud (AWS, GCP, and Azure) and automatic on-premises (sudo access provided) providers, node agents are automatically installed on each universe node during provisioning using SSH. After the node agent is installed, all the legacy SSH calls are replaced with node agent calls. Just like an SSH daemon, node agent is run as a root user to perform provisioning of the nodes. After node provisioning, you can revoke the SSH access, but it's recommended to retain access for debugging.

For manually provisioned on-premises providers, node agent is installed on each node either using a script or manually as part of the manual provisioning process.

{{<lead link="/stable/yugabyte-platform/prepare/server-nodes-software/software-on-prem-legacy">}}
Legacy provisioning
{{</lead>}}

### Why does YugabyteDB Anywhere prompt for SSH details if the node agent is installed manually and can replace SSH?

When creating an on-premises provider, you are prompted to provide SSH credentials, which are used during legacy provisioning. After provisioning and adding the instances to the provider (including installing the node agent), YugabyteDB Anywhere no longer requires SSH or sudo access to nodes.

If you are manually provisioning nodes, these credentials aren't needed to provision nodes.

However, SSH keys are still required to connect to the node for debugging purposes (by navigating to **universe > Nodes > Actions > Connect** for example).

If you don't want to provide YugabyteDB Anywhere with an SSH key for a manually provisioned on-premises provider (because you can log in to the nodes over SSH for debugging outside of YugabyteDB Anywhere), then you can provide a dummy SSH key in the provider configuration.

### What is registration and unregistration of node agent?

Node agent is a secure service that authenticates every remote call from YugabyteDB Anywhere. Registration is the process of:

- making YugabyteDB Anywhere aware of the node agent service; and
- establishing trust between the node agent and YugabyteDB Anywhere.

No provider-level details are needed for registration.

Unregistration is the process of removing the node agent entry from YugabyteDB Anywhere such that there is no further communication. In effect, unregistration makes YugabyteDB Anywhere and the node forget each other.

### Why does node agent installation ask for provider and other details during legacy on-premises manual node agent setup?

Node agent is used to run preflight checks on the node during various day-0 and day-2 operations. These checks need information like the non-root user's home directory, expected port number for Prometheus Node Exporter, the NTP servers, and so on. These are attributes configured with a provider. As a result, to run these preflight checks the node agent needs to be configured to a provider in YugabyteDB Anywhere, and these details are needed to make the node agent aware of the YugabyteDB Anywhere provider which the node will become a part of.

### How do I move a node provisioned for one provider to a different provider?

In v2.18.6 and later, moving a node from one provider to another does not require unregistering the node agent, as node agents aren't linked to providers.

{{<lead link="../../yugabyte-platform/prepare/server-nodes-software/software-on-prem-manual/#reconfigure-a-node-agent">}}
To change the provider of a node, follow the procedure in [Reconfigure a node agent](../../yugabyte-platform/prepare/server-nodes-software/software-on-prem-manual/#reconfigure-a-node-agent).
{{</lead>}}

As long as the IP does not change, the node agent does not try to register again.

Note that first removing the node instance from the provider is very important, because after the provider configuration information changes in the configuration file as part of running the `config` command, the node agent will no longer be able to find the node to delete it, as the scope is always with the current provider and availability zone.

### How does a node agent perform preflight checks?

Prior to adding a node that you have provisioned as an instance to your provider, you run a preflight check to determine if the node satisfies the requirements for YugabyteDB.

A node agent does the following when the preflight check command is run:

1. Runs the [preflight_check](https://github.com/yugabyte/yugabyte-db/blob/master/managed/node-agent/resources/preflight_check.sh) script.
1. Collects the output and converts it to a well-formatted JSON payload.
1. Sends the payload to YugabyteDB Anywhere for validation. YugabyteDB Anywhere has preflight check threshold values defined in the [runtime configuration settings](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) of the provider. The prefix path for the configuration key name is `yb.node_agent.preflight_checks`. The values can be changed if needed.

### How do I disable node agent?

You can disable node agents of a provider's universes any time by setting the `yb.node_agent.client.enabled` [Provider Runtime Configuration](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) for the provider to false.

This disables all node agents for universes created using the provider, and YugabyteDB Anywhere falls back to using SSH to communicate with universe nodes, using credentials provided during provider creation.

Note that in future versions of YugabyteDB Anywhere, node agent must be enabled on all nodes managed by YugabyteDB Anywhere in order to upgrade YugabyteDB Anywhere.

### How do I change the node agent default port of 9070?

Use the `yb.node_agent.server.port` [Provider Runtime Configuration](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/).

The change is reflected only on newly created node agents.

<!--
### Is it okay to manually edit the configuration file for node agent?

It is not recommended to do so because editing the file can interfere with the self-upgrade workflow. If it is a minor change, it is better to stop the node agent service first to keep YugabyteDB Anywhere away from starting the upgrade process.
-->

### How does YugabyteDB Anywhere determine that a node instance record maps to a node agent entry if they are not related?

YugabyteDB Anywhere uses the IP address to identify a node instance. The IP can be a DNS from YugabyteDB Anywhere v2.18.5 and later.

For YugabyteDB Anywhere versions 2.18.6 or 2.20.2, a bind address that defaults to the IP will be added in case a DNS is supplied and the node agent has to listen to a specific interface IP.

### How does YugabyteDB Anywhere clean up node agents?

For providers where node agents are installed automatically (that is, all providers except on-premises with manual provisioning), YugabyteDB Anywhere removes the node agent entry when it releases the node back to the node instances pool of the provider. You can choose to leave the node agent service running, but when the node is again re-used, the node agent is re-installed.
