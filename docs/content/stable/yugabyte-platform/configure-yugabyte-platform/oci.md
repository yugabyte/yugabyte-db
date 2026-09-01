---
title: Configure the OCI provider configuration
headerTitle: Create cloud provider configuration
linkTitle: Cloud providers
description: Configure the Oracle Cloud Infrastructure (OCI) provider configuration.
headContent: For deploying universes to cloud providers
menu:
  stable_yugabyte-platform:
    identifier: set-up-cloud-provider-4-oci
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">
  <li>
    <a href="../aws/" class="nav-link">
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
    <a href="../oci/" class="nav-link active">
      <i class="icon-oracle" aria-hidden="true"></i>
      OCI
    </a>
  </li>

</ul>

Before you can deploy universes using YugabyteDB Anywhere, you must create a provider configuration.

Create an Oracle Cloud Infrastructure (OCI) provider configuration if your target cloud is OCI, and you can provide full SSH permissions to YugabyteDB Anywhere to orchestrate universe management.

{{< tip title="Use on-premises provider" >}}

If you can't provide YugabyteDB Anywhere with cloud permissions or SSH access to cloud VMs (due to security policies or other restrictions), you can still deploy to OCI using an on-premises provider configuration. Refer to [On-premises provider configurations](../on-premises/).

{{< /tip >}}

When deploying a universe, YugabyteDB Anywhere uses the provider configuration settings to do the following:

- Create VMs on OCI using the following:
  - API signing key credentials or the YBA host's instance principal
  - specified regions and availability domains (this can be a subset of those specified in the provider configuration)
  - a Linux image

- Provision those VMs with YugabyteDB software

## Prerequisites

- An OCI compartment where YugabyteDB Anywhere will create compute instances and block volumes. Required input: Compartment OCID.
- Authentication to OCI, using one of the following:
  - An API signing key for an OCI user with sufficient privileges. Required input: Tenancy OCID, User OCID, API key fingerprint, and PEM private key.
  - Instance principal, if YugabyteDB Anywhere is running on an OCI compute instance that belongs to a dynamic group with sufficient privileges.
- An OCI VCN for each region, with a subnet in each availability domain where you will deploy nodes. Required input: for each region, a VCN OCID; for each availability domain, a Subnet OCID.
- Optionally, an OCI DNS zone if you want YugabyteDB Anywhere to manage canonical names for universes. Required input: DNS Zone OCID.

For more information on setting up OCI credentials, policies, and networking, refer to [Cloud permissions to deploy nodes](../../prepare/cloud-permissions/cloud-permissions-nodes-oci/).

## Configure OCI

Navigate to **Integrations > Infrastructure > Oracle Cloud Infrastructure** to see a list of all currently configured OCI providers.

### Create a provider

To create an OCI provider:

1. Click **Create Config** to open the **Create OCI Provider Configuration** page.

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Validate and Save Configuration** when you are done and wait for the configuration to validate and complete.

    If you want to save your progress, you can skip validation by choosing the **Ignore and save provider configuration anyway** option, which saves the provider configuration without validating. Note that you may not be able to create universes using an incomplete or unvalidated provider.

### View and edit providers

To view a provider, select it in the list of OCI Configs to display the **Overview**.

