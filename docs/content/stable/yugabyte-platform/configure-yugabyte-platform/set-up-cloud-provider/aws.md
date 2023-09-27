---
title: Configure the AWS cloud provider
headerTitle: Create provider configuration
linkTitle: Create provider configuration
description: Configure the Amazon Web Services (AWS) provider configuration.
headContent: Configure an AWS provider configuration
menu:
  stable_yugabyte-platform:
    identifier: set-up-cloud-provider-1-aws
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../aws/" class="nav-link active">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure/" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vmware-tanzu/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

  <li>
    <a href="../openshift/" class="nav-link">
      <i class="fa-brands fa-redhat" aria-hidden="true"></i>
      OpenShift
    </a>
  </li>

  <li>
    <a href="../on-premises/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

Before you can deploy universes using YugabyteDB Anywhere, you must create a provider configuration. Create an Amazon Web Services (AWS) provider configuration if your target cloud is AWS.

A provider configuration describes your cloud environment (such as its security group, regions and availability zones, NTP server, certificates that may be used to SSH to VMs, the Linux disk image to be used for configuring the nodes, and so on). The provider configuration is used as an input when deploying a universe, and can be reused for many universes.

When deploying a universe, YugabyteDB Anywhere uses the provider configuration settings to do the following:

- Create VMs on AWS using the following:
  - the service account
  - specified regions and availability zones (this can be a subset of those specified in the provider configuration)
  - a Linux image

- Provision those VMs with YugabyteDB software

Note: YugabyteDB Anywhere needs network connectivity to the VMs (via VPCs), security groups for the provisioning step above, and for subsequent management, as described in [Cloud prerequisites](../../../install-yugabyte-platform/prepare-environment/aws/).

## Prerequisites

- An AWS Service Account with sufficient privileges. This account must have permissions to create VMs, and access to the VPC and security groups described below. Required input: Access Key ID and Secret Access Key for the AWS Service Account.
- An AWS VPC for each region. Required input: for each region, a VPC ID.
- AWS Security Groups must exist to allow network connectivity so that YugabyteDB Anywhere can create AWS VMs when deploying a universe. Required input: for each region, a Security Group ID.

For more information on setting up an AWS service account and security groups, refer to [Prepare the AWS cloud environment](../../../install-yugabyte-platform/prepare-environment/aws/).

## Configure AWS

Navigate to **Configs > Infrastructure > Amazon Web Services** to see a list of all currently configured AWS providers.

### View and edit providers

To view a provider, select it in the list of AWS Configs to display the **Overview**.

