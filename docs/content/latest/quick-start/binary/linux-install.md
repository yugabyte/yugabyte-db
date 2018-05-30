## Prerequisites

a) One of the following operating systems

<i class="icon-centos"></i> CentOS 7 

<i class="icon-ubuntu"></i> Ubuntu 16.04+

b) Verify thatyou have python2 installed. Support for python3 is in the works.

```{.sh .copy .separator-dollar}
$ python --version
```
```sh
Python 2.7.10
```

## Download

Download the YugaByte DB CE package as shown below.


```{.sh .copy .separator-dollar}
$ wget https://downloads.yugabyte.com/yugabyte-ce-1.0.2.0-linux.tar.gz
```
```{.sh .copy .separator-dollar}
$ tar xvfz yugabyte-ce-1.0.2.0-linux.tar.gz && cd yugabyte-1.0.2.0/
```

## Configure

```{.sh .copy .separator-dollar}
$ ./bin/post_install.sh
```
