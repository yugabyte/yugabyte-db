---
title: Multi-cloud Universe Setup
headerTitle: Multi-cloud setup
linkTitle: Multi-cloud setup
description: Set up a universe that spans different clouds
headcontent: Set up a universe that spans different clouds
menu:
  preview:
    identifier: multicloud-universe-setup
    parent: build-multicloud-apps
    weight: 100
type: docs
---

You can create a multi-cloud YugabyteDB universe spanning multiple geographic regions and cloud providers. Let us over the various steps involved in setting up such a multi-cloud universe using the `yugabyted` and also with the on-prem provider in [YugabyteDB Anywhere](../../../yugabyte-platform/create-deployments/create-universe-multi-cloud/).

## Topology

For illustration, let us set up a 6-node universe across AWS-`us-west`, Google Cloud Provider-`us-central`, and Microsoft Azure- `us-east`.

![Multi-cloud Yugabyte](/images/develop/multicloud/multicloud-topology.png)

<!-- begin: nav tabs -->
{{<nav/tabs list="local,cloud,anywhere" active="local"/>}}

{{<nav/panels>}}
{{<nav/panel name="local" active="true">}}
<!-- BEGIN: local cluster setup instructions -->

## Set up VPC peering

{{<warning title="Remember">}}
Although for the current example, you **do not** have to set up VPC peering, for different clouds to be able to talk to each other, you need to set up multi-cloud VPC peering through a VPN tunnel.
{{</warning>}}

