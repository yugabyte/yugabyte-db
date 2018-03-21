
## 1. Create a new project (optional)

A project forms the basis for creating, enabling and using all GCP services, managing APIs, enabling billing, adding and removing collaborators, and managing permissions. You would need browse to the [GCP cloud resource manager](https://console.cloud.google.com/cloud-resource-manager) and click on create project to get started. You can follow these instructions to [create a new GCP project](https://cloud.google.com/resource-manager/docs/creating-managing-projects).

Give the project a suitable name (eg: `yugabyte-gcp`) and note the project ID (eg: `yugabyte-gcp`). You should see a dialog that looks like the screenshot below.

![Creating a GCP project](/images/ee/gcp-setup/project-create.png)


## 2. Set up a new service account

Yugaware admin console requires a service account with the appropriate permissions to provision and manage compute instances. Go to the `IAM & admin` -> `Service accounts` and click on `Create Service Account`. You can follow these instructions to [create a service account](https://cloud.google.com/iam/docs/creating-managing-service-accounts).

Fill the form with the following values:

- Service account name is `yugaware` (you can customize the name if needed).
- Set role to `Project` -> `Owner`.
- Check the box for `Furnish a new private key`, choose `JSON` option.

Here is a screenshot with the above values in the form, click create once the values are filled in.

![Service Account -- filled create form](/images/ee/gcp-setup/service-account-filled-create.png)

**NOTE**: Your browser would have downloaded the respective JSON format key. It is important to store it safely. This JSON key is needed to configure the Yugaware Admin Console.


## 3. Creating a firewall rule

In order to access Yugaware from outside the GCP environment, you would need to enable firewall rules. You will at minimum need to:

- Access the Yugaware instance over ssh (port tcp:22)
- Check, manage and upgrade Yugaware (port tcp:8800)
- View the Yugaware console ui (port tcp:80)

Let us create a firewall entry enabling all of that!

Go to `VPC network` -> `Firewall rules` tab:

![Firewall -- service entry](/images/ee/gcp-setup/firewall-tab.png)

**NOTE**: If this is a new project, you might see a message saying `Compute Engine is getting ready`. If so, you would need to wait for a while. Once complete, you should see the default set of firewall rules for your default network, as shown below.

![Firewall -- fresh list](/images/ee/gcp-setup/firewall-fresh-list.png)

Click on the `CREATE FIREWALL RULE` button and fill in the following.

- Enter `yugaware-firewall-rule` as the name (you can change the name if you want).
- Add a description (eg: `Firewall setup for Yugaware Admin Console`).
- Add a tag `yugaware-server` to the `Target tags` field. This will be used later when creating instances.
- Add the appropriate ip addresses to the `Source IP ranges` field. To allow access from any machine, add `0.0.0.0/0` but note that this is not very secure.
- Add the ports `tcp:22,8800,80` to the `Protocol and ports` field.

You should see something like the screenshot below, click `Create` next.

![Firewall -- create full](/images/ee/gcp-setup/firewall-create-full.png)


## 4. Provision instance for Yugaware

Create an instance to run Yugaware. In order to do so, go to `Compute Engine` -> `VM instances` and click on `Create`. Fill in the following values.

- Enter `yugaware-1` as the name.
- Pick a region/zone (eg: `us-east1-b`).
- Choose `4 vCPUs` (`n1-standard-4`) as the machine type.
- Change the boot disk image to `Ubuntu 16.04` and increase the boot disk size to `100GB`.
- Open the `Management, disks, networking, SSH keys` -> `Networking` tab. Add `yugaware-server` as the network tag (or the custom name you chose when setting up the firewall rules).
- Switch to the `SSH Keys` tab and add a custom public key and login user to this instance. [Follow these instructions](https://cloud.google.com/compute/docs/instances/adding-removing-ssh-keys#metadatavalues) to create a new SSH key pair, as well as the expected format for this field (eg: `ssh-rsa [KEY_VALUE] [USERNAME]`). This is important to enable `ssh` access to this machine.

![VM instances -- filled in create](/images/ee/gcp-setup/vm-create-full.png)

Note on boot disk customization:

![VM instances -- pick boot disk](/images/ee/gcp-setup/vm-pick-boot-disk.png)


Note on networking customization:

![VM instances -- networking tweaks](/images/ee/gcp-setup/vm-networking.png)

Finally, click `Create` to launch the YugaWare server.
