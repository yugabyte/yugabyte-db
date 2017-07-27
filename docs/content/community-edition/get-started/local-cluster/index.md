---
date: 2016-03-09T00:11:02+01:00
title: Community Edition - Start a local cluster
weight: 15
---

 The **local cluster** option includes the ability to manage YugaByte clusters as well as monitor/alert on cluster performance. YugaWare, YugaByte's admin console, simplifies these tasks greatly and is available as a local docker-compose application for YugaByte Community Edition users. 

{{< note title="Note" >}}
The local cluster approach does not include high availability of YugaWare itself and hence is not recommended for mission-critical environments such as production. For such environments, follow the [Deploy](/enterprise-edition/deploy/) section of YugaByte Enterprise Edition.
{{< /note >}}


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

- YugaByte's container images are stored at Quay.io, a leading container registry. Create your free Quay.io account at [Quay.io](https://quay.io/signin/) and then email to [YugaByte Support](mailto:support@yugabyte.com) noting your Quay.io username. This is to ensure that the YugaByte DB docker image can be privately shared with you.

- Login to Quay.io from your command line. Detailed instructions [here](https://docs.quay.io/solution/getting-started.html). 

```sh
$ docker login quay.io -u <your-quay-id> -p <your-quay-password>
```

- Start the YugaByte admin console via docker-compose. This command will first pull in all the container images from Quay.io in case the images are not yet locally available. Depending on your network bandwidth, this pull process can take a while. 

```sh
$ docker-compose up -d
```

## Test

Open http://localhost:8080 in your browser and login to the admin console with the default username **admin** and default password **admin**. 

### Create universe

Universe is a cluster of YugaByte instances grouped together to perform as one logical distributed database. All instances belonging to a single Universe are identical to each from a client standpoint and run on the same type of cloud provider node. 

If there are no universes created yet, the Dashboard page will look like the following with **Docker** as a pre-configured [Cloud Provider](/admin/#configure-cloud-providers). 

![Dashboard with No Universes](/images/ready-for-local-test.png)

The above pre-configuration seeds two regions (with 3 availability zones each) on the same localhost that runs the docker-compose application, thus making it extremely easy to simulate multi-datancenter deployments. If you would like to run YugaByte on any other cloud provider such as Amazon Web Services, you can simply configure that provider on the respective tab.

Click on "Create Universe" to enter your intent for the universe. As soon as **Provider**, **Regions** and **Nodes** are entered, an intelligent Node Placement Policy kicks in to specify how the nodes should be placed across all the Availability Zones so that maximum availability is guaranteed. 

Here's how to create a universe on the Docker cloud provider.
![Create Universe on Docker](/images/create-univ-docker.png)

Here's how a Universe in Pending state looks like.

![Detail for a Pending Universe](/images/pending-univ-detail.png)

![Tasks for a Pending Universe](/images/pending-univ-tasks.png)

![Nodes for a Pending Universe](/images/pending-univ-nodes.png)

### Connect with cqlsh or redis-cli

\<docs coming soon\>

### Run a sample app

\<docs coming soon\>

### Expand or shrink universe

\<docs coming soon\>


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