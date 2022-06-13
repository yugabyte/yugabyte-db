---
title: Prerequisites
headerTitle: Prerequisites
linkTitle: Prerequisites
description: Explore the prerequisites, YugabyteDB Voyager installation, and so on.
beta: /preview/faq/general/#what-is-the-definition-of-the-beta-feature-tag
image: /images/section_icons/develop/learn.png
menu:
  preview:
    identifier: prerequisites-1
    parent: yb-voyager
    weight: 102
isTocNested: true
showAsideToc: true
---

The following sections include requirements to install YugabyteDB Voyager.

## Supported OS versions

You can install YugabyteDB Voyager on the following OS:

- CentOS 7
- Ubuntu 18.04, 20.04
- MacOS (currently supported only for PostgreSQL source database)

## Hardware requirements

- The recommended disk space must be at least 1.5 times the estimated size of the source database.
- 2 cores minimum (recommended)

## Prepare the host

The node where you'll run the yb-voyager command should:

- connect to both the source and the target database.
- have sudo access.

## Create an export directory

yb-voyager uses the directory to store source data, schema files, and the migration state. The file system in which the directory resides must have enough free space to hold the entire source database. Create an export directory in the local file system on the machine where YugabyteDB Voyager will be installed, and place its path in an environment variable.

```sh
mkdir -p ~/export-dirs/sakila
export EXPORT_DIR=~/export-dirs/sakila
```

Refer to [Export directory](../../yb-voyager/reference/#export-directory), to learn more.

## Next steps

- [Install yb-voyager](../../yb-voyager/install-yb-voyager/).
- [Prepare the source database](../../yb-voyager/install-yb-voyager/#prepare-the-source-database).
- [Prepare the target database](../../yb-voyager/install-yb-voyager/#prepare-the-target-database).
