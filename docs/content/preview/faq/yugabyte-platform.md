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
    weight: 20
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

For a list of operating systems supported by YugabyteDB Anywhere, see [Operating system support](../../reference/configuration/operating-systems/#yugabytedb-anywhere).

For Replicated-based installations, YugabyteDB Anywhere supports operating systems supported by Replicated. Replicated only supports Linux-based operating systems. The Linux OS should be 3.10+ kernel, 64-bit, and ready to run docker-engine 1.7.1 - 17.06.2-ce (with 17.06.2-ce being the recommended version). For a complete list of operating systems supported by Replicated, see [Supported Operating Systems](https://help.replicated.com/docs/native/customer-installations/supported-operating-systems/).

YugabyteDB Anywhere also requires the following:

- Connectivity to the Internet, either directly or via an HTTP proxy.
- The following ports open on the platform host: 443 (HTTPS access to the YugabyteDB Anywhere UI), 22 (SSH).
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

## Node Agent

### What is a node agent?

Node Agent is an RPC service running on a YugabyteDB node, and is used to manage communication between YugabyteDB Anywhere and the database node. It includes the following functions:

- Invoke shell commands directly on the remote host like running commands over SSH. Similar to SSH, the agent also does shell-login such that any previous command modifying the environment in the resource files (for example, ~/.bashrc) gets reflected in the subsequent command.
- Invoke procedures or methods on the node agent.
- For on-premesis (manual provisioned nodes), node agent functions as a utility to run preflight checks, and add a node instance.

### How is node agent installed on a YugabyteDB node?

Installation depends on how the nodes are provisioned. For cloud (AWS, GCP, and Azure) and non-manual on-prem nodes (sudo access provided), the agents are installed as a part of a universe task during pre-provisioning step using SSH. So, a sudo user with SSH access (for example, ec2-user for AWS) is needed. After the node agent is installed, all the legacy SSH calls are replaced with node agent calls. Just like SSH daemon, node agent is run as a root user to perform provisioning of the nodes. After node provisioning, you can revoke the SSH access, but it's recommended to enable the access for debugging.

For on-prem manual provisioning, an SSH user does not have sudo access but sudo is available on the local YugabyteDB node with a password.
Users do not want node-agent to be run as root.
Users can install node agent manually as described here https://docs.yugabyte.com/preview/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises-manual/#install-node-agent

It requires
Downloading the node-agent as yugabyte user
Run the installer as yugabyte user
Install Systemd service as a sudo user but it runs the node agent service as yugabyte user.
NOTE: Manual provisioning must be set to true in this case.

### Why does YBA UI ask for SSH if the node agent is installed manually and can replace SSH?

This is because of the legacy UI. There are some cases where an SSH key is needed for integrity but a ficticious non-working SSH key can be supplied.

### What is registration and unregistration in node agent?

Node agent is a secure service that performs authentication on every remote call from YBA. The service requires a set-up to make:

- YBA aware of the node agent service.
- the node agent trust YBA and vice-versa.
This step is called registration. Note that registration has nothing to do with a provider.

Unregistration is the step to disconnect YBA and a node agent from each other.

### Why does node agent installation ask for provider and other details during on-prem manual node agent setup?

Node agent registration internally does not need configuration values such as region, availability zone, and so on, but it still asks for them because of the additional preflight check capability that it provides. It stores the configuration details in a config file called `$NODE_AGENT_HOME/config/config.yml`, and the values are used when the preflight check is run, or the node is added to the node instance of the provider. This is convenient for you when you run the command as there is no need to type all the input values again.

### What happens if I want to change the provider settings of a node agent, or move it to another provider?

A node agent does not need to be unregistered to move a provider as it is not tied to a provider. Following are the steps to change the provider prior to YBA version 2.18.5:

1. Remove the node instance from the provider if it is already added.
1. Unregister the existing node agent (This step is only required prior to YBA v2.18.5).
1. Stop the `systemd` service.
1. Run the installation again, or the command `node-agent node configure`. Running the command is faster as it does not install node agent again.
1. Start the `systemd` service.

For YBA version 2.18.5 and later, do the following:

1. Remove the node instance from the provider if it is already added using the command `node-agent node delete-instance`.
1. Run the command `node-agent node configure`.

As long as the IP does not change, the node agent does not try to register again.

Note that the first step to change the provider is very important because after the provider configuration information changes in the config file as part of the second step, the node agent cannot find the node to delete it as the scope is always with the provider and availability zone.

### How does a node agent perform preflight checks?

A node agent utility (node-agent) does the following when the preflight-check command is run:

1. Runs the [preflight_check](https://github.com/yugabyte/yugabyte-db/blob/master/managed/node-agent/resources/preflight_check.sh) script.
1. Collects the output and converts it to a well-formatted JSON payload.
1. Sends the payload to YBA for validation. YBA has preflight check threshold values defined in the [runtime configuration](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) of the provider. The prefix path for the config key name is `yb.node_agent.preflight_checks`. The values can be changed if needed.

### How to disable a node agent?

A node agent can be disabled anytime at the [provider scope](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/). It defaults to SSH for remote communication. The config key name is `yb.node_agent.client.enabled`.

### How to change the node agent default port of 9070?

Use the global [runtime configuration](../../yugabyte-platform/administer-yugabyte-platform/manage-runtime-config/) key `yb.node_agent.server.port`. The change is reflected only on newly created node agents.

### Is it ok to manually edit the configuration file for node agent?

It is not recommended to do so because editing the file can interfere with the self-upgrade workflow. If it is a minor change, it is better to stop the node agent service first to keep YBA away from starting the upgrade process.

### How does YBA determine that a node instance record maps to a node agent entry if they are not related?

As mentioned above, a node agent registration has nothing to do with node instance entry. YBA uses the IP to identify. The IP can be a DNS from >= 2.18.5.

A bind address (defaults to the IP) is being added in > 2.18.5 in case DNS is supplied and node agent has to listen to a specific interface IP.

### How does YBA clean up node agents for on-prem NON-manual nodes?

YBA removes the node agent entry when the node is cleaned up. It can leave node agent service running but when the node is again re-used, node agent is re-installed.
