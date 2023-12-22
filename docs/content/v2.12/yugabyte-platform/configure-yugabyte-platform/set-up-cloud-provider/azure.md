---
title: Configure the Microsoft Azure cloud provider
headerTitle: Configure the Microsoft Azure cloud provider
linkTitle: Configure the cloud provider
description: Configure the Microsoft Azure cloud provider
menu:
  v2.12_yugabyte-platform:
    identifier: set-up-cloud-provider-3-azure
    parent: configure-yugabyte-platform
    weight: 20
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp" class="nav-link">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure" class="nav-link active">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="../kubernetes" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="../vmware-tanzu" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="../openshift" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="../on-premises" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

You can configure an Azure cloud provider for YugabyteDB clusters using the Yugabyte Platform console.

## Prerequisites

You need to add the following cloud provider credentials via the Yugabyte Platform console:

- Subscription ID
- Tenant ID
- SSH port and user
- Application client ID and secret
- Resource group

Yugabyte Platform uses the credentials to automatically provision and deprovision YugabyteDB instances.

When the configuration is completed, you can see all the resources managed by Yugabyte Platform in your resource group, including virtual machines, network interface, network security groups, public IP addresses, and disks.

## Configure Azure

You configure the Microsoft Azure cloud provider by completing the fields of the configuration page shown in the following illustration:

![Prepare Azure cloud to install Yugabyte Platform](/images/yb-platform/install/azure/platform-azure-prepare-cloud-env-4.png)

- **Provider Name** translates to an internal Yugabyte Platform tag used for organizing cloud providers.
- **Subscription ID** is required for cost management. The virtual machine resources managed by Yugabyte Platform are tagged with this subscription.
- **Resource Group** represents the group in which YugabyteDB nodes compute and network resources are created. Your Azure Active Directory application (client ID and client secret) needs to have `Network Contributor` and `Virtual Machine Contributor` roles assigned for this resource group.
- **Tenant ID** represents the Azure Active Directory tenant ID which belongs to an active subscription. To find your tenant ID, follow instructions provided in [How to find your Azure Active Directory tenant ID](https://learn.microsoft.com/en-us/azure/active-directory/fundamentals/how-to-find-tenant).
- **SSH Port** allows you to specify the connection port number if you use custom images. The default port is 54422.
- **SSH User** represents the user name for the **SSH Port**.
- **Client ID** represents the [ID of an application](https://docs.microsoft.com/en-us/azure/active-directory/develop/howto-create-service-principal-portal#option-2-create-a-new-application-secret) registered in your Azure Active Directory.
- **Client Secret** represents the secret of an application registered in your Azure Active Directory.
- **Private DNS zone** lets you use a custom domain name for the nodes in your universe. For details and instructions, see [How to define a private DNS zone](#how-to-define-a-private-dns-zone).
- **Virtual Network Setup** allows you to customize your network, including the virtual network.

### How to obtain Azure resource IDs

To find an Azure resource's ID, navigate to the resource in question and click **JSON View** at the top right, as per the following illustration:

![JSON view link on a resource](/images/yb-platform/install/azure/resource-get-json.png)

Azure resource IDs typically have the following format:

```output
/subscriptions/<subscriptionID>/resourceGroups/<resourceGroup>/providers/Microsoft.<service>/path/to/resource
```

### How to define a private DNS zone

You may choose to define a private DNS zone to instruct Yugabyte Platform to register the universe name to all of the IP addresses in the universe in that DNS zone. For more information, see [What is a private Azure DNS zone](https://docs.microsoft.com/en-us/azure/dns/private-dns-privatednszone).

You can set a private DNS zone as follows:

1. On the Azure portal, create the Private DNS Zone, as per the following illustration:

    ![Private DNS: basics tab](/images/yb-platform/install/azure/private-dns-basics-tab.png)

1. Navigate to the resource page and click **Settings > Virtual Network Links** on the left tab, as per the following illustration:

    ![Resource menu](/images/yb-platform/install/azure/resource-menu.png)

1. Add a link to the virtual network to which you want it to be connected. For more information, see [Create an Azure private DNS zone using the Azure portal](https://docs.microsoft.com/en-us/azure/dns/private-dns-getstarted-portal).

1. To use the private DNS zone in Yugabyte Platform, add either the resource ID or the name of the DNS zone to the **Private DNS Zone** field of the **Cloud Provider Configuration** page in the Yugabyte Platform UI.

    Note that if you provide the Resource ID, Yugabyte Platform infers the resource group from it. If you provide only the name, then Yugabyte Platform assumes that the resource group is the same as the group used in the cloud provider.

In the setup example shown in the following illustration, the private DNS zone is specified as `dns.example.com`, and the resource group is explicitly specified as `myRG`:

![Private DNS setup example](/images/yb-platform/install/azure/private-dns-myrg.png)

In the following setup example, a complete resource ID is specified for the private DNS zone:

```output
/subscriptions/SUBSCRIPTION_ID/resourceGroups/different-rg/providers/Microsoft.Network/privateDnsZones/dns.example.com
```

The preceding setting specifies the resource group as `different-rg` and the DNS zone as `dns.example.com`. The `different-rg` resource group in the resource ID overrides the setting in the **Resource Group** field, as shown in the following illustration:

![img](/images/yb-platform/install/azure/private-dns-myrg-2.png)

## Specify region

You can specify a region as follows:

1. Click **Add Region**.

2. Use the **Specify Region Info** dialog to select a region and provide a virtual network name from your Azure portal.

3. Optionally, specify the security group, if the database VM is in a different network than the platform.

4. Provide a URN to a marketplace image or a shared gallery image by following instructions provided in [How to use a shared image gallery](#how-to-use-a-shared-image-gallery). If you are using custom images, you need to specify the SSH port and user, as described in [Configure Azure](#configure-azure).

5. Provide a mapping of subnet IDs to use for each availability zone you wish to deploy. This is required for ensuring that Yugabyte Platform can deploy nodes in the correct network isolation that you need in your environment.

   ![Prepare Azure cloud to install Yugabyte Platform](/images/yb-platform/install/azure/platform-azure-prepare-cloud-env-5.png)

6. Click **Add Region** on the **Specify Region Info** dialog.

7. Click **Save** on the **Cloud Provider Configuration** page.

Typically, it takes a few minutes for the cloud provider to be configured. When the configuration completes, you will be ready to create a YugabyteDB universe on Azure.

### How to use a shared image gallery

You can use shared image galleries as an alternative to using marketplace image URNs. A gallery allows you to provide your own custom image to use for creating universe instances. For more information on shared image galleries, refer to [Store and share images in an Azure Compute Gallery](https://docs.microsoft.com/en-us/azure/virtual-machines/shared-image-galleries).

You set up a shared gallery image on Azure as follows:

1. On the Azure portal, create a shared image gallery.

1. Create an image definition.

1. Create a VM of which you want to take a snapshot.

1. Navigate to the VM and click **Capture** on the top menu.

1. Fill in the information and then choose the gallery and image definition you created in the previous steps, as per the following illustration:

    ![img](/images/yb-platform/install/azure/shared-gallery-capture.png)

    Ensure that the images are replicated to each region in which you are planning to use them. For example, configuration shown in the following illustration would only work for US East:

    ![description](/images/yb-platform/install/azure/shared-gallery-replication.png)

1. To use the image in Platform, put the image version's resource ID into the Marketplace Image URN/Shared Gallery Image ID field of the Region Info popup.

    The gallery image ID could be defined by a full URL containing a subscription ID, a resource group name, and the resource name itself. If the subscription ID or the resource group is different from the default values, Yugabyte Platform will use them instead.
