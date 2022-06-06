---
title: Install YB Voyager
linkTitle: Install YB Voyager
description: Steps to install YB Voyager.
menu:
  preview:
    identifier: install-yb-voyager
    parent: prerequisites-1
    weight: 402
isTocNested: true
showAsideToc: true
---

Set up a machine which satisfies the [Prerequisites](../../reference/connectors/yb-migration-reference/#migrator-machine-requirements) using the following steps:

- Clone the yb-voyager repository.

```sh
git clone https://github.com/yugabyte/yb-voyager.git
```

- Change the directory to `yb-voyager/installer_scripts`.

```sh
cd yb-voyager/installer_scripts
```

- Depending on the Linux distribution (CentOS or Ubuntu) you're running, execute the appropriate installer script:

```sh
#CentOS
./yb_migrate_installer__centos.sh
```

```sh
#Ubuntu
./yb_migrate_installer__ubuntu.sh
```

It is safe to execute the script multiple times. On each run, the script regenerates the `yb-voyager` executable based on the latest commit in the git repository. If the script fails for some reason, check the `yb_migrate_installer.log` in the current working directory.

- The script generates a `.yb_migrate_installer_bashrc` file in the home directory. Source the file to ensure that the correct environment variables are set using the following command:

```sh
source ~/.yb_migrate_installer_bashrc
```

- Check that yb-voyager is installed using the following command:

```sh
yb-voyager --help
```

## Learn more

- For more information on SSL connectivity, sharding strategies, `yb-voyager` CLI reference and more, refer to [YB Voyager CLI].
