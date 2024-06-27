---
title: Configure the GCP cloud provider
headerTitle: Create cloud provider configuration
linkTitle: Cloud providers
description: Configure the Google Cloud Platform (GCP) cloud provider.
headContent: For deploying universes on GCP
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

</ul>

Before you can deploy universes using YugabyteDB Anywhere (YBA), you must create a provider configuration. Create a Google Cloud Platform (GCP) provider configuration if your target cloud is GCP.

When deploying a universe, YBA uses the provider configuration settings to do the following:

- Create instances on GCP using the following:
  - your cloud provider credentials
  - specified regions and availability zones (this can be a subset of those specified in the provider configuration)
  - a Linux image
  - optionally, an [instance template](#gcp-instance-templates)

- Provision those VMs with YugabyteDB software.

## Prerequisites

- Cloud provider credentials. YBA uses your credentials to automatically provision and de-provision instances that run YugabyteDB. An instance for YugabyteDB includes a compute instance, as well as local or remote disk storage attached to the compute instance.

For more information on setting up a GCP service account, refer to [Cloud permissions to deploy nodes](../../prepare/cloud-permissions/cloud-permissions-nodes-gcp/).

## Configure GCP

Navigate to **Configs > Infrastructure > Google Cloud Platform** to see a list of all currently configured GCP providers.

### Create a provider

To create a GCP provider:

1. Click **Create Config** to open the **Create GCP Provider Configuration** page.

    ![Create GCP provider](/images/yb-platform/config/yba-gcp-config-create-stable.png)

1. Enter the provider details. Refer to [Provider settings](#provider-settings).

1. Click **Validate and Save Configuration** option to proceeds with validation of the fields and save the provider if the validation succeeds. In case of errors, the respective fields will be highlighted with an error message. Validation can be retried after fixing the errant fields.

If you want to skip validation, choose **Ignore and save provider configuration anyway** option, which will ignore the validation errors and save the provider anyway.

As part of validation, the following checks are performed for the given fields.

| Field category | Field name | Check |
| :------------- | :--------- | :---- |
| Cloud Info | Credential Type, Service Account | Validate credentials and verify role bindings (if the Service Account has required permissions to manage VMs). |
| | Shared VPC Project, VPC Setup | Validate existence in the specified GCP project (if existing VPC option is selected). Validate permissions to create new VPC (if new VPC option is selected). |
| Regions | Subnet | Validate existence in the specified project, region, and VPC. |
| | Instance Template | Validate existence in the specified GCP project. |
| Linux Version Catalog | Machine Image ID | Validate existence in the specified GCP project. |
| SSH Key Pairs | SSH Port | Verify if the SSH port is open. |
| | Key Pair Management | Validate that it is an RSA key pair. |
| Advanced | Firewall Tags | Verify the existence of the tag to any of the firewall rules for the given VPC. |
| | NTP Servers | Verify NTP server is a valid IP/hostname. |

The create provider process includes generating a new VPC, a network, subnetworks in all available regions, as well as a new firewall rule, VPC peering for network connectivity, and a custom SSH key pair for YBA-to-YugabyteDB connectivity.

Now you are ready to create a YugabyteDB universe on GCP.

### View and edit providers

To view a provider, select it in the list of GCP Configs to display the **Overview**.

To edit the provider, select **Config Details**, make changes, and click **Apply Changes**. For more information, refer to [Provider settings](#provider-settings). Note that for YBA version 2.20.1 and later, depending on whether the provider has been used to create a universe, you can only edit a subset of fields such as the following:

- Provider Name
- Credential Type. You can upload a new Google Service Account JSON file (`gceApplicationCredentials`). Note that the `project_id` field can't have a new entry. For example:

    ```json
    {
      "type": "service_account",
      "project_id": "new-project-yb",
       ...
    }
    ```

    If `new-project-yb` is a new GCE project, the backend request fails and you will be notified that you can't change the GCE project for an in-use provider.

- Regions - You can add regions and zones to an in-use provider. Note that you cannot edit existing region details, delete a region if any of the region's zones are in use, or delete zones that are in use.

To view the universes created using the provider, select **Universes**.

To delete the provider, click **Actions** and choose **Delete Configuration**. You can only delete providers that are not in use by a universe.

## Provider settings

### Provider Name

Enter a Provider name. The Provider name is an internal tag used for organizing provider configurations.

### Cloud Info

If your YBA instance is not running inside GCP, you need to supply YBA with credentials to the desired GCP project by uploading a configuration file. To do this, set **Credential Type** to **Upload Service Account config** and proceed to upload the JSON file that you obtained when you created your service account, as described in [Cloud permissions](../../prepare/cloud-permissions/cloud-permissions-nodes/).

If your YBA instance is running inside GCP, the preferred method for authentication to the GCP APIs is to add a service account role to the GCP instance running YBA and then configure YBA to use the instance's service account. To do this, set **Credential Type** to **Use service account from this YBA host's instance**.

### VPC Setup

Specify the VPC to use for deploying YugabyteDB nodes.

You may choose one of the following options:

- **Specify an existing VPC**. Select this option to use a VPC that you have created in Google Cloud, and enter the Custom GCE Network Name of the VPC.
- **Use VPC from YBA host instance**. If your YBA host machine is also running on Google Cloud, you can use the same VPC on which the YBA host machine runs. **Credential Type** must be set to **Use service account from this YBA host's instance** to use this option.

  Note that choosing to use the same VPC as YBA is an advanced option, which assumes that you are in complete control of this VPC and will be responsible for setting up the networking, SSH access, and firewall rules for it.

- **Create a new VPC**. Select this option to create a new VPC using YBA. This option is considered beta and, therefore, not recommended for production use cases. If there are any classless inter-domain routing (CIDR) conflicts, using this option can result in a silent failure. For example, the following will fail:

  - Configure more than one GCP cloud provider with different CIDR block prefixes and selecting the **Create a new VPC** option.
  - Creating a new VPC with a CIDR block that overlaps with any of the existing subnets.

  To use this option, contact {{% support-platform %}}.

### Regions

For each region that you want to use for this configuration, do the following:

- Click **Add Region**.
- Select the region.
- Optionally, specify a **Custom Machine Image**.
- Enter the ID of a shared subnet.
- Optionally, if you have an [instance template](#gcp-instance-templates), specify the template name in the **Instance Template** field.

### Linux version catalog

Specify the machine images to be used to install on nodes of universes created using this provider.

To add machine images recommended and provisioned by YBA, select the **Include Linux versions that are chosen and managed by YugabyteDB Anywhere in the catalog** option, and choose the architectures.

To add your own machine images to the catalog:

1. Click **Add Linux Version**.

1. Provide a name for the Linux version. You can see this name when creating universes using this provider.

1. Enter the Machine Image ID to use for each [provider region](#regions).

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

- Select **Use GCP's NTP Server** to enable cluster nodes to connect to the GCP internal time servers. For more information, consult the GCP documentation such as [Configure NTP on a VM](https://cloud.google.com/compute/docs/instances/configure-ntp).
- Select **Specify Custom NTP Server(s)** to provide your own NTP servers and allow the cluster nodes to connect to those NTP servers.
- Select **Assume NTP server configured in machine image** to prevent YBA from performing any NTP configuration on the cluster nodes. For data consistency, you will be responsible for manually configuring NTP.

    {{< warning title="Important" >}}
Use this option with caution. Time synchronization is critical to database data consistency; failure to run NTP may cause data loss.
    {{< /warning >}}

### GCP instance templates

You can optionally add a GCP [instance template](https://cloud.google.com/compute/docs/instance-templates) as a region-level property when creating a GCP provider in YBA.

Instance templates provide a way to specify a set of arbitrary instance parameters, which can then be used when creating instances in Google Cloud. Instance templates define the machine type, boot disk image or container image, labels, startup script, and other instance properties. When a template is added to a GCP provider, YBA will use most (but not all) of the configuration defined by the template to create the nodes when deploying a universe.

{{< note title="Note" >}}
Instance templates are only supported in YBA version 2.18.2.0 and later.
{{< /note >}}

Using an instance template allows you to customize instance features that are not accessible to a provider alone, such as (but not limited to) the following:

- Volume disk encryption
- Startup scripts
- On-host maintenance
- Sole tenancy
- Confidential VM service

For instructions on creating an instance template on Google Cloud, refer to [Create instance templates](https://cloud.google.com/compute/docs/instance-templates/create-instance-templates) in the Google documentation.

When creating the template in Google Cloud, ensure that you create the template under the right GCP project and choose the correct network and subnetwork under **Advanced Options** > **Networking**.

Note that not all template customizations are honored by YBA when creating a universe using a provider with a template. The following properties can't be overridden by an instance template:

- Project
- Zone
- Boot disk (Auto- delete, disk type, and disk image)
- IP forwarding
- Instance type
- SSH keys
- Project wide SSH keys (always blocked)
- Cloud NAT
- Subnetwork
- Volume (type, size, and source (always None))
