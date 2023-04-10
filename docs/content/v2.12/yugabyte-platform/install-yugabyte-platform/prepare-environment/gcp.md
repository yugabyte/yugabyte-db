---
title: Prepare the Google Cloud Platform (GCP) environment
headerTitle: Prepare the Google Cloud Platform (GCP) environment
linkTitle: Prepare the environment
description: Prepare the Google Cloud Platform (GCP) environment
menu:
  v2.12_yugabyte-platform:
    parent: install-yugabyte-platform
    identifier: prepare-environment-2-gcp
    weight: 55
type: docs
---

<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li>
    <a href="../aws" class="nav-link">
      <i class="fa-brands fa-aws" aria-hidden="true"></i>
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
    <a href="../openshift" class="nav-link">
      <i class="fa-solid fa-cubes" aria-hidden="true"></i>
      OpenShift
    </a>
 </li>

  <li>
    <a href="../on-premises" class="nav-link">
      <i class="fa-solid fa-building" aria-hidden="true"></i>
      On-premises
    </a>
  </li>

</ul>

## 1. Create a new project (optional)

A project forms the basis for creating, enabling, and using all GCP services, managing APIs, enabling billing, adding and removing collaborators, and managing permissions. Go to the [GCP cloud resource manager](https://console.cloud.google.com/cloud-resource-manager) and click on create project to get started. You can follow these instructions to [create a new GCP project](https://cloud.google.com/resource-manager/docs/creating-managing-projects).

Give the project a suitable name (for example, `yugabyte-gcp`) and note the project ID (for example, `yugabyte-gcp`). You should see a dialog that looks like the screenshot below.

![Creating a GCP project](/images/ee/gcp-setup/project-create.png)

## 2. Set up a new service account

The Yugabyte Platform console requires a service account with the appropriate permissions to provision and manage compute instances. Go to *IAM & admin > Service accounts* and click **Create Service Account**. You can follow these instructions to [create a service account](https://cloud.google.com/iam/docs/creating-managing-service-accounts).

Fill the form with the following values:

- Service account name is `yugaware` (you can customize the name, if needed).
- Set the **Project** role to `Owner`.
- Check the box for **Furnish a new private key**, choose the `JSON` option.

Here is a screenshot with the above values in the form, click create once the values are filled in.

![Service Account -- filled create form](/images/ee/gcp-setup/service-account-filled-create.png)

**Note**: Your web browser downloads the respective JSON format key. It is important to store it safely. This JSON key is needed to configure the Yugabyte Platform console.

## 3. Give permissions to the service account

- Find the email address associated with the service account by going to **IAM & admin > Service accounts**. Copy this value. The screen should look as shown below.

![Service Account Email Address](/images/ee/gcp-setup/gcp-service-account-email.png)

- Next, go to **IAM & admin > IAM** and click **ADD**. Add the compute admin role for this service account. A screenshot is shown below.

![Service Account Add Roles](/images/ee/gcp-setup/gcp-service-account-permissions.png)

## 4. Creating a firewall rule

In order to access the Yugabyte Platform from outside the GCP environment, you would need to enable firewall rules. You will at minimum need to:

- Access the Yugabyte Platform instance over SSH (port `tcp:22`)
- Check, manage, and upgrade Yugabyte Platform (port `tcp:8800`)
- View the Yugabyte Platform console (port `tcp:80`)

If you are using your own custom VPCs (self-managed configuration), the following additional TCP ports must be accessible: 7000, 7100, 9000, 9100, 11000, 12000, 9300, 9042, 5433, and 6379. For more information on ports used by YugabyteDB, refer to [Default ports](../../../../reference/configuration/default-ports).

Create a firewall entry enabling these by going to **VPC network > Firewall rules**:

![Firewall -- service entry](/images/ee/gcp-setup/firewall-tab.png)

**Note**: If this is a new project, you might see a message saying `Compute Engine is getting ready`. If so, you would need to wait for a while. Once complete, you should see the default set of firewall rules for your default network, as shown below.

![Firewall -- fresh list](/images/ee/gcp-setup/firewall-fresh-list.png)

Click **CREATE FIREWALL RULE** and fill in the following.

- Enter `yugaware-firewall-rule` as the name (you can change the name if you want).
- Add a description (for example, `Firewall setup for Yugabyte Platform console`).
- Add a tag `yugaware-server` to the **Target tags** field. This will be used later when creating instances.
- Add the appropriate IP addresses to the **Source IP ranges** field. To allow access from any machine, add `0.0.0.0/0` but note that this is not very secure.
- Add the ports `tcp:22,8800,80` to the **Protocol and ports** field. For a self-managed configuration, also add 7000, 7100, 9000, 9100, 11000, 12000, 9300, 9042, 5433, and 6379 to the tcp ports list.

You should see something like the screenshot below, click **Create** next.

![Firewall -- create full](/images/ee/gcp-setup/firewall-create-full.png)

## 5. Provision instance for Yugabyte Platform

Create an instance to run Yugabyte Platform. In order to do so, go to **Compute Engine > VM instances** and click **Create**. Fill in the following values.

- Enter `yugaware-1` as the name.
- Pick a region/zone (for example, `us-west1-b`).
- Choose `4 vCPUs` (`n1-standard-4`) as the machine type.
- Change the boot disk image to `Ubuntu 16.04` and increase the boot disk size to `100GB`.
- Open **Management, disks, networking, SSH keys -> Networking** tab. Add `yugaware-server` as the network tag (or the custom name you chose when setting up the firewall rules).
- Switch to the **SSH Keys** tab and add a custom public key and login user to this instance. First create a key-pair.

You can do this as shown below.

```sh
$ ssh-keygen -t rsa -f ~/.ssh/yugaware-1-gcp -C centos
```

Set the appropriate credentials for the SSH key.

```sh
$ chmod 400 ~/.ssh/yugaware-1-gcp
```

Now enter the contents of `yugaware-1-gcp.pub` as the value for this field.

[Here are the detailed instructions](https://cloud.google.com/compute/docs/instances/adding-removing-ssh-keys#metadatavalues) to create a new SSH key pair, as well as the expected format for this field (for example, `ssh-rsa [KEY_VALUE] [USERNAME]`). This is important to enable SSH access to this machine.

![VM instances -- filled in create](/images/ee/gcp-setup/vm-create-full.png)

Note on boot disk customization:

![VM instances -- pick boot disk](/images/ee/gcp-setup/vm-pick-boot-disk.png)

Note on networking customization:

![VM instances -- networking tweaks](/images/ee/gcp-setup/vm-networking.png)

Finally, click **Create** to launch the Yugabyte Platform server.

## 6. Connect to the Yugabyte Platform machine

From the GCP web management console, find the public IP address of the instance you just launched.

You can connect to this machine by running the following command (remember to replace `XX.XX.XX.XX` below with the IP address, and also to enter the appropriate SSH key instead of `yugaware-1-gcp`).

```sh
$ ssh -i ~/.ssh/yugaware-1-gcp centos@XX.XX.XX.XX
```
