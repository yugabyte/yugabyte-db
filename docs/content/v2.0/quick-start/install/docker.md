---
title: 1. Install YugabyteDB
linkTitle: 1. Install YugabyteDB
description: Install YugabyteDB
block_indexing: true
menu:
  v2.0:
    parent: quick-start
    name: 1. Install YugabyteDB
    identifier: install-3-docker
    weight: 110
type: page
isTocNested: true
showAsideToc: true
---


<ul class="nav nav-tabs-alt nav-tabs-yb">

  <li >
    <a href="/latest/quick-start/install/macos" class="nav-link">
      <i class="fab fa-apple" aria-hidden="true"></i>
      macOS
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/install/linux" class="nav-link">
      <i class="fab fa-linux" aria-hidden="true"></i>
      Linux
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/install/docker" class="nav-link active">
      <i class="fab fa-docker" aria-hidden="true"></i>
      Docker
    </a>
  </li>

  <li >
    <a href="/latest/quick-start/install/kubernetes" class="nav-link">
      <i class="fas fa-cubes" aria-hidden="true"></i>
      Kubernetes
    </a>
  </li>

</ul>

{{< note title="Note" >}}

The Docker option to run local clusters is recommended only for advanced Docker users. This is because running stateful apps like YugabyteDB in Docker is more complex and error-prone than the more common stateless app use cases.

{{< /note >}}


## Prerequisites

a) You must have the Docker runtime installed on your localhost. Follow the links below to download and install Docker if you have not done so already.

<i class="fab fa-apple" aria-hidden="true"></i> [Docker for Mac](https://store.docker.com/editions/community/docker-ce-desktop-mac)

<i class="fab fa-centos"></i> [Docker for CentOS](https://store.docker.com/editions/community/docker-ce-server-centos)

<i class="fab fa-ubuntu"></i> [Docker for Ubuntu](https://store.docker.com/editions/community/docker-ce-server-ubuntu)

<i class="icon-debian"></i> [Docker for Debian](https://store.docker.com/editions/community/docker-ce-server-debian)

<i class="fab fa-windows" aria-hidden="true"></i> [Docker for Windows](https://store.docker.com/editions/community/docker-ce-desktop-windows)

b) Verify that you have python2 installed.Support for Python 3 is in the works, status can be tracked on [GitHub](https://github.com/yugabyte/yugabyte-db/issues/3025).

```sh
$ python --version
```

```
Python 2.7.10
```

## Download yb-docker-ctl

Download the [yb-docker-ctl](../../../admin/yb-docker-ctl/) utility. This utility has a set of pre-built commands to create and thereafter administer a containerized local cluster.

```sh
$ mkdir ~/yugabyte && cd ~/yugabyte
```

```sh
$ wget https://raw.githubusercontent.com/yugabyte/yugabyte-db/master/bin/yb-docker-ctl && chmod +x yb-docker-ctl
```

## Install

Confirm that Docker and python are installed correctly.

```sh
$ docker ps
```

```sh
$ python --version
```

Pull the YugabyteDB container.

```sh
$ docker pull yugabytedb/yugabyte
```
