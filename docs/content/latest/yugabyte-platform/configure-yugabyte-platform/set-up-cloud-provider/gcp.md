---
title: Configure the Google Cloud Platform (GCP) cloud provider
headerTitle: Configure the Google Cloud Platform (GCP) cloud provider
linkTitle: Configure the cloud provider
description: Configure the Google Cloud Platform (GCP) cloud provider.
aliases:
  - /latest/deploy/enterprise-edition/configure-cloud-providers/gcp
menu:
  latest:
    identifier: set-up-cloud-provider-2-gcp
    parent: configure-yugabyte-platform
    weight: 20
isTocNested: false
showAsideToc: true
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/aws" class="nav-link">
      <i class="fab fa-aws"></i>
      AWS
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/gcp" class="nav-link active">
      <i class="fab fa-google" aria-hidden="true"></i>
      GCP
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/azure" class="nav-link">
      <i class="icon-azure" aria-hidden="true"></i>
      &nbsp;&nbsp; Azure
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/vmware-tanzu" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      VMware Tanzu
    </a>
  </li>

<li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/openshift" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>OpenShift</a>
  </li>

  <li>
    <a href="/latest/yugabyte-platform/configure-yugabyte-platform/set-up-cloud-provider/on-premises" class="nav-link">
      <i class="fas fa-building"></i>
      On-premises
    </a>
  </li>

</ul>

You can configure Google Cloud Platform (GCP) for YugabyteDB clusters using the Yugabyte Platform console. If no cloud providers are configured yet, the main Dashboard page highlights the need to configure at least one cloud provider, as per the following illustration:

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

## Prerequisites

To run YugabyteDB nodes on GCP, all you need to provide on the Yugabyte Platform console is your cloud provider credentials. Yugabyte Platform uses those credentials to automatically provision and de-provision instances that run YugabyteDB. An instance for YugabyteDB includes a compute instance, as well as local or remote disk storage attached to the compute instance.

## Configure GCP

To start, navigate to **Configuration > GCP**, as per the following illustration:

![GCP Configuration -- empty](/images/ee/gcp-setup/gcp-configure-empty.png)

Complete the fields, as per the following illustration:

![GCP Configuration -- full](/images/ee/gcp-setup/gcp-configure-full.png)

Take note of the following for configuring your GCP provider:

- Supply a descriptive name that preferably contains Google or GCP, which is especially important if you are planning to configure other providers.

- Upload the JSON file that you obtained when you created your service account, as described in [Prepare the Google Cloud Platform (GCP) environment](../../../install-yugabyte-platform/prepare-environment/gcp).

- If this is a new deployment, it is recommended that you create a new VPC specifically for YugabyteDB nodes. You have to ensure that the Yugabyte Platform host machine can connect to your Google Cloud account where this new VPC will be created. Otherwise, you may choose to specify an existing VPC for YugabyteDB nodes. The third option that is available only when your Yugabyte Platform host machine is also running on Google Cloud, is to use the same VPC on which the Yugabyte Platform host machine runs.

  Note that choosing to use the same VPC as Yugabyte Platform is an advanced option, which assumes that you are in complete control over this VPC and will be responsible for setting up the networking, SSH access, and firewall rules for it.


Click **Save** and wait a few minutes for the configuration to complete. This includes generating a new VPC, a network, subnetworks in all available regions, as well as a new firewall rule, VPC peering for network connectivity, and a custom SSH key pair for Yugabyte Platform-to-YugabyteDB connectivity.

The following illustration shows the steps involved in creating GCP:

![GCP Configuration -- in progress](/images/ee/gcp-setup/gcp-configure-inprogress.png)

Upon completion, you should see the following:

![GCP Configuration -- success](/images/ee/gcp-setup/gcp-configure-success.png)

Now you are ready to create a YugabyteDB universe on GCP.
