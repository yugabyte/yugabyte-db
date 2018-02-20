---
title: Configure Cloud Providers
weight: 680
---

This section details how to configure cloud providers for YugaByte DB using the YugaWare Admin Console. If no cloud providers are configured in YugaWare yet, the main Dashboard page highlights the need to configure at least 1 cloud provider.

![Configure Cloud Provider](/images/ee/configure-cloud-provider.png)

## Prerequisites

### Public cloud

If you plan to run YugaByte DB nodes on public cloud providers such as Amazon Web Services (AWS) or Google Cloud Platform (GCP), all you need to provide on YugaWare UI is your cloud provider credentials. YugaWare will use those credentials to automatically provision and de-provision instances that run YugaByte. An 'instance' for YugaByte includes a compute instance as well as local or remote disk storage attached to the compute instance.

If you are using AWS, you will also need to share your AWS Account ID with YugaByte Support so that we can make our YugaByte base AMI accessible to your account. You can find your AWS Account ID at the top of the [AWS My Account](https://console.aws.amazon.com/billing/home?#/account) page.

{{< note title="Note" >}}
You will need to agree to the AWS Marketplace Terms [here](https://aws.amazon.com/marketplace/pp/B00O7WM7QW) for CentOS 7 before you can spin up YugaByte instances that are based on CentOS 7. 
{{< /note >}}

### Private cloud or on-premises data centers

The prerequisites here are same as that of the [Community Edition](/deploy/multi-node-cluster/#prerequisites).

## Amazon Web Services

YugaWare ensures that YugaByte DB nodes run inside your own AWS account and are secured by a dedicated VPC and Key Pair. After you provide your [AWS Access Key ID and Secret Key](http://docs.aws.amazon.com/general/latest/gr/managing-aws-access-keys.html), YugaWare invokes AWS APIs to perform the following actions. Note that the AWS Account Name should be unique for each instance of YugaWare integrating with a given AWS Account.

1. Retrieves the regions/AZs as well as the available instance types configured for this AWS account and initializes its own Amazon cloud provider.

2. Creates a new [AWS Key Pair](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/ec2-key-pairs.html) to be used to SSH into the YugaByte instances. The private key will be available for download later from the YugaWare UI.

3. Creates a new [AWS VPC](https://docs.aws.amazon.com/AWSEC2/latest/UserGuide/using-vpc.html) for YugaByte instances and then peers them with YugaWare's own VPC

![Configure AWS](/images/ee/configure-aws-1.png)

![AWS Configuration in Progress](/images/ee/configure-aws-2.png)

![AWS Configured Successfully](/images/ee/configure-aws-3.png)

Now we are ready to create a YugaByte universe on AWS.

## Google Cloud Platform

![AWS Configured Successfully](/images/ee/configure-gcp-3.png)

## Microsoft Azure

Support for Microsoft Azure in YugaByte DB Enterprise Edition is currently in the works. You are recommended to treat Microsoft Azure as an [On-Premises Datacenter](/deploy/enterprise-edition/configure-cloud-providers/#on-premises-datacenters) for now.

## Docker Platform

The [local cluster] (/quick-start/install/) approach is great for testing operational scenarios including fault-tolerance and auto rebalancing. The same local cluster approach is also possible in YugaWare.

Go to the Docker tab in the Configuration section and click Setup to initialize Docker as a cloud provider. Note that Docker Platform is already installed on the YugaWare host when you installed Replicated.

![Configure Docker](/images/ee/configure-docker-1.png)

![Docker Configured Successfully](/images/ee/configure-docker-2.png)

As you can see above, the above initialization setup creates 2 dummy regions (US West and US East) with 3 dummy availability zones each. Now we are ready to create a containerized YugaByte universe running on the YugaWare host.


## On-Premises Datacenters

### Prerequisites

Dedicated hosts or cloud VMs running CentOS 7 with local or remote attached storage. All these hosts should be accessible over SSH from the YugaWare host. If your instance does not have public Internet access, make sure the following packages have been installed (all can be retrieved from the yum repo **epel**, make sure to use the latest epel release repo):

- epel-release
- ntp
- collectd
- python-pip
- python-devel
- python-psutil
- libsemanage-python
- policycoreutils-python

Here's the command to install these packages.

```{.sh .copy .separator-dollar}
# install pre-requisite packages
$ sudo yum install -y epel-release ntp collectd python-pip python-devel python-psutil libsemanage-python policycoreutils-python
```

### Configure the On-Premises Datacenter provider

![Configure On-Premises Datacenter Provider](/images/ee/configure-onprem-1.png)

![On-Premises Datacenter Provider Configuration in Progress](/images/ee/configure-onprem-2.png)

![On-Premises Datacenter Provider Configured Successfully](/images/ee/configure-onprem-3.png)

## Next Step

You are now ready to create YugaByte DB universes as outlined  in the [next section](/manage/enterprise-edition/create-universe/).
