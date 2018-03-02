---
title: Install Admin Console
weight: 670
---

YugaWare, the YugaByte Admin Console, is a containerized application that is installed and managed via [Replicated](https://www.replicated.com/) for mission-critical environments (such as production or performance or failure mode testing). Replicated is a purpose-built tool for on-premises deployment and lifecycle management of containerized applications. For environments that are not mission-critical such as those needed for local development or simple functional testing, you can also use the [Community Edition](/quick-start/install).

## Prerequisites

A dedicated host or VM with the following characteristics is needed for YugaWare to run via Replicated.

### Operating systems supported

Only Linux-based systems are supported by Replicated at this point. This Linux OS should be 3.10+ kernel, 64bit and ready to run docker-engine 1.7.1 - 17.06.2-ce (with 17.06.2-ce being the recommended version). Some of the supported OS versions are:

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

- Following ports should be open on the YugaWare host: 8800 (replicated ui), 80 (http for yugaware ui), 22 (ssh)
- Attached disk storage (such as persistent EBS volumes on AWS): 100 GB minimum
- A YugaByte license file (attached to your welcome email from YugaByte Support)
- Ability to connect from the YugaWare host to all the YugaByte DB data nodes. If this is not setup, [setup passwordless ssh](/deploy/enterprise-edition/admin-console/#step-5-troubleshoot-yugaware).

If you are running on AWS, all you need is a dedicated [**c4.xlarge**] (https://aws.amazon.com/ec2/instance-types/) or higher instance running Ubuntu 16.04. If you are running in the US West (Oregon) Region, use `ami-a58d0dc5` to launch a new instance if you don't already have one.


## Step 1. Install Replicated 

### On an Internet-connected host

YugaByte clusters are created and managed from YugaWare. First step to getting started with YugaWare is to install Replicated. 


```{.sh .copy .separator-dollar}
# uninstall any older versions of docker (ubuntu-based hosts)
$ sudo apt-get remove docker docker-engine
```

```{.sh .copy .separator-dollar}
# uninstall any older versions of docker (centos-based hosts)
$ sudo yum remove docker \
                docker-common \
                container-selinux \
                docker-selinux \
                docker-engine
```

```{.sh .copy .separator-dollar}
# install replicated
$ curl -sSL https://get.replicated.com/docker | sudo bash
```

```{.sh .copy .separator-dollar}
# install replicated behind a proxy
$ curl -x http://<proxy_address>:<proxy_port> https://get.replicated.com/docker | sudo bash
```

```{.sh .copy .separator-dollar}
# after replicated install completes, make sure it is running 
$ sudo docker ps
```
You should see an output similar to the following.

![Replicated successfully installed](/images/replicated/replicated-success.png)

Next step is install YugaWare as described in the [section below](/deploy/enterprise-edition/admin-console/#step-2-install-yugaware-via-replicated).

### On an airgapped host

An “airgapped” host has no path to inbound or outbound Internet traffic at all. In order to install Replicated and YugaWare on such a host, we first download the binaries on a machine that has Internet connectivity and then copy the files over to the appropriate host.

On a machine connected to the Internet, perform the following steps.

```{.sh .copy .separator-dollar}
# make a directory for downloading the binaries
$ sudo mkdir /opt/downloads
```

```{.sh .copy .separator-dollar}
# change the owner user for the directory
$ sudo chown -R ubuntu:ubuntu /opt/downloads
```

```{.sh .copy .separator-dollar}
# change to the directory
$ cd /opt/downloads
```

```{.sh .copy .separator-dollar}
# get the replicated binary
$ wget https://downloads.yugabyte.com/replicated.tar.gz 
```

```{.sh .copy .separator-dollar}
# get the yugaware binary where the 0.9.6.0 refers to the version of the binary. change this number as needed.
$ wget https://downloads.yugabyte.com/yugaware-0.9.6.0.airgap
```

On the host marked for installation, first ensure that a supported version of docker-engine (currently 1.7.1 to 17.03.1-ce). If you do not have docker-engine installed, follow the instructions [here](https://help.replicated.com/docs/kb/supporting-your-customers/installing-docker-in-airgapped/) to first install docker-engine.

After docker-engine is installed, perform the following steps to install replicated.

```{.sh .copy .separator-dollar}
# change to the directory
$ cd /opt/downloads
```

```{.sh .copy .separator-dollar}
# expand the replicated binary
$ ar xzvf replicated.tar.gz
```

```{.sh .copy .separator-dollar}
# install replicated (yugaware will be installed via replicated ui after replicated install completes)
# pick eth0 network interface in case multiple ones show up
$ cat ./install.sh | sudo bash -s airgap
```

```{.sh .copy .separator-dollar}
# after replicated install completes, make sure it is running 
$ sudo docker ps
```

You should see an output similar to the following.

![Replicated successfully installed](/images/replicated/replicated-success.png)

Next step is install YugaWare as described in the [section below](/deploy/enterprise-edition/admin-console/#step-2-install-yugaware-via-replicated).

## Step 2. Install YugaWare via Replicated

### Setup HTTPS for Replicated

Launch Replicated UI by going to [http://yugaware-host-public-ip:8800](http://yugaware-host-public-ip:8800). The warning shown next states that the connection to the server is not private (yet). We will address this warning as soon as we setup HTTPS for the Replicated Admin Console in the next step. Click Continue to Setup and then ADVANCED to bypass this warning and go to the Replicated Admin Console.

![Replicated Browser TLS](/images/replicated/replicated-browser-tls.png)

![Replicated SSL warning](/images/replicated/replicated-warning.png)


You can provide your own custom SSL certificate along with a hostname. 

![Replicated HTTPS setup](/images/replicated/replicated-https.png)

The simplest option is use a self-signed cert for now and add the custom SSL certificate later. Note that you will have to connect to the Replicated Admin Console only using IP address (as noted below).

![Replicated Self Signed Cert](/images/replicated/replicated-selfsigned.png)

### Upload License File

Now upload the YugaByte license file received from YugaByte Support. 

![Replicated License Upload](/images/replicated/replicated-license-upload.png)

Two options to install YugaWare are presented.

### 1. Online Install

![Replicated License Online Install](/images/replicated/replicated-license-online-install-option.png)

![Replicated License Online Progress](/images/replicated/replicated-license-progress.png)

### 2. Airgapped Install

![Replicated License Airgapped Install](/images/replicated/replicated-license-airgapped-install-option.png)

![Replicated License Airgapped Path](/images/replicated/replicated-license-airgapped-path.png)

![Replicated License Airgapped Progress](/images/replicated/replicated-license-airgapped-progress.png)

### Secure Replicated

The next step is to add a password to protect the Replicated Admin Console (note that this Admin Console is for Replicated and is different from YugaWare, the Admin Console for YugaByte DB).

![Replicated Password](/images/replicated/replicated-password.png)

### Pre-Flight Checks

Replicated will perform a set of pre-flight checks to ensure that the host is setup correctly for the YugaWare application.

![Replicated Checks](/images/replicated/replicated-checks.png)

Clicking Continue above will bring us to YugaWare configuration.

In case the pre-flight check fails, review the [Troubleshoot YugaWare](/deploy/enterprise-edition/admin-console/#step-5-troubleshoot-yugaware) section below to identify the resolution.

## Step 3. Configure YugaWare

Configuring YugaWare is really simple. A randomly generated password for the YugaWare config database is already pre-filled. You can make a note of it for future use or change it to a new password of your choice. Additionally, `/opt/yugabyte` is pre-filled as the location of the directory on the YugaWare host where all the YugaWare data will be stored.  Clicking Save on this page will take us to the Replicated Dashboard.

![Replicated YugaWare Config](/images/replicated/replicated-yugaware-config.png)

For airgapped installation , all the containers powering the YugaWare application are already available with Replicated. For non-airgapped installations, these containers will be downloaded from the Quay.ui Registry when the Dashboard is first launched. Replicated will automatically start the application as soon as all the container images are available.

![Replicated Dashboard](/images/replicated/replicated-dashboard.png)

Click on "View release history" to see the release history of the YugaWare application.

![Replicated Release History](/images/replicated/replicated-release-history.png)

After starting the YugaWare application, you must register a new tenant in YugaWare by following the instructions in the section below

### Register tenant

Go to [http://yugaware-host-public-ip/register](http://yugaware-host-public-ip/register) to register a tenant account. Note that by default YugaWare runs as a single-tenant application. 

![Register](/images/ee/register.png)

After clicking Submit, you will be automatically logged into YugaWare. By default, [http://yugaware-host-public-ip](http://yugaware-host-public-ip) redirects to [http://yugaware-host-public-ip/login](http://yugaware-host-public-ip/login). Login to the application using the credentials you had provided during the Register customer step.

![Login](/images/ee/login.png)

By clicking on the top right dropdown or going directly to [http://yugaware-host-public-ip/profile](http://yugaware-host-public-ip/profile), you can change the profile of the customer provided during the Register customer step.

![Profile](/images/ee/profile.png)

Next step is to configure one or more cloud providers in YugaWare as documented [here](/deploy/enterprise-edition/configure-cloud-providers/).

## Step 4. Maintain YugaWare

### Backup 

We recommend a weekly machine snapshot and weekly backups of `/opt/yugabyte`.

Doing a machine snapshot and backing up the above directory before performing an update is recommended as well.

### Upgrade

Upgrades to YugaWare are managed seamlessly in the Replicated UI. Whenever a new YugaWare version is available for upgrade, the Replicated UI will show the same. You can apply the upgrade anytime you wish.

Upgrades to Replicated are as simple as rerunning the Replicated install command. This will upgrade Replicated components with the latest build.


### Uninstall

Stop and remove the YugaWare application on Replicated first. 

```{.sh .copy .separator-dollar}
# stop the yugaware application on replicated
$ /usr/local/bin/replicated apps
```
```{.sh .copy .separator-dollar}
# replace <appid> with the application id of yugaware from the command above
$ /usr/local/bin/replicated app <appid> stop
```
```{.sh .copy .separator-dollar}
# remove yugaware app
$ /usr/local/bin/replicated app <appid> rm
```
```{.sh .copy .separator-dollar}
# remove all yugaware containers
$ docker images | grep "yuga" | awk '{print $3}' | xargs docker rmi -f
```
```{.sh .copy .separator-dollar}
# delete the mapped directory
$ rm -rf /opt/yugabyte
```

And then uninstall Replicated itself by following instructions documented [here](https://www.replicated.com/docs/distributing-an-application/installing-via-script/#removing-replicated).

## Step 5. Troubleshoot 

### SELinux turned on on YugaWare host

If your host has SELinux turned on, then docker-engine may not be able to connect with the host. Run the following commands to open the ports using firewall exceptions.

```{.sh .copy}
sudo firewall-cmd --zone=trusted --add-interface=docker0
sudo firewall-cmd --zone=public --add-port=9874-9879/tcp
sudo firewall-cmd --zone=public --add-port=80/tcp
sudo firewall-cmd --zone=public --add-port=80/tcp
sudo firewall-cmd --zone=public --add-port=5432/tcp
sudo firewall-cmd --zone=public --add-port=4000/tcp
sudo firewall-cmd --zone=public --add-port=9000/tcp
sudo firewall-cmd --zone=public --add-port=9090/tcp
```

### Unable to perform passwordless ssh into the data nodes

If your YugaWare host is not able to do passwordless ssh to the data nodes, follow the steps below.

```{.sh .copy .separator-dollar}
# Generate key pair
$ ssh-keygen -t rsa
```
```{.sh .copy .separator-dollar}
# Setup passwordless ssh to the data nodes with private IPs 10.1.13.150, 10.1.13.151, 10.1.13.152
$ for IP in 10.1.13.150 10.1.13.151 10.1.13.152; do
  ssh $IP mkdir -p .ssh;
  cat ~/.ssh/id_rsa.pub | ssh $IP 'cat >> .ssh/authorized_keys';
done
```

### Check host resources on the data nodes 
heck resources on the data nodes with private IPs 10.1.13.150, 10.1.13.151, 10.1.13.152
```{.sh .copy}
for IP in 10.1.13.150 10.1.13.151 10.1.13.152; do echo $IP; ssh $IP 'echo -n "CPUs: ";cat /proc/cpuinfo | grep processor | wc -l; echo -n "Mem: ";free -h | grep Mem | tr -s " " | cut -d" " -f 2; echo -n "Disk: "; df -h / | grep -v Filesystem'; done
```
```sh
10.1.12.103
CPUs: 72
Mem: 251G
Disk: /dev/sda2       160G   13G  148G   8% /
10.1.12.104
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G   22G  187G  11% /
10.1.12.105
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G  5.1G  203G   3% /
```

### Create mount paths on the data nodes

Create mount paths on the data nodes with private IPs 10.1.13.150, 10.1.13.151, 10.1.13.152.
```{.sh .copy}
for IP in 10.1.12.103 10.1.12.104 10.1.12.105; do ssh $IP mkdir -p /mnt/data0; done
```

### SELinux turned on for data nodes

Add firewall exceptions on the data nodes with private IPs 10.1.13.150, 10.1.13.151, 10.1.13.152.
```{.sh .copy}
for IP in 10.1.12.103 10.1.12.104 10.1.12.105
do
  ssh $IP firewall-cmd --zone=public --add-port=7000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=7100/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9100/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=11000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=12000/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9300/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=9042/tcp;
  ssh $IP firewall-cmd --zone=public --add-port=6379/tcp;
done
```