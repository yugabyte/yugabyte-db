---
title: Install Software
linkTitle: 2. Install Software
description: Install Software
aliases:
  - /deploy/manual-deployment/install-software
menu:
  v1.1:
    identifier: deploy-manual-deployment-install-software
    parent: deploy-manual-deployment
    weight: 612
isTocNested: true
showAsideToc: true
---

Install the software on each of the nodes using the steps shown below.

## Download

Download the YugaByte CE binary package as described in the [Quick Start section](../../../quick-start/install/).

Copy the YugaByte DB package into each instace and then running the following commands.

```sh
$ tar xvfz yugabyte-ce-<version>-<os>.tar.gz && cd yugabyte-<version>/
```

## Configure

- Run the **post_install.sh** script to make some final updates to the installed software.

```sh
$ ./bin/post_install.sh
```
