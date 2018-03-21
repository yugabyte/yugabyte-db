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
