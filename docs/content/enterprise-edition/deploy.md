---
date: 2016-03-09T00:11:02+01:00
title: Deploy Admin Console
weight: 50
---
YugaByte Enterprise Edition is best fit for mission-critical deployments such as production or pre-production test. It starts out by first installing **YugaWare**, the YugaByte admin console, in a highly available mode and then spinning up YugaByte clusters on one or more datacenters (across public cloud and private on-premises datacenters).

## Prerequisites

YugaWare, the YugaByte admin console, is a containerized application that is installed and managed via [Replicated](https://www.replicated.com/) for mission-critical environments (such as production or performance or failure mode testing). Replicated is a purpose-built tool for on-premises deployment and lifecycle management of containerized applications. For environments that are not mission-critical such as those needed for local development or simple functional testing, you can also use the [Community Edition](/community-edition/get-started/).

A dedicated host or VM with the following characteristics is needed for YugaWare to run via Replicated.

### Operating systems supported

Only Linux-based systems are supported by Replicated at this point. This Linux OS should be 3.10+ kernel, 64bit and ready to run docker-engine 1.7.1 - 17.03.1-ce (with 17.03.1-ce being the recommended version). Some of the supported OS versions are:

- Ubuntu 16.04+
- Red Hat Enterprise Linux 6.5+
- CentOS 7+
- Amazon AMI 2014.03 / 2014.09 / 2015.03 / 2015.09 / 2016.03 / 2016.09

The complete list of operating systems supported by Replicated are listed [here](https://www.replicated.com/docs/distributing-an-application/supported-operating-systems/)

### Permissions necessary for Internet-connected host

- Connectivity to the Internet, either directly or via a http proxy
- Ability to install and configure [docker-engine](https://docs.docker.com/engine/)
- Ability to install and configure [Replicated](https://www.replicated.com/), which is a containerized application itself and needs to pull containers from its own Replicated.com container registry
- Ability to pull YugaByte container images from [Quay.io](https://quay.io/) container registry, this will be done by Replicated automatically

### Permissions necessary for airgapped host

An “airgapped” host has no path to inbound or outbound Internet traffic at all. For such hosts, the installation is performed as a sudo user.

### Additional requirements

For airgapped hosts a supported version of docker-engine (currently 1.7.1 to 17.03.1-ce). If you do not have docker-engine installed, follow the instructions [here](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/) to first install docker-engine.

- Following ports should be open on the YugaWare host:
8800 (replicated ui), 80 (http for yugaware ui), 22 (ssh)
- Attached disk storage (such as persistent EBS volumes on AWS): 100 GB minimum
- A YugaByte license file (attached to your welcome email from YugaByte Support)

If you are running on AWS, all you need is a dedicated [**c4.xlarge**] (https://aws.amazon.com/ec2/instance-types/) or higher instance running Ubuntu 16.04. If you are running in the US West (Oregon) Region, use `ami-a58d0dc5` to launch a new instance if you don't already have one.


## Install on Internet-connected host

### Install Replicated

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

# install replicated behind a proxy
curl -x http://<proxy_address>:<proxy_port> https://get.replicated.com/docker | sudo bash

# after replicated install completes, make sure it is running 
sudo docker ps
```
You should see an output similar to the following.

![Replicated successfully installed](/images/replicated-success.png)



## Install on airgapped host

### Install Replicated

An “airgapped” host has no path to inbound or outbound Internet traffic at all. In order to install Replicated and YugaWare on such a host, we first download the binaries on a machine that has Internet connectivity and then copy the files over to the appropriate host.

On a machine connected to the Internet, perform the following steps.

```sh
# make a directory for downloading the binaries
sudo mkdir /opt/downloads

# change the owner user for the directory
sudo chown -R ubuntu:ubuntu /opt/downloads

# change to the directory
cd /opt/downloads

# get the replicated binary
wget https://s3-us-west-2.amazonaws.com/download.yugabyte.com/replicated.tar.gz 

# get the yugaware binary where the 85 refers to the version of the binary. change this number as needed.
wget https://s3-us-west-2.amazonaws.com/download.yugabyte.com/yugaware+-+85.airgap

# copy the binaries to the host
```

{{< note title="Note" >}}
On the host marked for installation, first ensure that a supported version of docker-engine (currently 1.7.1 to 17.03.1-ce). If you do not have docker-engine installed, follow the instructions [here](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/) to first install docker-engine.
{{< /note >}}

On the host marked for installation, first ensure that a supported version of docker-engine (currently 1.7.1 to 17.03.1-ce). If you do not have docker-engine installed, follow the instructions [here](https://www.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/) to first install docker-engine.

After docker-engine is installed, perform the following steps to install replicated.

```sh
# change to the directory
cd /opt/downloads

# expand the replicated binary
tar xzvf replicated.tar.gz

# install replicated (yugaware will be installed via replicated ui after replicated install completes)
cat ./install.sh | sudo bash -s airgap

# after replicated install completes, make sure it is running 
sudo docker ps
```

You should see an output similar to the following.

![Replicated successfully installed](/images/replicated-success.png)


### Install YugaWare via Replicated

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

Two options to install YugaWare are presented.

#### Online Install
![Replicated License Online Install](/images/replicated-license-online-install-option.png)

![Replicated License Online Progress](/images/replicated-license-progress.png)

#### Airgapped Install
![Replicated License Airgapped Install](/images/replicated-license-airgapped-install-option.png)

![Replicated License Airgapped Path](/images/replicated-license-airgapped-path.png)

![Replicated License Airgapped Progress](/images/replicated-license-airgapped-progress.png)

#### Secure Replicated
The next step is to add a password to protect the Replicated admin console (note that this admin console is for Replicated and is different from YugaWare, the admin console for YugaByte).

![Replicated Password](/images/replicated-password.png)

Replicated is going to perform a set of pre-flight checks to ensure that the host is configured correctly for the YugaWare application.

![Replicated Checks](/images/replicated-checks.png)

Clicking Continue above will bring us to YugaWare configuration.

## Configure 

Configuring YugaWare is really simple. A randomly generated password for the YugaWare config database is already pre-filled. You can make a note of it for future use or change it to a new password of your choice. Additionally, `/opt/yugabyte` is pre-filled as the location of the directory on the YugaWare host where all the YugaWare data will be stored.  Clicking Save on this page will take us to the Replicated Dashboard.

![Replicated YugaWare Config](/images/replicated-yugaware-config.png)

For airgapped installation , all the containers powering the YugaWare application are already available with Replicated. For non-airgapped installations, these containers will be downloaded from the Quay.ui Registry when the Dashboard is first launched. Replicated will automatically start the application as soon as all the container images are available.

![Replicated Dashboard](/images/replicated-dashboard.png)

Click on "View release history" to see the release history of the YugaWare application.

![Replicated Release History](/images/replicated-release-history.png)

After starting the YugaWare application, you must register a new tenant in YugaWare by following the instructions in the section below

### Register tenant

Go to [http://yugaware-host-public-ip/register] (http://yugaware-host-public-ip/register) to register a tenant account. Note that by default YugaWare runs as a single-tenant application. If you are using YugaWare in a local node or local cluster mode, then this single tenant has already been pre-created for your convenience.

![Register](/images/register.png)

After clicking Submit, you will be automatically logged into YugaWare. By default, [http://yugaware-host-public-ip](http://yugaware-host-public-ip) redirects to [http://yugaware-host-public-ip/login](http://yugaware-host-public-ip/login). Login to the application using the credentials you had provided during the Register customer step.

![Login](/images/login.png)

By clicking on the top right dropdown or going directly to [http://yugaware-host-public-ip/profile](http://yugaware-host-public-ip/profile), you can change the profile of the customer provided during the Register customer step.

![Profile](/images/profile.png)

Now you are ready to administer YugaByte clusters as documented [here](/enterprise-edition/admin/).

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
docker images | grep "yuga" | awk '{print $3}' | xargs docker rmi -f

# delete the mapped directory
rm -rf /opt/yugabyte

```

And then uninstall Replicated itself by following instructions documented [here](https://www.replicated.com/docs/distributing-an-application/installing-via-script/#removing-replicated)

