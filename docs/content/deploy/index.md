---
date: 2016-03-09T00:11:02+01:00
title: Deploy
weight: 30
---
Deploying YugaByte in a **production** or **pre-production test** environment is easy. First install **YugaWare**, the YugaByte admin console, in a highly available mode and then spin up YugaByte clusters on any public cloud or private datacenters in no time.

## Prerequisites

### YugaWare

YugaWare, YugaByte's Admin Console, is a containerized application that's installed and managed via [Replicated] (https://www.replicated.com/) for mission-critical environments. Replicated is purpose built for on-premises deployment and lifecycle management of containerized applications such as YugaWare. A dedicated host or VM with the following characteristics is needed for YugaWare to run via Replicated.

#### Operating systems (3.10+ kernel, 64bit, ready to run Docker 1.13.1+)

- Ubuntu 16.04+ (recommended)
- Red Hat Enterprise Linux 6.5+
- CentOS 7+ 
- Amazon AMI 2014.03 / 2014.09 / 2015.03 / 2015.09 / 2016.03 / 2016.09

#### Additional requirements

- Following ports should be open on the YugaWare host: 
8800 (replicated ui), 80 (http for yugaware ui), 22 (ssh)
- Attached disk storage (such as persistent EBS volumes on AWS): 40 GB minimum
- A YugaByte license file (attached to your welcome email from YugaByte Support)

If you are running on AWS, all you need is a dedicated [**c4.xlarge**] (https://aws.amazon.com/ec2/instance-types/) or higher instance running Ubuntu 16.04. Use `ami-a58d0dc5` to launch a new instance if you don't already have one.

### YugaByte database

#### Public cloud

If you plan to create YugaByte clusters on public cloud providers such as Amazon Web Services (AWS) or Google Cloud Platform (GCP), all you need to provide on YugaWare UI is your cloud provider credentials. YugaWare will use those credentials to automatically provision and de-provision nodes that run YugaByte. A 'node' for YugaByte includes a compute instance as well as local or remote disk storage attached to the compute instance.

If you are using AWS, you will also need to share your AWS Account ID with YugaByte Support so that we can make our YugaByte base AMI accessible to your account. You can find your AWS Account ID at the top of the [AWS My Account](https://console.aws.amazon.com/billing/home?#/account) page.

{{< note title="Note" >}}
You will need to agree to the AWS Marketplace Terms [here](https://aws.amazon.com/marketplace/pp/B00O7WM7QW) for Centos 7 before you can spin up YugaByte instances that are based on Centos 7. 
{{< /note >}}

#### Private cloud or on-premises data centers

Dedicated hosts or VMs running Centos 7+ with local or remote attached storage. All these hosts should be accessible over SSH from the YugaWare host


## Install

### Installing Replicated

YugaByte clusters are created and managed from YugaWare. First step to getting started with YugaWare is to install Replicated. 


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

#### Setup HTTPS for Replicated

Launch Replicated UI by going to [http://yugaware-host-public-ip:8800] (http://yugaware-host-public-ip:8800). The warning shown next states that the connection to the server is not private (yet). We will address this warning as soon as we setup HTTPS for the Replicated admin console in the next step. Click Continue to Setup and then ADVANCED to bypass this warning and go to the Replicated admin console.

![Replicated Browser TLS](/images/replicated-browser-tls.png)

![Replicated SSL warning](/images/replicated-warning.png)


You can provide your own custom SSL certificate along with a hostname. 

![Replicated HTTPS setup](/images/replicated-https.png)

The simplest option is use a self-signed cert for now and add the custom SSL certificate later. Note that you will have to connect to the Replicated admin console only using IP address (as noted below).

![Replicated Self Signed Cert](/images/replicated-selfsigned.png)

#### Upload License File

Now we are ready to upload the YugaByte license file received from YugaByte Support. 

![Replicated License Upload](/images/replicated-license-upload.png)

![Replicated License Progress](/images/replicated-license-progress.png)

The next step is to add a password to protect the Replicated admin console

![Replicated Password](/images/replicated-password.png)

Replicated is going to perform a set of pre-flight checks to ensure that the host is configured correctly for the YugaWare application.

![Replicated Checks](/images/replicated-checks.png)

Clicking Continue above will bring us to YugaWare configuration.

## Configure

Configuring YugaWare is really simple. A randomly generated password for the YugaWare config database is already pre-filled. You can make a note of it for future use or change it to a new password of your choice. Additionally, `/opt/yugabyte` is pre-filled as the location of the directory on the YugaWare host where all the YugaWare data will be stored.  Clicking Save on this page will take us to the Replicated Dashboard.

![Replicated YugaWare Config](/images/replicated-yugaware-config.png)

All the containers powering the YugaWare application will be downloaded from the Replicated Registry when the Dashboard is first launched. Replicated will automatically start the application as soon as all the container images are downloaded.

![Replicated Dashboard](/images/replicated-dashboard.png)

Click on "View release history" to see the release history of the YugaWare application.

![Replicated Release History](/images/replicated-release-history.png)

After starting the YugaWare application, you can register a new customer in YugaWare by following the instructions in the [Admin] (/admin/#register-customer) section.

## Backup

We recommend a weekly machine snapshot and weekly backups of `/opt/yugabyte`.

Doing a machine snapshot and backing up the above directory before performing an update is recommended as well.


## Maintain

### Upgrade

Upgrades to YugaWare are managed seamlessly in the Replicated UI. Whenever a new YugaWare version is available for upgrade, the Replicated UI will show the same. You can apply the upgrade anytime you wish.

Upgrades to Replicated are as simple as rerunning the Replicated install command. This will upgrade Replicated components with the latest build.


### Uninstall
Stop and remove the YugaWare application on Replicated first. 

```sh
# stop the yugaware application on replicated
replicated apps

# replace <appid> with the application id of yugaware from the command above
replicated app <appid> stop

# remove yugaware app
replicated app <appid> rm

# remove all yugaware containers
docker images | grep "yuga" | awk '{print $1}' | xargs docker rm

# delete the mapped directory
rm -rf /opt/yugabyte

```

And then uninstall Replicated itself by following instructions documented [here](https://www.replicated.com/docs/distributing-an-application/installing-via-script/#removing-replicated)

