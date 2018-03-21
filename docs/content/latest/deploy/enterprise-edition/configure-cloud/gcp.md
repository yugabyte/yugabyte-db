Let's go to the `Configuration` nav on the left-side and then click on the GCP tab. You should see
something like this:

![GCP Configuration -- empty](/images/ee/gcp-setup/gcp-configure-empty.png)

Fill in the couple of pieces of data and you should get something like:

![GCP Configuration -- full](/images/ee/gcp-setup/gcp-configure-full.png)

Take note of the following for configuring your GCP provider:

- Give this provider a relevant name. We recommend something that contains Google or GCP in it,
especially if you will be configuring other providers as well.
- Upload the JSON file that you obtained when you created your service account.
- Assuming this is a brand new deployment, we recommend keeping the toggle for using the host VPC
on not. This way, you allow YugaWare to provision another VPC, designed specifically for housing
YugaByte nodes and which will have network connectivity to your current VPC, in which YugaWare
resides.
- Finally, click `Save` and give it a couple of minutes, as it will need to do a bit of work in
the background. This includes generating a new VPC, a network, subnetworks in all available
regions, as well as a new firewall rule, VPC peering for network connectivity and a custom SSH
keypair for YugaWare-to-YugaByte connectivity

Note: Choosing to use the same VPC as YugaWare is an advanced option, which currently assumes that
you are in complete control over this VPC and will be responsible for setting up the networking,
SSH access and firewall rules for it!

If all went well, you should see something like:

![GCP Configuration -- success](/images/ee/gcp-setup/gcp-configure-success.png)

Now we are ready to create a YugaByte universe on GCP.
