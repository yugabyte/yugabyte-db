---
title: Cloud setup for deploying YugabyteDB Anywhere
headerTitle: To deploy nodes
linkTitle: To deploy nodes
description: Prepare your cloud for deploying YugabyteDB universe nodes.
headContent: Prepare your cloud for deploying YugabyteDB universe nodes
menu:
  stable_yugabyte-platform:
    identifier: cloud-permissions-nodes-3-gcp
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
    <a href="../cloud-permissions-nodes-gcp" class="nav-link active">
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
    <a href="../cloud-permissions-nodes-k8s" class="nav-link">
      <i class="fa-regular fa-dharmachakra"></i>
      Kubernetes
    </a>
  </li>
</ul>

## GCP

The [Compute Admin role](https://cloud.google.com/compute/docs/access/iam#compute.admin) permission is required on the GCP service account where you will deploy:

```sh
roles/compute.admin
```

To grant the required access, you must do the following:

- Create a service account in GCP.
- [Grant the required roles](https://cloud.google.com/iam/docs/grant-role-console) to the account.

Then use one of the following methods:

- Obtain a file containing a JSON that describes the service account credentials. You will need to provide this file later to YBA.
- [Attach the service account](https://cloud.google.com/compute/docs/access/create-enable-service-accounts-for-instances#using) to the GCP VM that will run YBA.

| Save for later | To configure |
| :--- | :--- |
| Service account JSON | [GCP provider configuration](../../../configure-yugabyte-platform/gcp/) |

## Managing SSH keys for VMs

When creating VMs on the public cloud, YugabyteDB requires SSH keys to access the VM. You can manage the SSH keys for VMs in two ways:

- YBA managed keys. When YBA creates VMs, it will generate and manage the SSH key pair.
- Provide a custom key pair. Create your own custom SSH keys and upload the SSH keys when you create the provider.

If you will be using your own custom SSH keys, then ensure that you have them when installing YBA and creating your public cloud provider.

| Save for later | To configure |
| :--- | :--- |
| Custom SSH keys | [GCP provider configuration](../../../configure-yugabyte-platform/gcp/) |
