---
title: Shut down YugabyteDB Anywhere
headerTitle: Shut down YugabyteDB Anywhere
linkTitle: Shut down
description: Shut YugabyteDB Anywhere down gracefully to perform maintenance.
menu:
  v2.18_yugabyte-platform:
    identifier: shutdown
    parent: administer-yugabyte-platform
    weight: 60
type: docs
---

If you need to perform maintenance on the server hosting your YugabyteDB Anywhere installation requiring a shutdown or restart, you should shut YugabyteDB Anywhere down gracefully.

## YBA Installer

If you installed YugabyteDB Anywhere using [YBA Installer](../../install-yugabyte-platform/install-software/installer/#service-management), shut down using the following command:

```sh
$ sudo yba-ctl stop
```

To restart the service, use the following command:

```sh
$ sudo yba-ctl start
```

The service should automatically start up after a reboot.

## Replicated

If you installed YugabyteDB Anywhere using Replicated, stop the application via the Replicated console.

You can also use the following command:

```sh
replicatedctl app stop
```

## Kubernetes

For Kubernetes, node patching doesn't require YBA coordination.
