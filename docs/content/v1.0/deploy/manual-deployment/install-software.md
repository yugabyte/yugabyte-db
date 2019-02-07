---
title: Install Software
linkTitle: 2. Install Software
description: Install Software
menu:
  v1.0:
    identifier: deploy-manual-deployment-install-software
    parent: deploy-manual-deployment
    weight: 612
---

Install the software on each of the nodes using the steps shown below.

## Download

Download the YugaByte CE binary package as described in the [Quick Start section](../../../quick-start/install/).

Copy the YugaByte DB package into each instace and then running the following commands.
<div class='copy separator-dollar'>
```sh
$ tar xvfz yugabyte-ce-<version>-<os>.tar.gz && cd yugabyte-<version>/
```
</div>

## Configure

- Run the **post_install.sh** script to make some final updates to the installed software.

You can do this as shown below.
<div class='copy separator-dollar'>
```sh
$ ./bin/post_install.sh
```
</div>
