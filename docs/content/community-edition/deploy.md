---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Deploy 
weight: 30
---
Multi-node clusters of YugaByte Community Edition can be manually deployed on any cloud provider of choice including major public cloud platforms and private on-premises datacenters.

## Prerequisites

Dedicated hosts or cloud VMs running Centos 7+ with local or remote attached storage are required to run the YugaByte DB. If your instance does not have public Internet access, make sure the following packages have been installed (all can be retrieved from the yum repo **epel**, make sure to use the latest epel release repo):

- epel-release
- ntp
- cyrus-sasl-plain
- cyrus-sasl-devel

Here's the command to install these packages.

```sh
# install prerequisite packages
$ sudo yum install -y epel-release ntp cyrus-sasl-plain cyrus-sasl-devel
```

## Install

Install YugaByte DB on each instance.

```sh
$ mkdir ~/yugabyte
$ tar xvfz yugabyte.<version>-centos.tar.gz -C yugabyte
$ cd yugabyte
```

Run the **configure** script to ensure all dependencies get auto-installed. If not already installed, this script will also install a two libraries (`cyrus-sasl` and `cyrus-sasl-plain`) and will request for a sudo password in case you are not running the script as root.

```sh
$ ./bin/configure
```

## Start YB-Masters

## Start YB-TServers




