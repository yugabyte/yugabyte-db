---
title: Create clusters example for ybm CLI automation
headerTitle: "ybm CLI: Create clusters"
linkTitle: Create clusters
description: Tutorial for using YugabyteDB Aeon ybm CLI to create clusters.
headcontent: Create and connect to a cluster from the command line
menu:
  preview_yugabyte-cloud:
    identifier: managed-cli-example-create
    parent: managed-cli-examples
    weight: 60
type: docs
---

The following tutorial shows how you can use ybm CLI to create clusters in YugabyteDB Aeon.

## Prerequisites

This guide assumes you have already done the following:

- Created and saved an [API key](../../../managed-apikeys/).
- [Installed ybm CLI](../../../managed-cli/managed-cli-overview/#install-ybm).
- [Configured ybm CLI](../../../managed-cli/managed-cli-overview/#configure-ybm) with your API key.

Note that you can only create one Sandbox cluster per account. To create VPCs and dedicated clusters, you need to [choose a plan](https://www.yugabyte.com/pricing/), or you can [start a free trial](../../../../managed-freetrial/). For more information, refer to [What are the differences between Sandbox and Dedicated clusters](../../../../../faq/yugabytedb-managed-faq/#what-are-the-differences-between-sandbox-and-dedicated-clusters).

## Create a sandbox cluster

To create your Sandbox cluster, enter the following command:

```sh
ybm cluster create \
  --cluster-name my-sandbox \
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

To [connect to your database](../../../../cloud-connect/) from your desktop or an application, you need the following:

- your device added to the cluster IP allow list
- the cluster TLS certificate
- the host address of the cluster

### Create and assign an IP allow list

To connect to your cluster from your computer, you need an [IP allow list](../../../../cloud-secure-clusters/add-connections/) with your computer's IP address. Create one as follows:

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

To connect to a cluster in YugabyteDB Aeon using a shell, you need the [cluster certificate](../../../../cloud-secure-clusters/cloud-authentication/). Download the certificate using the following command:

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
my-sandbox   035f24da-3000-41c0-b12f-ef20004a3000   2.17.1.0-b439   ACTIVE    💚

Provider   Tier      Fault Tolerance   Nodes     Total Res.(Vcpu/Mem/Disk)
AWS        Sandbox   NONE              1         2 / 4GB / 10GB


Regions
Region      Nodes     vCPU/Node   Mem/Node   Disk/Node   VPC
us-west-2   1         2           4GB        10GB        


Endpoints
Region      Accessibility   State     Host
us-west-2   PUBLIC          ACTIVE    us-west-2.000f20da-3000-40c0-b10f-ef20004a3000.aws.yugabyte.cloud


Network AllowList
Name          Description   Allow List
my-computer                 173.200.10.100/32


Nodes
Name            Region[zone]            Health    Master    Tserver   ReadReplica   Used Memory(MB)
my-sandbox-n1   us-west-2[us-west-2a]   💚        ✅        ✅        ❌            40MB
```

The public host address to use to connect to your cluster is displayed under **Endpoints**.

To connect to your cluster using the ysqlsh or ycqlsh shells, follow the instructions in [Connect via client shells](../../../../cloud-connect/connect-client-shell/). Use the public endpoint host address, the database credentials you specified when you created the sandbox, and the certificate you downloaded.

## Create dedicated clusters

### Create a single-region cluster

The following command creates a single-region dedicated cluster in Tokyo:

```sh
ybm cluster create \
  --credentials username=admin,password=password \
  --cloud-provider AWS \
  --cluster-type SYNCHRONOUS \
  --region-info region=ap-northeast-1,num-nodes=3,num-cores=4,disk-size-gb=200  \
  --cluster-tier Dedicated \
  --fault-tolerance ZONE \
  --database-version Innovation \
  --cluster-name my-single-region \
  --wait
```

```output
The cluster my-single-region has been created
Name               Tier        Version        State     Health    Regions          Nodes     Total Res.(Vcpu/Mem/Disk)
my-single-region   Dedicated   2.17.2.0-b216  ACTIVE    💚        ap-northeast-1   3         12 / 48GB / 600GB
```

### Create a VPC and multi-region cluster

Multi-region clusters must be deployed in a [VPC](../../../../cloud-basics/cloud-vpcs/). The following example creates a VPC on GCP:

```sh
ybm vpc create \
  --name gcp-vpc \
  --cloud-provider GCP \
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

The following command creates a [replicate-across-regions](../../../../cloud-basics/create-clusters/create-clusters-multisync/) cluster in the VPC you created:

```sh
ybm cluster create \
  --cluster-name my-multi-region \
  --credentials username=admin,password=password \
  --cloud-provider GCP \
  --cluster-type SYNCHRONOUS \
  --region-info region=us-east1,num-nodes=1,num-cores=2,disk-size-gb=200,vpc=gcp-vpc \
  --region-info region=us-west1,num-nodes=1,num-cores=2,disk-size-gb=200,vpc=gcp-vpc \
  --region-info region=us-central1,num-nodes=1,num-cores=2,disk-size-gb=200,vpc=gcp-vpc \
  --cluster-tier Dedicated \
  --fault-tolerance REGION \
  --database-version Production \
  --wait

```

```output
The cluster my-multi-region has been created
Name              Tier        Version        State     Health    Regions          Nodes     Total Res.(Vcpu/Mem/Disk)
my-multi-region   Dedicated   2.14.7.0-b51   ACTIVE    💚        us-central1,+2   3         6 / 24GB / 600GB
```

## Encryption at rest

YugabyteDB Aeon supports [encryption at rest](../../../../cloud-secure-clusters/managed-ear) (EAR). Before you can create a cluster with EAR, you need to create a customer managed key (CMK) in a cloud provider Key Management Service (KMS). See [Prerequisites](../../../../cloud-secure-clusters/managed-ear/#prerequisites).

### Create a cluster with EAR

Use the following commands to create a new cluster with EAR in AWS, GCP, or Azure.

{{< tabpane text=true >}}

{{% tab header="AWS" %}}

```sh
ybm cluster create \
  --cluster-name my-sandbox \
  --cloud-provider AWS \ 
  --cluster-tier Dedicated \
  --cluster-type SYNCHRONOUS \
  ---encryption-spec cloud-provider=AWS,aws-secret-key=<your-secret-key>,aws-access-key=<your-access-key>aws-arn=<your-aws-arn-key> \
  --credentials username=admin,password=password \
  --fault-tolerance=ZONE \
  --region-info region=us-east-2,num-nodes=3,num-cores=4
```

```output
The cluster my-sandbox has been created
Name           Tier        Version           State     Health    Provider   Regions     Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
my-sandbox   Dedicated   {{< yb-version version="preview" format="build">}}       ACTIVE    💚        AWS        us-east-2   3         4 / 16GB / 200GB / 3000
```

{{% /tab %}}

{{% tab header="GCP" %}}

```sh
ybm cluster create 
  --cluster-name my-sandbox \
  --cloud-provider GCP \
  --cluster-tier Dedicated \
  --cluster-type SYNCHRONOUS \
  --encryption-spec cloud-provider=GCP,gcp-resource-id=projects/<your-project>/locations/<your-location>/keyRings/<your-key-ring-name>/cryptoKeys/<your-key-name>,gcp-service-account-path=creds.json \ 
  --credentials username=admin,password=password \
  --fault-tolerance=ZONE \
  --region-info region=us-central1,num-nodes=3,num-cores=4
```

```output
The cluster my-sandbox has been created
Name           Tier        Version           State     Health    Provider   Regions     Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
my-sandbox   Dedicated   {{< yb-version version="preview" format="build">}}       ACTIVE    💚        GCP        us-central1   3         4 / 16GB / 200GB / 3000
```

{{% /tab %}}

{{% tab header="Azure" %}}

```sh
ybm cluster create \
  --cluster-name my-sandbox \
  --cloud-provider AZURE \
  --cluster-tier Dedicated \
  --cluster-type SYNCHRONOUS \
  --encryption-spec cloud-provider=AZURE,azu-client-id=<your-client-id>,azu-client-secret=<your-client-secret>,azu-tenant-id=<your-tenant-id>,azu-key-name=test-key,azu-key-vault-uri=<your-key-vault-uri> \ 
  --credentials username=admin,password=password \
  --fault-tolerance=ZONE --region-info region=eastus,num-nodes=3,num-cores=4 \
```

```output
The cluster my-sandbox has been created
Name           Tier        Version           State     Health    Provider   Regions     Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
my-sandbox   Dedicated   {{< yb-version version="preview" format="build">}}       ACTIVE    💚        AZURE      eastus   3         4 / 16GB / 200GB / 3000
```

{{% /tab %}}

{{< /tabpane >}}

The EAR details are displayed with the cluster details.

{{< tabpane text=true >}}

{{% tab header="AWS" %}}

```sh
ybm cluster describe --cluster-name my-sandbox
```

```output
General
Name                 ID                                     Version        State     Health
my-sandbox   b1676d3f-8898-4c04-a1d6-bedf5bXXXXXX   2.18.3.0-b75   ACTIVE    💚

Provider   Tier        Fault Tolerance   Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
AWS        Dedicated   ZONE, RF 3        3         4 / 16GB / 200GB / 3000


Regions
Region      Nodes     vCPU/Node   Mem/Node   Disk/Node   VPC
us-east-2    3         4           16GB       200GB       


Endpoints
Region      Accessibility   State     Host
us-east-2    PUBLIC          ACTIVE    us-east-2 .XXXXXXXX-8898-4c04-a1d6-bedf5bXXXXXX.aws.devcloud.yugabyte.com


Encryption at Rest
Provider   Key Alias                              Last Rotated               Security Principals                                                           CMK Status
AWS        0a80e409-e690-42fc-b209-XXXXXXXXXXX   2023-11-03T07:37:26.351Z   arn:aws:kms:us-east-1:<your-account-id>:key/<your-key-id>   ACTIVE


Nodes
Name                    Region[zone]            Health    Master    Tserver   ReadReplica   Used Memory(MB)
my-sandbox-n1   us-east-2 [us-east-2 a]   💚        ✅        ✅        ❌            75MB
my-sandbox-n2   us-east-2 [us-east-2 b]   💚        ✅        ✅        ❌            96MB
my-sandbox-n3   us-east-2 [us-east-2 c]   💚        ✅        ✅        ❌            76MB
```

{{% /tab %}}

{{% tab header="GCP" %}}

```sh
ybm cluster describe --cluster-name my-sandbox
```

```output
General
Name                 ID                                     Version        State     Health
my-sandbox   b1676d3f-8898-4c04-a1d6-bedf5bXXXXXX   2.18.3.0-b75   ACTIVE    💚

Provider   Tier        Fault Tolerance   Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
GCP        Dedicated   ZONE, RF 3        3         4 / 16GB / 200GB / 3000


Regions
Region      Nodes     vCPU/Node   Mem/Node   Disk/Node   VPC
us-central1    3         4           16GB       200GB       


Endpoints
Region      Accessibility   State     Host
us-central1    PUBLIC          ACTIVE    us-central1 .b1676d3f-8898-4c04-a1d6-bedf5bXXXXXX.gcp.devcloud.yugabyte.com


Encryption at Rest
Provider   Key Alias      Last Rotated               Security Principals                                                                              CMK Status
GCP        GCP-test-key   2023-11-03T07:37:26.351Z   projects/<your-project-id>/locations/global/keyRings/GCP-test-key-ring/cryptoKeys/GCP-test-key   ACTIVE


Nodes
Name                    Region[zone]            Health    Master    Tserver   ReadReplica   Used Memory(MB)
my-sandbox-n1   us-central1 [us-central1 a]   💚        ✅        ✅        ❌            75MB
my-sandbox-n2   us-central1 [us-central1 b]   💚        ✅        ✅        ❌            96MB
my-sandbox-n3   us-central1 [us-central1 c]   💚        ✅        ✅        ❌            76MB
```

{{% /tab %}}

{{% tab header="Azure" %}}

```sh
ybm cluster describe --cluster-name my-sandbox
```

```output
General
Name                 ID                                     Version        State     Health
my-sandbox   b1676d3f-8898-4c04-a1d6-bedf5b7867ff   2.18.3.0-b75   ACTIVE    💚

Provider   Tier        Fault Tolerance   Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
AZURE        Dedicated   ZONE, RF 3        3         4 / 16GB / 200GB / 3000


Regions
Region      Nodes     vCPU/Node   Mem/Node   Disk/Node   VPC
eastus   3         4           16GB       200GB       


Endpoints
Region      Accessibility   State     Host
eastus   PUBLIC          ACTIVE    eastus.b1676d3f-8898-4c04-a1d6-bedf5b7867ff.azure.devcloud.yugabyte.com


Encryption at Rest
Provider   Key Alias                              Last Rotated               Security Principals                      CMK Status
AZURE      8aXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX5b   2023-11-03T07:37:26.351Z   <your-key-vault-uri>   ACTIVE


Nodes
Name                    Region[zone]            Health    Master    Tserver   ReadReplica   Used Memory(MB)
my-sandbox-n1   eastus[eastusa]   💚        ✅        ✅        ❌            75MB
my-sandbox-n2   eastus[eastusb]   💚        ✅        ✅        ❌            96MB
my-sandbox-n3   eastus[eastusc]   💚        ✅        ✅        ❌            76MB
```

{{% /tab %}}

{{< /tabpane >}}

There is also an alternate way to list the EAR configuration directly, using the encryption list command.

```sh
ybm cluster encryption describe --cluster-name my-sandbox
```

```output
A newer version is available. Please upgrade to the latest version v0.1.22
Provider   Key Alias                              Last Rotated   Security Principals                                                           CMK Status
AWS        XXXXXXXX-e690-42fc-b209-baf969930b2c   -              arn:aws:kms:us-east-1:712345678912:key/db272c8d-1592-4c73-bfa3-420d05822933   ACTIVE
```

### Update CMK configuration

Use the following commands to update the CMK configuration. If no existing configuration is found, the command creates a new one; otherwise, it updates the current configuration.

Note: Only credentials can be modified in the current configuration (for example, AWS access/secret keys or GCP service account credentials).

{{< tabpane text=true >}}

{{% tab header="AWS" %}}

```sh
ybm cluster encryption update \
  --cluster-name my-sandbox \
  --encryption-spec cloud-provider=AWS,aws-secret-key=<your-secret-key>,aws-access-key=<your-access-key>
```

{{% /tab %}}

{{% tab header="GCP" %}}

```sh
ybm cluster encryption update \
  --cluster-name my-sandbox \
  --encryption-spec cloud-provider=GCP,resource-id=projects/yugabyte/locations/global/keyRings/test-byok/cryptoKeys/key1,k=<path-to-service-account-file> 
```

{{% /tab %}}

{{% tab header="Azure" %}}

```sh
ybm cluster encryption update \
  --cluster-name my-sandbox \
  --encryption-spec cloud-provider=AZURE,azu-client-id=<your-client-id>,azu-client-secret=<your-client-secret>,azu-tenant-id=<your-tenant-id>,azu-key-name=test-key,azu-key-vault-uri=<your-key-vault-uri>
```

{{% /tab %}}

{{< /tabpane >}}

### Update CMK state

Use the following commands to enable or disable the CMK state.

#### enable CMK

```sh
ybm cluster encryption update-state \
  --cluster-name my-sandbox
  --enable
```

```output
Successfully ENABLED encryption spec status for cluster my-sandbox
```

#### disable CMK

```sh
ybm cluster encryption update-state \
  --cluster-name my-sandbox
  --disable
```

```output
Successfully DISABLED encryption spec status for cluster my-sandbox
```

## Pause, resume, and terminate

To list your clusters, enter the following command:

```sh
ybm cluster list
```

```output
Name               Tier        Version         State     Health    Regions                      Nodes     Total Res.(Vcpu/Mem/Disk)
my-multi-region    Dedicated   2.14.7.0-b51    ACTIVE    💚        us-central1,+2               3         6 / 24GB / 600GB
my-sandbox         Sandbox     2.17.1.0-b439   ACTIVE    💚        us-west-2                    1         2 / 4GB / 10GB
my-single-region   Dedicated   2.14.7.0-b51    ACTIVE    💚        ap-northeast-1               3         12 / 48GB / 600GB
```

You can pause a cluster when you don't need it:

```sh
ybm cluster pause \
  --cluster-name my-single-region \
  --wait
```

```output
The cluster my-single-region has been paused
Name               Tier        Version         State     Health    Regions          Nodes     Total Res.(Vcpu/Mem/Disk)
my-single-region   Dedicated   2.17.2.0-b216   PAUSED    ❓        ap-northeast-1   3         12 / 48GB / 600GB
```

(Note that the Sandbox is free, and can't be paused.)

To resume a cluster:

```sh
ybm cluster resume \
  --cluster-name my-single-region \
  --wait
```

```output
The cluster my-single-region has been resumed
Name               Tier        Version         State     Health    Regions          Nodes     Total Res.(Vcpu/Mem/Disk)
my-single-region   Dedicated   2.14.7.0-b51    ACTIVE    💚        ap-northeast-1   3         12 / 48GB / 600GB
```

If you don't need a cluster anymore, you can terminate it:

```sh
ybm cluster delete \
  --cluster-name my-multi-region \
  --wait
```

```output
The cluster my-multi-region has been deleted
```
