Go to the `Configuration` nav on the left-side and then click on the GCP tab. You should see
something like this:

![GCP Configuration -- empty](/images/ee/gcp-setup/gcp-configure-empty.png)

Fill in the couple of pieces of data and you should get something like:

![GCP Configuration -- full](/images/ee/gcp-setup/gcp-configure-full.png)

Take note of the following for configuring your GCP provider:

- Give this provider a relevant name. We recommend something that contains Google or GCP in it, especially if you will be configuring other providers as well.

- Upload the JSON file that you obtained when you created your service account as per the [Initial Setup](/deploy/enterprise-edition/prepare-cloud-environment/).

- Assuming this is a new deployment, we recommend creating a new VPC specifically for YugaByte DB nodes. You have to ensure that the YugaWare host machine is able to connect to your Google Cloud account where this new VPC will be created. Otherwise, you can choose to specify an existing VPC for YugaByte DB nodes. The 3rd option that is available only when your YugaWare host machine is also running on Google Cloud is to use the same VPC that the YugaWare host machine runs on. 

- Finally, click `Save` and give it a couple of minutes, as it will need to do a bit of work in the background. This includes generating a new VPC, a network, subnetworks in all available regions, as well as a new firewall rule, VPC peering for network connectivity and a custom SSH keypair for YugaWare-to-YugaByte connectivity

Note: Choosing to use the same VPC as YugaWare is an advanced option, which currently assumes that you are in complete control over this VPC and will be responsible for setting up the networking, SSH access and firewall rules for it!

The following shows the steps involved in creating this cloud provider.

![GCP Configuration -- in progress](/images/ee/gcp-setup/gcp-configure-inprogress.png)


If all went well, you should see something like:

![GCP Configuration -- success](/images/ee/gcp-setup/gcp-configure-success.png)

Now we are ready to create a YugaByte DB universe on GCP.
