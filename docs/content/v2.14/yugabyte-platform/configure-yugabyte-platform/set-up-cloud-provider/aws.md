---
title: Configure the AWS cloud provider
headerTitle: Configure the AWS cloud provider
linkTitle: Configure cloud providers
description: Configure the Amazon Web Services (AWS) cloud provider.
menu:
  v2.14_yugabyte-platform:
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
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes/" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
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
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="../on-premises/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

You can configure Amazon Web Services (AWS) for YugabyteDB using YugabyteDB Anywhere. If no cloud providers have been configured yet, the main **Dashboard** page prompts you to configure at least one cloud provider.

## Prerequisites

To run YugabyteDB nodes on AWS, you need to supply your cloud provider credentials on the YugabyteDB Anywhere UI. YugabyteDB Anywhere uses the credentials to automatically provision and deprovision YugabyteDB instances. A YugabyteDB instance includes a compute instance, as well as attached local or remote disk storage.

## Configure AWS

You configure AWS by completing the fields of the page shown in the following illustration:

![AWS Empty Provider](/images/ee/aws-setup/aws_provider_empty.png)

### Provider name

Provider name is an internal tag used for organizing cloud providers.

### Credentials

In order to deploy YugabyteDB nodes in your AWS account, YugabyteDB Anywhere requires access to a set of cloud credentials which can be provided in one of the following ways:

