---
title: System statistics
linkTitle: System statistics
headerTitle: Check system statistics
description: How to check system statistics on your YugabyteDB cluster
menu:
  preview:
    parent: troubleshoot-nodes
    weight: 30
type: docs
---

There are some system statistics that might help with troubleshooting.

## Host resource usage

To check the CPU, memory, and disk usage on a Linux machine, you can run the following command:

```sh
sudo echo -n "CPUs: ";cat /proc/cpuinfo | grep processor | wc -l; echo -n "Mem: ";free -h | grep Mem | tr -s " " | cut -d" " -f 2; echo -n "Disk: "; df -h / | grep -v Filesystem;
```

```output
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

Generally, common tools such as `top` or `iostat` may be useful.

### Auditd

If `top` reports high CPU usage for the `auditd` process, it may have rules auditing some system calls frequently used by YugabyteDB which can significantly affect performance. You can try temporarily disabling `audit` by running the following command on each YugabyteDB node:

```sh
auditctl -e 0
```

Then you would check if this improves performance.

To re-enable `audit` afterwards, run the following command:

```sh
auditctl -e 1
```

## YugabyteDB processes state

YugabyteDB provides the following web endpoints where the current state of each process is aggregated. This includes logs, flags, as well as memory, disk, and network usage metrics. Additionally, it provides dedicated metrics endpoints for YCQL and YSQL requests:

| Description | URL |
|-------------|-----|
| Master Web Page | `<node-ip>:7000` |
| TServer Web Page | `<node-ip>:9000` |
| YCQL Metrics | `<node-ip>:12000/metrics` |
| YSQL Metrics | `<node-ip>:13000/metrics` |

When running yb-ctl locally with default values, three local IP addresses are created: `127.0.0.1`, `127.0.0.2`, and `127.0.0.3`, one for each YugabyteDB node.
