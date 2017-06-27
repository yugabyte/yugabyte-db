---
date: 2016-03-09T00:11:02+01:00
title: Get started (with Admin Console, manage multi-node clusters)
weight: 20
---

While the single-node approach is great for developing apps locally against the YugaByte API endpoints, deploying along with YugaWare, the YugaByte Admin Console, adds the ability to test of all operational management scenarios (incl. scale up/down). However, this deployment does not include high availabilty of YugaWare itself and hence is not recommended for production environments. For production environments, use the [Get started (with HA Admin Console)](/get-started-adminconsole-ha) mode.

## Prerequisites

### Docker

#### Mac OS or Windows Desktop

- Install [Docker for Mac](https://docs.docker.com/docker-for-mac/install/) or [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows). Please check carefully that all prerequisites are met.

- Confirm that the Docker daemon is running in the background. If you don't see the daemon running, start the Docker application. Also ensure that Docker Compose is installed correctly.

```sh
$ docker version
Client:
 Version:      17.03.1-ce
 API version:  1.27
 Go version:   go1.7.5
 Git commit:   c6d412e
 Built:        Tue Mar 28 00:40:02 2017
 OS/Arch:      darwin/amd64

Server:
 Version:      17.03.1-ce
 API version:  1.27 (minimum version 1.12)
 Go version:   go1.7.5
 Git commit:   c6d412e
 Built:        Fri Mar 24 00:00:50 2017
 OS/Arch:      linux/amd64
 Experimental: true

$ docker-compose version
docker-compose version 1.11.2, build dfed245
docker-py version: 2.1.0
CPython version: 2.7.12
OpenSSL version: OpenSSL 1.0.2j  26 Sep 2016
```

#### Linux

{{< note title="Note" >}}
Docker for Linux requires sudo privileges. 
{{< /note >}}

- Install [Docker for Linux](https://docs.docker.com/engine/installation/linux/ubuntulinux/). Please check carefully that all prerequisites are met.

- Confirm that the Docker daemon is running in the background. If you don't see the daemon running, start the Docker daemon.

```sh
$ sudo su 
$ docker version
```

- Install [Docker Compose for Linux](https://docs.docker.com/compose/install/). Please check carefully that all prerequisites are met.

```sh
$ curl -L https://github.com/docker/compose/releases/download/1.13.0/docker-compose-`uname -s`-`uname -m` > /usr/local/bin/docker-compose
$ chmod +x /usr/local/bin/docker-compose
```

- Confirm that the Docker Compose is installed correctly.

```sh
$ docker-compose version
```


## Install

- Clone the repo housing the YugaByte docker compose file

```sh
$ git clone https://yugabyte@bitbucket.org/snippets/yugabyte/M9djd/yugabyte.git
$ cd yugabyte
```

- YugaByte's container images are stored at Quay.io, a leading container registry. If you are starting from http://try.yugabyte.com then your Quay.io credentials are listed on the same page. For all other usage scenarios, create your free Quay.io account at [Quay.io](https://quay.io/signin/) and then email to [YugaByte Support](mailto:support@yugabyte.com) noting your Quay.io username. This is to ensure that the YugaByte DB docker image can be privately shared with you.

- Login to Quay.io from your command line. Detailed instructions [here](https://docs.quay.io/solution/getting-started.html). 

```sh
$ docker login quay.io -u <your-quay-id> -p <your-quay-password>
```

- Start the YugaByte admin console via docker-componse

```sh
$ docker-compose up -d

```

Open http://localhost:8080 in your browser and login to the admin console with the default username and password (admin/admin). Follow the instructions in the [Administer](/admin) section on how to use YugaWare to create and manage YugaByte clusters.


## Maintain

- Review logs of the YugaWare container

```sh
$ docker logs yugaware
```

- Open a bash shell inside the YugaWare container.

```sh
$ docker exec -it yugaware bash
```

- Stop and remove all the container instances

```sh
$ docker rm -f $(docker ps -aq)
```

- Upgrade all the containers used in the YugaWare app

```sh
$ docker-compose pull 
```