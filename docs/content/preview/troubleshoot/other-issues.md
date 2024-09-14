---
title: Troubleshoot miscellaneous issues
headerTitle: Other issues
linkTitle: Other issues
description: Troubleshoot miscellaneous other issues in YugabyteDB
menu:
  preview:
    identifier: troubleshoot-miscellaneous
    parent: troubleshoot
    weight: 400
type: docs
rightnav:
    hideh3: true
---

## Leader preference

### Tablets not being placed according to leader preference or placement policy

Leader preference is enforced by the [load balancer](../../architecture/yb-master#load-balancing) component of the [YB-Master](../../architecture/yb-master) service. This load balancer has a 2 minute delay in triggering after the cluster is started. For the tables created within this 2 minute window, it will take a while for the tablets to be placed according to the placement policy.

## Unable to start a local installation of YugabyteDB on MacOS

When starting a local cluster on MacOS, you might see an error like the following:

```bash{.nocopy}
ERROR: Master node present at 127.0.0.1:7000 is not reachable.
```

MacOS Monterey enables AirPlay receiving by default, which listens on port 7000. This conflicts with YugabyteDB.

If you are using yugabyted, use the [--master_webserver_port flag](#advanced-flags) when you start the cluster to change the default port number, as follows:

```sh
./bin/yugabyted start --master_webserver_port=9999
```

Alternatively, you can disable AirPlay receiving, then start YugabyteDB normally, and then, optionally, re-enable AirPlay receiving.

{{<tip>}}
If you do not need AirPlay, it is advisable to permanently disable it.
{{</tip>}}