To edit the provider, select **Config Details**, make changes, and click **Apply Changes**. For more information, refer to [Provider settings](#provider-settings). Note that, depending on whether the provider has been used to create a universe, you can only edit a subset of options.

To view the universes created using the provider, select **Universes**.

To delete the provider, click **Actions** and choose **Delete Configuration**. You can only delete providers that are not in use by a universe.

### Create a provider

To create an AWS provider:

1. Click **Create Config** to open the **Create AWS Provider Configuration** page.

    ![Create AWS provider](/images/yb-platform/config/yba-aws-config-create.png)

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Create Provider Configuration** when you are done and wait for the configuration to complete.

This process includes configuring a network, subnetworks in all available regions, firewall rules, VPC peering for network connectivity, and a custom SSH key pair for YugabyteDB Anywhere-to-YugabyteDB connectivity.

## Provider settings

### Provider Name

Enter a Provider name. The Provider name is an internal tag used for organizing provider configurations.

### Cloud Info

**Credential Type**. YugabyteDB Anywhere requires the ability to create VMs in AWS. To do this, you can do one of the following:

- **Specify Access ID and Secret Key** - Create an AWS Service Account with the required permissions (refer to [Prepare the AWS cloud environment](../../../install-yugabyte-platform/prepare-environment/aws/)), and provide your AWS Access Key ID and Secret Access Key.
- **Use IAM Role from this YBA host's instance** - Provision the YugabyteDB Anywhere VM instance with an IAM role that has sufficient permissions by attaching an [IAM role](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html) to the YugabyteDB Anywhere VM in the **EC2** tab. For more information, see [Deploy the YugabyteDB universe using an IAM role](../../../install-yugabyte-platform/prepare-environment/aws/#deploy-the-yugabytedb-universe-using-an-iam-role). This option is only available if YugabyteDB Anywhere is installed on AWS.

**Use AWS Route 53 DNS Server**. Choose whether to use the cloud DNS Server / load balancer for universes deployed using this provider. Generally, SQL clients should prefer to use [smart client drivers](../../../../drivers-orms/smart-drivers/) to connect to cluster nodes, rather than load balancers. However, in some cases (for example, if no smart driver is available in the language), you may use a DNS Server or load-balancer. The DNS Server acts as a load-balancer that routes clients to various nodes in the database universe. YugabyteDB Anywhere integrates with [Amazon Route53](https://docs.aws.amazon.com/Route53/latest/DeveloperGuide/Welcome.html) to provide managed Canonical Name (CNAME) entries for your YugabyteDB universes, and automatically updates the DNS entry as nodes get created, removed, or undergo maintenance.

### Regions

You can customize your network, including the virtual network, as follows:

- **AMI Type**. Choose the type of Amazon Machine Image (AMI) to use for deployments that use this configuration, as follows:
  - Default x86
  - Default AArch64
  - Custom

- **VPC Setup**. Choose the VPC setup to use:
  - **Specify an existing VPC**. Select this option to use a VPC that you have created in AWS.
  - **Create a new VPC** ([Beta](../../../../faq/general/#what-is-the-definition-of-the-beta-feature-tag)). Select this option to create a new VPC using YugabyteDB Anywhere. This option is not recommended for production use cases. If you use this feature and there are any classless inter-domain routing (CIDR) conflicts, the operation can fail silently. This would include, for example, doing the following:
    - Configuring more than one AWS cloud provider with different CIDR block prefixes.
    - Creating a new VPC with a CIDR block that overlaps with any of the existing subnets.

    To use this option, contact {{% support-platform %}}.

- Click **Add Region** to add a region to the configuration.

  For information on configuring your regions, see [Add regions](#add-regions).

### SSH Key Pairs

To be able to provision Amazon Elastic Compute Cloud (EC2) instances with YugabyteDB, YugabyteDB Anywhere requires SSH access. The following are two ways to provide SSH access:

- Enable YugabyteDB Anywhere to create and manage [Key Pairs](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html). In this mode, YugabyteDB Anywhere creates SSH Key Pairs across all the regions you choose to set up and stores the relevant private key part of these locally in order to SSH into future EC2 instances.
- Use your own existing Key Pairs. To do this, provide the name of the Key Pair, as well as the private key content and the corresponding SSH user. This information must be the same across all the regions you provision.

If you use YugabyteDB Anywhere to manage SSH Key Pairs for you and you deploy multiple YugabyteDB Anywhere instances across your environment, then the AWS provider name should be unique for each instance of YugabyteDB Anywhere integrating with a given AWS account.

### Advanced

You can customize the Network Time Protocol server, as follows:

- Select **Use AWS's NTP server** to enable cluster nodes to connect to the AWS internal time servers. For more information, consult the AWS documentation such as [Keeping time with Amazon time sync service](https://aws.amazon.com/blogs/aws/keeping-time-with-amazon-time-sync-service/).
- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, you will be responsible for manually configuring NTP.

    {{< warning title="Important" >}}

Use this option with caution. Time synchronization is critical to database data consistency; failure to run NTP may cause data loss.

    {{< /warning >}}

### Add regions

For deployment, YugabyteDB Anywhere aims to provide you with access to the many regions that AWS makes available globally. To that end, YugabyteDB Anywhere allows you to select which regions to which you wish to deploy.

#### Specify an existing VPC

If you choose to use VPCs that you have configured, you are responsible for having preconfigured networking connectivity. For single-region deployments, this might just be a matter of region or VPC local Security Groups. Across regions, however, the setup can get quite complex. It is recommended that you use the [VPC peering](https://docs.aws.amazon.com/vpc/latest/peering/working-with-vpc-peering.html) feature of [Amazon Virtual Private Cloud (Amazon VPC)](https://docs.aws.amazon.com/vpc/latest/userguide/what-is-amazon-vpc.html) to set up private IP connectivity between nodes located across regions, as follows:

- VPC peering connections must be established in an N x N matrix, such that every VPC in every region you configure must be peered to every other VPC in every other region.
- Routing table entries in every regional VPC should route traffic to every other VPC CIDR block across the PeeringConnection to that respective VPC. This must match the Subnets that you provided during the configuration step.
- Security groups in each VPC can be hardened by only opening up the relevant ports to the CIDR blocks of the VPCs from which you are expecting traffic.
- If you deploy YugabyteDB Anywhere in a different VPC than the ones in which you intend to deploy YugabyteDB nodes, then its own VPC must also be part of this cross-region VPC mesh, as well as setting up routing table entries in the source VPC (YugabyteDB Anywhere) and allowing one further CIDR block (or public IP) ingress rule on the security groups for the YugabyteDB nodes (to allow traffic from YugabyteDB Anywhere or its VPC).
- When a public IP address is not enabled on a universe, a network address translation (NAT) gateway or device is required. You must configure the NAT gateway before creating the VPC that you add to the YugabyteDB Anywhere UI. For more information, see [NAT](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-nat.html) and [Creating a VPC with public and private subnets](https://docs.aws.amazon.com/AmazonECS/latest/developerguide/create-public-private-vpc.html) in the AWS documentation.

To configure a region using your own custom VPCs, click **Add Region** and do the following:

1. Select the **Region**.
1. Specify the **VPC ID** of the VPC to use for the region.
1. Specify the **Security Group ID** to use for the region. This is attached to all YugabyteDB nodes and must allow traffic from all other YugabyteDB nodes, even across regions, if you deploy across multiple regions.
1. If you chose to use a custom AMI, specify the **Custom AMI ID**.

For each availability zone in which you wish to be able to deploy in the region, do the following:

1. Click **Add Zone**.
1. Select the zone.
1. Enter the Subnet ID to use for the zone. This is required to ensure that YugabyteDB Anywhere can deploy nodes in the correct network isolation that you desire in your environment.

<!--
### Create a new VPC

If you use YugabyteDB Anywhere to configure, own, and manage a full cross-region deployment of Virtual Private Clouds (VPCs), YugabyteDB Anywhere generates a YugabyteDB-specific VPC in each selected region, then interconnects them (including the VPC in which YugabyteDB Anywhere is deployed) using [VPC peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html). This mode also sets up all other relevant sub-components in all regions, such as subnets, security groups, and routing table entries.

You have an option to provide the following:

- A custom CIDR block for each regional VPC. If not provided, YugabyteDB Anywhere chooses defaults, aiming to not overlap across regions.

- A custom AMI ID to use in each region.

  YugabyteDB Anywhere supports x86 and ARM (aarch64) CPU architectures. If you plan to deploy YugabyteDB on AWS Graviton-based EC2 instances, use a custom AMI certified for 64-bit ARM (arm64) architecture.

  If you don't provide an AMI ID, a recent x86 CentOS image is used. For additional information, see [CentOS on AWS](https://wiki.centos.org/Cloud/AWS). See [Supported operating systems and architectures](../../supported-os-and-arch/) for a complete list of supported operating systems.

To use automatic provisioning to bring up a universe on [AWS Graviton](https://aws.amazon.com/ec2/graviton/), you need to pass in the Arch AMI ID of AlmaLinux or Ubuntu. Note that this requires a YugabyteDB release for Linux ARM, which is available through one of the release pages (for example, the [current preview release](/preview/releases/release-notes/preview-release/)). YugabyteDB Anywhere enables you to import releases via S3 or HTTP, as described in [Upgrade the YugabyteDB software](../../../manage-deployments/upgrade-software/).

#### Limitations

If you create more than one AWS cloud provider with different CIDR block prefixes, your attempt to create a new VPC as part of your [VPC setup](#vpc-setup) will result in a silent failure.
-->

## Marketplace acceptance

If you did not provide your own custom AMI IDs, before you can proceed to creating a universe, verify that you can actually spin up EC2 instances with the default AMIs in YugabyteDB Anywhere.

While logged into your AWS account, do the following:

- For the default x86 AMI, go to [AlmaLinux OS 8 (x86_64) product](https://aws.amazon.com/marketplace/pp/prodview-mku4y3g4sjrye) and click **Continue to Subscribe**.
- For the default aarch64 AMI, go to [AlmaLinux OS 8 (arm64) product](https://aws.amazon.com/marketplace/pp/prodview-zgsymdwitnxmm) and click **Continue to Subscribe**.

If you are not already subscribed and have not accepted the **Terms and Conditions**, then you should see the following message:

![Marketplace accept](/images/ee/aws-setup/marketplace-accept.png)

If that is the case, click **Accept Terms** and wait for the page to switch to a successful state. After the operation completes, or if you previously subscribed and accepted the terms, you should see a message similar to the following:

![Marketplace success](/images/ee/aws-setup/marketplace-success-almalinux.png)
