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

The following sections describe the prerequisites for installing YugabyteDB Voyager.

## Operating system

You can install YugabyteDB Voyager on the following:

- CentOS 7
- Ubuntu 18.04, 20.04
- MacOS (currently supported only for PostgreSQL source database)

## Hardware requirements

- Disk space must be at least 1.5 times the estimated size of the source database.
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

For more information, refer to [Export directory](../../yb-voyager/reference/#export-directory).

## Install yb-voyager

YugabyteDB Voyager consists of the yb-voyager command line executable. yb-voyager keeps all of its migration state, including exported schema and data, in a local directory called the *export directory*. For more information, refer to [Export directory](../../yb-voyager/reference/#export-directory).

To install yb-voyager on a machine which satisfies the [Prerequisites](../../yb-voyager/prerequisites/), do the following:

- Clone the yb-voyager repository.

```sh
git clone https://github.com/yugabyte/yb-voyager.git
```

- Change the directory to `yb-voyager/installer_scripts`.

```sh
cd yb-voyager/installer_scripts
```

- Install yb-voyager using the following script:

```sh
install-yb-voyager.sh
```

It is safe to execute the script multiple times. If the script fails, check the `/tmp/install-yb-voyager.log` file.

- The script generates a `yb-voyager.rc` file in the home directory. Source the file to ensure that the environment variables are set using the following command:

```sh
source ~/.yb-voyager.rc
```

- Check that yb-voyager is installed using the following command:

```sh
yb-voyager --help
```

## Next steps

- [Prepare the source database](../../yb-voyager/prepare-databases/#prepare-the-source-database).
- [Prepare the target database](../../yb-voyager/prepare-databases/#prepare-the-target-database).
