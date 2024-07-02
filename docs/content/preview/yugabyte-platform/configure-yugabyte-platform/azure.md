---
title: Configure the Microsoft Azure cloud provider
headerTitle: Create cloud provider configuration
linkTitle: Cloud providers
description: Configure the Microsoft Azure provider configuration
headContent: For deploying universes on Azure
aliases:
  - /preview/deploy/enterprise-edition/configure-cloud-providers/azure
menu:
  preview_yugabyte-platform:
    identifier: set-up-cloud-provider-3-azure
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
    <a href="../azure/" class="nav-link active">
      <i class="icon-azure" aria-hidden="true"></i>
      Azure
    </a>
  </li>

</ul>

Before you can deploy universes using YugabyteDB Anywhere (YBA), you must create a provider configuration. Create a Microsoft Azure provider configuration if your target cloud is Azure.

When deploying a universe, YBA uses the provider configuration settings to do the following:

- Create VMs on Azure using the following:
  - the resource group
  - specified regions and availability zones (this can be a subset of those specified in the provider configuration)
  - a Linux image

- Provision those VMs with YugabyteDB software

## Prerequisites

You need to add the following Azure cloud provider credentials via YBA:

- Subscription ID
- Tenant ID
- SSH port and user
- Application client ID and secret
- Resource group

YBA uses the credentials to automatically provision and deprovision YugabyteDB instances.

For more information on setting up an Azure account and resource groups, refer to [Cloud permissions to deploy nodes](../../prepare/cloud-permissions/cloud-permissions-nodes-azure/).

## Configure Azure

Navigate to **Configs > Infrastructure > Microsoft Azure** to see a list of all currently configured Azure providers.

### Create a provider

To create an Azure provider:

1. Click **Create Config** to open the **Create Azure Provider Configuration** page.

    ![Create Azure provider](/images/yb-platform/config/yba-azure-config-create-220.png)

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Validate and Save Configuration** when you are done and wait for the configuration to validate and complete.

    If you want to save your progress, you can skip validation by choosing the **Ignore and save provider configuration anyway** option, which saves the provider configuration without validating. Note that you may not be able to create universes using an incomplete or unvalidated provider.

The create provider process includes configuring a network, subnetworks in all available regions, firewall rules, VPC peering for network connectivity, and a custom SSH key pair for YugabyteDB Anywhere-to-YugabyteDB connectivity.

When the configuration is completed, you can see all the resources managed by YBA in your resource group, including virtual machines, network interface, network security groups, public IP addresses, and disks.

