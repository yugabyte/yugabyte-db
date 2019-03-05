Configuring YugaWare to deploy universes in AWS provides several knobs for you to tweak, depending on your preferences:

![AWS Empty Provider](/images/ee/aws-setup/aws_provider_empty.png)

## Provider name
This is an internal tag used for organizing your providers, so you know where you want to deploy your YugaByte DB universes.

## Credentials
In order to actually deploy YugaByte DB nodes in your AWS account, YugaWare will require access to a set of cloud credentials. These can be provided in one of the following ways:

- Directly provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html)
- Attach an [IAM role](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/iam-roles-for-amazon-ec2.html) to the YugaWare VM in the EC2 tab

## KeyPairs
In order to be able to provision EC2 instances with YugaByte DB, YugaWare will require SSH access to these. To that end, there are two options to choose from:

- Allow YugaWare to create and manage [KeyPairs](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html). In this mode, YugaWare will create KeyPairs across all the regions you choose to setup and store the relevant private key part of these locally in order to SSH into future EC2 instances.
- Use your own already existing KeyPairs. For this you will need to provide the name of the KeyPair, as well as the private key content and the corresponding SSH user. **Note that currently, all this info must be the same across all the regions you choose to provision!**

## Enabling Hosted Zones
Integrating with hosted zones can make YugaByte DB universes easily discoverable. YugaWare can integrate with Route53 to provide you managed CNAME entries for your YugaByte DB universes, which will be updated as you change the set of nodes, to include all the relevant ones for each of your universes.

## Global deployment
For deployment, YugaWare aims to provide you with easy access to the many regions that AWS makes available globally. To that end, it allows you to select which regions you wish to deploy to and supports two different ways of configuring your setup, based on your environment:

### YugaWare managed configuration

If you choose to allow YugaWare to configure, own and manage a full cross-region deployment of VPCs, it will generate a YugaByte specific VPC in each selected region, then interconnect them, as well as the VPC in which YugaWare was deployed, through [VPC Peering](https://docs.aws.amazon.com/vpc/latest/userguide/vpc-peering.html). This mode will also setup all the other relevant sub-components in all regions, such as Subnets, Security Groups and Routing Table entries. Some notes:

- You can **optionally** provide a custom CIDR block for each regional VPC, else we will choose some sensible defaults internally, aiming to not overlap across regions.
- You can **optionally** provide a custom AMI ID to use in each region, else, we will use a recent [marketplace centos AMI](https://wiki.centos.org/Cloud/AWS).

![New Region Modal](/images/ee/aws-setup/aws_new_region.png)

### Self managed configuration

If you wish to use your own custom VPCs, this is also supported. This will allow you the most level of customization over your VPC setup:

- You **must** provide a VPC ID to use for each region.
- You **must** provide a Security Group ID to use for each region. This will be attached to all YugaByte DB nodes and must allow traffic from all other YugaByte DB nodes, even across regions, if you deploy across multiple regions.
- You **must** provide the mapping of what Subnet IDs to use for each Availability Zone in which you wish to be able to deploy. This is required to ensure YugaWare can deploy nodes in the correct network isolation that you desire in your environment.
- You can **optionally** provide a custom AMI ID to use in each region, else, we will use a recent [marketplace centos AMI](https://wiki.centos.org/Cloud/AWS).

![Custom Region Modal](/images/ee/aws-setup/aws_custom_region.png)

One really important note if you choose to provide your own VPC information: **it is your responsibility to have preconfigured networking connectivity!** In the case of a single region deployment, this might simply be a matter of region or VPC local Security Groups. However, across regions, the setup can get quite complex. We suggest using the VPC Peering feature of AWS, such that you can setup private IP connectivity between nodes across regions:

- VPC Peering Connections must be established in an N x N matrix, such that every VPC in every region you configure must be peered to every other VPC in every other region.
- Routing Table entries in every regional VPC should route traffic to every other VPC CIDR block across the PeeringConnection to that respective VPC. This must match the Subnets that you provided during the configuration step.
- Security Groups in each VPC can be hardened by only opening up the relevant ports to the CIDR blocks of the VPCs from which you are expecting traffic.
- Lastly, if you deploy YugaWare in a different VPC than the ones in which you intend to deploy YugaByte DB nodes, then its own VPC must also be part of this cross-region VPC mesh, as well as setting up Routing Table entries in the source VPC (YugaWare) and allowing one further CIDR block (or public IP) ingress rule on the Security Groups for the YugaByte DB nodes (to allow traffic from YugaWare or its VPC).

## Final notes
If you allow YugaWare to manage KeyPairs for you and you deploy multiple YugaWare instances across your environment, then the AWS Provider name should be unique for each instance of YugaWare integrating with a given AWS Account.

## Marketplace acceptance

Finally, in case you did not provide your own custom AMI IDs, before we can proceed to creating a universe, let us check that you can actually spin up EC2 instances with our default AMIs. Our reference AMIs come from a [Marketplace CentOS 7 Product](https://aws.amazon.com/marketplace/pp/B00O7WM7QW/). Visit that link while logged into your AWS account and click the top-right `Continue to Subscribe` button.

If you are not already subscribed and have thus not accepted the `Terms and Conditions`, then you should see something like this:

![Marketplace accept](/images/ee/aws-setup/marketplace-accept.png)

If so, please click the `Accept Terms` button and wait for the page to switch to a successful state. You should see the following once the operation completes, or if you had already previously subscribed and accepted the terms:

![Marketplace success](/images/ee/aws-setup/marketplace-success.png)

Now we are ready to create a YugaByte DB universe on AWS.
