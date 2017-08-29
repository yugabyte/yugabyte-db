---
date: 2016-03-09T19:56:50+01:00
title: Enterprise Edition - Administer YugaByte
weight: 40
---

This section details how to administer YugaByte using the YugaWare admin console.

## Prerequisites

### Public cloud

If you plan to create YugaByte clusters on public cloud providers such as Amazon Web Services (AWS) or Google Cloud Platform (GCP), all you need to provide on YugaWare UI is your cloud provider credentials. YugaWare will use those credentials to automatically provision and de-provision instances that run YugaByte. An 'instance' for YugaByte includes a compute instance as well as local or remote disk storage attached to the compute instance.

If you are using AWS, you will also need to share your AWS Account ID with YugaByte Support so that we can make our YugaByte base AMI accessible to your account. You can find your AWS Account ID at the top of the [AWS My Account](https://console.aws.amazon.com/billing/home?#/account) page.

{{< note title="Note" >}}
You will need to agree to the AWS Marketplace Terms [here](https://aws.amazon.com/marketplace/pp/B00O7WM7QW) for Centos 7 before you can spin up YugaByte instances that are based on Centos 7. 
{{< /note >}}

### Private cloud or on-premises data centers

The prerequisites here are same as that of the [Community Edition](/community-edition/deploy/#prerequisites/).

## Configure cloud providers

If no cloud providers are configured yet, the main Dashboard page highlights the need to configure at least 1 cloud provider.

![Configure Cloud Provider](/images/configure-cloud-provider.png)

### Amazon Web Services

YugaWare ensures that YugaByte instances run inside your own AWS account and are secured by a dedicated VPC and Key Pair. After you provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html), YugaWare invokes AWS APIs to perform the following actions. Note that the AWS Account Name should be unique for each instance of YugaWare integrating with a given AWS Account.

1. Retrieves the regions/AZs as well as the available instance types configured for this AWS account and initializes its own Amazon cloud provider.

2. Creates a new [AWS Key Pair](s://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html) to be used to SSH into the YugaByte instances. The private key will be available for download later from the YugaWare UI.

3. Creates a new [AWS VPC](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-vpc.html) for YugaByte instances and then peers them with YugaWare's own VPC

![Configure AWS](/images/configure-aws-1.png)

![AWS Configured Successfully](/images/configure-aws-2.png)

Now we are ready to create a YugaByte universe on AWS.

### Docker Platform

The [local node](/get-started/local-node/) is a great way to get started in a developer's localhost environment and the [local cluster] (/get-started/local-cluster/)  approach is great for testing operational scenarios including YugaByte clustering. The same local cluster approach is also possible in the highly available version of YugaWare.

Go to the Docker tab in the Configuration section and click Setup to initialize Docker as a cloud provider. Note that Docker Platform is already installed on the YugaWare host when you installed Replicated.

![Configure Docker](/images/configure-docker-1.png)

![Docker Configured Successfully](/images/configure-docker-2.png)

As you can see above, the above initialization setup creates 2 dummy regions (US West and US East) with 3 dummy availability zones each. Now we are ready to create a containerized YugaByte universe running on the YugaWare host.

### Google Cloud Platform

\<docs coming soon\>

### On-Premises Datacenters

\<docs coming soon\>

## Create universe

Universe is a cluster of YugaByte instances grouped together to perform as one logical distributed database. All instances belonging to a single Universe run on the same type of cloud provider node. 

If there are no universes created yet, the Dashboard page will look like the following.

![Dashboard with No Universes](/images/no-univ-dashboard.png)

Click on "Create Universe" to enter your intent for the universe. The **Provider**, **Regions** and **Instance Type** fields were initialized based on the [cloud providers configured](/admin/#configure-cloud-providers). As soon as **Provider**, **Regions** and **Nodes** are entered, an intelligent Node Placement Policy kicks in to specify how the nodes should be placed across all the Availability Zones so that maximum availability is guaranteed. 

Here's how to create a universe on the [AWS](#amazon-web-services) cloud provider.
![Create Universe on AWS](/images/create-univ.png)

Here's how to create a universe on the [Docker](#docker-platform) cloud provider.
![Create Universe on Docker](/images/create-univ-docker.png)

Here's how a Universe in Pending state looks like.
![Dashboard with Pending Universe](/images/pending-univ-dashboard.png)

![Detail for a Pending Universe](/images/pending-univ-detail.png)

![Tasks for a Pending Universe](/images/pending-univ-tasks.png)

![Nodes for a Pending Universe](/images/pending-univ-nodes.png)

## Expand or shrink universe

\<docs coming soon\>
