---
title: System Configuration
linkTitle: 1. System Configuration
description: System Configuration
menu:
  v1.0:
    identifier: deploy-manual-deployment-system-config
    parent: deploy-manual-deployment
    weight: 611
---

Do the following configuration steps on each of the nodes in the cluster.

## ntp

 If your instance does not have public Internet access, make sure the following packages have been installed (all can be retrieved from the yum repo **epel**, make sure to use the latest epel release repo):

- epel-release
- ntp

Here's the command to install these packages.

```{.sh .copy .separator-dollar}
$ sudo yum install -y epel-release ntp
```

## Setting ulimits

In Linux, `ulimit` is used to limit and control the usage of system resources (threads, files, and network connections) on a per-process or per-user basis.

### Checking ulimits

Run the following command to check the ulimit settings.

```{.sh .copy .separator-dollar}
$ ulimit -a
```

The following settings are recommended when running YugaByte DB.

```
core file size          (blocks, -c) unlimited
data seg size           (kbytes, -d) unlimited
scheduling priority             (-e) 0
file size               (blocks, -f) unlimited
pending signals                 (-i) 119934
max locked memory       (kbytes, -l) 64
max memory size         (kbytes, -m) unlimited
open files                      (-n) 1048576
pipe size            (512 bytes, -p) 8
POSIX message queues     (bytes, -q) 819200
real-time priority              (-r) 0
stack size              (kbytes, -s) 8192
cpu time               (seconds, -t) unlimited
max user processes              (-u) 12000
virtual memory          (kbytes, -v) unlimited
file locks                      (-x) unlimited
```

### Setting system-wide ulimits

You can change values by substituting the -n option for any possible value in the output of ulimit -a. Issue a command in the following form to change a `ulimit` setting.

```
ulimit -n <value>
```

```
-f (file size): unlimited
-t (cpu time): unlimited
-v (virtual memory): unlimited [1]
-l (locked-in-memory size): unlimited
-n (open files): 64000
-m (memory size): unlimited [1] [2]
-u (processes/threads): 64000
```

{{< note title="Note" >}}
- After changing a ulimit setting, the YB-Master and YB-TServer processes must be restarted in order for the new settings to take effect. Check the `/proc/<process pid>` file to see the current settings.
- Changes made using ulimit may revert following a system restart depending on the system configuration.
{{< /note >}}



