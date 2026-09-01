---
title: Cloud setup for deploying universe nodes on OCI
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying universe nodes using an OCI provider configuration.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  stable_yugabyte-platform:
    identifier: cloud-permissions-nodes-5-oci
    parent: cloud-permissions
    weight: 20
type: docs
---

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
    <a href="../cloud-permissions-nodes-azure" class="nav-link">
      <i class="fa-brands fa-microsoft"></i>
      Azure
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-oci/" class="nav-link active">
      <i class="icon-oracle" aria-hidden="true"></i>
      OCI
    </a>
  </li>
  <li>
    <a href="../cloud-permissions-nodes-k8s" class="nav-link">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

For YugabyteDB Anywhere (YBA) to be able to deploy and manage YugabyteDB universes using an OCI [cloud provider configuration](../../../yba-overview/#public-cloud), you need to provide YBA with privileges on your cloud infrastructure to create, delete, and modify VMs, mount and unmount disk volumes, and so on. The more permissions that you can provide, the more YBA can automate.

{{<tip>}}
If you can't provide YBA with the necessary permissions, you can still deploy to OCI using an [on-premises provider](../cloud-permissions-nodes/).
{{</tip>}}

## OCI

The following policy statements are required in the compartment where you will deploy universes. Replace `yba-admins` with the group that the API-key user belongs to (or the dynamic group, if using instance principal), and `<compartment>` with the compartment name or OCID.

```properties
Allow group yba-admins to manage instance-family in compartment <compartment>
Allow group yba-admins to manage volume-family in compartment <compartment>
Allow group yba-admins to use virtual-network-family in compartment <compartment>
Allow group yba-admins to manage app-catalog-listing in tenancy
Allow group yba-admins to inspect compartments in tenancy
```

If you will use an Instance Configuration OCID when adding regions, also grant:

```properties
Allow group yba-admins to read instance-configurations in compartment <compartment>
```

For more information on writing policies, see [How Policies Work](https://docs.oracle.com/en-us/iaas/Content/Identity/Concepts/policies.htm) in the OCI documentation.

To grant the required access, you do one of the following:

- Create an API signing key for an OCI user in a group that has the policy. You'll later provide YBA with the Tenancy OCID, User OCID, fingerprint, and PEM private key when creating the OCI provider configuration.
- Assign the YugabyteDB Anywhere compute instance to a dynamic group that has the policy, and authenticate with instance principal.

### API signing key

If using an API signing key, record the following information. You will need to provide this information later to YBA.

Generate an API signing key in the OCI Console under the user's **API Keys**, and download the PEM private key. For more information, see [Required Keys and OCIDs](https://docs.oracle.com/en-us/iaas/Content/API/Concepts/apisigningkey.htm) in the OCI documentation.

If you are intending to back up to OCI Object Storage, the same user (or instance principal) can also be granted object-storage permissions; refer to [Permissions to back up and restore](../cloud-permissions-storage/).

| Save for later | To configure |
| :--- | :--- |
| Tenancy OCID | [OCI provider configuration](../../../configure-yugabyte-platform/oci/) |
| User OCID | |
| API key fingerprint | |
| PEM private key | |
| Compartment OCID | |

### Instance principal

If YugabyteDB Anywhere is running on an OCI compute instance, you can authenticate using instance principal instead of storing an API signing key.

1. Create a dynamic group whose matching rule includes the YBA instance. For example:

    ```properties
    ALL {instance.id = '<yba-instance-ocid>'}
    ```

    For more information, see [Managing Dynamic Groups](https://docs.oracle.com/en-us/iaas/Content/Identity/Tasks/managingdynamicgroups.htm) in the OCI documentation.

1. Create a policy that grants the dynamic group the same permissions listed above, replacing `group yba-admins` with `dynamic-group <dynamic-group-name>`.

1. When creating the OCI provider configuration, choose **Instance Principal** as the authentication type.

### Provide access to compute images

In addition to OCI cloud permissions, to create VMs on OCI YBA needs access to the operating system disk images.

You must grant this access, and also accept any OS licensing terms manually before providing this access to YBA.

By default, YBA requires access to the AlmaLinux OS 9 x86_64 and AArch64 Partner Image Catalog listings.

#### Default case

If you plan to use YBA defaults, then, while logged into the OCI Console, go to **Compute > Partner Images**, subscribe to AlmaLinux OS 9 for both x86_64 and AArch64, and accept the terms.

If needed, be sure to do this in every region where you intend to deploy database clusters.

#### Custom disk image

If you plan to use a custom operating system and disk image, then verify that the [API signing key user](#api-signing-key) or [instance principal](#instance-principal) that you provisioned earlier has access to the required OS disk image (that is, the specific image OCID) in every region where you intend to deploy database clusters.

## Managing SSH keys for VMs

When creating VMs on the public cloud using a [cloud provider configuration](../../../yba-overview/#public-cloud), YugabyteDB requires SSH keys to access the VM. You can manage the SSH keys for VMs in two ways:

- YBA managed keys. When YBA creates VMs, it will generate and manage the SSH key pair.
- Provide a custom key pair. Create your own custom SSH keys and upload the SSH keys when you create the provider.

YBA injects the public key into instance metadata (`ssh_authorized_keys`) when launching compute instances. The key pair authenticates as the image's default login user. For the user requirements, see [Software requirements for cloud provider nodes](../../server-nodes-software/software-cloud-provider/).

If you will be using your own custom SSH keys, ensure they are authorized for that user and that you have them when installing YBA and creating your OCI cloud provider configuration.

| Save for later | To configure |
| :--- | :--- |
| Custom SSH keys | [OCI provider configuration](../../../configure-yugabyte-platform/oci/) |
