**NOTE:**
The Docker option to run local clusters is recommended only for advanced Docker users. This is because running stateful apps like YugabyteDB in Docker is more complex and error-prone than the more common stateless app use cases.

## Prerequisites

a) You must have the Docker runtime installed on your localhost. Follow the links below to download and install Docker if you have not done so already.

<i class="fab fa-apple" aria-hidden="true"></i> [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac)

<i class="fab fa-centos"></i> [Docker for CentOS](https://store.docker.com/editions/community/docker-ce-server-centos)

<i class="fab fa-ubuntu"></i> [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu)

<i class="icon-debian"></i> [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian)

<i class="fab fa-windows" aria-hidden="true"></i> [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows)

b) Verify that you have python2 or python3 installed.

## Install

Verify that Docker and Python are installed correctly.

```sh
$ docker ps
```

```sh
$ python --version
```

Pull the YugabyteDB Docler container.

```sh
$ docker pull yugabytedb/yugabyte
```