- Directly provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html).
- Attach an [IAM role](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html) to the YugabyteDB Anywhere VM in the **EC2** tab. For more information, see [Deploy the YugabyteDB universe using an IAM role](../../../install-yugabyte-platform/prepare-environment/aws/#deploy-the-yugabytedb-universe-using-an-iam-role).

### SSH key pairs

In order to be able to provision Amazon Elastic Compute Cloud (EC2) instances with YugabyteDB, YugabyteDB Anywhere requires SSH access. The following are two ways to provide SSH access:

- Enable YugabyteDB Anywhere to create and manage [Key Pairs](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html). In this mode, YugabyteDB Anywhere creates SSH Key Pairs across all the regions you choose to set up and stores the relevant private key part of these locally in order to SSH into future EC2 instances.
- Use your own existing Key Pairs. To do this, provide the name of the Key Pair, as well as the private key content and the corresponding SSH user. This information must be the same across all the regions you provision.

If you use YugabyteDB Anywhere to manage SSH Key Pairs for you and you deploy multiple YugabyteDB Anywhere instances across your environment, then the AWS provider name should be unique for each instance of YugabyteDB Anywhere integrating with a given AWS account.

### Hosted zones

Integrating with hosted zones can make YugabyteDB universes easily discoverable. YugabyteDB Anywhere can integrate with [Amazon Route53](https://docs.aws.amazon.com/Route53/latest/DeveloperGuide/Welcome.html) to provide managed Canonical Name (CNAME) entries for your YugabyteDB universes, which will be updated as you change the set of nodes to include all relevant ones for each of your universes.

### VPC setup

You can customize your network, including the virtual network, as follows:

- Select an existing Virtual Private Cloud (VPC).
- Create a new VPC, with certain limitations. For example, an attempt to configure more than one AWS cloud provider with the **Create a new VPC** option enabled will result in a silent failure.

### NTP setup

You can customize the Network Time Protocol server, as follows:

- Select **Use provider's NTP server** to enable cluster nodes to connect to the AWS internal time servers. For more information, consult the AWS documentation such as [Keeping time with Amazon time sync service](https://aws.amazon.com/blogs/aws/keeping-time-with-amazon-time-sync-service/).
- Select **Manually add NTP Servers** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Don't set up NTP** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, ensure that NTP is correctly configured on your machine image.

## Global deployment

For deployment, YugabyteDB Anywhere aims to provide you with easy access to the many regions that AWS makes available globally. To that end, YugabyteDB Anywhere allows you to select which regions to which you wish to deploy and supports two different ways of configuring your setup, based on your environment: YugabyteDB Anywhere-managed configuration and self-managed configuration.

### YugabyteDB Anywhere-managed configuration

If you use YugabyteDB Anywhere to configure, own, and manage a full cross-region deployment of Virtual Private Cloud (VPC), YugabyteDB Anywhere generates a YugabyteDB-specific VPC in each selected region, then interconnects them, as well as the VPC in which YugabyteDB Anywhere is deployed, using [VPC peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html). This mode also sets up all other relevant sub-components in all regions, such as subnets, security groups, and routing table entries.

You have an option to provide the following:

- A custom CIDR block for each regional VPC. If not provided, YugabyteDB Anywhere chooses defaults, aiming to not overlap across regions.

- A custom Amazon Machine Image (AMI) ID to use in each region.

  YugabyteDB Anywhere supports both x86 and ARM (aarch64) CPU architectures. See [Supported operating systems and architectures](../supported-os-and-arch/) for a complete list of supported operating systems. If you plan to deploy YugabyteDB on AWS Graviton-based EC2 instances, use a custom AMI certified for 64-bit ARM (arm64) architecture.

  If you don't provide an AMI ID, a recent x86 CentOS image is used. For additional information, see [CentOS on AWS](https://wiki.centos.org/Cloud/AWS).

![New Region Modal](/images/ee/aws-setup/aws_new_region.png)

To use automatic provisioning to bring up a universe on [AWS Graviton](https://aws.amazon.com/ec2/graviton/), you need to pass in the Arch AMI ID of AlmaLinux or Ubuntu. Note that this requires a YugabyteDB release for Linux ARM, which is available from [Downloads](https://download.yugabyte.com/). YugabyteDB Anywhere enables you to import releases via S3 or HTTP, as described in [Upgrade the YugabyteDB software](../../../manage-deployments/upgrade-software/).

### Self-managed configuration

You can use your own custom VPCs. This allows you the highest level of customization for your VPC setup. You can provide the following:

- A VPC ID to use for each region.
- A Security Group ID to use for each region. This is attached to all YugabyteDB nodes and must allow traffic from all other YugabyteDB nodes, even across regions, if you deploy across multiple regions.
- A mapping of what Subnet IDs to use for each Availability Zone in which you wish to be able to deploy. This is required to ensure that YugabyteDB Anywhere can deploy nodes in the correct network isolation that you desire in your environment.
- A custom AMI ID to use in each region. For a non-exhaustive list of options, see [Ubuntu 18 and Oracle Linux 8 support](#ubuntu-18-and-oracle-linux-8-support). If you do not provide any values, a recent [AWS Marketplace CentOS AMI](https://wiki.centos.org/Cloud/AWS) is used.

![Custom Region Modal](/images/ee/aws-setup/aws_custom_region.png)

If you choose to provide your own VPC information, you will be responsible for having preconfigured networking connectivity. For single-region deployments, this might just be a matter of region or VPC local Security Groups. Across regions, however, the setup can get quite complex. It is recommended that you use the [VPC peering](https://docs.aws.amazon.com/vpc/latest/peering/working-with-vpc-peering.html) feature of [Amazon Virtual Private Cloud (Amazon VPC)](https://docs.aws.amazon.com/vpc/latest/userguide/what-is-amazon-vpc.html) to set up private IP connectivity between nodes located across regions, as follows:

- VPC peering connections must be established in an N x N matrix, such that every VPC in every region you configure must be peered to every other VPC in every other region.
- Routing table entries in every regional VPC should route traffic to every other VPC CIDR block across the PeeringConnection to that respective VPC. This must match the Subnets that you provided during the configuration step.
- Security groups in each VPC can be hardened by only opening up the relevant ports to the CIDR blocks of the VPCs from which you are expecting traffic.
- If you deploy YugabyteDB Anywhere in a different VPC than the ones in which you intend to deploy YugabyteDB nodes, then its own VPC must also be part of this cross-region VPC mesh, as well as setting up routing table entries in the source VPC (YugabyteDB Anywhere) and allowing one further CIDR block (or public IP) ingress rule on the security groups for the YugabyteDB nodes (to allow traffic from YugabyteDB Anywhere or its VPC).
- When a public IP address is not enabled on a universe, a network address translation (NAT) gateway or device is required. You must configure the NAT gateway before creating the VPC that you add to the YugabyteDB Anywhere UI. For more information, see [NAT](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-nat.html) and [Tutorial: Creating a VPC with Public and Private Subnets for Your Clusters](https://docs.aws.amazon.com/AmazonECS/latest/developerguide/create-public-private-vpc.html) in the AWS documentation.

### Limitations

If you create more than one AWS cloud provider with different CIDR block prefixes, your attempt to create a new VPC as part of your [VPC setup](#vpc-setup) will result in a silent failure.

## Marketplace acceptance

If you did not provide your own custom AMI IDs, before you can proceed to creating a universe, verify that you can actually spin up EC2 instances with the default AMIs in YugabyteDB Anywhere.

While logged into your AWS account, navigate to the AWS site [Marketplace CentOS 7 product](https://aws.amazon.com/marketplace/pp/B00O7WM7QW/) and click **Continue to Subscribe**.

If you are not already subscribed and have not accepted the **Terms and Conditions**, then you should see the following message:

![Marketplace accept](/images/ee/aws-setup/marketplace-accept.png)

If that is the case, click **Accept Terms** and wait for the page to switch to a successful state. After the operation completes, or if you previously subscribed and accepted the terms, you should see the following:

![Marketplace success](/images/ee/aws-setup/marketplace-success.png)

Now, you are ready to create a YugabyteDB universe on AWS.
