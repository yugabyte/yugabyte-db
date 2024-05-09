---
title: System configuration
headerTitle: System configuration
linkTitle: 1. System configuration
description: How to configure system parameters to get the YugabyteDB database cluster to run correctly.
menu:
  preview:
    identifier: deploy-manual-deployment-system-config
    parent: deploy-manual-deployment
    weight: 611
type: docs
---

Perform the following configuration on each node in the cluster:

- ntp or chrony
- ulimits
- transparent hugepages

Keep in mind that, although YugabyteDB is PostgreSQL compatible and runs a postgres process, it is not a PostgreSQL distribution. The PostgreSQL it runs doesn't need the same OS and system resources that open source PostgreSQL requires. For this reason, the kernel configuration requirements are different.

In particular, the main YugabyteDB process, the YB-TServer, is multi-threaded. As a result, you don't need to modify settings for shared memory and inter-process communication (IPC), because there is no inter-process communication or shared memory in a multi-threaded process model (all memory is shared by the same process).

## ntp

If your instance does not have public Internet access, make sure the ntp package is installed:

```sh
$ sudo yum install -y ntp
```

As of CentOS 8, `ntp` is no longer available and has been replaced by `chrony`. To install, run:

```sh
$ sudo yum install -y chrony
```

## ulimits

In Linux, `ulimit` is used to limit and control the usage of system resources (threads, files, and network connections) on a per-process or per-user basis.

### Check ulimits

Run the following command to check the `ulimit` settings.

```sh
$ ulimit -a
```

The following settings are recommended when running YugabyteDB.

```output
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

### Set system-wide ulimits

You can change values by substituting the `-n` option for any possible value in the output of `ulimit -a`. Issue a command in the following form to change a `ulimit` setting.

```sh
$ ulimit -n <value>
```

```sh
-f (file size): unlimited
-t (cpu time): unlimited
-v (virtual memory): unlimited [1]
-l (locked-in-memory size): unlimited
-n (open files): 64000
-m (memory size): unlimited [1] [2]
-u (processes/threads): 64000
```

{{< note title="Restart servers" >}}

After changing a ulimit setting, the YB-Master and YB-TServer servers must be restarted in order for the new settings to take effect. Check the [yb-tserver.INFO](../start-tservers/#verify-health) file to verify that the ulimits are applied. You can also check the `/proc/<process pid>` file to see the current settings.

{{< /note >}}

Changes made using ulimit may revert following a system restart depending on the system configuration. These settings should be applied permanently by adding the following in `/etc/security/limits.conf`:

```conf
*                -       core            unlimited
*                -       data            unlimited
*                -       fsize           unlimited
*                -       sigpending      119934
*                -       memlock         64
*                -       rss             unlimited
*                -       nofile          1048576
*                -       msgqueue        819200
*                -       stack           8192
*                -       cpu             unlimited
*                -       nproc           12000
*                -       locks           unlimited
```

On CentOS, `/etc/security/limits.d/20-nproc.conf` must also be configured to match.

```conf
*          soft    nproc     12000
```

After changing a `ulimit` setting in `/etc/security/limits.conf`, you will need to log out and back in. To update system processes, you may need to restart.

{{< note title="Using other distributions" >}}
If you're using a desktop-distribution, such as ubuntu-desktop, the preceding settings may not suffice. The operating system needs additional steps to change `ulimit` for GUI login.

In the case of ubuntu-desktop, in `/etc/systemd/user.conf` and `/etc/systemd/system.conf`, add `DefaultLimitNOFILE=64000` at the end of file.

Something similar may be needed for other distributions.
{{< /note >}}

### Kernel settings

If running on a virtual machine, execute the following to tune kernel settings :

1. Configure the parameter `vm.swappiness` as follows:

    ```sh
    sudo bash -c 'sysctl vm.swappiness=0 >> /etc/sysctl.conf'
    ```

1. Setup path for core files as follows:

    ```sh
    sudo sysctl kernel.core_pattern=/home/yugabyte/cores/core_%p_%t_%E
    ```

1. Configure the parameter `vm.max_map_count` as follows:

    ```sh
    sudo sysctl -w vm.max_map_count=262144
    sudo bash -c 'sysctl vm.max_map_count=262144 >> /etc/sysctl.conf'
    ```

1. Validate the change as follows:

    ```sh
    sysctl vm.max_map_count
    ```

### Using systemd

If you're using [systemd](https://systemd.io/) to start the processes, and the ulimits are not propagated, add the ulimits in the `Service` section of the systemd configuration file:

```output
[Unit]
.....

[Service]
.....
ulimits options here

[Install]
.....
```

For more details, see the systemd example configuration for [yb-master.service](/code-samples/yb-master.service), and [yb-tserver.service](/code-samples/yb-tserver.service).

The mappings of ulimit options with values are:

Data type | ulimit equivalent | Value |
----------|-------------|-----|
Directive       | ulimit equivalent    | Value |
LimitCPU=       | ulimit -t            | infinity |
LimitFSIZE=     | ulimit -f            | infinity |
LimitDATA=      | ulimit -d            | infinity |
LimitSTACK=     | ulimit -s            | 8388608 |
LimitCORE=      | ulimit -c            | infinity |
LimitRSS=       | ulimit -m            | infinity |
LimitNOFILE=    | ulimit -n            | 1048576  |
LimitAS=        | ulimit -v            | infinity |
LimitNPROC=     | ulimit -u            | 12000  |
LimitMEMLOCK=   | ulimit -l            | 64 |
LimitLOCKS=     | ulimit -x            | infinity |
LimitSIGPENDING=| ulimit -i            | 119934 |
LimitMSGQUEUE=  | ulimit -q            | 819200 |
LimitNICE=      | ulimit -e            | 0 |
LimitRTPRIO=    | ulimit -r            | 0 |

If a ulimit is set to `unlimited`, set it to `infinity` in the systemd configuration file.

## transparent hugepages

Transparent hugepages should be enabled for optimal performance. By default, they are enabled.

You can check with the following command:

```sh
$ cat /sys/kernel/mm/transparent_hugepage/enabled
```

It is generally not necessary to adjust the kernel command line if the output is as follows:

```output
[always] madvise never
```

However, if the value is set to "madvise" or "never", you should modify your kernel command line to set transparent hugepages to "always".

You should consult your operating system documentation to determine the best way to modify a kernel command line argument for your operating system.

On RHEL or CentOS 7 or 8, using grub2, the following steps are one solution:

1. Append "transparent_hugepage=always" to `GRUB_CMDLINE_LINUX` in `/etc/default/grub`.

    ```sh
    GRUB_CMDLINE_LINUX="rd.lvm.lv=rhel/root rd.lvm.lv=rhel/swap ... transparent_hugepage=always"
    ```

1. Rebuild `/boot/grub2/grub.cfg` using grub2-mkconfig.

    Be sure to take a backup of the existing `/boot/grub2/grub.cfg` before rebuilding.

    On BIOS-based machines:

    ```sh
    grub2-mkconfig -o /boot/grub2/grub.cfg
    ```

    On UEFI-based machines:

    ```sh
    grub2-mkconfig -o /boot/efi/EFI/redhat/grub.cfg
    ```

1. Reboot the system.
