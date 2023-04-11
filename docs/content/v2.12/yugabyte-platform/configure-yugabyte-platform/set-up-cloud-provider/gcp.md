---
title: Configure the Google Cloud Platform (GCP) cloud provider
headerTitle: Configure the Google Cloud Platform (GCP) cloud provider
linkTitle: Configure the cloud provider
description: Configure the Google Cloud Platform (GCP) cloud provider.
menu:
  v2.12_yugabyte-platform:
    identifier: set-up-cloud-provider-2-gcp
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
    <a href="../gcp" class="nav-link active">
      <i class="fa-brands fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="../azure" class="nav-link">
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

You can configure Google Cloud Platform (GCP) for YugabyteDB clusters using the Yugabyte Platform console. If no cloud providers are configured yet, the main Dashboard page prompts you to configure at least one cloud provider.

## Prerequisites

To run YugabyteDB nodes on GCP, all you need to provide on the Yugabyte Platform console is your cloud provider credentials. Yugabyte Platform uses those credentials to automatically provision and de-provision instances that run YugabyteDB. An instance for YugabyteDB includes a compute instance, as well as local or remote disk storage attached to the compute instance.

## Configure GCP

You can configure GCP as follows:

- Navigate to **Configs > Google Cloud Platform**.

- Click **Add Configuration** to open the **Cloud Provider Configuration** page shown in the following illustration:

  ![GCP Configuration empty](/images/ee/gcp-setup/gcp-configure-empty.png)

- Complete the fields, keeping in mind the following guidelines:
  - Supply a descriptive name that preferably contains Google or GCP, which is especially important if you are planning to configure other providers.

  - Upload the JSON file that you obtained when you created your service account, as described in [Prepare the Google Cloud Platform (GCP) environment](../../../install-yugabyte-platform/prepare-environment/gcp).

  - If this is a new deployment, it is recommended that you use the **VPC Setup** field to create a new VPC specifically for YugabyteDB nodes. You have to ensure that the Yugabyte Platform host machine can connect to your Google Cloud account where this new VPC will be created.

    Alternatively, you may choose to specify an existing VPC for YugabyteDB nodes, in which case you would need to map regions by providing the following:

    - A region name.
    - A subnet ID.
    - Optionally, a custom machine image. Yugabyte Platform allows you to bring up universes on Ubuntu 18.04 host nodes, assuming you have Python 2 or later installed on the host, as well as the provider created with a custom AMI and custom SSH user.

    The third option that is available only when your Yugabyte Platform host machine is also running on Google Cloud, is to use the same VPC on which the Yugabyte Platform host machine runs. Note that choosing to use the same VPC as Yugabyte Platform is an advanced option, which assumes that you are in complete control over this VPC and will be responsible for setting up the networking, SSH access, and firewall rules for it.

- Click **Save** and wait for the configuration to complete.

  This process includes generating a new VPC, a network, subnetworks in all available regions, as well as a new firewall rule, VPC peering for network connectivity, and a custom SSH key pair for Yugabyte Platform-to-YugabyteDB connectivity.

Upon completion, you should see the configuration similar to the following:

![GCP Configuration success](/images/ee/gcp-setup/gcp-configure-success.png)

Now you are ready to create a YugabyteDB universe on GCP.
