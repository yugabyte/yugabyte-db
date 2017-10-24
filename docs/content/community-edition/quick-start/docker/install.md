## Prerequisites

You must have the Docker runtime installed on your localhost. Follow the links below to download and install Docker if you have not done so already.

- [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac)
- [Docker for Centos](https://store.docker.com/editions/community/docker-ce-server-centos)
- [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu)
- [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian)
- [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows)

## Download

Download the [yb-docker-ctl](/admin/yb-docker-ctl/) utility [here](http://www.yugabyte.com#download). This utility has a set of pre-built commands to create and thereafter administer a containerized local cluster. 

## Install

Confirm that Docker is installed correctly.

```sh
$ docker ps
```

Pull the YugaByte DB container.

```sh
$ docker pull yugabytedb/yugabyte
```

Execute the following commands.

```sh
$ mkdir ~/yugabyte
$ mv yb-docker-ctl ~/yugabyte
$ cd yugabyte
```