If you encounter problems, see [Troubleshoot Azure cloud provider configuration](../../troubleshoot/cloud-provider-config-issues/#azure-cloud-provider-configuration-problems).

### View and edit providers

To view a provider, select it in the list of AZU Configs to display the **Overview**.

To edit the provider, select **Config Details**, make changes, and click **Apply Changes**. For more information, refer to [Provider settings](#provider-settings). Note that for YBA version 2.20.1 and later, depending on whether the provider has been used to create a universe, you can only edit a subset of fields such as the following:

- Provider Name
- Client Secret
- Regions - You can add regions and zones to an in-use provider. Note that you cannot edit existing region details, delete a region if any of the region's zones are in use, or delete zones that are in use.

To view the universes created using the provider, select **Universes**.

To delete the provider, click **Actions** and choose **Delete Configuration**. You can only delete providers that are not in use by a universe.

## Provider settings

### Provider Name

Enter a Provider name. The Provider name is an internal tag used for organizing cloud providers.

### Cloud Info

- **Client ID** represents the [ID of an application](https://docs.microsoft.com/en-us/azure/active-directory/develop/howto-create-service-principal-portal#option-2-create-a-new-application-secret) registered in your Azure Active Directory.
- **Client Secret** represents the secret of an application registered in your Azure Active Directory. You need to enter the `Value` of the secret (not the `Secret ID`).
- **Resource Group** represents the group in which YugabyteDB nodes compute and network resources are created. Your Azure Active Directory application (client ID and client secret) needs to have `Network Contributor` and `Virtual Machine Contributor` roles assigned for this resource group.
- **Subscription ID** is required for cost management. The virtual machine resources managed by YBA are tagged with this subscription.
- **Tenant ID** represents the Azure Active Directory tenant ID which belongs to an active subscription. To find your tenant ID, follow instructions provided in [How to find your Azure Active Directory tenant ID](https://learn.microsoft.com/en-us/azure/active-directory/fundamentals/how-to-find-tenant).
- **Private DNS zone** lets you use a custom domain name for the nodes in your universe. For details and instructions, see [Define a private DNS zone](#define-a-private-dns-zone).

### Regions

You can specify a region as follows:

1. Click **Add Region**.

1. Use the **Add Region** dialog to select a region and provide a virtual network name from your Azure portal.

1. Optionally, specify the security group, if the database VM is in a different network than YBA.

1. Click **Add Zone** and provide a mapping of subnet IDs to use for each availability zone you wish to deploy. This is required for ensuring that YBA can deploy nodes in the correct network isolation that you need in your environment.

1. Click **Add Region**.

### Linux version catalog

Specify the machine images to be used to install on nodes of universes created using this provider.

To add machine images recommended and provisioned by YBA, select the **Include Linux versions that are chosen and managed by YugabyteDB Anywhere in the catalog** option, and choose the architectures.

To add your own machine images to the catalog:

1. Click **Add Linux Version**.

1. Provide a name for the Linux version. You can see this name when creating universes using this provider.

1. Provide a URN to a marketplace image or a shared gallery image by following instructions provided in [Use a shared image gallery](#use-a-shared-image-gallery).

1. Provide the SSH user and port to use to access the machine image OS. Leave this empty to use the [default SSH user](#ssh-key-pairs).

1. Click **Add Linux Version**.

To edit custom Linux versions, remove Linux versions, and set a version as the default to use when creating universes, click **...** for the version you want to modify.

### SSH Key Pairs

To be able to provision cloud instances with YugabyteDB, YBA requires SSH access.

Enter the SSH user and port to use by default for machine images. You can override these values for custom Linux versions that you add to the Linux Version Catalog.

You can manage SSH key pairs in the following ways:

- Enable YBA to create and manage Key Pairs. In this mode, YBA creates SSH Key Pairs and stores the relevant private key so that you will be able to SSH into future instances.
- Use your own existing Key Pairs. To do this, provide the name of the Key Pair, as well as the private key content.

### Advanced

You can customize the Network Time Protocol server, as follows:

- Select **Use AZU's NTP server** to enable cluster nodes to connect to the Azure internal time servers. For more information, consult the Microsoft Azure documentation such as [Time sync for Linux VMs in Azure](https://docs.microsoft.com/en-us/azure/virtual-machines/linux/time-sync).
- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YBA from performing any NTP configuration on the cluster nodes. For data consistency, you will be responsible for manually configuring NTP.

    {{< warning title="Important" >}}

Use this option with caution. Time synchronization is critical to database data consistency; failure to run NTP may cause data loss.

    {{< /warning >}}

## Configuring Azure Portal

### Obtain Azure resource IDs

To find an Azure resource's ID, navigate to the resource in question and click **JSON View** at the top right, as per the following illustration:

![JSON view link on a resource](/images/yb-platform/install/azure/resource-get-json.png)

Azure resource IDs typically have the following format:

```output
/subscriptions/<subscriptionID>/resourceGroups/<resourceGroup>/providers/Microsoft.<service>/path/to/resource
```

### Define a private DNS zone

You may choose to define a private DNS zone to instruct YBA to register the universe name to all of the IP addresses in the universe in that DNS zone. For more information, see [What is a private Azure DNS zone](https://docs.microsoft.com/en-us/azure/dns/private-dns-privatednszone).

You can set a private DNS zone as follows:

1. On the Azure portal, create the Private DNS Zone, as per the following illustration:

    ![Private DNS: basics tab](/images/yb-platform/install/azure/private-dns-basics-tab.png)

1. Navigate to the resource page and click **Settings > Virtual Network Links**, as per the following illustration:

    ![Resource menu](/images/yb-platform/install/azure/resource-menu.png)

1. Add a link to the virtual network to which you want it to be connected. For more information, see [Create an Azure private DNS zone using the Azure portal](https://docs.microsoft.com/en-us/azure/dns/private-dns-getstarted-portal).

1. To use the private DNS zone in YBA, add either the resource ID or the name of the DNS zone to the **Private DNS Zone** field of the **Cloud Provider Configuration** page in the YBA UI.

    If the private DNS zone is defined by an ID, YBA will use it together with the default subscription ID and the resource group. If the private DNS zone is defined by a full URL that contains both the subscription ID and resource group, then these two values will be used instead of default values.

In the setup example shown in the following illustration, the private DNS zone is specified as `dns.example.com`, and the resource group is explicitly specified as `myRG`:

![Private DNS setup example](/images/yb-platform/install/azure/private-dns-myrg.png)

In the following setup example, a complete resource ID is specified for the private DNS zone:

```output
/subscriptions/SUBSCRIPTION_ID/resourceGroups/different-rg/providers/Microsoft.Network/privateDnsZones/dns.example.com
```

The preceding setting specifies the resource group as `different-rg` and the DNS zone as `dns.example.com`. The `different-rg` resource group in the resource ID overrides the setting in the **Resource Group** field, as shown in the following illustration:

![Azure Private DNS override](/images/yb-platform/install/azure/private-dns-myrg-2.png)

### Use a shared image gallery

You can use shared image galleries as an alternative to using marketplace image URNs. A gallery allows you to provide your own custom image to use for creating universe instances. For more information on shared image galleries, refer to [Store and share images in an Azure Compute Gallery](https://docs.microsoft.com/en-us/azure/virtual-machines/shared-image-galleries).

You set up a shared gallery image on Azure as follows:

1. On the Azure portal, create a shared image gallery.

1. Create an image definition.

1. Create a VM of which you want to take a snapshot.

1. Navigate to the VM and click **Capture** on the top menu.

1. Fill in the information and then choose the gallery and image definition you created in the previous steps, as per the following illustration:

    ![Azure Shared Image Gallery](/images/yb-platform/install/azure/shared-gallery-capture.png)

    Ensure that the images are replicated to each region in which you are planning to use them. For example, configuration shown in the following illustration would only work for US East:

    ![Azure Shared Image Gallery example](/images/yb-platform/install/azure/shared-gallery-replication.png)

1. To use the image in YBA, enter the image version's resource ID into the **Marketplace Image URN / Shared Gallery Image ID** field of the **Specify Region Info** dialog.

    The gallery image ID could be defined by a full URL containing a subscription ID, a resource group name, and the resource name itself. If the subscription ID or the resource group is different from the default values, YBA will use them instead.
