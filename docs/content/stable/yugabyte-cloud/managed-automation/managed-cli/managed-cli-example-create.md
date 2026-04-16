---
title: Create clusters example for ybm CLI automation
headerTitle: Create cluster example
linkTitle: Create cluster example
description: Tutorial for using YugabyteDB Aeon ybm CLI to create clusters.
headcontent: Create and connect to a cluster from the command line
menu:
  stable_yugabyte-cloud:
    identifier: managed-cli-example-create
    parent: managed-cli
    weight: 60
aliases:
  - /stable/yugabyte-cloud/managed-automation/managed-cli/managed-cli-examples/managed-cli-example-create/
  - /stable/yugabyte-cloud/managed-automation/managed-cli/managed-cli-examples/
type: docs
---

The following tutorial shows how you can use ybm CLI to create clusters in YugabyteDB Aeon. The example shows how to use the [cluster](https://github.com/yugabyte/ybm-cli/blob/main/docs/ybm_cluster.md) resource.

## Prerequisites

This guide assumes you have already done the following:

- Created and saved an [API key](../../managed-apikeys/).
- [Installed ybm CLI](../#install-ybm).
- [Configured ybm CLI](../#configure-ybm) with your API key.

Note that you can only create one Sandbox cluster per account. To create VPCs and dedicated clusters, you need to [choose a plan](https://www.yugabyte.com/pricing/), or you can [start a free trial](../../../managed-freetrial/). For more information, refer to [What are the differences between Sandbox and Dedicated clusters](../../../../faq/yugabytedb-managed-faq/#what-are-the-differences-between-sandbox-and-dedicated-clusters).

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

To [connect to your database](../../../cloud-connect/) from your desktop or an application, you need the following:

- your device added to the cluster IP allow list
- the cluster TLS certificate
- the host address of the cluster

### Create and assign an IP allow list

To connect to your cluster from your computer, you need an [IP allow list](../../../cloud-secure-clusters/add-connections/) with your computer's IP address. Create one as follows:

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

To connect to a cluster in YugabyteDB Aeon using a shell, you need the [cluster certificate](../../../cloud-secure-clusters/cloud-authentication/). Download the certificate using the following command:

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

To connect to your cluster using the ysqlsh or ycqlsh shells, follow the instructions in [Connect via client shells](../../../cloud-connect/connect-client-shell/). Use the public endpoint host address, the database credentials you specified when you created the sandbox, and the certificate you downloaded.

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

Multi-region clusters must be deployed in a [VPC](../../../cloud-basics/cloud-vpcs/). The following example creates a VPC on GCP:

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

The following command creates a [replicate-across-regions](../../../cloud-basics/create-clusters/create-clusters-multisync/) cluster in the VPC you created:

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

### Scale your cluster

Use the `ybm cluster update` command with `--region-info` flag to scale your cluster by adding or removing nodes, cores, and disk size. You can also change the region name. For example, to change the number of nodes in the `us-east1` region from 1 to 3, and the number of cores from 2 to 4, enter the following command:

```sh
ybm cluster update \
  --cluster-name my-multi-region \
  --region-info region=us-east1,num-nodes=3,num-cores=4,disk-size-gb=250 \
  --wait
```

For more information, see [Scale and configure clusters](../../../cloud-clusters/configure-clusters/).

## Encryption at rest

YugabyteDB Aeon supports [encryption at rest](../../../cloud-secure-clusters/managed-ear) (EAR). Before you can create a cluster with EAR, you need to create a customer managed key (CMK) in a cloud provider Key Management Service (KMS). See [Prerequisites](../../../cloud-secure-clusters/managed-ear/#prerequisites).

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
  --encryption-spec cloud-provider=AWS,aws-secret-key=<your-secret-key>,aws-access-key=<your-access-key>,aws-arn=<your-aws-arn-key> \
  --credentials username=admin,password=password \
  --fault-tolerance ZONE \
  --region-info region=us-east-2,num-nodes=3,num-cores=4
```

```output
The cluster my-sandbox has been created
Name           Tier        Version           State     Health    Provider   Regions     Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
my-sandbox   Dedicated   {{< yb-version version="stable" format="build">}}       ACTIVE    💚        AWS        us-east-2   3         4 / 16GB / 200GB / 3000
```

You can list the EAR details using the encryption list command.

```sh
ybm cluster encryption list --cluster-name my-sandbox
```

```output
Provider   Key Alias                              Last Rotated   Security Principals                                                           CMK Status
AWS        XXXXXXXX-e690-42fc-b209-baf969930b2c   -              arn:aws:kms:us-east-1:712345678912:key/db272c8d-1592-4c73-bfa3-420d05822933   ACTIVE
```

EAR details are also shown when you use the `cluster describe` command.

{{% /tab %}}

{{% tab header="GCP" %}}

```sh
ybm cluster create \
  --cluster-name my-sandbox \
  --cloud-provider GCP \
  --cluster-tier Dedicated \
  --cluster-type SYNCHRONOUS \
  --encryption-spec cloud-provider=GCP,gcp-resource-id=projects/<your-project>/locations/<your-location>/keyRings/<your-key-ring-name>/cryptoKeys/<your-key-name>,gcp-service-account-path=creds.json \
  --credentials username=admin,password=password \
  --fault-tolerance ZONE \
  --region-info region=us-central1,num-nodes=3,num-cores=4
```

```output
The cluster my-sandbox has been created
Name           Tier        Version           State     Health    Provider   Regions     Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
my-sandbox   Dedicated   {{< yb-version version="stable" format="build">}}       ACTIVE    💚        GCP        us-central1   3         4 / 16GB / 200GB / 3000
```

You can list the EAR details using the encryption list command.

```sh
ybm cluster encryption list --cluster-name my-sandbox
```

```output
Provider   Key Alias      Last Rotated               Security Principals                                                                              CMK Status
GCP        <your-key-name>   2023-11-03T07:37:26.351Z   projects/<your-project-id>/<your-location>/global/keyRings/<your-key-ring-name>/cryptoKeys/<your-key-name>   ACTIVE
```

EAR details are also shown when you use the `cluster describe` command.

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
  --fault-tolerance ZONE \
  --region-info region=eastus,num-nodes=3,num-cores=4 \
```

```output
The cluster my-sandbox has been created
Name           Tier        Version           State     Health    Provider   Regions     Nodes     Node Res.(Vcpu/Mem/DiskGB/IOPS)
my-sandbox   Dedicated   {{< yb-version version="stable" format="build">}}       ACTIVE    💚        AZURE      eastus   3         4 / 16GB / 200GB / 3000
```

You can list the EAR details using the encryption list command.

```sh
ybm cluster encryption list --cluster-name my-sandbox
```

```output
Provider   Key Alias                              Last Rotated               Security Principals                      CMK Status
AZURE      8aXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX5b   2023-11-03T07:37:26.351Z   <your-key-vault-uri>   ACTIVE
```

EAR details are also shown when you use the `cluster describe` command.

{{% /tab %}}

{{< /tabpane >}}

### Rotate your CMK

Use the `encryption update` command to rotate your CMK. You can also use this command to encrypt a cluster that does not already have EAR.

When encrypting an existing cluster, YugabyteDB Aeon uses lazy encryption.

{{< tabpane text=true >}}

{{% tab header="AWS" %}}

```sh
ybm cluster encryption update \
  --cluster-name my-sandbox \
  --encryption-spec cloud-provider=AWS,aws-secret-key=<new-secret-key>,aws-access-key=<new-access-key>
```

{{% /tab %}}

{{% tab header="GCP" %}}

```sh
ybm cluster encryption update \
  --cluster-name my-sandbox \
  --encryption-spec cloud-provider=GCP,gcp-resource-id=projects/yugabyte/locations/global/keyRings/test-byok/cryptoKeys/key1,gcp-service-account-path=<path-to-service-account-file>
```

{{% /tab %}}

{{% tab header="Azure" %}}

```sh
ybm cluster encryption update \
  --cluster-name my-sandbox \
  --encryption-spec cloud-provider=AZURE,azu-client-id=<new-client-id>,azu-client-secret=<new-client-secret>,azu-tenant-id=<new-tenant-id>,azu-key-name=test-key,azu-key-vault-uri=<new-key-vault-uri>
```

{{% /tab %}}

{{< /tabpane >}}

### Enable and disable EAR

To disable EAR on a cluster, use the following command:

```sh
ybm cluster encryption update-state \
  --cluster-name my-sandbox \
  --disable
```

```output
Successfully DISABLED encryption at rest for cluster my-sandbox
```

After you disable EAR, YugabyteDB Aeon uses lazy decryption to decrypt the cluster.

You can check the status of the EAR using the encryption list command.

```sh
ybm cluster encryption list --cluster-name my-sandbox
```

```output
Provider   Key Alias                              Last Rotated               Security Principals                      CMK Status
AZURE      8aXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX5b   2023-11-03T07:37:26.351Z   <your-key-vault-uri>   DISABLED

To re-enable EAR on a cluster, use the following command:

```sh
ybm cluster encryption update-state \
  --cluster-name my-sandbox \
  --enable
```

```output
Successfully ENABLED encryption at rest for cluster my-sandbox
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