YugabyteDB requires a particular [set of firewall ports](../../../yugabyte-platform/install-yugabyte-platform/prepare-on-prem-nodes/#ports) to be opened for cluster operations.

You should use non-overlapping [Classless Inter-Domain Routing (CIDR)](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#Further_reading) blocks for each subnet across different clouds.

All public cloud providers enable VPN tunneling across VPCs and their subnets to enable network discovery. As an example, see [VPN between two clouds](https://medium.com/google-cloud/vpn-between-two-clouds-e2e3578be773).

## Set up the multi-cloud universe

We are going to set up a 6-node universe with 2 nodes each in AWS,GCP and Azure.

### First cloud - AWS

{{<setup/local
    numnodes="2"
    rf="3"
    basedir="/tmp/ydb-aws-"
    status="no"
    dataplacement="no"
    locations="aws.us-west-2.us-west-2a,aws.us-west-2.us-west-2b">}}

### Second cloud - GCP

{{<note title="Note">}} These nodes in GCP will join the cluster in AWS (127.0.0.1) {{</note>}}

{{<setup/local
    numnodes="2"
    rf="3"
    destroy="no"
    ips="127.0.0.3,127.0.0.4"
    masterip="127.0.0.1"
    basedir="/tmp/ydb-gcp-"
    dataplacement="no"
    status="no"
    locations="gcp.us-central-1.us-central-1a,gcp.us-central-1.us-central-1b">}}

### Third cloud - Azure

{{<note title="Note">}} These nodes in Azure will join the cluster in AWS (127.0.0.1) {{</note>}}

{{<setup/local
    numnodes="2"
    rf="3"
    destroy="no"
    ips="127.0.0.5,127.0.0.6"
    masterip="127.0.0.1"
    basedir="/tmp/ydb-azu-"
    dataplacement="yes"
    status="yes"
    locations="azu.us-east-1.us-east-1a,azu.us-east-1.us-east-1b">}}

## Destroy the universe

After exploring the multi-cloud setup locally, you can destroy the cluster using the following command

```bash
for d in {aws,gcp,azu}-{1,2} ; do ./bin/yugabyted destroy --base_dir=/tmp/ydb-${d} ; done
```

<!-- END: local cluster setup instructions -->
{{</nav/panel>}}
{{<nav/panel name="cloud">}} {{<setup/cloud>}} {{</nav/panel>}}
{{<nav/panel name="anywhere">}}
<!-- BEGIN: YBA cluster setup instructions -->

## Set up instance VMs

When you create a universe, you need to import nodes that can be managed by YugabyteDB Anywhere. To set up your nodes, follow the instructions in [Prepare nodes (on-premises)](../../../yugabyte-platform/install-yugabyte-platform/prepare-on-prem-nodes/).

Note the following:

* Your nodes across different cloud providers should be of similar configuration: vCPUs, DRAM, storage, and networking.
* For more information on ports used by YugabyteDB, refer to [Default ports](../../../reference/configuration/default-ports/).
* Ensure that your YugabyteDB nodes conform to the requirements outlined in the [deployment checklist](../../../deploy/checklist/), which gives an idea of [recommended instance types across public clouds](../../../deploy/checklist/#running-on-public-clouds).

## Set up VPC peering

You need to set up multi-cloud VPC peering through a VPN tunnel.

YugabyteDB requires a particular [set of firewall ports](../../install-yugabyte-platform/prepare-on-prem-nodes/#ports) to be opened for cluster operations.

You should use non-overlapping [Classless Inter-Domain Routing (CIDR)](https://en.wikipedia.org/wiki/Classless_Inter-Domain_Routing#Further_reading) blocks for each subnet across different clouds.

All public cloud providers enable VPN tunneling across VPCs and their subnets to enable network discovery. As an example, see [VPN between two clouds](https://medium.com/google-cloud/vpn-between-two-clouds-e2e3578be773).

## Configure the on-premises cloud provider

You can configure the on-premises cloud provider for YugabyteDB using YugabyteDB Anywhere. If no cloud providers are configured, the main **Dashboard** page highlights that you need to configure at least one cloud provider. Refer to [Configure the on-premises cloud provider](../../../yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/) for more information.

### Set up the cloud provider

You need to navigate to **Configs > On-Premises Datacenters**, click **Add Configuration**, and then select  **Provider Info**. Enter sample values into the following fields:

* **Provider Name** is `multi-cloud-demo`.
* **SSH User** is the user who will run Yugabyte on the node (`yugabyte` in this case).
* **SSH Port** should remain the default of `22` unless your servers have a different SSH port.
* **Manually Provision Nodes** should be disabled so that YugabyteDB Anywhere installs the software on these nodes.
* **SSH Key** is the contents of the private key file to be used for authentication.
  \
  Note that Paramiko is used for SSH validation, which typically does not accept keys generated with OpenSSL. If you generate your keys with OpenSSL, use a format similar to the following:

    ```sh
      ssh-keygen -m PEM -t rsa -b 2048 -f test_id_rsa
    ```

* **Air Gap Install** should only be enabled if your nodes do not have Internet connectivity.

  ![caption](/images/ee/multi-cloud-provider-info.png)

### Define an instance type

Select **Instance Types** and enter a machine description that matches the nodes you will be using. The machine type can be any logical name, given the machine types will be different between all three regions. The following example uses `8core`:

![Multi-cloud instance description](/images/ee/multi-cloud-instances.png)

### Define regions

Select **Regions and Zones** and define your regions, using descriptive names, as per the following illustration:

![Multi-cloud regions](/images/ee/multi-cloud-regions.png)

Click **Finish** to create your cloud provider. Once fully configured, the provider should look similar to the following:

![Multi-cloud provider map view](/images/ee/multi-cloud-provider-map.png)

### Provision instances

After you have defined your cloud provider configuration, click **Manage Instances** to provision as many nodes as your application requires. Follow the instructions provided in [Configure the on-premises cloud provider](../../../yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises/#add-yugabytedb-nodes).

The provider's instance list should be similar to the following:

![Multi-cloud instance list](/images/ee/multi-cloud-provider-instance-list.png)

## Create a universe

You can create a multi-region universe as follows:

1. Navigate to **Dashboard** or **Universes** and click **Create Universe**.

1. Complete the **Primary Cluster** fields, as shown in the following illustration:

    ![New universe details](/images/ee/multi-cloud-create-universe1.png)

1. Enter the universe name: `helloworld2`

1. Enter the set of regions: `us-aws-west-2`, `us-azu-east-1`, `us-centra1-b`

1. Set the instance type to `8core`.

1. Add the following flags for Master and T-Server:

    * `leader_failure_max_missed_heartbeat_periods=10` - As the data is globally replicated, RPC latencies are higher. This flag increases the failure-detection interval to compensate.
    * `use_cassandra_authentication=true` - Deployments on public clouds require security.
    * `ysql_enable_auth=true` - Deployments on public clouds require security.

    ![New universe flags details](/images/ee/multi-cloud-create-universe2.png)

1. Click **Create**.

At this point, YugabyteDB Anywhere begins to provision your new universe across multiple cloud providers. When the universe is provisioned, it appears on the **Dashboard** and **Universes**. You can click the universe name to open its **Overview**.

![Universe overview page](/images/ee/multi-cloud-universe-overview.png)

The new universe's nodes list will be similar to the following:

![Universe overview](/images/ee/multi-cloud-universe-nodes.png)

<!-- END: YBA cluster setup instructions -->
{{</nav/panel>}}
{{</nav/panels>}}
<!-- end: nav tabs -->

## Failover

On a region failure, the multi-cloud YugabyteDB universe will automatically failover to either of the remaining cloud regions depending on the [application design pattern](../../build-global-apps/) you had chosen for your setup. For example if you had set the preferred zones order to be `gcp:1 aws:2 azu:3`, then when `GCP` fails, apps will move to `AWS` and the followers in `AWS` will be promoted to leaders.

![Multi-cloud Yugabyte](/images/develop/multicloud/multicloud-failover.png)

## Learn more

* [Build global applications](../../build-global-apps/)
