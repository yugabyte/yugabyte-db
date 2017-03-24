---
date: 2016-03-09T00:11:02+01:00
title: Get started
weight: 10
---

## Prerequisites

### YugaWare

YugaWare, YugaByte's admin console, is a containerized application that's installed and managed via [Replicated] (https://www.replicated.com/). Replicated is a management platform for deploying containerized applications like YugaWare on-premises. A dedicated host or VM with the following characteristics is needed for YugaWare to run via Replicated.

#### Operating systems (3.10+ kernel, 64bit, ready to run Docker 1.13.1+)

- Ubuntu 14.04 / 15.10 / 16.04 (recommended)
- Red Hat Enterprise Linux 6.5+
- CentOS 7+ 
- Amazon AMI 2014.03 / 2014.09 / 2015.03 / 2015.09 / 2016.03 / 2016.09

#### Additional requirements

- Following ports should be open on the YugaWare host: 
8800 (replicated ui), 80 (http for yugaware ui), 22 (ssh)
- Attached disk storage (such as persistent EBS volumes on AWS): 40 GB minimum
- A YugaByte license file (attached to your welcome email from YugaByte Support)

If you are running on AWS, all you need is a dedicated [**c4.xlarge**] (https://aws.amazon.com/ec2/instance-types/) or higher instance running Centos 7+.

### YugaByte database

#### Public cloud

If you plan to create YugaByte clusters on public cloud providers such as Amazon Web Services (AWS) or Google Cloud Platform (GCP), all you need to provide on YugaWare UI is your cloud provider credentials. YugaWare will use those credentials to automatically provision and de-provision nodes that run YugaByte. Note that a node includes a compute instance as well as local or remote disk storage attached to the compute instance.

If you are using AWS, you will also need to share your AWS Account ID with YugaByte Support so that we can make our YugaByte base AMI accesible to your account.

#### Private cloud or on-premises data centers

Dedicated hosts or VMs running Centos 7+ with local or remote attached storage. All these hosts should be accessible over SSH from the YugaWare host


## Installation

### Installing Replicated

YugaByte clusters can be created and then managed from YugaWare. First step to getting started with YugaWare is to install Replicated. 


```sh

# uninstall any older versions of docker (ubuntu-based hosts)
sudo apt-get remove docker docker-engine

# uninstall any older versions of docker (centos-based hosts)
sudo yum remove docker \
                docker-common \
                container-selinux \
                docker-selinux \
                docker-engine

# install replicated
curl -sSL https://get.replicated.com/docker | sudo bash

# after replicated install completes, make sure it is running 
sudo docker ps
```
You should see the following output

![Replicated successfully installed](/images/replicated-success.png)

### Configuring Replicated for YugaWare

Launch Replicated UI by going to [http://host-public-ip:8800] (http://host-public-ip:8800). The browser warning shown next states that the connection to the server is not private. We will adress this warning as soon as we setup HTTPS for the Replicated admin console in the next step. Click ADVANCED to bypass this warning and go to the Replicated admin console.

![Replicated SSL warning](/images/replicated-warning.png)

Setup HTTPS for Replicated

You can provide your own custom SSL certificate along with a hostname. 

![Replicated HTTPS setup](/images/replicated-https.png)

The simplest option is use a self-signed cert for now and add the custom SSL certificate later. Note that you will have to connect to the Replicated admin console only using IP address (as noted below).

![Replicated Self Signed Cert](/images/replicated-selfsigned.png)

Now we are ready to upload the YugaByte license file received from YugaByte Support. 

![Replicated License Upload](/images/replicated-license-upload.png)

![Replicated License Progress](/images/replicated-license-progress.png)

The next step is to add a password to protect the Replicated admin console

![Replicated Password](/images/replicated-password.png)

Replicated is going to perform a set of pre-flight checks to ensure that the host is configured correctly for the YugaWare application.

![Replicated Checks](/images/replicated-checks.png)

## Configuration

Configuring YugaWare is really simple. A randomly generated password for the YugaWare config database is already pre-filled. You can make a note of it for future use or change it to a new password of your choice. Additionally, `/opt/yugabyte` is pre-filled as the location of the directory on the YugaWare host where all the YugaWare data will be stored.  Clicking Save on this page will take us to the Replicated Dashboard.

![Replicated YugaWare Config](/images/replicated-yugaware-config.png)

All the containers powering the YugaWare application will be downloaded from the Replicated Registry when the Dashboard is first launched. Replicated will automatically start the application as soon as all the container images are downloaded.

![Replicated Dashboard](/images/replicated-dashboard.png)

After starting the YugaWare application, you can register a new customer in YugaWare by following the instructions in the [Admin](/http://localhost:1313/admin/#register-customer) section.

## Backup

We recommend a weekly machine snapshot and weekly backups of `/opt/yugabyte`.

Doing a machine snapshot and backing up the above directory before performing an update is recommended as well.
