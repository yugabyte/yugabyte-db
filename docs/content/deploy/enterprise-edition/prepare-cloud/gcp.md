## Creating the project

Go to https://console.cloud.google.com/cloud-resource-manager and click on `CREATE PROJECT`.

Give the project a representative name (eg: `yb-testing-1`) and take note of the project ID that
will be assigned (eg: `yb-testing-1`). Assuming the name was unique enough, the ID will likely be
exactly the same.

You should see something like:

<img src="/images/ee/gcp-setup/project-create.png" style="max-width:600px;" alt="Creating a project" />

Click `Create` to start the project creation. It should take less than a minute for the project to
become available. Once it does, click on it in the `Cloud Resource Manager` view to go check it
out, or directly visit https://console.cloud.google.com/home/dashboard?project=PROJECT_ID, where
`PROJECT_ID` is the ID from before.

## Setting up a new Service Account

In order for YugaWare to be able to provision VMs on your behalf, it will require a service
account, with relevant permissions across your project. You can create one now!

First, go to the `IAM & admin` service and visit the `Service accounts` tab.

Assuming you have no service accounts, as this is a brand new project, you should see something
like this:

![Service Account -- empty view](/images/ee/gcp-setup/service-account-empty-view.png)

Click on the `Create service account` button and fill in the form as follows:

- Give it a relevant name (eg: `yb-testing-service-account-1`).
- Allow it a strong set of permissions (`Project -> Owner`).
- Check the box for `Furnish a new private key` in `JSON` format.

Once you are done, you should see something along the lines of:

![Service Account -- filled create form](/images/ee/gcp-setup/service-account-filled-create.png)

Click `CREATE` and note the following will happen within a matter of seconds:

- Your browser would have downloaded the respective JSON format key.
- You will get a notification in the foreground about this key. Feel free to press `CLOSE` whenever.

![Service Account -- pop up on create](/images/ee/gcp-setup/service-account-popup.png)

- An entry for that service account will appear in the background.

![Service Account -- background list entry](/images/ee/gcp-setup/service-account-background.png)

Note: It is important that you safely store the JSON key as we will need to provide it to the
Admin Console in order for it to be able to manage your YugaByte GCP environment for you!

## Creating a Firewall rule

Prepare for deploying YugaWare by enabling a firewall rule so that you will be able to access the
machine from outside of the GCP environment. You will at minimum need to SSH to it (default tcp:22),
check and manage its versioning through the Replicated interface (tcp:8800), as well as directly
access the service itself (tcp:80). Let us create a firewall entry enabling all of that!

Go to `VPC network` service and visit the `Firewall ruels` tab:

![Firewall -- service entry](/images/ee/gcp-setup/firewall-tab.png)

Since this is a new project, you might see the following, saying `Compute Engine is getting ready`:

![Firewall -- prepare compute engine](/images/ee/gcp-setup/firewall-prepare.png)

If so, give this a minute and then refresh the page if it does not do so automatically. Once
complete, you should see the default set of firewall rules for your default network, as in:

![Firewall -- fresh list](/images/ee/gcp-setup/firewall-fresh-list.png)

Now, to create the YugaWare firewall rule, click on the `CREATE FIREWALL RULE` button and fill in
the relevant information, as follows:

- Give it a relevant name (eg: `yugaware-firewall-rule`).
- You can put in some relevant description (eg: `Opening up ports for contacting YugaWare`).
- Add a tag to the `Target tags` field for later use when creating the VM (eg: `yugaware-server`).
- Add a rule in `Source IP ranges` for accepting traffic from the world (eg: `0.0.0.0/0`).
- Add the TCP ports discussed above, to the set allowed set (eg `tcp:22,8800,80`).

Finally, you should see something like:

![Firewall -- create full](/images/ee/gcp-setup/firewall-create-full.png)

Click `Create` and give it a couple of seconds to be ready.

## Creating the YugaWare VM itself

We're ready to create the VM that will house YugaWare!

Go to the `Compute Engine` service and visit the `VM instances` tab:

![VM instances -- tab](/images/ee/gcp-setup/vm-tab.png)

Assuming that this is a brand new project with no VMs created, you should see a view such as:

![VM instances -- empty list](/images/ee/gcp-setup/vm-list-empty.png)

Click on `Create` to setup your first VM and fill in the form as follows:

- Give the VM a relevant name (eg: `yugaware-1`).
- Pick a region/zone that's relevant to your deployment (eg: `us-east1-b`).
- Change the instance type to a sensible `4vCPU` setup (eg: `n1-standard-4`).
- Change the boot disk to start up an `Ubuntu 16.04` and increase the boot disk size to `100GB`.
- Open up the `Management, disks, networking, SSH keys` subcomponent and navigate to the
`Networking` tab. Add the network tag you created on the firewall setup stage (eg: `yugaware-server`).
- Switch to the `SSH Keys` tab and add a custom public key and login user to this instance. You can
follow https://cloud.google.com/compute/docs/instances/adding-removing-ssh-keys#metadatavalues
for notes on both how to create a new SSH key pair, as well as the particular expected format for
this field when creating GCP VMs (eg: `ssh-rsa [KEY_VALUE] [USERNAME]`). This is important, so you
can custom SSH into this machine through a native `ssh` command instead of requiring the `gcloud`
command for access.

![VM instances -- filled in create](/images/ee/gcp-setup/vm-create-full.png)

Note on boot disk customization:

![VM instances -- pick boot disk](/images/ee/gcp-setup/vm-pick-boot-disk.png)


Note on networking customization:

![VM instances -- networking tweaks](/images/ee/gcp-setup/vm-networking.png)

Finally, click `Create` to launch the YugaWare server.
