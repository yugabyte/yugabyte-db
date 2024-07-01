---
title: Prepare the Google Cloud Platform (GCP) environment
headerTitle: Cloud prerequisites
linkTitle: Cloud prerequisites
description: Prepare the Google Cloud Platform (GCP) environment
headContent: Prepare GCP for YugabyteDB Anywhere
menu:
  v2.20_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: prepare-environment-2-gcp
    weight: 55
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws/" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="../gcp/" class="nav-link active">
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
      <i class="fa-regular fa-dharmachakra" aria-hidden="true"></i>
      Kubernetes
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
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

## Create a project (optional)

A project forms the basis for creating, enabling, and using all GCP services, managing APIs, enabling billing, adding and removing collaborators, and managing permissions.

For instructions on how to create a project using [GCP cloud resource manager](https://console.cloud.google.com/cloud-resource-manager), see [Create and managing projects](https://cloud.google.com/resource-manager/docs/creating-managing-projects) in the GCP documentation.

You should include `yugabyte` as part of the project name (for example, `yugabyte-gcp`) and note the project ID.

## Create a service account

YugabyteDB Anywhere requires a service account with the appropriate permissions to provision and manage compute instances.

To create a service account, perform the following:

- Open your project in GCP and use the left-side menu to navigate to **IAM & Admin > Service Accounts**.

- Click **Create Service Account**.

- Complete the **Service account details** fields and click **Create and Continue**.

- In the **Grant this service account access to project** section, select the **Owner** role.

- In the **Grant users access to this service account** section, enter the email associated with this service account. To retrieve the email information, navigate to **IAM & Admin > Service Accounts** and copy the **Email** value.

- Navigate back to **IAM & Admin > Service Accounts**, click the email address of the service account, and then select the **KEYS** tab.

- Click **ADD KEY** and select **Create new key**.

- In the **Create private key** dialog, select **JSON** as the key type, and then click **Create** to download a service account key file. Note that after you download the key, you need to store this file, as you cannot download it again and this key is required for configuring the YugabyteDB Anywhere UI.

  For additional information, see [Creating and managing service account keys](https://cloud.google.com/iam/docs/creating-managing-service-account-keys) in the GCP documentation.

- Navigate to **IAM & Admin > IAM**, click **ADD**, and then provide principals and roles.

For more information, see [Creating and managing service accounts](https://cloud.google.com/iam/docs/creating-managing-service-accounts) in the GCP documentation.

## Create a firewall rule

In order to access YugabyteDB Anywhere from outside the GCP environment, you have to enable firewall rules. At a minimum, you need to be able to do the following:

- Access the YugabyteDB Anywhere instance over SSH (port `tcp:22`).
- Check, manage, and upgrade YugabyteDB Anywhere (port `tcp:8800`).
- View the YugabyteDB Anywhere UI (port `tcp:80`).

If you are using your own Virtual Private Cloud (VPC) as a self-managed configuration, the following additional TCP ports must be accessible: 7000, 7100, 9000, 9100, 18018, 11000, 12000, 13000, 9300, 9042, 5433, 6379, 54422. For more information, see [Default ports](../../../../reference/configuration/default-ports).

Next, you need to create a firewall entry, as follows:

- From your project's main page, navigate to **VPC network > Firewall**.
- Create firewall rules by following instructions provided in [Using firewall rules](https://cloud.google.com/vpc/docs/using-firewalls) in the GCP documentation. When creating the rules:
  - Add a tag `yugabyte-server` to the **Target tags** field.
  - Add the appropriate IP addresses to the **Source IP ranges** field.
  - Enter a comma-delimited list of TCP ports 22, 8800, and 80 to the **Protocol and ports** field. If required, also add TCP ports for a self-managed configuration.

## Provision instance for YugabyteDB Anywhere

You need to create an instance to run YugabyteDB Anywhere. To do this, from your project's main page, navigate to **Compute Engine > VM instances**, click **Create**, and then follow instructions provided in [Virtual machine instances](https://cloud.google.com/compute/docs/instances) in the GCP documentation. When creating instances:

- Select a region as, for example, us-west1.
- Select a zone as, for example, us-west1-b.
- Select 4 vCPUs (n1-standard-4) as the machine type.
- Change the boot disk image to Ubuntu 18.04 TLS and increase the boot disk size to 100.
- Specify whether to use the default or your own service account.
- Specify whether to use the default or your own VPC.
- Use the **Networking** tab to add `yugabyte-server` as the network tag (or the custom name you chose when setting up the firewall rules).

After the instance has been created, use the **SSH Keys** tab to add a custom public key and a login user to this instance. To do so, you start by creating a key-pair, as follows:

```sh
ssh-keygen -t rsa -f ~/.ssh/yugabyte-1-gcp -C <login-user>
```

You can set the appropriate credentials for the SSH key as follows:

```sh
chmod 400 ~/.ssh/yugabyte-1-gcp
```

Enter the contents of `yugabyte-1-gcp.pub` as the value for this field.

For more information, see the following GCP documentation:

- [Cloud Key Management Service](https://cloud.google.com/blog/products/gcp/protect-your-compute-engine-resources-with-keys-managed-in-cloud-key-management-service)

- [Choosing an access method](https://cloud.google.com/compute/docs/instances/access-overview#metadatavalues) provides details on how to create a new SSH key pair, as well as the expected format for this field: `ssh-rsa [KEY_VALUE] [USERNAME]`.

## Connect to the YugabyteDB Anywhere server

Use the GCP Cloud Console to find the public IP address of the instance you launched.

To connect to this server, execute the following command:

```sh
ssh -i ~/.ssh/yugabyte-1-gcp <login-user>@NN.NN.NN.NN
```

Replace `NN.NN.NN.NN` with the IP address and `yugabyte-1-gcp` with the appropriate SSH key.
