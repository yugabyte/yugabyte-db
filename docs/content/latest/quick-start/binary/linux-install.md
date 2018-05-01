## Prerequisites

One of the following operating systems

<i class="icon-centos"></i> CentOS 7 

<i class="icon-ubuntu"></i> Ubuntu 16.04+

Verify you have python2 installed

```{.sh .copy .separator-dollar}
$ python --version
```

## Download

Download the YugaByte CE package as shown below.


```{.sh .copy .separator-dollar}
$ wget https://downloads.yugabyte.com/yugabyte-ce-1.0.0.0-linux.tar.gz
```
```{.sh .copy .separator-dollar}
$ tar xvfz yugabyte-ce-1.0.0.0-linux.tar.gz && cd yugabyte-1.0.0.0/
```

## Configure

```{.sh .copy .separator-dollar}
$ ./bin/post_install.sh
```
