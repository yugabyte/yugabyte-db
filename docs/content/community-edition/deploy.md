---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Deploy 
weight: 30
---
YugaByte Community Edition can be manually deployed on any cloud provider of choice including major public cloud platforms and private on-premises datacenters.

## Prerequisites

Dedicated hosts or cloud VMs running Centos 7+ with local or remote attached storage. All these hosts should be accessible over SSH from the YugaWare host. If your instance does not have public Internet access, make sure the following packages have been installed (all can be retrieved from the yum repo **epel**, make sure to use the latest epel release repo):

- epel-release
- libstdc++
- collectd
- ntp
- cyrus-sasl-plain
- cyrus-sasl-devel
- python-pip
- python-devel
- python-psutil
- libsemanage-python
- policycoreutils-python

Here's the command to install these packages.

```sh
# install pre-requisite packages
sudo yum install -y epel-release libstdc++ collectd ntp cyrus-sasl-plain cyrus-sasl-devel python-pip python-devel python-psutil libsemanage-python policycoreutils-python
```
