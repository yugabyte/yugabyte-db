## Prerequisites

a) One of the following operating systems

<i class="icon-centos"></i> CentOS 7 

<i class="icon-ubuntu"></i> Ubuntu 16.04+

b) Verify thatyou have python2 installed. Support for python3 is in the works.
<div class='copy separator-dollar'>
```sh
$ python --version
```
</div>
```sh
Python 2.7.10
```

## Download

Download the YugaByte DB CE package as shown below.
<div class='copy separator-dollar'>
```sh
$ wget https://downloads.yugabyte.com/yugabyte-ce-1.0.7.0-linux.tar.gz
```
</div>
<div class='copy separator-dollar'>
```sh
$ tar xvfz yugabyte-ce-1.0.7.0-linux.tar.gz && cd yugabyte-1.0.7.0/
```
</div>

## Configure

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ ./bin/post_install.sh
```
</div>
