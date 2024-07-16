---
title: Cloud setup for deploying YugabyteDB Anywhere
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying YugabyteDB universe nodes.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  preview_yugabyte-platform:
    identifier: cloud-permissions-nodes-4-azure
    parent: cloud-permissions
    weight: 20
type: docs
---

For YugabyteDB Anywhere (YBA) to be able to deploy and manage YugabyteDB clusters, you need to provide YBA with privileges on your cloud infrastructure to create, delete, and modify VMs, mount and unmount disk volumes, and so on.

The more permissions that you can provide, the more YBA can automate.

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../cloud-permissions-nodes/" class="nav-link">
      <i class="fa-solid fa-building"></i>
      On-premises
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-aws/" class="nav-link">
      <i class="fa-brands fa-aws"></i>
      AWS
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-gcp" class="nav-link">
      <i class="fa-brands fa-google"></i>
      GCP
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-azure" class="nav-link active">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-k8s" class="nav-link">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

## Azure

### Application and resource group

YugabyteDB Anywhere requires cloud permissions to create VMs. You grant YugabyteDB Anywhere access to manage Azure resources such as VMs by registering an application in the Azure portal so the Microsoft identity platform can provide authentication and authorization services for your application. Registering your application establishes a trust relationship between your application and the Microsoft identity platform.

For more information, refer to [Register an application with the Microsoft identity platform](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app?tabs=certificate) in the Microsoft Entra documentation.

Your Azure application needs to have a [resource group](https://learn.microsoft.com/en-us/azure/azure-resource-manager/management/overview#resource-groups) with the following permissions:

```sh
Network Contributor
Virtual Machine Contributor 
```

For more information, refer to [Assign Azure roles using the Azure portal](https://learn.microsoft.com/en-us/azure/role-based-access-control/role-assignments-portal?tabs=delegate-condition) in the Microsoft Azure documentation.

### Credentials

YugabyteDB Anywhere can authenticate with Azure using one of the following methods:

- [Add credentials](https://learn.microsoft.com/en-us/entra/identity-platform/quickstart-register-app?tabs=client-secret#add-credentials), in the form of a client secret, to your registered application.

    For information on creating client secrets, see [Create a new client secret](https://learn.microsoft.com/en-us/entra/identity-platform/howto-create-service-principal-portal#option-3-create-a-new-client-secret) in the Microsoft Entra documentation.

- [Assign a managed identity](https://learn.microsoft.com/en-us/entra/identity/managed-identities-azure-resources/qs-configure-portal-windows-vm) to the Azure VM hosting YugabyteDB Anywhere. Azure will use the managed identity assigned to your instance to authenticate.

    For information on assigning roles for managed identities, see [Assign Azure roles using the Azure portal](https://learn.microsoft.com/en-us/azure/role-based-access-control/role-assignments-portal?tabs=delegate-condition) in the Microsoft Azure documentation.

Record the following information about your service account. You will need to provide this information later when creating an Azure provider configuration.

| Save for later | To configure |
| :--- | :--- |
| **Service account details** | [Azure provider configuration](../../../configure-yugabyte-platform/azure/) |
| Client ID: | |
| Client Secret:<br>(not required when using managed identity) | |
| Resource Group: | |
| Subscription ID: | |
| Tenant ID: | |

## Managing SSH keys for VMs

When creating VMs on the public cloud, YugabyteDB requires SSH keys to access the VM. You can manage the SSH keys for VMs in two ways:

- YBA managed keys. When YBA creates VMs, it will generate and manage the SSH key pair.
- Provide a custom key pair. Create your own custom SSH keys and upload the SSH keys when you create the provider.

If you will be using your own custom SSH keys, then ensure that you have them when installing YBA and creating your public cloud provider.

| Save for later | To configure |
| :--- | :--- |
| Custom SSH keys | [Azure provider configuration](../../../configure-yugabyte-platform/azure/) |