To edit the provider, select **Config Details**, make changes, and click **Apply Changes**. For more information, refer to [Provider settings](#provider-settings). If the provider has been used to create a universe, you can only edit a subset of fields, including the following:

- Provider Name
- API private key (when using API Key authentication)
- Default Region
- Regions - You can add regions and zones to an in-use provider. Note that you cannot edit existing region details, delete a region if any of the region's zones are in use, or delete zones that are in use.
- Linux version catalog

To view the universes created using the provider, select **Universes**.

To delete the provider, click **Actions** and choose **Delete Configuration**. You can only delete providers that are not in use by a universe.

## Provider settings

### Provider Name

Enter a Provider name. The Provider name is an internal tag used for organizing provider configurations.

### Cloud Info

**Authentication Type**. YugabyteDB Anywhere requires the ability to create VMs in OCI. Choose one of the following:

- **API Key** - Provide an OCI API signing key with the required permissions (refer to [Cloud permissions](../../prepare/cloud-permissions/cloud-permissions-nodes-oci/)). Enter the Tenancy OCID, User OCID, fingerprint, and upload the PEM private key.
- **Instance Principal** - Use the identity of the OCI compute instance that hosts YugabyteDB Anywhere. The instance must belong to a dynamic group with a policy that grants the required permissions. This option is only available if YugabyteDB Anywhere is installed on OCI.

**Compartment OCID**. OCID of the compartment where YugabyteDB Anywhere creates compute instances, volumes, and related resources.

**Default Region**. Region used as the default for OCI API calls (for example, `us-ashburn-1`).

**DNS Zone OCID** (optional). Choose whether to use OCI DNS for universes deployed using this provider. Generally, SQL clients should prefer to use [smart client drivers](/stable/develop/drivers-orms/smart-drivers/) to connect to cluster nodes, rather than load balancers. However, in some cases (for example, if no smart driver is available in the language), you may use a DNS server. YugabyteDB Anywhere can manage Canonical Name (CNAME) entries in an [OCI DNS](https://docs.oracle.com/en-us/iaas/Content/DNS/Concepts/dnszonemanagement.htm) zone and update the DNS entry as nodes get created, removed, or undergo maintenance.

### Regions

You provide existing VCNs; YugabyteDB Anywhere does not create VCNs for OCI providers.

Click **Add Region** to add a region to the configuration. For information on configuring your regions, see [Add regions](#add-regions).

### Linux version catalog

Specify the machine images to be used to install on nodes of universes created using this provider.

To add machine images recommended and provisioned by YugabyteDB Anywhere, select the **Include Linux versions that are chosen and managed by YugabyteDB Anywhere in the catalog** option. YBA-managed images use AlmaLinux OS 9 from the OCI Partner Image Catalog. Internet connectivity from the database nodes is required.

To add your own machine images to the catalog:

1. Click **Add Linux Version**.

1. Provide a name for the Linux version. You can see this name when creating universes using this provider.

1. Choose a CPU architecture.

1. Enter the compute image OCID to use for each [provider region](#regions). Image OCIDs are region-scoped.

1. Provide the SSH user and port to use to access the machine image OS. The SSH user is required; it must have passwordless sudo access and must not be named `yugabyte`. For standard OCI images, use the image's default login user (`opc` for Oracle Linux and AlmaLinux).

1. Click **Add Linux Version**.

To edit custom Linux versions, remove Linux versions, and set a version as the default to use when creating universes, click **...** for the version you want to modify.

### SSH Key Pairs

To be able to provision OCI compute instances with YugabyteDB, YugabyteDB Anywhere requires SSH access.

YugabyteDB Anywhere-managed Linux versions use `opc`.

You can manage SSH key pairs in the following ways:

- Enable YugabyteDB Anywhere to create and manage SSH Key Pairs. In this mode, YugabyteDB Anywhere generates a key pair and injects the public key into instance metadata (`ssh_authorized_keys`) when launching compute instances.
- Use your own existing Key Pairs. To do this, provide the name of the Key Pair, as well as the private key content.

### Advanced

**DB Nodes have public internet access?** If enabled, YugabyteDB Anywhere installs some software packages on the DB nodes by downloading from the public internet. If not, all installation of software on the nodes downloads from only this YugabyteDB Anywhere instance.

You can customize the Network Time Protocol server, as follows:

- Select **Use OCI's NTP Server** to enable cluster nodes to connect to the OCI internal time servers.
- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, you will be responsible for manually configuring NTP.

    {{< warning title="Important" >}}

Use this option with caution. Time synchronization is critical to database data consistency; failure to run NTP may cause data loss.

    {{< /warning >}}

### Add regions

For deployment, you can select the regions where you wish to deploy.

You are responsible for having preconfigured networking connectivity. For single-region deployments, this might just be a matter of VCN local security lists or network security groups. Across regions, you must peer the VCNs so that nodes can communicate on private IPs. It is recommended that you use [remote VCN peering](https://docs.oracle.com/en-us/iaas/Content/Network/Tasks/remoteVCNpeering.htm) through a Dynamic Routing Gateway (DRG), as follows:

- VCNs in every region you configure must be peered to every other regional VCN.
- Routing table entries in every regional VCN should route traffic to every other VCN CIDR block across the DRG. This must match the subnets that you provide during the configuration step.
- Security lists or network security groups in each VCN can be hardened by only opening up the relevant ports to the CIDR blocks of the VCNs from which you are expecting traffic. For the ports YugabyteDB requires, see [Networking](../../prepare/networking/).
- If you deploy YugabyteDB Anywhere in a different VCN than the ones in which you intend to deploy YugabyteDB nodes, then its own VCN must also be part of this cross-region mesh, as well as setting up routing table entries in the source VCN (YugabyteDB Anywhere) and allowing one further CIDR block (or public IP) ingress rule on the security lists for the YugabyteDB nodes (to allow traffic from YugabyteDB Anywhere or its VCN).
- When a public IP address is not enabled on a universe, a NAT gateway is required so that nodes can reach package repositories and the OCI APIs as needed. You must configure the NAT gateway before creating the VCN that you add to the YugabyteDB Anywhere UI. For more information, see [NAT Gateway](https://docs.oracle.com/en-us/iaas/Content/Network/Tasks/managingNATgateway.htm) in the OCI documentation.

To configure a region using your own VCNs, click **Add Region** and do the following:

1. Select the **Region**.
1. Specify the **VCN ID** of the VCN to use for the region. This must be an OCID in the form `ocid1.vcn...`.
1. Optionally, specify an **Instance Configuration OCID** to seed launch details (such as shape or tags) for nodes created in this region. YugabyteDB Anywhere still overrides shape, image, subnet, and SSH metadata from the universe and provider settings.

For each availability domain in which you wish to be able to deploy in the region, do the following:

1. Click **Add Zone**.
1. Select the zone (availability domain).
1. Enter the Subnet OCID to use for the zone. This is required to ensure that YugabyteDB Anywhere can deploy nodes in the correct network isolation that you desire in your environment.

## Partner Image Catalog

If you use YBA-managed Linux versions, before you can proceed to creating a universe, verify that your tenancy can launch compute instances from the AlmaLinux OS 9 Partner Image Catalog listings.

While logged into the OCI Console, go to **Compute > Partner Images** (or **Marketplace**), subscribe to AlmaLinux OS 9 for x86_64, and accept the terms. If you plan to deploy ARM (Ampere) shapes using a custom Linux version, also subscribe to the AArch64 listing.

Do this in every region where you intend to deploy database clusters.
