---
date: 2016-03-09T19:56:50+01:00
title: Administer
weight: 20
---

## Register customer

Go to [http://yugaware-host-public-ip/register] (http://yugaware-host-public-ip/register) to register a customer (aka tenant) account. Note that by default YugaWare runs as a single-tenant application.

![Register](/images/register.png)

After clicking Submit, you will be automatically logged into YugaWare.

## Login and change profile

By default, [http://yugaware-host-public-ip](http://yugaware-host-public-ip) redirects to [http://yugaware-host-public-ip/login](http://yugaware-host-public-ip/login). Login to the application using the credentials you had provided during the [Register customer](/admin/#register-customer) step.

![Login](/images/login.png)

By clicking on the top right dropdown or going directly to [http://yugaware-host-public-ip/profile](http://yugaware-host-public-ip/profile), you can change the profile of the customer provided during the [Register customer](/admin/#register-customer) step.

![Profile](/images/profile.png)

## Configure cloud providers

If no cloud providers are configured yet, the main Dashboard page highlights the need to create at least 1 cloud provider.

![Configure Cloud Provider](/images/configure-cloud-provider.png)

### Amazon Web Services

YugaWare ensures that YugaByte instances run inside your own AWS account and are secured by a dedicated VPC and Key Pair. After you provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html), YugaWare invokes AWS APIs to perform the following actions. Note that the AWS Account Name should be unique for each instance of YugaWare integrating with a given AWS Account.

1. Retrieves the regions/AZs as well as the available instance types configured for this AWS account and initializes it's own Amazon cloud provider.

2. Creates a new [AWS Key Pair](s://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html) to be used to SSH into the YugaByte instances. The private key will be available for download later from the YugaWare UI.

3. Creates a new [AWS VPC](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-vpc.html) for YugaByte instances and then peers them with YugaWare's own VPC

![Configure AWS](/images/configure-aws-1.png)

![AWS Configured Successfully](/images/configure-aws-2.png)

Now we are ready to create a YugaByte universe (aka cluster) on AWS.

### Google Cloud Platform

\<docs coming soon\>

### Docker Platform

\<docs coming soon\>

### On-Premises Datacenters

\<docs coming soon\>

## Create universe

Universe is a cluster of YugaByte instances grouped together to perform as one logical, distributed database. All instances belonging to a single Universe run on the same type of cloud provider node. 

If there are no universes created yet, the Dashboard page will look like the following.

![Dashboard with No Universes](/images/no-univ-dashboard.png)

Click on "Create Universe" to enter your intent for the universe. The **Provider**, **Regions** and **Instance Type** fields were initialized based on the [cloud providers configured](/admin/#configure-cloud-providers). As soon as **Provider**, **Regions** and **Nodes** are entered, an intelligent Node Placement Policy kicks in to specify how the nodes should be placed across all the Availability Zones so that maximum availability is guaranteed. 

![Create Universe](/images/create-univ.png)

![Dashboard with Pending Universe](/images/pending-univ-dashboard.png)

![Detail for a Pending Universe](/images/pending-univ-detail.png)

![Tasks for a Pending Universe](/images/pending-univ-tasks.png)

![Nodes for a Pending Universe](/images/pending-univ-nodes.png)

## Expand or shrink universe

\<docs coming soon\>
