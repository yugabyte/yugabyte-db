---
title: Configure the GCP cloud provider
headerTitle: Configure the GCP cloud provider
linkTitle: Configure cloud providers
description: Configure the Google Cloud Platform (GCP) cloud provider.
aliases:
  - /preview/deploy/enterprise-edition/configure-cloud-providers/gcp
menu:
  preview_yugabyte-platform:
    identifier: set-up-cloud-provider-2-gcp
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
    <a href="../gcp/" class="nav-link active">
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

You can configure Google Cloud Platform (GCP) for YugabyteDB universes using YugabyteDB Anywhere. If no cloud providers are configured, the main Dashboard page prompts you to configure at least one cloud provider.

## Prerequisites

To run YugabyteDB nodes on GCP, all you need to provide on the YugabyteDB Anywhere UI is your cloud provider credentials. YugabyteDB Anywhere uses those credentials to automatically provision and de-provision instances that run YugabyteDB. An instance for YugabyteDB includes a compute instance, as well as local or remote disk storage attached to the compute instance.

## Configure GCP

To configure GCP providers, navigate to **Configs > Infrastructure > Google Cloud Platform**.

This lists all currently configured providers.

To view a provider, select it in the list to display the **Overview**. You can perform the following on a profile configuration:

- To edit the configuration, select **Config Details**, make changes, and click **Apply Changes**. Note that, depending on whether the configuration has been used to create a universe, you can only edit a subset of options.
- To view the universes created using the profile, select **Universes**.
- To delete the configuration, click **Actions** and choose **Delete Configuration**. You can only delete configurations that are not in use by a universe.

To create a GCP provider, click **Create Config** to open the **Create GCP Provider Configuration** page.

### Provider settings

Enter a Provider name. The Provider name is an internal tag used for organizing cloud providers.

Provider settings are organized in the following sections.

#### Cloud Info

If your YugabyteDB Anywhere instance is not running inside GCP, you need to supply YugabyteDB Anywhere with credentials to the desired GCP project by uploading a configuration file. To do this, set **Credential Type** to **Upload Service Account config** and proceed to upload the JSON file that you obtained when you created your service account, as described in [Prepare the Google Cloud Platform (GCP) environment](../../../install-yugabyte-platform/prepare-environment/gcp).

If your YugabyteDB Anywhere instance is running inside GCP, the preferred method for authentication to the GCP APIs is to add a service account role to the GCP instance running YugabyteDB Anywhere and then configure YugabyteDB Anywhere to use the instance's service account. To do this, set **Credential Type** to **Use service account from this YBA host's instance**.

#### VPC Setup

Specify the VPC to use for deploying YugabyteDB nodes.

You may choose one of the following options:

- **Specify an existing VPC**. Select this option to use a VPC that you have created in Google Cloud, and enter the Custom GCE Network Name of the VPC.
- **Use VPC from YBA host instance**. If your YugabyteDB Anywhere host machine is also running on Google Cloud, you can use the same VPC on which the YugabyteDB Anywhere host machine runs. **Credential Type** must be set to **Use service account from this YBA host's instance** to use this option.

  Note that choosing to use the same VPC as YugabyteDB Anywhere is an advanced option, which assumes that you are in complete control of this VPC and will be responsible for setting up the networking, SSH access, and firewall rules for it.

- **Create a new VPC**. Select this option to create a new VPC using YugabyteDB Anywhere. This option is considered beta and, therefore, not recommended for production use cases. If there are any classless inter-domain routing (CIDR) conflicts, using this option can result in a silent failure. For example, the following will fail:

  - Configure more than one GCP cloud provider with different CIDR block prefixes and selecting the **Create a new VPC** option.
  - Creating a new VPC with a CIDR block that overlaps with any of the existing subnets.

  To use this option, contact {{% support-platform %}}.

#### Regions

For each region that you want to use for this configuration, do the following:

- Click **Add Region**.
- Select the region.
- Optionally, specify a **Custom Machine Image**. YugabyteDB Anywhere allows you to bring up universes on Ubuntu 18.04 host nodes, assuming you have Python 2 or later installed on the host, as well as the provider created with a custom AMI and custom SSH user.
- Enter the ID of a shared subnet.

#### Advanced

**NTP Setup** lets you to customize the Network Time Protocol server, as follows:

- Select **Use GCP's NTP Server** to enable cluster nodes to connect to the GCP internal time servers. For more information, consult the GCP documentation such as [Configure NTP on a VM](https://cloud.google.com/compute/docs/instances/configure-ntp).
- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YugabyteDB Anywhere from performing any NTP configuration on the cluster nodes. For data consistency, ensure that NTP is correctly configured on your machine image.

## Create the configuration

After you have entered the settings, click **Create Provider Configuration** and wait for the configuration to complete.

This process includes generating a new VPC, a network, subnetworks in all available regions, as well as a new firewall rule, VPC peering for network connectivity, and a custom SSH key pair for YugabyteDB Anywhere-to-YugabyteDB connectivity.

Upon completion, you should see the configuration similar to the following:

![GCP Configuration success](/images/ee/gcp-setup/gcp-configure-success.png)

Now you are ready to create a YugabyteDB universe on GCP.
