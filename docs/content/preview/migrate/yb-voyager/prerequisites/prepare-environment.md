---
title: Prepare the environment
linkTitle: Prepare the environment
description: Prerequisites required for installing YB Voyager.
menu:
  preview:
    identifier: prepare-environment
    parent: prerequisites-1
    weight: 401
isTocNested: true
showAsideToc: true
---

The following sections include the requirements to install YB Voyager.

## Supported OS versions

You can install YB Voyager on the following Linux distributions:

- CentOS 7
- Ubuntu 18.04, 20.04

## Hardware requirements

- The recommended disk space is a minimum of 1.5 times the estimated size of the source database.
- 2 cores minimum (recommended)

## Prepare the host

The node where you'll run the yb-voyager command should:

- connect to both the source and the target database.
- have sudo access.

## Create an export directory

Create an export directory in the local file system on the machine where YB Voyager is installed. yb-voyager uses the directory to store source data, schema files, and the migration state. The file system in which the directory resides must have enough free space to hold the entire source database. Create the directory and place its path in an environment variable.

```sh
mkdir -p ~/export-dirs/sakila
export EXPORT_DIR=~/export-dirs/sakila
```

## Next steps

- [Install YB Voyager]()
