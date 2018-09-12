---
title: System Stats
linkTitle: System Stats
description: Check System Stats
aliases:
  - /troubleshoot/nodes/check-stats/
menu:
  1.0:
    parent: troubleshoot-nodes
    weight: 846
---

## Host resource usage

To check the CPU, Memory and Disk usage on a Linux machine you can run:

```{.sh .copy .separator-dollar}
$ sudo echo -n "CPUs: ";cat /proc/cpuinfo | grep processor | wc -l; echo -n "Mem: ";free -h | grep Mem | tr -s " " | cut -d" " -f 2; echo -n "Disk: "; df -h / | grep -v Filesystem; 
```
```
CPUs: 72
Mem: 251G
Disk: /dev/sda2       160G   13G  148G   8% /
10.1.12.104
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G   22G  187G  11% /
10.1.12.105
CPUs: 88
Mem: 251G
Disk: /dev/sda2       208G  5.1G  203G   3% /
```
Generally, common tools like `top` or `iostat` may be useful.

### Auditd
If `top` reports high CPU usage for the `auditd` process, it may have some rules auditing some system calls frequently used YugaByte which can significantly affect performance. 
You can try temporarily disabling `audit` by running (on each YugaByte node):
```{.sh .copy .separator-dollar}
$ auditctl -e 0
```
and check if this improves peformance.

To re-enable it afterwards run:
```{.sh .copy .separator-dollar}
$ auditctl -e 1
```

## YugaByte processes state

YugaByte DB provides web endpoints where the current state of each process is aggregated. 
This includes logs, gflags as well as memory, disk, and network usage metrics.
Additionally, it provides dedicated metrics endpoints for CQL and, respectively, Redis requests.

| Description | URL |
|-------------|-----|
| Master Web Page | `<node-ip>:7000` |
| TServer Web Page | `<node-ip>:9000` |
| Redis Metrics | `<node-ip>:11000/metrics` |
| CQL Metrics | `<node-ip>:12000/metrics` |

_Note that, when running `yb-ctl` locally with default settings, it will create three local ips `127.0.0.1`, `127.0.0.2`, and `127.0.0.3`, one for each YugaByte DB node._
