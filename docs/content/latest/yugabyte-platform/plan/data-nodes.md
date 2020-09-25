---
title: Prepare YugabyteDB data nodes
headerTitle: Prepare YugabyteDB data nodes
linkTitle: Prepare YugabyteDB data nodes
description: Prepare your YugabyteDB data nodes.
menu:
  latest:
    parent: plan-yugabyte-platform
    identifier: reqs-data-node
    weight: 615
type: page
isTocNested: true
showAsideToc: true
---

???

From Install a Yugabyte Platform VM:

Epel and ntp packages are required. See below for details on installing

Verify the system resource limits here: https://docs.yugabyte.com/latest/deploy/manual-deployment/system-config/

Provision your Linux VM with following requirements:

Cores: 4 Cores
RAM: 8 GB
Storage Disk: 100 GB minimum
Supported Operating systems: CentOS, Ubuntu, RedHat 7

Verify SSE2 instruction set support:

SSE2 instruction set was introduced into Intel chips with the Pentium 4 in 2001 and AMD processors in 2003. Most systems produced in the last several years are equipped with SSE2. YugabyteDB requires this instruction set. 

To verify that your system supports SSE2, run the following command:

```sh
$ cat /proc/cpuinfo | grep sse2
```