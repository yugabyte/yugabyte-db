**NOTE:**
The Docker option to run local clusters is recommended only for advanced Docker users. This is because running stateful apps like YugaByte DB in Docker is more complex and error-prone than the more common stateless app use cases.

## Prerequisites

a) You must have the Docker runtime installed on your localhost. Follow the links below to download and install Docker if you have not done so already.

<i class="fab fa-apple" aria-hidden="true"></i> [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac) 

<i class="fab fa-centos"></i> [Docker for CentOS](https://store.docker.com/editions/community/docker-ce-server-centos) 

<i class="fab fa-ubuntu"></i> [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu) 

<i class="icon-debian"></i> [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian) 

<i class="fab fa-windows" aria-hidden="true"></i> [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows) 

b) Verify that you have python2 installed. Support for python3 is in the works.
<div class='copy separator-dollar'>
```sh
$ python --version
```
</div>
```sh
Python 2.7.10
```

## Download

Download the [yb-docker-ctl](../../admin/yb-docker-ctl/) utility. This utility has a set of pre-built commands to create and thereafter administer a containerized local cluster. 
<div class='copy separator-dollar'>
```sh
$ mkdir ~/yugabyte && cd ~/yugabyte
```
</div>
<div class='copy separator-dollar'>
```sh
$ wget https://downloads.yugabyte.com/yb-docker-ctl && chmod +x yb-docker-ctl
```
</div>

## Install

Confirm that Docker and python are installed correctly.
<div class='copy separator-dollar'>
```sh
$ docker ps
```
</div>
<div class='copy separator-dollar'>
```sh
$ python --version
```
</div>

Pull the YugaByte DB container.
<div class='copy separator-dollar'>
```sh
$ docker pull yugabytedb/yugabyte
```
</div>
