---
title: Guide for ybm CLI automation
headerTitle: "Guide: Create clusters using ybm CLI"
linkTitle: "Guide: ybm CLI"
description: Tutorials for using YugabyteDB Managed automation tools including API, CLI, and Terraform provider.
headcontent: Working examples for automation tools
menu:
  preview_yugabyte-cloud:
    identifier: managed-guide-cli
    parent: managed-automation
    weight: 60
type: docs
---

The following tutorial shows how you can use ybm CLI to create clusters in YugabyteDB Managed.

This guide assumes you have already done the following:

- Created and saved an [API key](../managed-apikeys/).
- [Installed ybm CLI](../managed-cli/managed-cli-overview/#install-ybm).
- [Configured ybm CLI](../managed-cli/managed-cli-overview/#configure-ybm) with your API key.

{{< note title="Note" >}}

You can only create one Sandbox cluster per account.

Before you can create dedicated clusters in YugabyteDB Managed, you need to add a [billing profile](../../cloud-admin/cloud-billing-profile/) and payment method, or you can [request a free trial](../../managed-freetrial/).

{{< /note >}}

## Create a sandbox cluster

To create your free [sandbox](../../cloud-basics/create-clusters/create-clusters-free/) cluster, enter the following command:

```sh
ybm cluster create \
  --cluster-name my-sandbox
  --credentials username=admin,password=password-123 \
  --cluster-tier Sandbox \
  --wait
```

When you use the `--wait` flag, you can see the progress of the cluster creation in your shell. When finished, you should see output similar to the following:

```output
The cluster my-sandbox has been created
Name         Tier      Version         State     Health    Regions     Nodes     Total Res.(Vcpu/Mem/Disk)
my-sandbox   Sandbox   2.17.1.0-b439   ACTIVE    💚        us-west-2   1         2 / 4GB / 10GB
```

## Connect to your cluster

### Create and assign an IP allow list

To connect to your cluster from your computer, you need an IP allow list with your computer's IP address. Create one as follows:

```sh
ybm network-allow-list create \
  --ip-addr $(curl ifconfig.me) \
  --name my-computer
```

```output
Name          Description   Allow List          Clusters
my-computer                 170.200.10.100/32   
NetworkAllowList my-computer successfully created
```

Assign the IP allow list to your Sandbox:

```sh
ybm cluster network allow-list assign \
  --network-allow-list my-computer \
  --cluster-name my-sandbox
```

```output
The network allow list my-computer is being assigned to the cluster my-sandbox
```

To list your current network allow lists, enter the following command:

```sh
ybm network-allow-list list
```

```output
Name                     Description              Allow List          Clusters
my-computer                                       173.206.17.104/32   my-sandbox
```

### Download the cluster certificate

To connect to a cluster in YugabyteDB Managed using a shell, you need the cluster certificate. Download the certificate using the following command:

```sh
ybm cluster cert download --out $HOME/root.crt
```

### Show cluster settings

To show details about your cluster, use the `describe` command as follows:

```sh
ybm cluster describe --cluster-name my-sandbox
```

```output
General
Name         ID                                     Version         State     Health
my-sandbox   035f24da-3972-41c0-b12f-ef20224a3169   2.17.1.0-b439   ACTIVE    💚

Provider   Tier      Fault Tolerance   Nodes     Total Res.(Vcpu/Mem/Disk)
AWS        Sandbox   NONE              1         2 / 4GB / 10GB


Regions
Region      Nodes     vCPU/Node   Mem/Node   Disk/Node   VPC
us-west-2   1         2           4GB        10GB        


Endpoints
Region      Accessibility   State     Host
us-west-2   PUBLIC          ACTIVE    us-west-2.035f24da-3972-41c0-b12f-ef20224a3169.aws.ybdb.io


Network AllowList
Name          Description   Allow List
my-computer                 173.206.17.104/32


Nodes
Name            Region[zone]            Health    Master    Tserver   ReadReplica   Used Memory(MB)
my-sandbox-n1   us-west-2[us-west-2a]   💚        ✅        ✅        ❌            40MB
```

The host address to use to connect to your cluster is displayed under **Endpoints**.

To connect to your cluster using the ysqlsh or ycqlsh shells, follow the instructions in [Connect via client shells](../../cloud-connect/connect-client-shell/). Use the cluster endpoint host address, the database credentials you specified when you created the sandbox, and the certificate you downloaded.

## Create a single-region dedicated cluster

The following command creates a single-region dedicated cluster in Tokyo:

```sh
ybm cluster create \
  --credentials username=admin,password=password \
  --cloud-type AWS \
  --cluster-type SYNCHRONOUS \
  --node-config num-cores=4,disk-size-gb=200 \
  --region-info region=ap-northeast-1,num-nodes=3 \
  --cluster-tier Dedicated \
  --fault-tolerance ZONE \
  --database-version Preview \
  --cluster-name my-single-region \
  --wait
```

## Create a VPC and multi-region dedicated cluster

Multi-region clusters must be deployed in a [VPC](../../cloud-basics/cloud-vpcs/). The following example creates a VPC on GCP:

```sh
ybm vpc create \
  --name gcp-vpc \
  --cloud GCP \
  --global-cidr 10.0.0.0/18 \
  --wait
```

```output
The VPC gcp-vpc has been created
Name      State     Provider   Region[CIDR]                        Peerings   Clusters
gcp-vpc   ACTIVE    GCP        asia-southeast2[10.0.19.0/24],+27   0          0
```

To list the available regions in GCP:

```sh
ybm region list --cloud-provider GCP
```

The following command creates a [replicate-across-regions](../../cloud-basics/create-clusters/create-clusters-multisync/) cluster in the VPC you created:

```sh
ybm cluster create \
  --cluster-name my-multi-region \
  --credentials username=admin,password=password \
  --cloud-type GCP \
  --cluster-type SYNCHRONOUS \
  --node-config num-cores=2,disk-size-gb=200 \
  --region-info region=us-east1,num-nodes=1,vpc=gcp-vpc \
  --region-info region=us-west1,num-nodes=1,vpc=gcp-vpc \
  --region-info region=us-central1,num-nodes=1,vpc=gcp-vpc \
  --cluster-tier Dedicated \
  --fault-tolerance REGION \
  --database-version Stable \
  --wait
```
